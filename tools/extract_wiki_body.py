#!/usr/bin/env python3
"""Structure-preserving body-text extraction for SCF v2.3.1.

Source: the gensim-data packaging of the English Wikipedia dump of
2017-10-01 (release wiki-english-20171001, part _00).  The same fixed
400 MiB byte prefix that tools/fetch_wiki_corpus.py streams is expected on
disk (fetch it once with an exact HTTP range request; see the report).  The
raw bytes are the single shared source of both corpus variants, so the
baseline and the clean-corpus condition differ only in what this script
keeps.

Every article is one JSON object with exactly three fields:

    title           article title                  (string)
    section_titles  top-level section headings     (list of strings)
    section_texts   top-level section bodies       (list of strings,
                                                    paragraphs separated by
                                                    newlines, wiki markup
                                                    already removed by
                                                    gensim's filter_wiki)

Mode `legacy` reproduces tools/fetch_wiki_corpus.py byte for byte (title +
" . " + all section texts, newlines flattened, one article per line).  That
is the v2.1/v2.2/v2.3 corpus and preprocessing.

Mode `body` keeps document and paragraph boundaries and only body text:

  * the `title` field is never emitted;
  * `section_titles` are never emitted;
  * sections whose title (whitespace-stripped, exact match) is one of the
    standard Wikipedia appendix headings in APPENDIX_SECTIONS (reference
    lists, link lists, notes) are dropped whole -- identified by the data
    source's own field, not by inspecting the text;
  * lines that are level-3+ wiki sub-headings (`=== Heading ===`, source
    markup that gensim leaves in the section text) are dropped -- this is a
    fixed syntactic marker of the source format, not a content heuristic;
  * every remaining non-empty line is one paragraph; internal whitespace is
    collapsed to single spaces.

No frequency, length, language-model, or similarity filtering is applied.
Image captions and table residue that gensim's markup filter emits as bare
lines carry no reliable field marker and are therefore KEPT (documented as a
residual in the report).  Tokenization, lowercasing, digit folding, and
sentence segmentation happen downstream in the C++ tool exactly as in v2.3.

Output format (mode body): one paragraph per line; documents are separated
by exactly one empty line.

Usage:
  python3 tools/extract_wiki_body.py --mode body   RAW.gz OUT.txt
  python3 tools/extract_wiki_body.py --mode legacy RAW.gz OUT.txt
"""

import argparse
import gzip
import hashlib
import json
import re
import sys

APPENDIX_SECTIONS = frozenset({
    "See also",
    "References",
    "External links",
    "Further reading",
    "Notes",
    "Bibliography",
    "Sources",
    "Footnotes",
    "Citations",
    "Notes and references",
    "References and notes",
    "Works cited",
})

# `== Title ==` lines; gensim splits top-level (==) sections into fields but
# leaves deeper (===, ====) headings inside the section text.
HEADING_LINE = re.compile(r"^={2,}[^=].*?={2,}$")


def iter_articles(raw_path):
    with gzip.open(raw_path, "rb") as stream:
        try:
            for raw in stream:
                yield json.loads(raw)
        except (EOFError, json.JSONDecodeError, OSError):
            return  # truncated gzip tail of the fixed-range download


def legacy_line(article):
    text = article.get("title", "") + " . " + " ".join(article.get("section_texts", []))
    return " ".join(text.split()) + "\n"


def body_paragraphs(article, stats):
    titles = article.get("section_titles", [])
    texts = article.get("section_texts", [])
    paragraphs = []
    for index, text in enumerate(texts):
        title = titles[index].strip() if index < len(titles) else ""
        if title in APPENDIX_SECTIONS:
            stats["sections_dropped_appendix"] += 1
            continue
        stats["sections_kept"] += 1
        for line in text.split("\n"):
            stripped = line.strip()
            if not stripped:
                continue
            if HEADING_LINE.match(stripped):
                stats["subheading_lines_dropped"] += 1
                continue
            paragraphs.append(" ".join(stripped.split()))
    return paragraphs


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--mode", choices=("body", "legacy"), required=True)
    parser.add_argument("raw_gz")
    parser.add_argument("output")
    args = parser.parse_args()

    stats = {
        "documents": 0,
        "documents_with_body": 0,
        "paragraphs": 0,
        "sections_kept": 0,
        "sections_dropped_appendix": 0,
        "subheading_lines_dropped": 0,
        "text_bytes": 0,
    }
    sha = hashlib.sha256()
    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        first = True
        for article in iter_articles(args.raw_gz):
            stats["documents"] += 1
            if args.mode == "legacy":
                line = legacy_line(article)
                out.write(line)
                sha.update(line.encode("utf-8"))
                stats["text_bytes"] += len(line)
                continue
            paragraphs = body_paragraphs(article, stats)
            if not paragraphs:
                continue
            stats["documents_with_body"] += 1
            chunk = ("" if first else "\n") + "\n".join(paragraphs) + "\n"
            first = False
            out.write(chunk)
            sha.update(chunk.encode("utf-8"))
            stats["paragraphs"] += len(paragraphs)
            stats["text_bytes"] += len(chunk)
    stats["sha256"] = sha.hexdigest()
    print(" ".join(f"{key}={value}" for key, value in stats.items()))


if __name__ == "__main__":
    main()
