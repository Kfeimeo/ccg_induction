# Research line 03 — Shared-context evidence scaling on real corpora (v2.1 – v2.2)

**Direction.** Take the v2.0 signature idea to real English text without an
oracle: per nested token scale, count *shared exact contexts*
`I_N(u, v) = C_N(u) ∩ C_N(v)` between frequent substrings, report full
evidence curves over thresholds, held-out replication, and POS purity
(evaluation only). v2.2 ablates sentence-terminal anchors against
punctuation.

**Status.** Complete. Shared-context counting is frequency-based (min-count
floor, hub cap, evidence thresholds) and is superseded as a *discovery*
mechanism by lines 04–05; the tokenizer, sentence segmentation and
UD-based POS diagnostic defined here are reused by both later lines
(library `scf_real_v21`).

| what | where |
|---|---|
| module | `include/scf/real_scaling.hpp`, `src/real_scaling.cpp` (namespace `scf::v21`, library `scf_real_v21`) |
| tools | `tools/scf_real_scaling.cpp`, `tools/scf_terminal_punct_ablation.cpp`, `tools/fetch_wiki_corpus.py` |
| tests | `tests/test_real_scaling.cpp` (`scf_real_v21_tests`) |
| results | `results/v2_1_real/`, `results/v2_2_ablation/` |
| reports | `reports/SCF_V2_1_REAL_SCALING_REPORT.md`, `reports/SCF_V2_2_TERMINAL_PUNCT_REPORT.md` |

## v2.1: real-corpus scaling of external-category evidence

Full results in `reports/SCF_V2_1_REAL_SCALING_REPORT.md`. v2.1 is a third
independent module (`scf::v21`; library `scf_real_v21`, tool
`scf_real_scaling`, test binary `scf_real_v21_tests`) that takes the v2.0
question to real data: with **no** abstraction, supervision, or new
heuristics, how far does exact-context substitution evidence converge as the
corpus grows from 1e5 to 1e8 tokens?

The corpus is real English text — the spec names FineWeb, but this
environment's network policy denies huggingface.co, so the closest reachable
real corpus is used and documented: the English Wikipedia 2017-10-01 dump
packaged by gensim-data (fetched deterministically by
`03_shared_context_evidence_scaling/tools/fetch_wiki_corpus.py` as a fixed 400 MiB range of the first release
part; ~2e8 tokens after normalization). Per nested scale N the module builds
high-frequency substrings (lengths 1–3, relative min-frequency floor), exact
single-token contexts `C_N(u)`, and shared-context evidence
`I_N(u,v) = C_N(u) ∩ C_N(v)` via context inversion (never an O(n²) pair
enumeration; contexts with more than `hub_cap` distinct substrings are
excluded from pair generation and fully accounted). It reports complete
evidence curves for m ∈ {1,2,4,8,16} (no threshold selection), coverage and
sparsity statistics, shared-context graph component structure (union-find as
a read-only diagnostic only — no transitive merging of substrings),
substitution neighborhoods of fixed probes with cross-scale Jaccard
stability, evidence growth on retained pairs, a held-out replication test by
train-evidence bucket (the core generalization metric), and an optional
POS-purity diagnostic against UD English EWT (labels never enter discovery).
Everything is deterministic and streaming-friendly (compact token ids; raw
text never held in memory), with runtime and peak RSS in the output.

```bash
python3 03_shared_context_evidence_scaling/tools/fetch_wiki_corpus.py data/real/wiki2017_head.txt
build/03_shared_context_evidence_scaling/scf_real_scaling --input data/real/wiki2017_head.txt \
  --output-dir 03_shared_context_evidence_scaling/results/v2_1_real --ud data/real/en_ewt-ud-train.conllu
```

writes `scaling_metrics.csv`, `pair_evidence_scaling.csv`,
`heldout_replication.csv`, and `neighborhood_samples.txt` (committed under
`03_shared_context_evidence_scaling/results/v2_1_real/`).

## v2.2: terminal x punctuation ablation

Full results in `reports/SCF_V2_2_TERMINAL_PUNCT_REPORT.md`. v2.2 adds a minimal
2x2 (+ one diagnostic) ablation on top of the v2.1 machinery — the evidence
definitions, thresholds, hub cap, and ladder are reused verbatim — to
separate the contribution of **sentence-terminal information** from
**punctuation information**:

```text
A: no-terminal    + punctuation-aware
B: no-terminal    + punctuation-free
C: terminal-anchor + punctuation-aware
D: terminal-anchor + punctuation-free
E: no-terminal    + punctuation-aware, sentence-final .?! kept as tokens
```

Sentence segmentation is fixed across conditions (every `.?!` ends a
sentence, as does a document boundary) and the train/held-out split is the
same set of sentences for every condition. `terminal-anchor` inserts a
sentence sentinel `<s>` that behaves exactly like `<doc>`: visible in exact
contexts (a sentence-final token sees right context `<s>`; a complete
sentence span sees `(<s>, <s>)`), never inside a substring, never in the
lexical inventory. `punctuation-free` removes internal punctuation tokens;
the sentence-final `.?!` consumed by segmentation never re-enters A–D, and E
keeps them as ordinary tokens to test whether final punctuation already
leaks the terminal signal. Two new observables — terminal-behavior purity of
evidence pairs and neighborhood terminal-completion rates — join the v2.1
metric set, with `delta_terminal = mean(C,D) − mean(A,B)` and
`delta_punct = mean(A,C) − mean(B,D)` rows in the output.

```bash
build/03_shared_context_evidence_scaling/scf_terminal_punct_ablation --input data/real/wiki2017_head.txt \
  --output-dir 03_shared_context_evidence_scaling/results/v2_2_ablation --ud data/real/en_ewt-ud-train.conllu
```

writes `terminal_punctuation_ablation.csv` and
`ablation_neighborhood_samples.txt` (committed under
`03_shared_context_evidence_scaling/results/v2_2_ablation/`).

