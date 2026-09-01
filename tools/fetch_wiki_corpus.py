#!/usr/bin/env python3
"""Deterministic corpus fetch for SCF v2.1.

The v2.1 spec asks for FineWeb; this execution environment's network policy
denies huggingface.co (CONNECT 403), so the closest reachable real English
corpus is used instead: the gensim-data packaging of the full English
Wikipedia dump of 2017-10-01, hosted as GitHub release assets
(piskvorky/gensim-data, release wiki-english-20171001, ~6.5 GB gzip in four
parts). Only a fixed byte prefix of the first part is downloaded (an exact
HTTP range request), which decompresses to far more than the 1.2e8 tokens the
experiment needs. The download is deterministic: fixed URL, fixed byte range,
articles emitted in stream order until the truncated gzip tail.

Output: one document per line (article title + section texts, newlines
flattened to spaces). Tokenization/normalization happens downstream in the
C++ tool; this script only extracts raw text.

Usage: python3 tools/fetch_wiki_corpus.py [output_path] [compressed_bytes]
"""

import gzip
import hashlib
import json
import sys
import urllib.request

URL = ("https://github.com/piskvorky/gensim-data/releases/download/"
       "wiki-english-20171001/wiki-english-20171001.gz_00")
DEFAULT_BYTES = 400 * 1024 * 1024  # fixed 400 MiB prefix of part _00


def main() -> None:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "data/real/wiki2017_head.txt"
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BYTES

    request = urllib.request.Request(URL, headers={"Range": f"bytes=0-{limit - 1}"})
    docs = 0
    text_bytes = 0
    sha = hashlib.sha256()
    with urllib.request.urlopen(request) as response:
        stream = gzip.GzipFile(fileobj=response)
        with open(out_path, "w", encoding="utf-8") as out:
            try:
                for raw in stream:
                    article = json.loads(raw)
                    text = article.get("title", "") + " . " + \
                        " ".join(article.get("section_texts", []))
                    line = " ".join(text.split()) + "\n"
                    out.write(line)
                    sha.update(line.encode("utf-8"))
                    docs += 1
                    text_bytes += len(line)
            except (EOFError, json.JSONDecodeError, OSError):
                pass  # truncated gzip tail of the fixed-range download
    print(f"documents={docs} text_bytes={text_bytes} sha256={sha.hexdigest()}")


if __name__ == "__main__":
    main()
