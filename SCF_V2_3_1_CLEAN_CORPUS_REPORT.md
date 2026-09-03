# SCF v2.3.1 — Clean-Corpus Replication on peS2o

## 0. Status and scope

This round changes nothing in the v2.3 learner. `observe_sentences` and
`ConservativeMerger` (Steps 3–5: different-by-default, direct exact-context
witness → candidate, transactional compositional-consistency check) are the
same compiled functions as in v2.3. What changes is only what is put in
front of them (corpus, preprocessing) and what is read out behind them
(witness-frame boundary types).

Two facts about the baseline must be stated up front:

1. **No FineWeb v2.3 result existed in the repository.** The v2.3 report
   says the real ladder "is not executed in this commit", and v2.1/v2.2 used
   Wikipedia-2017 because `huggingface.co` was blocked. In this session the
   network policy allowed Hugging Face, so the FineWeb 100k baseline was
   produced by running the **unchanged v2.3 CLI** (`scf_conservative_merging`)
   on a fixed FineWeb sample. Its output is stored verbatim in
   `results_v2_3_conservative/fineweb_baseline/` and is not re-interpreted
   or edited; the v2.3.1 tool's `v23d` mode reproduces it number for number
   (§5.1), which is how the frame-type breakdown of the baseline is obtained.
2. **peS2o was obtainable, so Wikipedia was not used.** The two corpora are
   never mixed.

Because v2.3 removed punctuation (condition D) and v2.3.1 keeps it, the
comparison is run as a 2×2 at the shared scales — {FineWeb, peS2o} ×
{v2.3-D preprocessing, v2.3.1 structured preprocessing} — so that the
corpus effect and the punctuation/segmentation effect can be read
separately. The main ladder (1e5, 1e6, 1e7, nested) is peS2o × v2.3.1.

## 1. Data

### 1.1 Clean corpus: peS2o v2 full-text body paragraphs

| item | value |
|---|---|
| source | `allenai/peS2o`, `data/v2/train-00010-of-00020.json.gz` (first `s2orc/train` full-text shard) |
| fetch | HTTP range `bytes=0-67108863` (first 64 MiB of the gzip stream), decoded until the truncated tail; only complete JSON lines used |
| raw sha256 | `d72beb2cec297cb8d931de71307dc4d091b6146916a1fa607f09366f91737d58` |
| records used | 7,792 `s2orc/train` documents (stream order) |
| body paragraphs / source lines / sentences | 88,691 / 329,481 / 1,371,122 |
| whitespace tokens (body only) | 32,607,179 |
| `.scs` sha256 | `4a05562940a8d86dad54a70574bfa37bce66be9905dd42feec45dddaedcfca81` |
| `.txt` sha256 (flat, for v2.3-D control) | `bbf9d30cc10903270a11654ef9fb8d873e92da664bd57a082dc1c9e856256ebe` |

peS2o's `text` field has a documented layout: title block, abstract block,
then body paragraphs separated by blank lines (Grobid section headers and
paragraphs; figures, tables and references already removed by peS2o). The
title and abstract blocks are excluded because the documented format
identifies them positionally (they come from Semantic Scholar metadata, not
from the body). **Section headers are kept**: no source field separates a
header from a one-line paragraph, and the spec forbids heuristic guessing.
This is the single most important preprocessing fact for reading the
results, because headers become one- to three-token complete "sentences"
(`introduction`, `| participants`, `conclusions`) and therefore carry the
`(ε,ε)` frame.

### 1.2 FineWeb sample (baseline corpus)

| item | value |
|---|---|
| source | `HuggingFaceFW/fineweb`, `sample/10BT/000_00000.parquet`, row groups 0–1 |
| records | 1,999 documents (stream order), 6,313,037 characters |
| raw sha256 | `7cdf441304180b9187b249d59ff560f8ef48df371c6d61a66f799b82c74750e1` |
| `.txt` sha256 (one document per line, input of the v2.3 CLI) | `2f56a565ec598f371744dd9b4d9a62656a025efeffc1ce31f3829c83fb183d34` |
| `.scs` sha256 (structured control) | `a0bb281af5a0897c6b9071778fd2a8a97aee56b032e187b7ec1162deca386669` |
| sentences (structured) | 64,742 |

FineWeb has no structural fields, so for the structured control every
newline-separated line of `text` is one paragraph.

### 1.3 POS labels (evaluation only)

UD English EWT `en_ewt-ud-train.conllu` (majority UPOS per lowercased form),
loaded after the partition is complete, exactly as in v2.3.

## 2. Preprocessing (v2.3.1 structured mode)

`tools/prepare_clean_corpus.py` writes a `.scs` file (`#doc`, `#par`, one
raw sentence per line) and the C++ loader (`scf::v231::load_structured_corpus`)
turns it into the `sentences + token_text` input that the v2.3 driver
builds internally.

1. Document and paragraph boundaries preserved (`#doc`, `#par`; per-sentence
   document/paragraph ids kept).
2. Body text only (title/abstract dropped by documented position; nothing
   else guessed).
3. Sentence segmentation: a fixed rule — a run of `. ? !`, optional closing
   quotes/brackets, whitespace, then a token starting with an uppercase
   letter or digit — unless the word before the punctuation is a single
   capital initial or in a fixed 40-entry abbreviation list (`et`, `al`,
   `e.g`, `fig`, `vs`, …). A single `\n` inside a peS2o block is a hard
   source line boundary and is never crossed.
4. Deterministic tokenization: the v2.1 tokenizer unchanged (ASCII lowercase,
   digit runs → `<num>`, single apostrophe kept inside words, every other
   visible ASCII character a one-character token, bytes ≥ 0x80 kept).
5. Punctuation **kept** as tokens (`,` `(` `)` `:` `|` `-` …).
6. The sentence-final `. ? !` run is consumed as the `<EOS>` boundary — the
   same segmentation semantics as v2.2 conditions A–D and v2.3 — so
   `<BOS>/<EOS>` remain metadata (trie root ε) and never become objects.
7. No substring crosses a sentence, paragraph or document boundary: each
   sentence is observed on its own.
8. Nested prefixes by whole sentence: the prefix for scale N is the shortest
   sentence prefix whose cumulative learner-visible token count reaches N.

Nothing is filtered by frequency, length, POS, grammaticality, similarity
or blacklist; there is no hub cap.

The `v23d` mode of the same tool reproduces the v2.3 condition-D pipeline
(one document per line, every `. ? !` ends a sentence, all punctuation
removed, condition-A nominal counts for prefix selection) and is verified
against the v2.3 CLI in §5.1.

## 3. Frame-type diagnostics

Every exact frame `(L, R)` is classified by whether `L` and/or `R` is ε
(trie root 0):

```text
empty_frame     : L=[] and R=[]      (u is a complete sentence)
left_boundary   : L=[] and R!=[]     (u is sentence-initial)
right_boundary  : L!=[] and R=[]     (u is sentence-final)
internal_frame  : L!=[] and R!=[]
```

Per type the tool reports witness count, candidate count (by the witness the
merger records for the pair — the first witness per unordered pair in
witness order, the same rule `ConservativeMerger::run` uses), accepted and
rejected merge counts, redundant candidates, induced unions, the number of
candidates/accepted merges whose witnesses are *exclusively* of that type,
and the accepted merges that end up inside the largest final class. It also
reports `#{u : (ε,ε) observed for u}` and lists every accepted merge
directly triggered by an empty frame. All of this is computed from the
merger's public state; the merger itself was not modified.
