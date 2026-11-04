#!/usr/bin/env python3
"""Build the OSCAR 2 Guide: turns the Markdown pages in src/ into a static HTML site in html/.

Usage:  python3 build.py            (writes html/)
        python3 build.py --out DIR  (writes DIR instead)

Needs Python 3.8+ and the 'markdown' package (python3-markdown on Debian/Ubuntu,
or 'pip install markdown').

The site works when opened straight from disk (file://) as well as from a web server:
the search index is a plain JavaScript file, not JSON fetched at run time.

Page source format
------------------
Each src/*.md page starts with a small header:

    ---
    title: Daily View
    index: Daily view; Calendar; Pie chart
    ---

- 'title' is the page title (also used in the sidebar).
- 'index' (optional) lists terms that point to the top of the page.
- Anywhere in the text, [[idx:Term]] adds an index entry pointing to that exact spot.
  Use [[idx:Main term!Sub term]] for a sub-entry.
- Every ## and ### heading is added to the index and to the search automatically.

Page order comes from src/nav.txt (one file name per line; lines starting with '#'
are section captions in the sidebar).
"""
import argparse
import html
import json
import re
import shutil
import sys
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("The 'markdown' Python package is needed: install python3-markdown or run 'pip install markdown'.")

HERE = Path(__file__).resolve().parent
SRC = HERE / "src"
ASSETS = HERE / "assets"

GUIDE_TITLE = "OSCAR 2 Guide"


def slugify(value, separator="-"):
    value = re.sub(r"<[^>]+>", "", value)
    value = re.sub(r"[^\w\s-]", "", value, flags=re.UNICODE).strip().lower()
    return re.sub(r"[\s_-]+", separator, value)


def read_page(path):
    text = path.read_text(encoding="utf-8")
    meta = {}
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    if m:
        for line in m.group(1).splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                meta[k.strip().lower()] = v.strip()
        text = text[m.end():]
    return meta, text


def read_nav():
    """Returns a list of ('caption', text, False) and ('page', filename, expand) entries.

    A page line ending in ' +' lists the page's ## headings under it in the sidebar."""
    nav = []
    for raw in (SRC / "nav.txt").read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            nav.append(("caption", line.lstrip("#").strip(), False))
        else:
            expand = line.endswith("+")
            nav.append(("page", line.rstrip("+").strip(), expand))
    return nav


def plain_text(fragment):
    fragment = re.sub(r"<(script|style)[^>]*>.*?</\1>", " ", fragment, flags=re.S)
    block = r"</?(p|li|ul|ol|h\d|table|thead|tbody|tr|td|th|dl|dt|dd|div|pre|br|blockquote)\b[^>]*>"
    fragment = re.sub(block, " ", fragment)
    fragment = re.sub(r"<[^>]+>", "", fragment)
    return re.sub(r"\s+", " ", html.unescape(fragment)).strip()


def build(out):
    nav = read_nav()
    pages = []
    for kind, value, _ in nav:
        if kind != "page":
            continue
        meta, body = read_page(SRC / value)
        pages.append({"file": value, "url": value[:-3] + ".html", "meta": meta, "body": body,
                      "title": meta.get("title", value[:-3])})

    index_entries = []   # (term, subterm, page title, url)
    search_docs = []     # {t: title, p: page title, u: url, x: text}

    for page in pages:
        counter = [0]
        page_index = []

        def idx_marker(m):
            counter[0] += 1
            anchor = f"idx-{counter[0]}"
            term = m.group(1).strip()
            main, _, sub = term.partition("!")
            page_index.append((main.strip(), sub.strip(), "#" + anchor))
            return f'<span class="idx-anchor" id="{anchor}"></span>'

        # A line holding only index markers becomes its own paragraph, so a list or
        # table written straight after it still starts a new block.
        body = re.sub(r"^((?:\[\[idx:[^\]]+\]\]\s*)+)$", r"\1\n", page["body"], flags=re.M)
        body = re.sub(r"\[\[idx:([^\]]+)\]\]", idx_marker, body)
        md = markdown.Markdown(extensions=["toc", "tables", "attr_list", "def_list", "admonition",
                                           "fenced_code", "sane_lists", "md_in_html"],
                               extension_configs={"toc": {"slugify": slugify, "toc_depth": "2-3"}})
        content = md.convert(body)
        # Links between pages are written as page.md in the source; the site uses page.html.
        content = re.sub(r'href="([\w-]+)\.md(#[^"]*)?"', lambda m: f'href="{m.group(1)}.html{m.group(2) or ""}"', content)
        page["content"] = content
        page["toc"] = md.toc_tokens

        # Headings in order, so each index anchor can be labelled with the section it is in.
        heading_re = r'<h([23])([^>]*)\bid="([^"]+)"[^>]*>(.*?)</h\1>'
        headings = [(m.start(), m.group(3), plain_text(m.group(4)), "noindex" in m.group(2))
                    for m in re.finditer(heading_re, content, flags=re.S)]

        def label_for(anchor_id):
            pos = content.find(f'id="{anchor_id}"')
            section = ""
            for start, _, text, _ in headings:
                if start <= pos:
                    section = text
            return f"{page['title']} › {section}" if section else page["title"]

        for term in [t.strip() for t in page["meta"].get("index", "").split(";") if t.strip()]:
            main, _, sub = term.partition("!")
            index_entries.append((main.strip(), sub.strip(), page["title"], page["url"]))
        for main, sub, anchor in page_index:
            index_entries.append((main, sub, label_for(anchor[1:]), page["url"] + anchor))
        for _, hid, text, noindex in headings:
            if not noindex:
                index_entries.append((text, "", f"{page['title']} › {text}", page["url"] + "#" + hid))
        # {.noindex} only steers the index; keep it out of the page.
        content = content.replace(' class="noindex"', "")
        page["content"] = content

        # Search documents: the page intro, then one per h2/h3 section.
        parts = re.split(r'(<h[23] id="[^"]+">.*?</h[23]>)', content, flags=re.S)
        current_title, current_url, buf = page["title"], page["url"], []
        for part in parts:
            h = re.match(r'<h[23] id="([^"]+)">(.*?)</h[23]>', part, flags=re.S)
            if h:
                if plain_text("".join(buf)):
                    search_docs.append({"t": current_title, "p": page["title"], "u": current_url,
                                        "x": plain_text("".join(buf))})
                current_title, current_url, buf = plain_text(h.group(2)), page["url"] + "#" + h.group(1), []
            else:
                buf.append(part)
        if plain_text("".join(buf)):
            search_docs.append({"t": current_title, "p": page["title"], "u": current_url,
                                "x": plain_text("".join(buf))})

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copytree(ASSETS, out / "assets", ignore=shutil.ignore_patterns("template.html"))
    images = SRC / "images"
    if images.is_dir():
        shutil.copytree(images, out / "images")
    # Device pictures come straight from the app's own icons (oscar/icons), referenced as icons/<name>.
    app_icons = HERE.parent / "oscar" / "icons"
    used = sorted({name for p in pages for name in re.findall(r'src="icons/([^"/]+)"', p["content"])})
    for name in used:
        if not (app_icons / name).is_file():
            sys.exit(f"Missing icon: oscar/icons/{name}")
        (out / "icons").mkdir(exist_ok=True)
        shutil.copy2(app_icons / name, out / "icons" / name)

    (out / "assets" / "search-index.js").write_text(
        "window.OSCAR_SEARCH = " + json.dumps(search_docs, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8")

    def sidebar(active_url):
        items = []
        for kind, value, expand in nav:
            if kind == "caption":
                items.append(f'<li class="nav-caption">{html.escape(value)}</li>')
            else:
                p = next(p for p in pages if p["file"] == value)
                cls = ' class="active"' if p["url"] == active_url else ""
                sub = ""
                if expand and p["toc"]:
                    sub = '<ul class="nav-sub">' + "".join(
                        f'<li><a href="{p["url"]}#{t["id"]}">{t["name"]}</a></li>' for t in p["toc"]) + "</ul>"
                items.append(f'<li{cls}><a href="{p["url"]}">{html.escape(p["title"])}</a>{sub}</li>')
        items.append('<li class="nav-caption">Finding things</li>')
        for url, title in (("genindex.html", "Index"), ("search.html", "Search")):
            cls = ' class="active"' if url == active_url else ""
            items.append(f'<li{cls}><a href="{url}">{title}</a></li>')
        return "\n".join(items)

    def page_toc(tokens):
        if not tokens:
            return ""
        lines = ['<nav class="page-toc" aria-label="On this page"><div class="page-toc-title">On this page</div><ul>']
        for t in tokens:
            lines.append(f'<li><a href="#{t["id"]}">{t["name"]}</a>')
            if t.get("children"):
                lines.append("<ul>" + "".join(f'<li><a href="#{c["id"]}">{c["name"]}</a></li>'
                                               for c in t["children"]) + "</ul>")
            lines.append("</li>")
        lines.append("</ul></nav>")
        return "\n".join(lines)

    template = (ASSETS / "template.html").read_text(encoding="utf-8")

    def render(url, title, content, toc="", prev=None, nxt=None):
        pager = ""
        if prev or nxt:
            pager = '<nav class="pager">'
            pager += (f'<a class="prev" href="{prev["url"]}"><span>Previous</span>{html.escape(prev["title"])}</a>'
                      if prev else "<span></span>")
            pager += (f'<a class="next" href="{nxt["url"]}"><span>Next</span>{html.escape(nxt["title"])}</a>'
                      if nxt else "<span></span>")
            pager += "</nav>"
        doc = (template.replace("{{guide_title}}", GUIDE_TITLE)
                       .replace("{{title}}", html.escape(title))
                       .replace("{{sidebar}}", sidebar(url))
                       .replace("{{toc}}", toc)
                       .replace("{{pager}}", pager)
                       .replace("{{content}}", content))
        (out / url).write_text(doc, encoding="utf-8")

    for i, page in enumerate(pages):
        content = page["content"]
        if not content.lstrip().startswith("<h1"):
            content = f"<h1>{html.escape(page['title'])}</h1>\n" + content
        render(page["url"] if page["file"] != "index.md" else "index.html", page["title"], content,
               page_toc(page["toc"]), pages[i - 1] if i > 0 else None, pages[i + 1] if i + 1 < len(pages) else None)

    # Alphabetical index
    grouped = {}
    for main, sub, ptitle, url in index_entries:
        if not main:
            continue
        grouped.setdefault(main, {}).setdefault(sub, [])
        # One link per section: entries with the same label lead to the same place.
        if all(t != ptitle for t, _ in grouped[main][sub]):
            grouped[main][sub].append((ptitle, url))
    # A link to the top of a page is redundant when the same entry also points into that page.
    for subs in grouped.values():
        for sub, refs in subs.items():
            pages_with_anchor = {u.split("#")[0] for _, u in refs if "#" in u}
            subs[sub] = [(t, u) for t, u in refs if "#" in u or u not in pages_with_anchor]
    letters = {}
    for term in sorted(grouped, key=lambda s: s.casefold()):
        first = term[0].upper()
        letters.setdefault(first if first.isalpha() else "#", []).append(term)

    def links(refs):
        return ", ".join(f'<a href="{u}">{html.escape(t)}</a>' for t, u in refs)

    body = ["<h1>Index</h1>",
            '<p class="lead">Every topic in the guide, A to Z. Type in the box to narrow the list.</p>',
            '<input type="search" id="index-filter" class="index-filter" placeholder="Filter the index" aria-label="Filter the index">',
            '<p class="letter-bar">' + " ".join(f'<a href="#letter-{slugify(l) or "sym"}">{l}</a>' for l in letters) + "</p>"]
    for letter, terms in letters.items():
        body.append(f'<section class="index-letter"><h2 id="letter-{slugify(letter) or "sym"}">{letter}</h2><dl class="genindex">')
        for term in terms:
            subs = grouped[term]
            main_refs = subs.get("", [])
            body.append(f'<div class="index-entry"><dt>{html.escape(term)}</dt><dd>{links(main_refs)}')
            others = sorted((s for s in subs if s), key=lambda s: s.casefold())
            if others:
                body.append("<ul>" + "".join(f"<li>{html.escape(s)}: {links(subs[s])}</li>" for s in others) + "</ul>")
            body.append("</dd></div>")
        body.append("</dl></section>")
    render("genindex.html", "Index", "\n".join(body))

    render("search.html", "Search", '<h1>Search</h1>\n<form class="search-page-form" action="search.html">'
           '<input type="search" name="q" id="search-page-input" placeholder="Search the guide" aria-label="Search the guide">'
           '<button type="submit">Search</button></form>\n<div id="search-results" aria-live="polite"></div>')

    print(f"Built {len(pages)} pages, {len(grouped)} index terms, {len(search_docs)} search sections into {out}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=HERE / "html")
    build(ap.parse_args().out.resolve())
