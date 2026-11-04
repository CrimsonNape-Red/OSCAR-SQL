# OSCAR 2 Guide

The user's guide for OSCAR 2, written in Markdown and built into a static HTML site
with a contents sidebar, an A–Z index and full-text search.

## Building

```
python3 UserGuide/build.py
```

The site is written to `UserGuide/html/` (ignored by git). Open `html/index.html`
in a browser, either straight from disk or from a web server. Search works in both
cases, because the search index is a plain JavaScript file.

Needs Python 3.8+ and the `markdown` package (`python3-markdown` on Debian/Ubuntu,
or `pip install markdown`).

## Layout

| Path | Contents |
|---|---|
| `src/nav.txt` | Page order and sidebar section headings (`# Heading`). A page line ending in ` +` (for example `preferences.md +`) also lists that page's `##` sections under it in the sidebar |
| `src/*.md` | The pages |
| `src/images/` | Screenshots (copied to `html/images/`); reference them as `images/name.png` |
| `../oscar/icons/` | Device pictures: reference an app icon as `icons/name.png` and the build copies it (for example `![EMAY EMO-80](icons/emay.png){.device}`) |
| `assets/` | Page template, styles and the search/index script |
| `build.py` | The builder |

## Writing pages

Each page starts with a header:

```
---
title: Daily View
index: Daily view; Calendar; Journal
---
```

- `index` lists terms that point to the top of the page. `Main!Sub` makes a sub-entry.
- `[[idx:Term]]` anywhere in the text adds an index entry that points to that spot.
- Every `##` and `###` heading is added to the index and the search automatically,
  and gets a link anchor: `## Session Splitting Settings` becomes
  `preferences.html#session-splitting-settings`.
- Notes and warnings use admonitions: `!!! note`, `!!! tip`, `!!! warning "Title"`,
  with the text indented four spaces.
- Definition lists (`Term` on one line, `:   description` on the next) are used for options.
- Text inside `<!-- ... -->` is held back: it stays in the source for later but is left out of
  the site and the search. The AppImage instructions are held back this way until it is released.
- Code inside a numbered list must be an indented block (8 spaces); fenced ``` blocks do not work there.
