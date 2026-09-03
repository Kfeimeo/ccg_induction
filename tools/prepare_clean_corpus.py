#!/usr/bin/env python3
"""SCF v2.3.1 -- deterministic clean-corpus acquisition and preprocessing.

Two sources are supported.  Both fetches are byte-deterministic (fixed URL,
fixed range / fixed row groups, records emitted in stream order):

  pes2o    allenai/peS2o v2, shard data/v2/train-00010-of-00020.json.gz
           (the first full-text `s2orc/train` shard), first 64 MiB of the
           gzip stream, decoded until the truncated tail.  Only complete
           JSON lines are used.
  fineweb  HuggingFaceFW/fineweb sample/10BT/000_00000.parquet, first two
           row groups (1,999 documents), read by HTTP range requests.

Structure-preserving preprocessing (no frequency / length / POS / LLM
filtering, no blacklist):

  * peS2o: `text` is documented as title block, abstract block, then body
    paragraphs separated by "\n\n".  The title and abstract blocks are
    dropped because the documented format identifies them positionally;
    section headers inside the body are NOT separable from paragraph text
    by any source field, so they are kept as-is (no heuristic guessing).
    A single "\n" inside a body block is a hard line boundary of the source
    and is never crossed by a sentence.
  * FineWeb: no structural fields exist; every "\n"-separated line of `text`
    is one paragraph.

Sentence segmentation is a fixed rule: a boundary is a run of . ? !
(optionally followed by closing quotes/brackets) followed by whitespace and
a token that starts with an uppercase letter or digit (optionally after
opening quotes/brackets), unless the word before the punctuation is a
single capital initial or in the fixed abbreviation list below.  Sentences
never cross paragraph or document boundaries.

Outputs:
  <stem>.scs   structured corpus: `#doc <id>` lines, `#par` lines, one raw
               sentence per line (tokenization happens in C++).
  <stem>.txt   the same body text as one document per line (whitespace
               flattened) -- the exact input format of the unchanged v2.3
               CLI, used for the v2.3 condition-D control runs.

Usage:
  python3 tools/prepare_clean_corpus.py pes2o   OUT_STEM [--raw FILE]
  python3 tools/prepare_clean_corpus.py fineweb OUT_STEM [--raw FILE]
"""

import gzip
import hashlib
import io
import json
import re
import sys
import urllib.request

PES2O_URL = ("https://huggingface.co/datasets/allenai/peS2o/resolve/main/"
             "data/v2/train-00010-of-00020.json.gz")
PES2O_BYTES = 64 * 1024 * 1024
FINEWEB_URL = ("https://huggingface.co/datasets/HuggingFaceFW/fineweb/resolve/"
               "main/sample/10BT/000_00000.parquet")
FINEWEB_ROW_GROUPS = 2

ABBREVIATIONS = {
    "al", "et", "e.g", "i.e", "eg", "ie", "vs", "cf", "ca", "approx", "etc",
    "fig", "figs", "eq", "eqs", "ref", "refs", "no", "nos", "vol", "vols",
    "pp", "p", "sec", "secs", "chap", "dr", "mr", "mrs", "ms", "prof",
    "st", "jr", "sr", "inc", "ltd", "co", "corp", "univ", "dept", "resp",
    "ed", "eds", "tab", "sect", "viz", "ibid",
}

BOUNDARY = re.compile(r'([.?!]+)(["\'”’)\]]*)(\s+)'
                      r'(?=["\'“‘(\[]*[A-Z0-9])')
WORD_BEFORE = re.compile(r'([A-Za-z]+(?:\.[A-Za-z]+)*)$')


def split_sentences(line):
    """Deterministic rule-based sentence segmentation of one source line."""
    sentences = []
    start = 0
    for match in BOUNDARY.finditer(line):
        before = line[start:match.start(1)]
        word = WORD_BEFORE.search(before)
        if word is not None:
            token = word.group(1)
            if token.lower() in ABBREVIATIONS or (len(token) == 1 and token.isupper()):
                continue
        sentence = " ".join(line[start:match.end(2)].split())
        if sentence:
            sentences.append(sentence)
        start = match.end(3)
    tail = " ".join(line[start:].split())
    if tail:
        sentences.append(tail)
    return sentences


def pes2o_documents(raw_path):
    with gzip.open(raw_path, "rt", encoding="utf-8") as stream:
        try:
            for raw in stream:
                record = json.loads(raw)
                if record.get("source") != "s2orc/train":
                    continue
                blocks = record["text"].split("\n\n")
                body = blocks[2:]  # documented layout: title, abstract, body...
                paragraphs = []
                for block in body:
                    lines = [ln for ln in block.split("\n") if ln.strip()]
                    if lines:
                        paragraphs.append(lines)
                yield str(record["id"]), paragraphs
        except (EOFError, json.JSONDecodeError, OSError):
            return  # truncated gzip tail of the fixed-range download


def fineweb_documents(raw_path):
    with open(raw_path, encoding="utf-8") as stream:
        for raw in stream:
            record = json.loads(raw)
            paragraphs = [[ln] for ln in record["text"].split("\n") if ln.strip()]
            yield str(record["id"]), paragraphs


def fetch_pes2o(raw_path):
    request = urllib.request.Request(PES2O_URL,
                                     headers={"Range": f"bytes=0-{PES2O_BYTES - 1}"})
    with urllib.request.urlopen(request) as response, open(raw_path, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)


def fetch_fineweb(raw_path):
    import fsspec  # only needed for the fetch itself
    import pyarrow.parquet as pq
    fs = fsspec.filesystem("https")
    with fs.open(FINEWEB_URL, "rb", block_size=8 * 1024 * 1024) as handle, \
            open(raw_path, "w", encoding="utf-8") as out:
        parquet = pq.ParquetFile(handle)
        for group in range(FINEWEB_ROW_GROUPS):
            table = parquet.read_row_group(
                group, columns=["id", "text", "dump", "url", "language",
                                "language_score", "token_count"])
            for record in table.to_pylist():
                out.write(json.dumps(record, ensure_ascii=False) + "\n")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("pes2o", "fineweb"):
        print(__doc__)
        sys.exit(2)
    source, stem = sys.argv[1], sys.argv[2]
    raw_path = None
    if len(sys.argv) >= 5 and sys.argv[3] == "--raw":
        raw_path = sys.argv[4]
    if raw_path is None:
        raw_path = stem + (".raw.json.gz" if source == "pes2o" else ".raw.jsonl")
        (fetch_pes2o if source == "pes2o" else fetch_fineweb)(raw_path)
    documents = pes2o_documents(raw_path) if source == "pes2o" else fineweb_documents(raw_path)

    stats = {"documents": 0, "paragraphs": 0, "lines": 0, "sentences": 0,
             "whitespace_tokens": 0}
    with open(stem + ".scs", "w", encoding="utf-8") as structured, \
            open(stem + ".txt", "w", encoding="utf-8") as flat:
        for doc_id, paragraphs in documents:
            if not paragraphs:
                continue
            stats["documents"] += 1
            structured.write(f"#doc {doc_id}\n")
            flat_parts = []
            for lines in paragraphs:
                stats["paragraphs"] += 1
                structured.write("#par\n")
                for line in lines:
                    stats["lines"] += 1
                    flat_parts.append(" ".join(line.split()))
                    for sentence in split_sentences(line):
                        stats["sentences"] += 1
                        stats["whitespace_tokens"] += len(sentence.split())
                        structured.write(sentence + "\n")
            flat.write(" ".join(flat_parts) + "\n")
    print(json.dumps({
        "source": source,
        "raw_file": raw_path,
        "raw_sha256": sha256_of(raw_path),
        "scs_sha256": sha256_of(stem + ".scs"),
        "txt_sha256": sha256_of(stem + ".txt"),
        **stats,
    }, indent=2))


if __name__ == "__main__":
    main()
