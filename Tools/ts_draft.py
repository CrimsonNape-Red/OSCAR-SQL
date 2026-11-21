#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Draft translations for the untranslated strings of every catalogue, without an API key.

translate_ts_properly.py sends the untranslated strings to the Anthropic API, which
needs API credit.  This tool splits the same job in two so that the translating can be
done by hand, or by an assistant working in a session, with only files in between:

    python Tools/ts_draft.py extract     write the strings that need translating
    python Tools/ts_draft.py status      show what is still untranslated, per language
    python Tools/ts_draft.py check       check the translations, change nothing
    python Tools/ts_draft.py apply       check the translations and put them in the .ts files

"extract" writes, into the work directory (Tools/ts_draft_work, ignored by git):

    sources.json        every distinct string some catalogue has no translation for,
                        as a list of {"id", "source", "comment", "contexts"}
    INSTRUCTIONS.md     what a translator has to do, and the rules the check applies

The translator writes one JSON object per language, {"<id>": "<translation>", ...}, to
out/<catalogue>.json (for example out/Deutsch.de.json).  A language may be split over
several files, out/Deutsch.de.part1.json and so on; "apply" reads them all.

"apply" fills only translations that are still empty, so it can be run again after a
correction, and it never touches a string somebody has already translated.  A translation
that fails a check (a lost %1, a changed HTML tag, ...) is reported and left empty.  The
strings stay type="unfinished": these are drafts for a translator to review in Linguist.

Plural (numerus) messages are skipped, as translate_ts_properly.py skips them: the number
of plural forms depends on the language.

The .ts files are edited through ts_splice, so everything but the translation bodies is
left byte for byte as lupdate wrote it.

Copyright (c) 2026 The OSCAR Team
"""

import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

import ts_splice

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TS_DIR = os.path.join(REPO, 'Translations')
WORK_DIR = os.path.join(REPO, 'Tools', 'ts_draft_work')
OUT_DIR = os.path.join(WORK_DIR, 'out')

# Catalogue name -> the language to translate into.  English.en_UK is left out on
# purpose: its "translations" are spelling changes, not something to draft in bulk.
LANGUAGES = {
    'Afrikaans.af': 'Afrikaans',
    'Arabic.ar': 'Arabic',
    'Bulgarian.bg': 'Bulgarian',
    'Chinese.zh_CN': 'Chinese (Simplified)',
    'Chinese.zh_TW': 'Chinese (Traditional)',
    'Czech.cz': 'Czech',
    'Dansk.da': 'Danish',
    'Deutsch.de': 'German',
    'Espaniol.es': 'Spanish (Spain)',
    'Espaniol.es_MX': 'Spanish (Mexican)',
    'Filipino.fil': 'Filipino',
    'Francais.fr': 'French',
    'Greek.el': 'Greek',
    'Hebrew.he': 'Hebrew',
    'Italiano.it': 'Italian',
    'Japanese.ja': 'Japanese',
    'Korean.ko': 'Korean',
    'Magyar.hu': 'Hungarian',
    'Nederlands.nl': 'Dutch',
    'Norsk.no': 'Norwegian',
    'Polski.pl': 'Polish',
    'Portugues.pt': 'Portuguese',
    'Portugues.pt_BR': 'Portuguese (Brazilian)',
    'Romanian.ro': 'Romanian',
    'Russkiy.ru': 'Russian',
    'Suomi.fi': 'Finnish',
    'Svenska.sv': 'Swedish',
    'Thai.th': 'Thai',
    'Turkish.tr': 'Turkish',
    'Ukrainska.uk': 'Ukrainian',
}

PLACEHOLDER_RE = re.compile(r'%(?:L?\d+|n)')
TAG_RE = re.compile(r'</?[A-Za-z][^>]*>')


def ts_path(lang):
    """Full path of a catalogue, given its name without the .ts extension."""
    return os.path.join(TS_DIR, lang + '.ts')


def untranslated(lang):
    """The messages of a catalogue that still need a translation.

    Returns (messages, total).  Each message is a dict with its 'index' (position among
    all <message> elements in document order, which is how ts_splice finds it again),
    'source', 'comment', 'context' and 'is_variant'.  'total' is the number of <message>
    elements, which ts_splice checks against the file text before it edits anything.
    """
    root = ET.parse(ts_path(lang)).getroot()
    messages = []
    index = -1
    for context in root.findall('context'):
        name = context.findtext('name') or ''
        for message in context.findall('message'):
            index += 1
            if message.get('numerus') == 'yes':
                continue
            source = message.findtext('source') or ''
            translation = message.find('translation')
            if translation is None or not source.strip():
                continue
            if translation.get('type') in ('obsolete', 'vanished'):
                continue

            # variants="yes" is on <translation>; its text is then in <lengthvariant>s.
            is_variant = translation.get('variants') == 'yes'
            if is_variant:
                done = any((lv.text or '').strip()
                           for lv in translation.findall('lengthvariant'))
            else:
                done = bool((translation.text or '').strip())
            if done:
                continue

            messages.append({
                'index': index,
                'source': source,
                'comment': message.findtext('comment') or '',
                'context': name,
                'is_variant': is_variant,
            })
    return messages, ts_splice.message_count(root)


def key_of(message):
    """What identifies a string across catalogues: its text and disambiguation comment."""
    return (message['source'], message['comment'])


def load_sources():
    """sources.json as {(source, comment): id}."""
    with open(os.path.join(WORK_DIR, 'sources.json'), encoding='utf-8') as f:
        return {(s['source'], s['comment']): str(s['id']) for s in json.load(f)}


def problems_with(source, translation):
    """Reasons a translation cannot be used, as a list of short texts (empty if none)."""
    problems = []
    if not isinstance(translation, str) or not translation.strip():
        return ['empty']
    if set(PLACEHOLDER_RE.findall(source)) != set(PLACEHOLDER_RE.findall(translation)):
        problems.append('placeholders (%1, %2, %n ...) differ')
    if sorted(TAG_RE.findall(source)) != sorted(TAG_RE.findall(translation)):
        problems.append('HTML tags differ')
    if source.count('\n') != translation.count('\n'):
        problems.append('number of line breaks differs')
    lead = len(source) - len(source.lstrip())
    trail = len(source) - len(source.rstrip())
    if (translation[:len(translation) - len(translation.lstrip())] != source[:lead]
            or translation[len(translation.rstrip()):] != source[len(source) - trail:]):
        problems.append('leading or trailing whitespace differs')
    return problems


def cmd_extract():
    """Write sources.json and INSTRUCTIONS.md for everything still untranslated."""
    os.makedirs(OUT_DIR, exist_ok=True)
    sources = {}        # key -> {'id', 'source', 'comment', 'contexts'}
    for lang in LANGUAGES:
        messages, _ = untranslated(lang)
        for message in messages:
            entry = sources.setdefault(key_of(message), {
                'id': len(sources) + 1,
                'source': message['source'],
                'comment': message['comment'],
                'contexts': [],
            })
            if message['context'] not in entry['contexts']:
                entry['contexts'].append(message['context'])
        print('%-18s %4d to translate' % (lang, len(messages)))

    with open(os.path.join(WORK_DIR, 'sources.json'), 'w', encoding='utf-8') as f:
        json.dump(list(sources.values()), f, ensure_ascii=False, indent=1)

    rows = '\n'.join('| `out/%s.json` | %s |' % item for item in LANGUAGES.items())
    with open(os.path.join(WORK_DIR, 'INSTRUCTIONS.md'), 'w', encoding='utf-8') as f:
        f.write(INSTRUCTIONS % {'count': len(sources), 'rows': rows})
    print('\n%d distinct strings written to %s' % (len(sources), WORK_DIR))


def read_translations(lang):
    """Every out/<lang>*.json merged into one {id: translation}."""
    merged = {}
    pattern = os.path.join(OUT_DIR, glob.escape(lang) + '*.json')
    for path in sorted(glob.glob(pattern)):
        # "Espaniol.es" must not pick up "Espaniol.es_MX.json".
        rest = os.path.basename(path)[len(lang):]
        if not (rest == '.json' or rest.startswith('.part')):
            continue
        with open(path, encoding='utf-8') as f:
            merged.update({str(k): v for k, v in json.load(f).items()})
    return merged


def cmd_apply(langs, write):
    """Check the translations of the given catalogues and, if *write*, splice them in."""
    ids = load_sources()
    rejected = 0
    for lang in langs:
        translations = read_translations(lang)
        if not translations:
            print('%-18s no file in out/' % lang)
            continue

        messages, total = untranslated(lang)
        resolved = {}
        missing = 0
        for message in messages:
            translation = translations.get(ids.get(key_of(message), ''))
            if translation is None:
                missing += 1
                continue
            problems = problems_with(message['source'], translation)
            if problems:
                rejected += 1
                print('  %s id %s rejected: %s' % (lang, ids[key_of(message)],
                                                   '; '.join(problems)))
                continue
            resolved[message['index']] = (message['is_variant'], translation)

        if write and resolved:
            text, newline = ts_splice.read_text(ts_path(lang))
            text, _ = ts_splice.splice_translations(text, resolved, total, newline)
            ts_splice.write_text(ts_path(lang), text)
        print('%-18s %4d %s, %d without a translation' % (
            lang, len(resolved), 'written' if write else 'would be written', missing))
    return 1 if rejected else 0


def cmd_status():
    """Print how many strings each catalogue still has untranslated."""
    for lang in LANGUAGES:
        print('%-18s %4d untranslated' % (lang, len(untranslated(lang)[0])))


INSTRUCTIONS = """\
# Drafting translations for OSCAR

OSCAR is a desktop program that shows the data recorded by CPAP machines and oximeters.
`sources.json` in this folder lists the %(count)d user-interface strings that have no
translation yet. Each entry has an `id`, the English `source`, a `comment` (a note that
tells two uses of the same English text apart; usually empty) and `contexts` (the class
or dialog the string appears in, as a hint to its meaning).

## What to write

For each language below, write one JSON object mapping every id to its translation:

    {"1": "...", "2": "...", ...}

| File | Language |
|---|---|
%(rows)s

A language may be split over several files: `out/Deutsch.de.part1.json`,
`out/Deutsch.de.part2.json`, and so on. Translate every id for every language; a string a
catalogue already has a translation for is simply not used.

## Rules

- Keep `%%1`, `%%2`, `%%n` and the like exactly as they are; they may be reordered.
- Keep HTML tags and their attributes exactly as they are; translate only the text
  between them.
- Keep line breaks, and any spaces at the start or end of a string.
- Product names, device names and abbreviations (OSCAR, CPAP, BiPAP, ResMed, AHI, SpO2,
  ...) stay as they are.
- An `&` in front of a letter marks a keyboard shortcut; keep one in the translation.
- Use the terms the catalogue already uses. When unsure of an established term (for
  example for "mask leak" or "pressure relief"), search `Translations/<catalogue>.ts`
  for a string that contains it.
- A string that is the same in the target language (a number format, a unit, a product
  name) is written out unchanged, not left empty.

## Checking and applying

Run these from the repository root:

    python Tools/ts_draft.py check  [catalogue ...]    report problems, change nothing
    python Tools/ts_draft.py apply  [catalogue ...]    write the translations
    python Tools/ts_draft.py status                    what is still untranslated
    python Tools/ts_check_scripts.py Translations      wrong-script characters

Without a catalogue name, `check` and `apply` take every language that has a file in
`out/`. A rejected translation is named by language and id with the reason; correct it in
the `out/` file and run `apply` again. `apply` only fills translations that are still
empty, so running it again is safe. Finish with `ts_check_scripts.py`, which must report
0 problems.

Do not edit the `.ts` files directly, do not run `lupdate`, and do not commit.
"""


def main():
    """Dispatch the extract / status / check / apply commands; returns the exit status."""
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    command = sys.argv[1] if len(sys.argv) > 1 else ''
    langs = sys.argv[2:] or list(LANGUAGES)
    unknown = [lang for lang in langs if lang not in LANGUAGES]
    if unknown:
        print('unknown catalogue: %s' % ', '.join(unknown))
        return 2

    if command == 'extract':
        cmd_extract()
    elif command == 'status':
        cmd_status()
    elif command in ('check', 'apply'):
        return cmd_apply(langs, write=(command == 'apply'))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
