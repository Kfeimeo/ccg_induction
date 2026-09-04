# Research line 04 — Conservative witness-driven merging (v2.3 – v2.3.1)

**Direction.** Remove every frequency from discovery: objects start in
singleton classes, a pair becomes a merge candidate only after one exact
full-sentence substitution witness `L u R, L v R ∈ D`, and a rollback
transaction checks observed composition before committing. v2.3.1 re-runs
the unchanged learner on a cleaner corpus (peS2o, structure-preserving
preprocessing) and classifies every witness by frame type.

**Status.** Superseded by line 05. The clean-corpus replication showed that
the largest class is always the `(ε,ε)` class — objects that are complete
short spans are merged with `<num>`, section headers and formula fragments
on a single shared frame — and that the mechanism is a property of the
witness semantics (`∃c: c[u], c[v] ∈ D ⇏ u ≡ v`), not of the corpus. The
corpus loader, frame-type classification and the merger itself are kept
here as the baseline that line 05 is compared against.

| what | where |
|---|---|
| modules | `include/scf/conservative_merging.hpp`, `src/conservative_merging.cpp` (`scf::v23`, library `scf_conservative_v23`); `include/scf/clean_corpus.hpp`, `src/clean_corpus.cpp` (`scf::v231`, library `scf_clean_corpus_v231`) |
| tools | `tools/scf_conservative_merging.cpp`, `tools/scf_clean_corpus.cpp`, `tools/prepare_clean_corpus.py` (deterministic peS2o / FineWeb fetch + preprocessing), `tools/summarize_clean_corpus.py` |
| tests | `tests/test_conservative_merging.cpp`, `tests/test_clean_corpus.cpp` |
| results | `results/v2_3_conservative/` (FineWeb baseline, oracle sanity), `results/v2_3_1_clean_corpus/` (2×2 corpus × preprocessing ladders) |
| reports | `reports/SCF_V2_3_CONSERVATIVE_MERGING_REPORT.md`, `reports/SCF_V2_3_1_CLEAN_CORPUS_REPORT.md` |

Paths quoted inside the two reports refer to the pre-reorganisation layout
(`results_v2_3_conservative/`, `results_v2_3_1_clean_corpus/`, `src/`,
`tools/`); they map one-to-one onto the `results/`, `src/`, `tools/`
directories of this line.

## v2.3 conservative evidence-driven merging

The v2.3 experiment starts every observed token/substring in a singleton
class and considers a pair only after an exact full-sentence substitution
witness. It uses no frequency threshold or similarity score, keeps local
witnesses separate from global equality, and checks observed composition in
a rollback transaction before committing a merge.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target scf_conservative_merging
build/04_conservative_witness_merging/scf_conservative_merging --oracle-only
build/04_conservative_witness_merging/scf_conservative_merging \
  --input data/real/wiki_text.txt \
  --scales 100000,1000000,10000000,100000000 \
  --output-dir 04_conservative_witness_merging/results/v2_3_conservative
```

See `reports/SCF_V2_3_CONSERVATIVE_MERGING_REPORT.md` for the semantics, diagnostics,
oracle cases, and the exact distinction between learner-state separation and
logical inequality.

## v2.3.1 clean-corpus replication (peS2o)

v2.3.1 re-runs the unchanged v2.3 conservative learner on a cleaner formal
English corpus (peS2o v2 full-text body paragraphs) with structure-
preserving preprocessing (document / paragraph / sentence boundaries kept,
punctuation kept, sentence-final `. ? !` consumed as the `<EOS>` boundary)
and classifies every witness by its frame boundary type
(`empty_frame`, `left_boundary`, `right_boundary`, `internal_frame`).
The merge semantics are untouched; only the corpus in front of the learner
and the diagnostics behind it differ.

```bash
python3 04_conservative_witness_merging/tools/prepare_clean_corpus.py pes2o   data/real/pes2o_body
python3 04_conservative_witness_merging/tools/prepare_clean_corpus.py fineweb data/real/fineweb_sample
cmake --build build --target scf_clean_corpus
build/04_conservative_witness_merging/scf_clean_corpus --input data/real/pes2o_body.scs --corpus pes2o \
  --preprocessing structured --scales 100000,1000000,10000000 \
  --output-dir 04_conservative_witness_merging/results/v2_3_1_clean_corpus/pes2o_structured \
  --ud data/real/en_ewt-ud-train.conllu
build/04_conservative_witness_merging/scf_clean_corpus --input data/real/fineweb_sample.txt --corpus fineweb \
  --preprocessing v23d --scales 100000,1000000 \
  --output-dir 04_conservative_witness_merging/results/v2_3_1_clean_corpus/fineweb_v23d
python3 04_conservative_witness_merging/tools/summarize_clean_corpus.py
```

`--preprocessing v23d` reproduces the v2.3 condition-D pipeline exactly
(verified against `scf_conservative_merging` output) so the FineWeb
baseline gets the same frame-type diagnostics. See
`reports/SCF_V2_3_1_CLEAN_CORPUS_REPORT.md`.

v1.2 adds a controlled synthetic benchmark, a gold evaluator, batch experiments, and identifiability diagnostics on top of the unchanged v1.1 core. Its purpose is to make one research question measurable:

> When does direct surface substitution evidence uniquely determine unlabeled binary structure, and when is structure underdetermined?

