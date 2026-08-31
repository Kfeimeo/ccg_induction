# SCF v1.1/v1.2/v1.2.1/v1.3/v1.4 implementation notes

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

# v1.2.1 audit additions

## 24. Audit hashes

`audit.hpp/cpp` serializes each relation canonically and hashes with FNV-1a 64 (standard-specified arithmetic, so hashes are platform-stable). `surface_language_hash`: sentences as token sequences, lexicographically sorted, deduplicated — independent of grammar labels, gold trees, and generation order. `sampled_corpus_hash`: the same over the sampled corpus. `raw_context_relation_hash`: the deduplicated `(L, R) -> yield` triples serialized as token text. `raw_witness_relation_hash`: for every yield pair with positive support, the pair plus its sorted raw-context set — precisely the object the tree objective consumes. All four appear per run in `summary.csv` and `metrics.json`.

## 25. Canonical token renaming

`build_canonical_renaming` renames tokens `t0, t1, ...` in first-occurrence order over the lexicographically sorted sentence set; relation hashes accept the mapping and serialize with renamed tokens. This greedy scheme is a heuristic canonical form: it provably canonicalizes the factorized families audited here (sorted full Cartesian products with per-position alphabets), which is what the audit needs to certify that `nested_balanced` is isomorphic to `right_branching`/`left_branching`; it is not a general graph canonization and may fail to align token orders for arbitrary languages (a false "different" is possible, a false "same" is not, since equal renamed serializations are an explicit isomorphism witness).

## 26. Saturation ablation result and data path

The 280-run A/B rerun (`scf_audit`, also reachable via `scf_experiment --run-saturation false`) found `parse_outputs_changed_runs = 0`. The causal explanation is structural: `EvidenceBuilder` takes only `const Corpus&` and buckets context records by exact raw `StringId` pairs without consulting the DSU; `solve_maximum_evidence_trees` consumes only that evidence. The solver's outputs feed diagnostics exclusively. The decoupling statement is now normative in the README, and `saturation_is_decoupled_from_parsing` pins it as a regression test so any future coupling must be introduced deliberately.

## 27. Span-length support law and balance preference

For the full Cartesian language `X1 x ... x Xn`, span `[i, j)` recurs under `prod_{k<i}|Xk| * prod_{k>=j}|Xk|` distinct raw external contexts, i.e. `q^(n-(j-i))` under uniform class size `q`; every same-length yield pair shares that full context set, so measured `max_pair_support` equals the theoretical count exactly (verified for n=4, K=2,3,4 in `span_length_bias.csv`). Consequence for length-4 trees: `balanced = 2q^2`, `left = right = q^2 + q` — an objective-induced balance preference with provably zero directional component. `score_by_span_length` reports per-length totals over *all* proper spans (zero-score spans included; `candidate_span_count` counts positive ones).

## 28. Hierarchical correlated families

`hierarchical_correlated_balanced` (`S = {a_i b_i} x {c_j d_j}`, K^2 sentences), `_right` (`S = A x {b_j (c_j d_j)}`), and `_left` (`S = {(a_j b_j) c_j} x D`) express bracketing in the surface distribution itself; all pairwise surface hashes differ, including after renaming, and also differ from the Cartesian counterpart (Acceptance F, guarded by tests). Empirically the balanced variant is fully identifiable (block spans substitute with K shared contexts), while the chain variants recover the top-level split as a forced span but keep the internally frozen block's bracket population-ambiguous (argmax 2, gold included) — identifiability tracks exactly where the surface varies.

## 29. Symmetry-breaking rate

`--symmetry-breaking-rate rho` (symmetric_abc only) appends `ceil(rho * K^2)` marker sentences `a_i b_j p` in canonical (i, j) order, each with gold `((a_i b_j) p)`. Two or more markers put two distinct yields in the `(epsilon, p)` context, raising `[0,2)` support to K+1 against the `[1,3)` tie at K, and unique recovery grows monotonically with rho. The single-marker regime is a documented anomaly: `(epsilon, p)` then has no substitution pair, while `b_i p ~ b_i c_k` in the shared `(a_i, epsilon)` context makes the marker sentence itself UNIQUE_WRONG — weak symmetry-breaking evidence can briefly mislead the objective. Markers live outside the CFG rule list; `grammar.json` records `symmetry_breaking_rate` and counts instead.

## 30. Audit grid, population vs sample, and aggregation

`scf_audit --seeds N` (default 20) runs 9 configurations (the five mainline CFG families, `nested_balanced` K=4 with 256 sentences, and the hierarchical trio) over the 7-point coverage grid, recording per-run metrics plus `effective_coverage = sampled/full` and the population reference computed once per configuration at coverage 1.0 (`population_identifiable`, gold-in-argmax rate, mean argmax). `finite_sample_symmetry_breaking.csv` classifies `symmetric_abc` sentences per run into `spurious_unique` (population-ambiguous, sample-unique) and `spurious_wrong_unique` (population gold-in-argmax, sample-unique, prediction != gold). Aggregates report mean/std/min/max and a normal-approximation 95% CI over seeds. All of this is diagnostics-side; the parser and its defaults are untouched (Acceptance G).

# v1.3 additions

## 31. Evidence objective laboratory

`EvidenceBuilder` now takes an `EvidenceObjective` (default `RawCount`). The shared witness structure (raw buckets, pair witness lists, |C(u)| per yield, geometry table) is built once; the objective only changes the pair strength. `opportunity` is occurrence-local: every context in one raw bucket shares the geometry `g = (|L|, |R|)`, so `U_g` (distinct observed contexts at that geometry) and `W_g(u,v)` (the pair's witnesses filtered to `g`) are exact; the denominator is never zero because the occurrence's own context belongs to `U_g`. `conditional` and `jaccard` are geometry-free ratios over deduplicated exact context sets; both are symmetric and bounded in [0, 1] (test-guarded). The pair table (`pair_evidence.tsv`) reports strength, shared/`|C|` counts, and for `opportunity` the best geometry's universe size.

## 32. Fixed-point scores and the tie epsilon

Normalized strengths are doubles in [0, 1]; the tree DP stays exact-integer by quantizing `score = round(strength * 1e12)` (`kStrengthScale`). Sums over spans then compare exactly, so "ties within 1e-12 of strength" are exact integer ties — the documented epsilon semantics — and the ambiguity-preserving forest machinery is untouched. `raw_count` keeps its unscaled integer semantics, so all v1.1/v1.2 score assertions hold verbatim. `SpanScore.score`/`SpanEvidence.score` widened to `uint64_t`; `strength` (double) and `confidence` (`|W(u,v*)|` at the argmax alternative, max over ties) ride along for diagnostics. Ranking never uses confidence.

## 33. Observable gold and forced-span metrics

`GoldSentence.observable_spans` (empty = same as full latent gold) restricts what the evaluator may demand; only the correlated chains set it (right: `{[1,4)}`, left: `{[0,3)}`), because their frozen blocks never vary internally. `write_dataset` emits `gold_observable_spans.tsv` alongside the latent `gold_spans.tsv`. Per sentence, `F_s` = proper spans forced across the whole optimal forest (from `forced_spans`, leaves/root stripped); `forced_precision = |F∩G|/|F|`, `forced_recall = |F∩G|/|G|`, computed against both golds, with `|F|=0 ⇒ precision 1` and `|G|=0 ⇒ recall 1`. Corpus means are exported in `metrics.json`, `summary.csv`, `forced_span_metrics.csv`, and the printed evaluation block.

## 34. Objective benchmark results (engineering summary)

Cartesian neutrality: all normalized objectives yield exact `balanced = left = right` ties (argmax 5) on `nested_balanced` for K = 2..5, while `raw_count` reproduces `2K² > K²+K`. `conditional`/`jaccard` fail Properties 2–3: normalizing by a yield's own context count awards 1.0 to single-witness coincidences, producing UNIQUE_WRONG parses on the correlated chains (gold-in-argmax 0 at full coverage) and anti-learning curves on `simple_np_vp`. `opportunity` matches `raw_count` wherever `raw_count` is right and fixes it where it is biased; its one measured regression is the rho sweep, where symmetry-breaking markers act purely through witness counts that the normalization divides away. Default unchanged (`raw_count`); the recommendation is recorded, not enacted.

## 35. Real-corpus constraint audit

`scf_real_audit` performs transparent whitespace-only preprocessing (no external tokenizer), a deterministic seeded scale sweep (`deterministic_shuffle`, same-input/same-seed byte-identical outputs — verified by diffing two runs), and computes: raw-context degree distributions (`deg(c)` = distinct yields per exact `(L,R)`), yield context/substitution degrees with percentiles and length buckets, proper-span occurrence witness coverage (with per-objective mean strengths), exact-context sparsity (singleton/repeat ratios), the two density proxies `R_proxy = witness_pairs / nontrivial_yields` and `R_occ` (labeled proxies, not rank arguments), full saturation diagnostics, and polysemy-pressure rankings (`contexts_total ≥ 5`, sorted by low `partner_overlap_ratio` — candidates only; no split). The regime classifier is rule-based and stated in the generated `REAL_CONSTRAINT_AUDIT.md`. Measured on the local documentation corpus: density rises with N while exact contexts stay 94% singleton and saturation collapses (86% single e-class at N=10,000) — regime `collapse-dominated`. Runtime: ~2.5 min for the full sweep, dominated by saturation at N ≥ 5,000.

## 36. What v1.3 deliberately did not do

No polysemy or lexical splitting (diagnostics only), no saturation semantics change (still parse-inert; v1.2.1 ablation and its regression test remain valid), no default-objective switch, no length penalty / branching prior / span bonus / gold-aware normalization / probability model / neural scorer, and no real-data parsing-accuracy claims.

# v1.4 additions

## 37. Context-indexed solver

`ContextIndexedSolver` (context_indexed.hpp/cpp) iterates `A_0(x)=x -> P_t -> A_{t+1}` over the logical context triples (set semantics makes occurrence multiplicity irrelevant; the whole-sentence hole is already excluded at corpus build time). Each round sorts the `(ContextKey, yield)` pairs once (`O(T log T)`), groups blocks, and re-partitions by exact signature equality using canonical first-occurrence class numbering, so identical partitions are identical vectors and the fixed point is a plain vector comparison. Epsilon carries a reserved sentinel signature and stays a singleton forever. Coarsening is invariant by induction (A_1 refines nothing, and images of equal profiles stay equal), hence `#classes` is non-increasing; a violation sets `monotonicity_violated()` instead of being repaired. No DSU union ever crosses a ContextKey.

## 38. Two theorems

**One-round idempotence (context_only).** Every context coordinate is a full sentence prefix (or suffix), so it has its own observed occurrence whose context is the complementary hole: if `P0(L1)=P0(L2)` then `(eps, x·R) in P0(L1) <=> sentence L1·x·R exists <=> (L1,R) in P0(x)`, and the same sentence-existence chase transfers every element — so profiles equal after round-1 renaming are already equal exactly, and `A* = A_1`. Verified empirically by the 100-seed naive-reference property test.

**Relation invariance (both signatures).** By induction, every pair that merges in any round (context_plus_concat merely delays merges, it never adds any) has exactly equal round-0 profiles; the same element chase then shows the exact blocks of the two merged coordinates contain identical yield sets, so fusing keys never creates a new locally related yield pair and never adds evidence coverage. `distinct_pairs_round0 == distinct_pairs_final` and `indexed_evidence_coverage == raw_evidence_coverage` hold on all synthetic families and on the real corpus at every N; both are pinned by tests/CSV columns. Genuinely new relations require a coarser context notion than exact prefix/suffix strings — deliberately out of scope (v1.5 candidate).

The `recursive_context_cascade` corpus ("w a m"/"w b m") shows the cascade that *is* possible: under context_plus_concat the decomposition signature delays merges into three genuine rounds (a~b; then "a m"~"b m" and "w a"~"w b"; then the sentences), traced in `recursive_cascade_trace.txt`. Under context_only the same corpus saturates in one round, as the theorem requires.

## 39. Diagnostics, attribution, and naming

Per-round stats (classes, keys, `(key, pair)` relation counts, merges, largest ratios) feed `context_indexed_rounds.csv`. Final diagnostics separate `ContextAbstractionClass` collapse from `LocalRoleBlock` sizes and from the diagnostic-only unindexed projection graph; the three-stage real-data attribution (`collapse_attribution.csv`) compares the raw direct substitution graph (Stage A), the indexed projection graph (Stage B), and the legacy global DSU (Stage C, whose largest class is its giant component). `global_vs_indexed.csv` sets legacy collapse against the indexed model's true collapse indicators (`indexed_largest_context_class_ratio`, `indexed_max_local_block_ratio`) — the projection giant is explicitly not one of them. Multi-role membership (`keys_of_yield`, blocks >= 2) quantifies surface ambiguity without any lexical split.

## 40. Latent-role soundness and purity

For single-token yields the latent role is the lexical-rule lhs with trailing digits stripped (fallback: the token spelling with digits stripped; CCG-lite uses its categories). Local relation pairs among role-known yields are classified true/false by role-set intersection; multi-token pairs are counted `unknown` and excluded. Context-key purity is the majority-role fraction among role-known yields per block, reported as mean/weighted/min. Measured precision is 1.0 on every family — including `ambiguous_lexicon`, whose legacy-DSU collapse (0.88) came entirely from projecting away the context index.

## 41. Experimental indexed-shadow evidence

`indexed_shadow_evidence` scores an occurrence by `max_v min(|R_c(u)|,|R_c(v)|)/|U_c|` over its final ContextKey's block, quantized like every other strength. It is opt-in (`--tree-evidence-source indexed-shadow`), synthetic-only, and measurably worse than raw/opportunity on the correlated chains (UNIQUE_WRONG at full coverage) while preserving Cartesian neutrality and symmetric honesty — recorded without repair in `indexed_shadow_parse_metrics.csv`; the default parser is untouched.

## 42. Real-data cost

On the documentation corpus at N=10,000 the context-indexed fixed point completes in a handful of rounds over ~4e5 logical triples (seconds; `runtime_ms` column), versus ~2.5 minutes for the legacy saturation baseline it is audited against.

# v2.0 additions (oracle category recovery module)

## 43. Module isolation

`scf::v2` (include/scf/oracle_v2.hpp, src/oracle_v2.cpp) is a separate static library with its own tool (`scf_oracle_v2`) and test binary (`scf_oracle_v2_tests`); it neither links nor includes the v1.x core, so the v1.x semantics and regression surface are untouched. The deterministic Fisher-Yates sampler is deliberately duplicated from synthetic.cpp (same rejection-sampling algorithm over mt19937_64) to keep the module dependency-free.

## 44. Exact oracle via a total category table

All strings of length 1..L+K over the family vocabulary live in one canonical index space (length-major, then lexicographic; index arithmetic only, nothing hashed). `CategoryTable` fills `Cats(s)` bottom-up over all binary splits, odometer-enumerated so prefix/suffix values are O(n) per string; `OracleParser` is an independent CKY recognizer, and `test_parser_and_table_agree` checks the two are pointwise equal. Every `Accept(LuR)` in the experiment is then an O(1) table lookup, which is what makes the full sweep (4 families x L<=6 x k<=4, ~4e8 context queries for the largest family) run in seconds. The one-byte cell caps grammars at 8 categories; `kMaxTableEntries` guards the index-space size.

## 45. Signatures as sparse accepting-context sets

`Sig_k(u)` is a total function on a context universe shared by every u, so signature equality is equality of the accepted-context subset, and only accepting `(u, context)` pairs need storing (`SignatureHits`, packed weight/ordinal/corpus-index per entry). Partition refinement then runs per context weight w = 0..K with canonical first-occurrence class numbering — equal partitions are equal vectors. Because contexts are evaluated against the true oracle, signatures are independent of the universe bound L; per-L results are prefix restrictions of the L=6 partition (`restrict_partition`, pinned by `restriction_consistency`). The positive-only ablation reuses the identical machinery with a retained-set bitmask over the corpus space: a hit counts only if the whole string was retained, which encodes "absence is not negative evidence" and makes 100% coverage provably identical to the oracle run (pinned by test).

## 46. Definitional flags, no heuristics

Observational equivalence of gold categories = full containment of >= 2 gold classes in one learned class at maximal k; the excluded merge-pair count is computed from class sizes and subtracted into `merge_errors_excl_obs_equiv` (the unadjusted count stays in the CSV). Composition labeling: a learned class is labelable iff no member has >= 2 gold categories and some member is a constituent; its label is the *set* of member categories, so an observationally merged class scores both gold rules (exists-semantics). A congruence violation is definitionally the same event as a nonfunctional input pair; both columns are emitted and must agree. Known consequence of exists-semantics: at very coarse k, recall stays 1.0 while precision collapses — precision is the informative low-k signal (report section 5).

## 47. Determinism pins

FNV-1a partition hashes over the canonical class vector (explicit little-endian bytes, no std::hash) are pinned for all four families at (L=3, K=2) in `test_pinned_partition_hashes`; `test_experiment_writes_files` additionally reruns the driver and compares CSV bytes.
