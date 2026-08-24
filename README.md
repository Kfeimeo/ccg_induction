# SCF — Symbolic Context Factorization v1.2

SCF is a correctness-first C++20 research prototype for discovering anonymous string equivalence and maximum-evidence projective binary structure from tokenized sentences. It uses no tags, labels, probabilities, embeddings, neural models, or external runtime libraries.

v1.2 adds a controlled synthetic benchmark, a gold evaluator, batch experiments, and identifiability diagnostics on top of the unchanged v1.1 core. Its purpose is to make one research question measurable:

> When does direct surface substitution evidence uniquely determine unlabeled binary structure, and when is structure underdetermined?

## Build and run

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The project test launcher also makes the requested shorter command work after the Release build, including with Visual Studio's multi-configuration generator:

```bash
ctest --test-dir build --output-on-failure
```

Run the supplied corpus:

```bash
build/Release/scf_cli --input data/synthetic/simple.txt --stats --dump-classes --dump-witnesses --dump-evidence --dump-trees
```

On Linux or a single-configuration Windows generator, the executable is normally `build/scf_cli`. The independent generator recreates all supplied controlled corpora:

```bash
scf_synthetic_generator data/synthetic
```

Important CLI options are `--max-len`, `--lowercase`, `--deduplicate`, `--config`, `--stats`, `--dump-classes`, `--dump-proofs`, `--dump-witnesses`, `--dump-evidence`, `--dump-optimal-forest`, and `--dump-trees`. Supplying `--output-dir DIR` writes:

- `saturation.csv`
- `pair_witnesses.csv`
- `span_evidence.csv`
- `sentence_analysis.csv`
- `eclasses.txt`
- `proofs.txt`

Input is one ASCII-whitespace-tokenized sentence per line. Empty lines are ignored. The defaults in `config/default.cfg` impose length 10, preserve case, deduplicate sentence types, disable global occurrence consistency, use `strict_global` equivalence, count exact raw surface contexts, aggregate with `max_pair_support`, and exclude leaf/root scores.

## Research semantics

### Finite observed universe

For every retained sentence type, preprocessing interns every non-empty contiguous substring plus one distinguished epsilon string. This finite set is the complete universe. Saturation only merges IDs already in that set; it never synthesizes a new non-empty surface string.

Every non-empty span creates an immutable occurrence `(sentence, begin, end, yield, left_context, right_context)`. Logical context triples are deduplicated by `(left, right, yield)` while retaining all source occurrence IDs. The whole-sentence context `(epsilon, epsilon)` is excluded. Every distinct observed string also contributes all of its internal binary surface decompositions as deduplicated concat triples.

### Context substitution

At the start of each context phase, records are keyed dynamically by:

```text
(find(left_context), find(right_context))
```

All distinct yields in one bucket are star-unioned. Literal context strings are not treated as permanently different: later equality can make two context signatures coincide.

### Concatenation congruence

Every observed decomposition `left + right = result` is keyed by:

```text
(find(left), find(right))
```

Results in one bucket are star-unioned. A concat triple is an algebraic surface fact, not a proposed syntax split. Congruence propagates equality upward only to observed result strings.

### Fixed point and context representation

Context and concat records are canonicalized again every round. Contexts deliberately have no separate union-find: context equivalence is exactly the product of the two current string e-classes. This is why a new string equality can merge context buckets on a later round and cause another equality, which can in turn enable more concat congruence.

With `M` observed strings, every successful union reduces the number of e-classes by one. There are at most `M-1` successful unions, no surface strings are generated, and a final no-change round terminates the algorithm.

### Provenance

Every successful DSU merge stores its two surface endpoints, rule kind, source record pair, canonical bucket key, and round. These edges form a proof forest over each final e-class. `proof_chain()` recovers a trace between any two equivalent strings; `--dump-proofs` and `proofs.txt` expose the individual edges.

## Contextual equivalence is not constituency

Equivalence is global over surface strings, but `u ≡ v` does not imply that occurrences of `u` and `v` must be constituents. Saturation may derive equality by canonical-context and concatenation cascades; those derived equalities remain valid algebraic facts but do not create tree evidence. Concat decompositions are likewise never interpreted as syntax.

Corpus substitution evidence is not converted to hard spans. The hard-constraint API is retained for explicit external axioms and tests; only crossing explicit hard spans make `hard_consistent=false`.

## Repeated substitutability as evidence

For exact surface context `c=(L,R)`, define `W(u,v)` as the set of distinct raw contexts in which both distinct yields occur. `support(u,v)=|W(u,v)|`. These contexts use original `StringId` identity, never final DSU representatives; repeated occurrences and duplicate sentences do not increase support.

Evidence remains occurrence-local. An occurrence `(u,c)` is a candidate only when `c ∈ W(u,v)` for some `v≠u`, and its score is:

```text
max over v with c in W(u,v) of support(u,v)
```

The builder records every maximizing alternative and its raw witness contexts as provenance. It never scores by e-class size, frequency, or a sum over alternatives.

## Maximum-evidence binary trees

The CKY-style DP maximizes the sum of evidence over proper nontrivial spans. Leaf and root evidence is always ignored. Every maximizing split is retained, and `optimal_tree_count` counts all trees in the packed optimal forest with checked `uint64_t` arithmetic. Crossing candidates are competing hypotheses: projectivity ensures that a tree selects at most one, without declaring an inconsistency.

`FORCED_OPTIMAL` spans occur in every maximum-scoring tree, not in every suboptimal or merely hard-consistent tree. They are computed by intersecting span sets over all optimal split alternatives. A concrete bracket tree is reconstructed only when `optimal_tree_count == 1`; otherwise the result remains explicitly ambiguous. Tests compare best score, optimal count, and forced spans with exhaustive Catalan enumeration.

## Structural identifiability

`simple.txt` has repeated evidence of support 2 for the left two-token phrases and support 1 for the competing right phrases, so all four sentences have a unique left-branching optimum. In contrast, the complete `C × D × B` design in `deep.txt` gives symmetric support to `[0,2)` and `[1,3)`. Both binary trees are optimal for every sentence, `optimal_tree_count == 2`, and there are no proper forced-optimal spans. This is correct non-identifiability under the stated symbolic objective, not a conflict to repair with a branching heuristic.

## Scope and known risk

SCF v1.1 intentionally does not implement CCG, PCFG probabilities, POS or constituency labels, neural representations, learned confidence, thresholds, branching bias, biclique/FCA or MDL objectives, Max-CSP, beam search, tries, GPUs, or distributed processing. It is single-threaded and uses contiguous integer-ID records, sort/group passes, and a DSU.

`strict_global` is deliberately strong. Lexical ambiguity, polysemy, syntactic ambiguity, or accidental substitution may initiate a congruence cascade and catastrophic e-class collapse. V1.1 does not repair equivalence with frequency, context-sensitive classes, or learned scores because its purpose is to test the symbolic axioms on controlled corpora.

## Why no dynamically updated Trie

Sentence length is at most 10 and all spans must already be enumerated. Token-vector interning plus immutable concat records keeps the observed-universe invariant explicit and inspectable. A dynamic Trie would complicate the correctness argument without changing v1.1 semantics. Prefix/reverse tries, suffix indexes, radix/external sorting, mmap storage, worklists, and parallel canonicalization are future scaling work.

## v1.2: synthetic benchmark and gold evaluator

### Why a synthetic benchmark first, and why CFG-style data

SCF learns `surface substitution evidence -> unlabeled binary bracketing`, not word-to-category or sentence-to-derivation mappings. CFG-style latent-tree grammars are therefore the v1.2 mainline: the gold tree is unambiguous, coverage, recursion depth, structural symmetry, and lexical ambiguity are all controllable, and evaluation stays purely on unlabeled binary spans without importing CCG derivational ambiguity or category induction. Real corpora have none of these controls, so they are restricted to a smoke test.

The `ccg_lite` family is auxiliary only. It supports the categories `NP, N, S, NP/N, S\NP, (S\NP)/NP` with forward/backward application and nothing else, and projects each derivation to an ordinary binary bracket. This is not full CCG induction; it is only a bracketing sanity check for application-only CCG fragments, and it must not replace the CFG-style benchmark.

### Grammar families

`ab_cartesian`, `simple_np_vp`, `symmetric_abc`, `nested_balanced`, `right_branching`, `left_branching`, `ambiguous_lexicon`, and the auxiliary `ccg_lite`. `scf_generate --list-grammars` prints the list. The generator emits `corpus.txt` (the only file the parser may see), `gold_spans.tsv` (`sentence_id begin end label`, half-open `[begin,end)`, labels for evaluation/debug only), `gold_brackets.txt`, and `grammar.json` (full configuration: seed, coverage, counts, rules). Gold labels and grammar nonterminals are never inputs to SCF.

### Running the generator

```bash
build/scf_generate --grammar nested_balanced --coverage 0.4 --seed 42 \
  --output-dir data/generated/nested_balanced_cov040_seed42
```

Coverage sampling generates the full finite language, applies a deterministic seeded Fisher-Yates shuffle (custom bounded sampling over `std::mt19937_64`, so results are identical across platforms), keeps the first `ceil(coverage * N)` sentences, applies the optional `--max-sentences` cap, and restores canonical order. Identical seeds reproduce byte-identical datasets; `grammar.json` records everything needed to regenerate them.

### Running the evaluator

```bash
build/scf_cli --input data/generated/.../corpus.txt \
  --gold data/generated/.../gold_spans.tsv --eval --stats
```

`--eval` prints corpus-level metrics; with `--output-dir` it also writes `metrics.json`, `sentence_metrics.tsv`, `failure_examples.txt`, and `top_eclasses.txt`. The evaluator compares proper nontrivial spans, excluding leaves `[i,i+1)` and the root `[0,n)` by default (both configurable in code via `EvalConfig`). Length-2 sentences have empty proper span sets on both sides; their F1 is defined as 1. Gold files may omit the root span; missing sentences or spans that do not form a projective full binary tree fail loudly.

### Gold-in-argmax and ambiguity-aware success

`gold_in_argmax` holds when `Score(T_gold) == Score*`, i.e. the gold tree attains the optimal DP score, whether or not the optimum is unique. It is the central ambiguity-aware metric. `exact_unique_match` is stricter: the optimum must be unique *and* equal to gold. The difference matters because non-identifiability is a first-class outcome: when the corpus genuinely underdetermines structure (e.g. `symmetric_abc` at full coverage), the correct behavior is `gold_in_argmax_rate = 1.0` with `unique_optimal_rate = 0`, not a forced tie-break. Every sentence is classified as one of `UNIQUE_CORRECT`, `UNIQUE_WRONG`, `AMBIGUOUS_GOLD_INCLUDED`, `AMBIGUOUS_GOLD_EXCLUDED`, or `HARD_INCONSISTENT`; reports must not reduce these to a single "accuracy". Ambiguity with gold included is a meaningful result, not simply failure.

For sentences of length <= 10 the evaluator additionally enumerates all Catalan trees to compute `second_best_score` and `margin` (the second element of the descending multiset of all tree scores, so ambiguous optima have margin 0 and single-tree sentences report `NA`), and to cross-check the DP `optimal_tree_count`. This brute-force pass is a correctness anchor for small synthetic corpora, not a scalable mechanism.

### Running batch experiments

```bash
build/scf_experiment --grammar simple_np_vp \
  --coverage-grid 0.2,0.6,1.0 --seeds 1,2 --output-dir results/simple_np_vp
```

Each `cov_X_seed_Y` run directory contains `corpus.txt`, `gold_spans.tsv`, `gold_brackets.txt`, `grammar.json`, `scf_output.txt`, `metrics.json`, `sentence_metrics.tsv`, `saturation.csv`, `top_eclasses.txt`, and `failure_examples.txt`; the root gains `summary.csv`.

### Interpreting summary.csv

One row per (grammar, seed, coverage) with corpus statistics (`distinct_strings`, `context_triples`, `concat_triples`), collapse diagnostics (`final_eclasses`, `collapse_ratio`, `largest_eclass`, `largest_eclass_ratio`, `suspicious_collapse` — flagged when `collapse_ratio > 0.8` or `largest_eclass_ratio > 0.25` — and `successful_unions`), all corpus-level metrics (`unique_optimal_rate`, `ambiguous_optimal_rate`, `exact_unique_match_rate`, `exact_unique_match_given_unique`, `gold_in_argmax_rate`, `mean/median_argmax_size`, `mean_best_score`, `mean_gold_score`, `zero_margin_rate`, `mean_finite_margin`, unlabeled precision/recall/F1 given unique), and the five outcome counts. `exact_unique_match_rate` uses all sentences as its denominator; the `_given_unique` variants use only unique-optimal sentences and print `NA` when that set is empty. `zero_margin_rate` equals the fraction of ambiguous-optimal sentences; `mean_finite_margin` averages over sentences where a margin exists (>= 2 candidate trees), with ambiguous sentences contributing 0.

### Empirical coverage-curve behavior (first full runs)

- `simple_np_vp`: at full coverage every sentence is `UNIQUE_CORRECT` (all rates 1.0); at low coverage single sampled sentences have no substitution evidence and stay ambiguous.
- `symmetric_abc`: even at coverage 1.0, `gold_in_argmax_rate = 1.0`, `unique_optimal_rate = 0`, `mean_argmax_size = 2` — exact structural non-identifiability, the `deep.txt` phenomenon reproduced under the benchmark.
- `nested_balanced`: at full coverage the gold constituents `[0,2)` and `[2,4)` each collect support 4 while the crossing spans `[1,3)`, `[0,3)`, `[1,4)` collect at most 2, so recovery is unique and exact with margin 2; lowering coverage degrades this smoothly.
- `right_branching` / `left_branching`: the correct reading of these curves has two separate layers (established by the v1.2.1 audit, see `SCF_v1_2_1_AUDIT.md`). **Layer 1:** the balanced / left / right latent grammars are observationally equivalent under the full-factorial surface generator — `right_branching` and `left_branching` generate the byte-identical corpus, and `nested_balanced` the same language up to token renaming, so no algorithm consuming these observations could distinguish the three latent grammars. **Layer 2:** given that shared observable corpus, the current raw-witness objective prefers the balanced tree because shorter spans receive more external-context witnesses (`support(i,j) = q^{n-(j-i)}`), an objective-induced balance preference with no left/right directional component (`left_score == right_score` on every sentence). These are two different findings; neither is a directional bias, and the first is not an SCF failure at all.
- `ambiguous_lexicon`: the shared token `x` triggers a congruence cascade (`collapse_ratio ≈ 0.88`, `suspicious_collapse = true`). That is the diagnostic target of this family, not a failure of v1.2.

## v1.2.1: observational equivalence and the identifiability audit

### Formal definitions

Two latent grammars are **observationally equivalent** when they induce the
same observations:

```text
G1 ~obs G2  <=>  Obs(G1) = Obs(G2)
```

Under the current set-based generator the observation of a grammar is its
surface language, so the implemented notion is support-level equivalence:

```text
Obs(G) = L(G)          =>          G1 ~support G2  <=>  L(G1) = L(G2)
```

If a future generator emits frequencies, the finer distribution-level notion
becomes available (`G1 ~distribution G2 <=> P_G1(s) = P_G2(s) for all s`);
v1.2.1 implements support-level diagnostics only. Every synthetic run records
four audit hashes in `summary.csv` and `metrics.json` — `surface_language_hash`
(sorted full language, independent of labels, gold trees, and generation
order), `sampled_corpus_hash`, `raw_context_relation_hash` (deduplicated
`(L,R) -> yield` triples), and `raw_witness_relation_hash` (the
`yield_pair -> set(raw_context)` relation exactly as the tree objective
consumes it). `scf_audit` compares families pairwise, both exactly and up to a
greedy canonical token renaming, and emits
`observational_equivalence_report.txt`. A case where surface languages agree
but gold trees differ is reported as *latent grammars observationally
indistinguishable under current observations*, never as "SCF failed to recover
the grammar".

### Audit findings (v1.2.1)

The full audit lives in `SCF_v1_2_1_AUDIT.md`; headline results:

- **Saturation is computationally decoupled from parsing.** In v1.2, the
  equivalence-saturation engine and the raw-witness tree-induction engine are
  computationally decoupled: rerunning the entire 280-run grid with saturation
  disabled (`scf_experiment --run-saturation false`, or the in-process
  ablation in `scf_audit`) changes zero parse outputs
  (`parse_outputs_changed_runs = 0`). Saturation remains a structural
  diagnostic but does not causally affect the tree objective.
- **Span-length support law.** In a full Cartesian language with class size
  `q`, a span of length `l` in a length-`n` sentence receives exactly
  `q^(n-l)` raw external contexts; verified exactly for `n = 4`,
  `K = 2, 3, 4`. Shorter spans therefore always out-score longer ones, which
  yields the objective-induced balance preference
  (`balanced = 2K^2 > left = right = K^2 + K`).
- **Population vs sample identifiability.** Full-coverage analysis fixes each
  family's population status; sampled runs are then classified as
  `SAMPLE_IDENTIFIED_CORRECTLY / SAMPLE_IDENTIFIED_WRONGLY / SAMPLE_AMBIGUOUS`
  against it. For `symmetric_abc`, 62/140 sampled runs contain spurious-unique
  sentences and 42/140 contain spurious-wrong-unique ones, while symmetry is
  restored in 20/20 full-coverage runs — the non-monotone curves are pure
  finite-sample symmetry breaking.
- **Correlated families break observational equivalence.** The new
  `hierarchical_correlated_balanced/right/left` families correlate tokens
  inside latent blocks, so all pairwise surface hashes differ (even after
  renaming). The balanced variant is fully identifiable; in the chain variants
  the top-level split is recovered (forced span) while the internally frozen
  block stays population-ambiguous with gold in the argmax — structure is
  recovered exactly where the surface distribution actually varies.
- **Graded symmetry breaking.** `--symmetry-breaking-rate rho` adds
  `ceil(rho*K^2)` marker sentences `a_i b_j p` to `symmetric_abc`; unique
  recovery rises monotonically with rho (0 at rho=0 to 1.0 at rho=1), giving
  an identifiability-vs-evidence curve rather than accuracy-vs-corpus-size. A
  single marker (rho=0.05) is a documented edge case that briefly misleads
  the objective before two or more markers make the block observable.

### v1.2.1 CLI additions

`scf_generate` / `scf_experiment` accept `--lexical-cardinality K` (2..5,
0 = family default; `nested_balanced --lexical-cardinality 4` yields the
256-sentence language used for the 20-seed audit grid) and
`--symmetry-breaking-rate RHO` (symmetric_abc only). `scf_experiment` also
accepts `--run-saturation false` and writes per-run
`score_by_span_length.csv`; `summary.csv` gained `requested_coverage`,
`effective_coverage` (= sampled/full — plot against it or absolute N, not
only nominal coverage; tiny languages make nominal coverage a step function),
`lexical_cardinality`, `symmetry_breaking_rate`, and the four audit hashes.
The audit itself is one deterministic command:

```bash
build/scf_audit --output-dir results/audit_v1_2_1 --seeds 20
```

### Real-data smoke test

```bash
build/scf_prepare_text --input raw.txt --output corpus.txt
```

applies simple sentence splitting, optional lowercasing/punctuation stripping (defaults `max_len=10`, `lowercase=true`, `strip_punctuation=true`, `deduplicate=true`), optional digit/symbol filtering, and writes `real_smoke_report.txt` with preprocessing counts, collapse diagnostics, unique/ambiguous rates, top e-classes, and example trees. No parse accuracy is claimed for real corpora: there is no gold.

### Known limitations of v1.2

Brute-force margins and gold-in-argmax validation require length <= 10. Coverage sampling operates on full finite languages, so grammars must be non-recursive. `symmetric_abc` gold is fixed to `((A B) C)`. `ccg_lite` covers application only. `strict_global` equivalence still collapses under lexical ambiguity by design — v1.2 measures and reports this rather than repairing it. There is no probabilistic model, no CCG category induction, no Treebank reader, and no parallelism.

## Layout

Public interfaces live under `include/scf`, implementations under `src`, controlled data under `data/synthetic`, the generators and experiment tools under `tools` (`scf_generate`, `scf_experiment`, `scf_prepare_text`, plus the v1.1 `scf_synthetic_generator`), and the assert-based regression suite under `tests`. `IMPLEMENTATION_NOTES.md` documents data structures, complexity, correctness caveats, and specification inconsistencies found during implementation.
