#!/usr/bin/env python3
"""Generate oscar/docs/shipped_channel_names.txt, the list of channel names OSCAR has shipped.

OSCAR used to store, for every profile, a full copy of each channel's full name, label and
description. Converting those copies into sparse user overrides needs a way to tell a real
rename from a default an older build wrote (an old English wording, or an old translation).
This developer tool builds that list from the git history. It is run once, at the schema v20
release; the output is bound into the application as a Qt resource and never needs
regenerating, because only pre-v20 builds wrote the stored copies.

Source strings (the English names):
  * every fullname, label and description in the channel_defaults table of an OSCAR database
    that was written by a run of OSCAR set to English (opened read-only; nothing else in the
    database is read), and
  * every string that has ever been a tr("...") or QObject::tr("...") literal in
    oscar/SleepLib/schema.cpp, oscar/SleepLib/common.cpp or oscar/SleepLib/loader_plugins/*.cpp
    (or the same files at their earlier locations, SleepLib/... and sleepyhead/SleepLib/...)
    in any commit, including the literals present at HEAD. Strings still in the code are
    kept because they may no longer be the same channel's name (for example "Large Leak"
    is still a literal, but is no longer the full name of the large-leak channel).

Output: those strings plus every <translation> that any Translations/*.ts file has ever
paired with one of them as its <source>, over every commit that touched Translations/*.ts
(only the files that commit changed) plus HEAD.

Format: UTF-8, one string per line, sorted, unique, no empty line. Inside a string a
backslash is written \\\\ and a newline \\n.

Usage:
  python Tools/gen_shipped_channel_names.py --repo . --db <oscar.db> \\
         --out oscar/docs/shipped_channel_names.txt
  python Tools/gen_shipped_channel_names.py --repo . --report "Flow Limit."

--report S prints every translation collected for source S (with its language file) and
writes nothing; it needs no database.

Copyright (c) 2026 The OSCAR Team
"""

import argparse
import html
import os
import re
import sqlite3
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

# Files whose tr() literals are channel-name candidates (git pathspecs, relative to the repo).
TR_PATHSPECS = [
    "oscar/SleepLib/schema.cpp",
    "oscar/SleepLib/common.cpp",
    "oscar/SleepLib/loader_plugins/*.cpp",
]
# The same files at the locations they had earlier in the history (before the oscar/
# layout, and before that as SleepyHead). Their tr() literals count as "once shipped" too.
OLD_TR_PATHSPECS = [
    "SleepLib/schema.cpp",
    "SleepLib/common.cpp",
    "SleepLib/machine_common.cpp",
    "SleepLib/loader_plugins/*.cpp",
    "sleepyhead/SleepLib/schema.cpp",
    "sleepyhead/SleepLib/common.cpp",
    "sleepyhead/SleepLib/machine_common.cpp",
    "sleepyhead/SleepLib/loader_plugins/*.cpp",
]
TS_PATH = re.compile(r"^Translations/[^/]+\.ts$")
GUARD_FRACTION = 0.80

# tr("a" "b") or QObject::tr("a"): one or more adjacent string literals after the opening
# parenthesis (a disambiguation argument after a comma is not part of the string).
TR_CALL = re.compile(r'\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
C_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
C_ESCAPE = re.compile(r"\\(x[0-9a-fA-F]+|[0-7]{1,3}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)", re.DOTALL)
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f",
                  "v": "\v", "\\": "\\", '"': '"', "'": "'", "?": "?"}

MESSAGE = re.compile(rb"<message\b.*?</message>", re.DOTALL)
SOURCE = re.compile(rb"<source>(.*?)</source>", re.DOTALL)


def log(text):
    """Print a progress line to stderr."""
    print(text, file=sys.stderr, flush=True)


def git(repo, *args):
    """Run git in repo and return stdout as bytes."""
    return subprocess.run(["git", "-C", repo] + list(args), check=True,
                          stdout=subprocess.PIPE).stdout


def decode_c_string(body):
    """Decode the C escapes of a string-literal body (without the quotes).

    \\x and octal escapes denote bytes of the UTF-8 encoded literal, so the result is built
    as bytes and decoded once.
    """
    out = bytearray()
    pos = 0
    for m in C_ESCAPE.finditer(body):
        out += body[pos:m.start()].encode("utf-8")
        pos = m.end()
        esc = m.group(1)
        if esc[0] == "x":
            out.append(int(esc[1:], 16) & 0xFF)
        elif esc[0] in "01234567":
            out.append(int(esc, 8) & 0xFF)
        elif esc[0] in "uU" and len(esc) > 1:
            out += chr(int(esc[1:], 16)).encode("utf-8")
        else:
            out += SIMPLE_ESCAPES.get(esc, esc).encode("utf-8")
    out += body[pos:].encode("utf-8")
    return out.decode("utf-8", errors="replace")


def tr_literals(text):
    """Return the set of strings that are tr() literals on the lines of text."""
    found = set()
    for m in TR_CALL.finditer(text):
        parts = C_LITERAL.findall(m.group(1))
        s = "".join(decode_c_string(p) for p in parts)
        if s:
            found.add(s)
    return found


def source_tr_strings(repo):
    """Every string that has ever been a tr() literal in the channel source files.

    Covers the history at all locations of those files, and the files at HEAD (a literal
    that no scanned diff shows as an added line is still read from HEAD).
    """
    log("Scanning the history of the channel source files for tr() literals...")
    log_out = git(repo, "log", "-p", "-U0", "--no-renames", "--format=%x00%H", "--",
                  *(TR_PATHSPECS + OLD_TR_PATHSPECS)).decode("utf-8", errors="replace")
    commits = 0
    ever = set()
    for chunk in log_out.split("\x00"):
        if not chunk.strip():
            continue
        commits += 1
        lines = [ln[1:] for ln in chunk.splitlines()
                 if ln[:1] in "+-" and not ln.startswith(("+++", "---"))]
        ever |= tr_literals("\n".join(lines))
    at_head = set()
    names = git(repo, "ls-files", "--", *TR_PATHSPECS).decode("utf-8").split()
    for name in names:
        at_head |= tr_literals(git(repo, "show", "HEAD:" + name).decode("utf-8", errors="replace"))
    ever |= at_head
    log("  %d commits, %d tr() literals ever (%d of them at HEAD)" %
        (commits, len(ever), len(at_head)))
    return ever, commits


def database_names(db_path):
    """The distinct non-empty fullname, label and description values in channel_defaults.

    The database is opened read-only; only these three columns are read.
    """
    uri = "file:" + db_path.replace("\\", "/") + "?mode=ro"
    con = sqlite3.connect(uri, uri=True)
    try:
        names = set()
        for row in con.execute("SELECT fullname, label, description FROM channel_defaults"):
            for value in row:
                if value:
                    names.add(value)
        return names
    finally:
        con.close()


def ts_blobs(repo):
    """Return (blob -> [paths], commit count) for every Translations/*.ts file version.

    Covers every commit that touched Translations (only the files that commit changed) and
    HEAD. Identical file contents are read once.
    """
    log("Listing Translations/*.ts versions in the history...")
    out = git(repo, "-c", "core.quotepath=false", "log", "-m", "--raw", "--no-abbrev", "--no-renames", "--format=%x00%H",
              "--", "Translations").decode("utf-8", errors="replace")
    blobs = {}
    seen = set()
    for chunk in out.split("\x00"):
        if not chunk.strip():
            continue
        seen.add(chunk.split()[0])
        for line in chunk.splitlines()[1:]:
            if not line.startswith(":"):
                continue
            meta, _, path = line.partition("\t")
            fields = meta.split()
            blob, status = fields[3], fields[4]
            if status.startswith("D") or set(blob) == {"0"} or not TS_PATH.match(path):
                continue
            blobs.setdefault(blob, set()).add(path)
    commits = len(seen)
    head_blobs = {}
    for line in git(repo, "-c", "core.quotepath=false", "ls-tree", "-r", "--full-tree", "HEAD", "--", "Translations").decode("utf-8").splitlines():
        meta, _, path = line.partition("\t")
        if TS_PATH.match(path):
            blob = meta.split()[2]
            head_blobs[path] = blob
            blobs.setdefault(blob, set()).add(path)
    log("  %d commits, %d distinct file versions, %d files at HEAD" %
        (commits, len(blobs), len(head_blobs)))
    return blobs, head_blobs, commits


def read_blobs(repo, blobs):
    """Yield (blob, bytes) for each blob id using one persistent git cat-file process."""
    proc = subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for blob in blobs:
            proc.stdin.write(blob.encode() + b"\n")
            proc.stdin.flush()
            header = proc.stdout.readline().split()
            size = int(header[2])
            data = proc.stdout.read(size)
            proc.stdout.read(1)
            yield blob, data
    finally:
        proc.stdin.close()
        proc.wait()


def language_of(path):
    """The file name of a translation file, for reports."""
    return os.path.basename(path)


def messages_for(data, wanted):
    """Yield (source, [translation texts]) for each message in a .ts whose source is wanted.

    A cheap regular expression finds each <message> and its <source>; only messages with a
    wanted source are parsed as XML, so entities are decoded by a real parser. Empty
    translations and unfinished ones without text are skipped; every <numerusform> counts.
    """
    for m in MESSAGE.finditer(data):
        block = m.group(0)
        sm = SOURCE.search(block)
        if sm is None:
            continue
        source = html.unescape(sm.group(1).decode("utf-8", errors="replace"))
        if source not in wanted:
            continue
        try:
            root = ET.fromstring(block)
        except ET.ParseError:
            continue
        source = root.findtext("source") or source
        texts = []
        tr = root.find("translation")
        if tr is None:
            continue
        forms = tr.findall("numerusform")
        candidates = forms if forms else [tr]
        for t in candidates:
            text = "".join(t.itertext())
            if not text:
                continue
            texts.append(text)
        yield source, texts


def collect_translations(repo, wanted, report_for=None):
    """Return (translations set, head_sources set, commits scanned, report rows).

    head_sources are the wanted strings that occur as a <source> in the .ts files at HEAD.
    report maps (file, translation) to whether that file has it at HEAD, for report_for.
    """
    blobs, head_blobs, commits = ts_blobs(repo)
    head_set = set(head_blobs.values())
    translations = set()
    head_sources = set()
    report = {}
    start = time.time()
    done = 0
    for blob, data in read_blobs(repo, sorted(blobs)):
        done += 1
        if done % 200 == 0:
            log("  parsed %d of %d file versions (%.0fs)" % (done, len(blobs), time.time() - start))
        for source, texts in messages_for(data, wanted):
            if blob in head_set:
                head_sources.add(source)
            for text in texts:
                translations.add(text)
                if report_for is not None and source == report_for:
                    for path in blobs[blob]:
                        at_head = head_blobs.get(path) == blob
                        key = (language_of(path), text)
                        report[key] = report.get(key, False) or at_head
    return translations, head_sources, commits, report


def escape_line(s):
    """Escape a string for the one-per-line output format."""
    return s.replace("\\", "\\\\").replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=".", help="path of the OSCAR git repository")
    ap.add_argument("--db", help="oscar.db whose channel_defaults was written by an English run")
    ap.add_argument("--out", help="output file, normally oscar/docs/shipped_channel_names.txt")
    ap.add_argument("--report", metavar="SOURCE",
                    help="print every translation collected for this source string and write nothing")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    started = time.time()

    if args.report is not None:
        _, _, commits, report = collect_translations(args.repo, {args.report}, args.report)
        print("Translations of %r over %d commits (file, translation, at HEAD):" % (args.report, commits))
        # A retired translation is one that the file no longer has at HEAD.
        for (lang, text), at_head in sorted(report.items()):
            print("%-28s %s%s" % (lang, text, "" if at_head else "   [retired]"))
        return 0

    if not args.db or not args.out:
        ap.error("--db and --out are required unless --report is given")

    db_names = database_names(args.db)
    log("channel_defaults: %d distinct names" % len(db_names))
    tr_strings, tr_commits = source_tr_strings(args.repo)
    sources = db_names | tr_strings
    translations, head_sources, ts_commits, _ = collect_translations(args.repo, sources)

    found = len(db_names & head_sources)
    if not db_names or found < GUARD_FRACTION * len(db_names):
        log("ERROR: only %d of %d channel_defaults names occur as a <source> in the .ts files at "
            "HEAD (need %d%%). The database was not written by an English run." %
            (found, len(db_names), int(GUARD_FRACTION * 100)))
        return 1

    lines = sorted({escape_line(s) for s in (sources | translations) if s} - {""})
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        for line in lines:
            f.write(line + "\n")
    size = os.path.getsize(args.out)
    print("Source strings: %d (%d from the database, %d distinct tr() literals, %d in both)" %
          (len(sources), len(db_names), len(tr_strings), len(db_names & tr_strings)))
    print("Output lines:   %d" % len(lines))
    print("Output size:    %d bytes" % size)
    print("Commits scanned: %d touching Translations, %d touching channel source files" %
          (ts_commits, tr_commits))
    print("Run time:       %.0f s" % (time.time() - started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
