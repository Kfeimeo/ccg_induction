# SCF v1.1 implementation notes

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
