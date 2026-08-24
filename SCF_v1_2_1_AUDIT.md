# SCF v1.2.1 Theoretical / Engineering Audit

Goal of this round: separate, with reproducible experiments, the three failure
sources that v1.2 reporting conflated —

1. the data itself being observationally non-identifiable,
2. a systematic bias of the current tree objective,
3. whether the equivalence-saturation engine actually participates in tree
   induction.

No heuristic, prior, penalty, or probabilistic model was added; the default
parser is unchanged (Acceptance G). All numbers below come from
`scf_audit --output-dir results/audit_v1_2_1 --seeds 20` (deterministic;
~2 s wall clock) and are regenerable from that single command.

---

## Q1 — Do nested_balanced, left_branching, and right_branching generate identical surface corpora?

**Yes, up to token renaming — and right/left are identical even literally.**
(`observational_equivalence_report.txt`; FNV-1a-64 hashes over canonically
sorted serializations.)

| comparison | same surface (exact) | same surface (renamed) | same raw context | same raw witness | same gold tree |
|---|---|---|---|---|---|
| right vs left | **true** (`9e8819f050357773`) | true | **true** | **true** | false |
| nested vs right | false (`05f0…` vs `9e88…`) | **true** (`27c37e5bf4566cdb`) | false / renamed **true** | false / renamed **true** | false |
| nested vs left | false | **true** | renamed **true** | renamed **true** | false |

`right_branching` and `left_branching` generate the *byte-identical* corpus,
raw context relation, and raw witness relation; only their gold trees differ.
`nested_balanced` differs from them only by its token alphabet (c,d,e,f vs
a,b,c,d): after greedy canonical renaming all three surface languages,
raw-context relations, and raw-witness relations hash identically.

**Verdict: latent grammars observationally indistinguishable under current
observations.** The v1.2 result "SCF failed to recover right_branching" was a
category error: no algorithm consuming these observations could distinguish
the three latent grammars, because the observations are the same.

The renaming canonicalization is a greedy first-occurrence scheme over the
sorted language — sufficient for the factorized families audited here, not a
general graph canonization (see IMPLEMENTATION_NOTES §25).

---

## Q2 — Does saturation affect tree parsing under the current raw-witness objective?

**No. 280/280 runs identical.** (`saturation_ablation.csv`.)

The full v1.2 grid (8 families × 7 coverages × 5 seeds) was rerun in two
modes over identical corpora:

- Mode A: normal pipeline (saturation runs, then raw-witness evidence, then
  tree DP);
- Mode B: saturation skipped entirely; same raw-witness evidence; same DP.

Compared per run: per-sentence `best_score`, `optimal_tree_count`,
`gold_in_argmax`, unique/exact status, `forced_optimal_spans`, the full
optimal-split forest, and the raw witness relation hash.

```text
parse_outputs_changed_runs   = 0
parse_outputs_unchanged_runs = 280
```

Data-path explanation: `EvidenceBuilder` is constructed from `Corpus` only —
it groups context records by **exact raw** `(left, right)` `StringId` pairs
and never calls `find()` on the DSU. `TreeSolver` consumes only that
evidence. The `EquivalenceSolver`'s output feeds nothing but diagnostics
(e-class listings, collapse ratios, proofs).

> In v1.2, the equivalence-saturation engine and the raw-witness
> tree-induction engine are computationally decoupled. Saturation remains a
> structural diagnostic but does not causally affect the tree objective.

A regression test (`saturation_is_decoupled_from_parsing`) now pins this
down; `scf_experiment --run-saturation false` exposes the ablation mode.

---

## Q3 — Does raw witness support systematically depend on span length?

**Yes, exactly as predicted.** In a full Cartesian language
X₁×…×Xₙ, a span [i,j) recurs under
∏ₖ﹤ᵢ|Xₖ| · ∏ₖ≥ⱼ|Xₖ| distinct external contexts; with uniform |Xₖ| = q this is
**support(i,j) = q^(n−(j−i))** — shorter spans receive strictly more raw
witnesses. Measured on `nested_balanced` at full coverage
(`span_length_bias.csv`):

| K | len-2: theory | len-2: actual max/mean | len-3: theory | len-3: actual max/mean |
|---|---|---|---|---|
| 2 | 4 | 4 / 4.0 | 2 | 2 / 2.0 |
| 3 | 9 | 9 / 9.0 | 3 | 3 / 3.0 |
| 4 | 16 | 16 / 16.0 | 4 | 4 / 4.0 |

`len2_score_gt_len3_score = true` in every configuration; theoretical and
measured values agree exactly (`max_pair_support` shows no deviation here
because every same-length pair shares the full external-context set in a
full-factorial language). Note `mean_gold_span_score` vs
`mean_non_gold_span_score` for length 3 (0 vs q): length-3 spans are never
gold in this family, yet still collect q witnesses each — support is a
function of span length, not of goldness.

---

## Q4 — Does the current tree objective contain a balance bias?

**It contains an objective-induced balance preference — and no directional
bias.** On the shared observable corpus, explicit shape scores
(`tree_shape_scores.tsv`, every length-4 sentence, all K):

| K | balanced = 2K² | left = K²+K | right = K²+K | best |
|---|---|---|---|---|
| 2 | 8 | 6 | 6 | balanced |
| 3 | 18 | 12 | 12 | balanced |
| 4 | 32 | 20 | 20 | balanced |

`balanced > left`, `balanced > right`, and `left == right` hold for every
sentence. This is the arithmetic consequence of Q3: the balanced tree spends
its two scored spans on two length-2 spans (2·q²), while either chain shape
spends one on a length-3 span (q² + q). Correct labels:

- **objective-induced balance preference** — yes (a structural property of
  `max |W|` summed over spans, given full-factorial data);
- **left/right directional bias** — no (`left_score == right_score`
  everywhere; the two chain grammars fail toward the same balanced tree, not
  toward "their" side).

---

## Q5 — Which ambiguities are population-level non-identifiability, and which are finite-sample ambiguity?

Population state = full-coverage (coverage 1.0) behavior of the finite
language; sample state = each sampled run (`audit_grid_runs.csv`, seeds
1..20, 7 coverages).

| family | population verdict |
|---|---|
| simple_np_vp | POPULATION_IDENTIFIABLE (unique & exact everywhere) |
| nested_balanced (K=2..4) | POPULATION_IDENTIFIABLE |
| hierarchical_correlated_balanced | POPULATION_IDENTIFIABLE |
| symmetric_abc | POPULATION_NON_IDENTIFIABLE (gold in argmax, argmax = 2) |
| hierarchical_correlated_right / _left | POPULATION_NON_IDENTIFIABLE for the block-internal bracket (gold in argmax, argmax = 2; top-level split forced) |
| right_branching / left_branching | population-unique but **wrong**: observationally equivalent to nested_balanced (Q1) + balance preference (Q4) — a misattribution risk, not sample noise |

Finite-sample effects, `symmetric_abc` (K=2, 140 runs,
`finite_sample_symmetry_breaking.csv`):

- 62/140 runs contain **spurious_unique** sentences (population-ambiguous but
  sample-unique: partial coverage accidentally breaks the symmetry);
- 42/140 runs contain **spurious_wrong_unique** sentences (the accidental
  tie-break even pushes gold out of the argmax);
- `symmetry_restored_at_full_coverage = true` in 20/20 full-coverage runs.

So the v1.2 atlas's non-monotone `symmetric_abc` curves are pure
finite-sample symmetry breaking, while its flat `gold_in_argmax = 1` at full
coverage is genuine population non-identifiability. The larger
`nested_balanced` K=4 language (256 sentences, Acceptance E) shows what a
genuinely continuous learning curve looks like once the discrete-step
artifact of tiny languages is removed: mean gold-in-argmax 0.70 ± 0.03 (95% CI) at
coverage 0.2 rising smoothly to 1.0 by coverage 0.8 (`audit_grid_aggregate.csv`).

---

## Q6 — Can a symmetry-breaking observable generator recover hierarchical structure?

**Yes — exactly to the extent that the observations vary.** Two new
mechanisms were added (both generator-side only):

**(a) Correlated-block families** (`hierarchical_correlated_balanced/right/
left`): tokens inside a latent block are correlated (`a_i b_i`), so different
bracketings now generate *different* surface languages — all pairwise surface
hashes differ, including after renaming (Acceptance F). Results at full
coverage, K=3:

- `hierarchical_correlated_balanced`: fully recovered — unique, exact,
  gold-in-argmax 1.0 (blocks substitute against each other: K shared
  contexts per block span).
- `hierarchical_correlated_right` / `_left`: the correlated chain's top-level
  split is recovered (forced span), but the *inside* of a correlated block
  never varies independently, so the inner bracket remains
  population-non-identifiable: gold-in-argmax 1.0, argmax exactly 2. The two
  variants behave symmetrically.

This sharpens the answer: hierarchical structure is recovered precisely where
the surface distribution actually varies; where a block is internally frozen,
no objective could identify its internal bracket, and SCF correctly reports
ambiguity instead of guessing.

**(b) Graded symmetry breaking** (`--symmetry-breaking-rate rho` on
`symmetric_abc`): ceil(rho·K²) marker sentences `a_i b_j p` add block-level
contexts `(ε, p)`. Identifiability vs rho (K=3, coverage 1.0,
`identifiability_vs_rho.csv`):

| rho | markers | gold_in_argmax | unique_optimal | mean argmax |
|---|---|---|---|---|
| 0.00 | 0 | 1.000 | 0.000 | 2.00 |
| 0.05 | 1 | 0.964 | 0.036 | 1.96 |
| 0.10 | 1 | 0.964 | 0.036 | 1.96 |
| 0.20 | 2 | 1.000 | 0.276 | 1.72 |
| 0.40 | 4 | 1.000 | 0.516 | 1.48 |
| 0.60 | 6 | 1.000 | 0.727 | 1.27 |
| 0.80 | 8 | 1.000 | 0.914 | 1.09 |
| 1.00 | 9 | 1.000 | 1.000 | 1.00 |

This is the requested *identifiability vs symmetry-breaking-evidence* curve
(not accuracy vs corpus size): unique recovery grows monotonically with rho
once rho ≥ 0.2. The rho = 0.05 dip is a real and instructive edge case: a
*single* marker's `(ε, p)` context has no substitution partner, so the marker
sentence's `[0,2)` span earns no witness, while its `[1,3)` span "b1 p"
accidentally substitutes with "b1 c_k" in the shared context `(a1, ε)` —
the marker sentence itself is parsed UNIQUE_WRONG as `(a1 (b1 p))`. From two
markers onward, `(ε, p)` holds two distinct yields, `[0,2)` support rises to
K+1 > K, and both the markers and a growing share of base sentences become
uniquely correct. Weak symmetry-breaking evidence can itself briefly mislead
the objective before it helps.

---

## Bottom line

> **SCF 当前失败,到底是因为 observation 中没有结构信息,还是因为 objective
> 解释错了已有信息?**

Both, in different places, and now they are cleanly separated:

1. **No structural information present** — `right_branching`/`left_branching`
   vs `nested_balanced` (observationally equivalent corpora, Q1);
   `symmetric_abc` at full coverage; the block-internal brackets of the
   correlated chain families (Q6a). Here every objective must fail or stay
   ambiguous; SCF's ambiguity reporting is the correct behavior.
2. **Objective misreads available information** — given an observationally
   shared corpus, the raw-witness sum prefers balanced trees because shorter
   spans structurally receive more external-context witnesses
   (support = q^(n−len), Q3/Q4). This is an objective property, fixable in
   v1.3, and *not* evidence of missing information.
3. **Saturation is currently irrelevant to parsing** (Q2) — any v1.3 plan
   that expects e-classes to influence trees must first connect them.
4. **Finite-sample ambiguity is a third, distinct phenomenon** (Q5) — partial
   coverage both fakes uniqueness and occasionally excludes gold; population
   statements require the full-language reference the audit grid now computes.

v1.3 design decisions (normalized/quotient-context evidence, e-class-aware
tree inference, or redefining the target as observational equivalence
classes) can now be made against these measurements; none of them was
implemented in this round.

---

## Artifact map

| file | produced by | answers |
|---|---|---|
| `results/audit_v1_2_1/observational_equivalence_report.txt` | `scf_audit` | Q1 |
| `results/audit_v1_2_1/saturation_ablation.csv` | `scf_audit` | Q2 |
| `results/audit_v1_2_1/span_length_bias.csv` | `scf_audit` | Q3 |
| `results/audit_v1_2_1/tree_shape_scores.tsv` | `scf_audit` | Q4 |
| `results/audit_v1_2_1/audit_grid_runs.csv` + `audit_grid_aggregate.csv` | `scf_audit` | Q5 (20 seeds, mean/std/min/max/95% CI) |
| `results/audit_v1_2_1/finite_sample_symmetry_breaking.csv` | `scf_audit` | Q5 |
| `results/audit_v1_2_1/identifiability_vs_rho.csv` | `scf_audit` | Q6 |
| per-run `score_by_span_length.csv`, hash columns in `summary.csv`/`metrics.json` | `scf_experiment` | Tasks 1, 5, 8 |

Acceptance: **A** (three pairwise comparisons, exact + renamed) ✔; **B**
(280-run ablation, changed = 0) ✔; **C** (n=4, K=2,3,4 support law) ✔; **D**
(explicit shape scores, balance preference confirmed, left == right) ✔; **E**
(nested_balanced K=4: 256 sentences × 20 seeds × 7 coverages) ✔; **F**
(hierarchical_correlated_* with distinct surface hashes) ✔; **G** (no
heuristic patch; default parser byte-identical) ✔.
