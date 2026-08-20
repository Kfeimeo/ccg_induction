# SCF v1.1/v1.2 implementation notes

## 1. Incremental boundary

V1.1 preserves the v1 finite observed string universe, token/string interners, immutable `ContextTriple` and `ConcatTriple` records, DSU, round-based context/concat canonicalization, fixed-point termination, and union provenance. No equivalence rule was replaced and no unseen string is generated. The v1 tree layer was the changed boundary.

The former automatic projection made every occurrence in a final canonical context bucket a hard constituent. That conflated global contextual equivalence with occurrence-level constituency and made crossing evidence inconsistent. V1.1 retires that projection from the default pipeline. Explicit hard spans and their crossing check remain supported as a separate API.

## 2. Raw contexts versus canonical contexts

The same immutable corpus records now serve two deliberately different relations:

- Equivalence saturation groups by `(find(left), find(right))` on every round. These canonical keys may merge as DSU classes evolve.
- Tree evidence groups once by the exact surface `StringId` pair `(left,right)`, without calling `find()`.

Consequently, raw contexts that later become equivalent still count as independent witnesses. Conversely, an equality derived through DSU transitivity or concat congruence adds no direct witness unless an exact raw context contains both yields.

## 3. Pair witnesses

`EvidenceBuilder` sorts all context records by `RawContextKey` and distinct yield. Each raw bucket receives a stable `RawContextId`. For the distinct yields `u1...uk` in the bucket, the builder emits each canonical `YieldPair{min(ui,uj),max(ui,uj)}` once. A sorted vector of raw-context IDs is retained for every pair.

Logical `ContextTriple` deduplication and per-bucket distinct yields ensure that an exact context contributes at most one witness to a pair. Occurrence frequency and duplicate sentences therefore cannot increase support. Public queries provide both `pair_support(a,b)` and the raw context IDs returned by `pair_witnesses(a,b)`.

## 4. Occurrence-local score and provenance

For occurrence `o=(u,c)`, only alternative yields in its exact raw bucket `c` are examined. Its score is:

```text
max support(u,v), for v != u and c in W(u,v)
```

Score zero occurrences are omitted from the candidate vector. Every candidate stores its occurrence ID, span, surface yield, score, all maximizing alternatives, and the union of their sorted raw witness contexts. A yield never inherits candidate status at an unrelated occurrence, and e-class size is not used.

## 5. Maximum-evidence DP

For leaves, `best[i,i+1]=0` and `count[i,i+1]=1`. For a non-leaf cell:

```text
best[i,j] = spanScore(i,j)
          + max over legal k (best[i,k] + best[k,j])
```

`spanScore` is forced to zero for leaves and the sentence root. For each cell, every split attaining the maximum is stored in `optimal_splits`. The optimal subtree count is the sum of `count[i,k] * count[k,j]` over precisely those splits. Additions and multiplications saturate at `uint64_t` maximum and set `overflowed`.

Explicit hard spans restrict legal splits exactly as in v1. Crossing hard spans still return `hard_consistent=false`. Automatic corpus evidence is passed as scores with an empty hard-span list, so crossing candidates simply compete inside the projective tree space.

## 6. Optimal count and unique reconstruction

`best_score` and `optimal_tree_count` are the root cell values. A split table for concrete reconstruction is produced only when the root count is one. No arbitrary split is selected for ties; the CLI prints `tree=<ambiguous; no tie-break>`.

The legacy `tree_count` and `consistent` result fields remain synchronized with `optimal_tree_count` and `hard_consistent` for source compatibility with the explicit-hard-span API.

## 7. Forced spans among optimal trees

Each DP cell stores the intersection of spans present in all of its optimal subtrees. A leaf set contains the leaf. For every optimal split `k`, its alternative set is:

```text
{[i,j)} union forced[i,k] union forced[k,j]
```

The cell result is the intersection over all optimal splits. Thus suboptimal trees never affect `forced_spans`. Internally the result includes invariant leaves and root; CLI and CSV `FORCED_OPTIMAL` counts filter both, leaving only proper nontrivial spans. Catalan enumeration tests independently compare best scores, optimal counts, and full forced-set intersections.

## 8. Structural non-identifiability

For `simple.txt`, `support(the dog,a cat)=2` while `support(dog runs,dog sleeps)=1`; each length-three sentence therefore has one left-branching optimum. For the complete `C × D × B` corpus in `deep.txt`, the left and right proper spans both score 2. Both binary trees remain optimal, the count is 2, and no proper span is forced. V1.1 treats this symmetry as underdetermination rather than adding a branching preference.

`cartesian.txt` continues to test that the equivalence partition is unchanged. Its sentences have length two, so the only binary tree is optimal and leaf evidence is correctly irrelevant.

## 9. Fixed-point and provenance tests

The record-level cascade fixture has three productive rounds followed by a no-change round:

```text
round 1 context -> concat
round 2 context -> concat
round 3 context
round 4 fixed point
```

It checks the final DSU classes and phase-specific union counts. Equivalence union reasons retain rule, source records, canonical bucket, and round. Evidence provenance separately retains the maximizing alternatives and exact raw contexts; the two provenance systems are intentionally not merged.

## 10. Complexity and memory

Let `M` be observed strings, `O` occurrences, `R` context records, `C` concat records, and `Q` saturation rounds. Saturation remains `O(Q(R log R + C log C))`. Raw context construction is `O(R log R)` plus `O(sum k_c^2)` pair emission for bucket sizes `k_c`. Occurrence scoring enumerates alternatives in each occurrence's raw bucket and performs logarithmic pair lookup.

For sentence length `n`, tree DP uses `O(n^3)` time plus explicit hard-constraint scans and dense `O(n^2)` score, count, split, and forced-set tables. With the default `n<=10`, inspectability is preferred over compact bitsets or worklists.

## 11. Objective limitations

`max_pair_support` captures the strongest repeated direct substitution relation for an occurrence. It does not model lexical ambiguity, dependence between witnesses, paradigm/biclique structure, semantic plausibility, confidence, or corpus sampling bias. Support one is retained, and no frequency, length, balance, left/right branching, threshold, or tie-breaking heuristic is applied.

This objective can leave many trees tied or can prefer an intuitively wrong tree on uncontrolled data. Those outcomes are properties of the stated v1.1 evidence objective, not licenses for silent repair.

## 12. Future work

Possible explicitly new models include top-k or logarithmic aggregation, biclique/Formal Concept Analysis support, MDL, context-sensitive e-classes, learned or probabilistic confidence, lexical ambiguity, and externally supplied semantic constraints. Scaling work may add incremental canonicalization, tries or suffix indexes, external sorting, and parallel pair generation. None is part of v1.1.

# v1.2 additions

## 13. Generator architecture

`include/scf/synthetic.hpp` holds a minimal internal grammar representation (`Rule{lhs,rhs}`, `Grammar{name,start_symbol,rules}`) with the grammar families hardcoded in `make_grammar()`. Terminals are simply symbols that never occur as a `lhs`. `generate_full_language()` recursively expands the start symbol over all rule alternatives with an odometer over child choices, rejecting recursive grammars explicitly (full-language enumeration requires a finite language). Duplicate token sequences with identical tree shape are deduplicated; identical sentences with different shapes are rejected as structural ambiguity, so every benchmark sentence has exactly one gold tree. There is no general JSON grammar parser; `grammar.json` is written by a small deterministic writer.

## 14. Grammar families

`ab_cartesian` (3x3, length 2), `simple_np_vp` (Det/N pairs deliberately bound as `the dog | a cat` so full coverage reproduces the v1.1 `simple.txt` corpus exactly), `symmetric_abc` (2x2x2, gold fixed to `((A B) C)`), `nested_balanced` (2^4, gold `((c d)(e f))`), `right_branching`/`left_branching` (2^4, one-sided nesting), `ambiguous_lexicon` (token `x` in both class A at position 0 and class B at position 3, 36 sentences), and the auxiliary `ccg_lite`.

`ccg_lite` uses a category string representation with a top-level slash splitter (rightmost top-level slash, matching left-associative CCG notation) and a chart-style fixpoint closure over the fixed lexicon with forward/backward application only, capped at 10 tokens. The 84-sentence language is `NP(6) x VP(2 + 2x6)`. Derivations are projected to plain binary brackets; category labels survive only as gold labels.

## 15. Gold tree and span format

`GoldNode{label, children}` with unary-chain collapse (`B -> V -> runs` becomes the leaf `runs`); any remaining non-binary internal node is an error. The compact `GoldTree{length, internal_spans}` stores the sorted length>=2 spans including the root. `gold_spans.tsv` rows are `sentence_id<TAB>begin<TAB>end<TAB>label` with half-open intervals; leaves are implicit (a single `[0,1)` row is emitted for length-1 sentences so every sentence id stays present). The reader tolerates leaf rows and an omitted root, then validates: exactly `n-1` internal spans, and a recursive unique-split decomposition from the root (this simultaneously rejects crossing spans, gaps, and multiply-decomposable span sets). Corpus/gold sentence-count or id mismatches fail loudly. A round-trip test keeps `gold_spans.tsv` and `gold_brackets.txt` consistent.

## 16. Coverage sampling

Full language -> Fisher-Yates shuffle driven by `std::mt19937_64` with unbiased rejection sampling (implemented manually because `std::shuffle` and `std::uniform_int_distribution` are implementation-defined) -> first `ceil(coverage*N)` (with a `1e-9` guard against binary rounding like `0.2*5 = 1.0000000000000002`) -> optional `max_sentences` cap -> canonical order restored by sorting selected indices. Byte-identical outputs for identical seeds across platforms; `full_sentence_count` and `sampled_sentence_count` are recorded in `grammar.json` and `metrics.json`. No timestamps are written anywhere, so determinism is unconditional.

## 17. Evaluator design

`evaluate_sentence()` consumes the unchanged `TreeSolveResult`. Gold score sums span evidence over gold proper nontrivial spans exactly as the DP objective does (leaves and root never scored), so `gold_in_argmax <=> gold_score == best_score`. Predicted spans exist only for unique optima and are extracted from `unique_tree_splits`; ambiguous sentences never expose a prediction (the CLI prints one optimal tree only under `--dump-one-optimal-tree-for-debug`, explicitly labeled as a debug sample). Matching and F1 use `EvalConfig{include_root_in_eval,include_leaves_in_eval}`, both false by default; the both-empty span case (length <= 2) defines P=R=F1=1. `evaluate_corpus()` aggregates all metrics of the specification including outcome counts, and `collapse_diagnostics()` adds `largest_eclass_ratio` and the `suspicious_collapse` flag (`collapse_ratio > 0.8` or `largest_eclass_ratio > 0.25`, thresholds configurable).

## 18. Brute-force enumerator role

`enumerate_binary_trees(n)` (recursive, span-set trees, capped at n <= 12) is the correctness anchor: it recomputes best score and argmax count against the DP in tests, and supplies `second_best_score`/`margin` in the evaluator for n <= 10. Margin is defined over the descending multiset of all tree scores, which reconciles the specification's two margin clauses: ambiguous optima get margin 0 (`zero_margin_rate == ambiguous_optimal_rate`), unique optima get the gap to the best remaining tree, single-tree sentences get `NA`, and `all_trees_tied` marks a single distinct score. This is explicitly not a large-scale mechanism; the main parser stays on the DP.

## 19. Metric definitions and ambiguity classification

`unique_optimal_rate`, `ambiguous_optimal_rate`, `exact_unique_match_rate` (all-sentence denominator), `exact_unique_match_given_unique` (`NA` when no unique optima), `gold_in_argmax_rate`, `mean/median_argmax_size`, `mean_best_score`, `mean_gold_score`, `zero_margin_rate`, `mean_finite_margin` (mean over sentences with a defined margin; ambiguous contribute 0), and precision/recall/F1 given unique. Outcomes: `UNIQUE_CORRECT`, `UNIQUE_WRONG`, `AMBIGUOUS_GOLD_INCLUDED`, `AMBIGUOUS_GOLD_EXCLUDED`, `HARD_INCONSISTENT` (the last only reachable through explicit hard-span conflicts, kept for API completeness). Summary rows count all five.

## 20. Diagnostics

`top_eclasses.txt` lists the 20 largest e-classes (member listing capped at 50). `failure_examples.txt` records up to 50 sentences that are `UNIQUE_WRONG`, `AMBIGUOUS_GOLD_EXCLUDED`, or `HARD_INCONSISTENT`, each with gold/predicted trees, scores, missing/extra spans, and the sentence's span evidence table. `sentence_metrics.tsv` exposes the full per-sentence diagnostic row including forced optimal spans.

## 21. Empirical identifiability results

At full coverage: `nested_balanced` is uniquely identifiable (gold spans get support 4, crossing spans at most 2, margin 2). `right_branching` and `left_branching` produce evidence tables identical to `nested_balanced` under Cartesian lexical sampling, so both converge on the balanced tree (`UNIQUE_WRONG` everywhere, and exactly mirror one another — the enforced regression is the symmetry itself, demonstrating the absence of a hidden branching bias, not an impossible exact recovery). `symmetric_abc` reproduces `deep.txt`-style non-identifiability (`gold_in_argmax_rate = 1`, argmax size 2). `ambiguous_lexicon` collapses to 10 e-classes with a 36-member class (`collapse_ratio ≈ 0.88`), which the suspicious-collapse diagnostic must and does flag.

## 22. CCG-lite and real-data limitations

CCG-lite is a bracketing sanity check for an application-only fragment: no type raising, composition, coordination, modifiers, punctuation, or semantics, and it must not be treated as CCG induction or as the main benchmark. `scf_prepare_text` performs naive splitting/normalization for smoke tests only; its report never claims parse accuracy because real corpora carry no gold.

## 23. Current performance limits

Everything remains single-threaded and n <= 10 by default. The evaluator's per-sentence brute force enumerates at most Catalan(9) = 4862 trees; batch experiments over the default 7x5 grid on the largest built-in family (`ccg_lite`, 84 sentences) complete in seconds. Larger sentence lengths require disabling the brute-force pass (margins become `NA`) and are out of scope for v1.2.
