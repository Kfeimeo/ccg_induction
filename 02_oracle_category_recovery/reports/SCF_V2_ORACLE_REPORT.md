# SCF v2.0 — Oracle Category Recovery Report

This report answers one question:

> **Are externally indistinguishable string equivalence classes sufficient to
> recover E, Lex, and Comp of a synthetic CCG-like grammar, given only the
> membership oracle `Accept(s)`?**

**Verdict: yes, exactly, with three precisely characterizable provisos.**

1. **Context depth must reach the deepest embedding of a category.** With
   contexts bounded by `|L| + |R| <= k`, a category is recovered iff `k` is at
   least the weight of its shallowest accepting context. `simple_np_vp`
   stabilizes at `k = 2`, `recursive_modifier` at `k = 3` (modifiers sit one
   level deeper), and `transitive` at `k = 4` (a transitive verb needs
   `the N _ the N`). Below that depth the affected category is
   indistinguishable from dead strings — *insufficient context depth*, not an
   algorithmic failure.
2. **Categories the language never distinguishes are unrecoverable in
   principle.** In the designed `observationally_equivalent_categories`
   family, no context whatsoever separates `dog` (gold `Nm`) from `cat` (gold
   `Nf`); the learner merges them, the pipeline flags the pair
   `[{Nm} ~ {Nf}]` as `observationally_equivalent_gold_categories`, and after
   excluding the flagged pairs the residual merge error is exactly 0. This is
   *observational non-identifiability* of the gold grammar, not an error of
   the learner, and it is never counted as an ordinary error.
3. **The recovered Comp is the composition of the syntactic congruence, which
   can strictly extend the gold rule set.** Under modifier recursion the
   learner correctly discovers `A · A ≡ A` and `D · A ≡ D` (adjective chains
   are congruent to single adjectives; "the big" is congruent to "the").
   These are true facts of the language absent from the gold rule list; they
   are reported as labeled triples outside gold `Comp`, not silently dropped.

Everything below is measured, not asserted: the sweep is
`4 families x L = 2..6 x k = 0..4`, plus the positive-only coverage ablation,
all fully deterministic (pinned partition hashes in the regression suite).
Raw data: `results_v2_oracle/category_recovery.csv`,
`results_v2_oracle/composition_recovery.csv`,
`results_v2_oracle/positive_only_recovery.csv`,
`results_v2_oracle/oracle_summary.txt`.

Reproduce with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/scf_oracle_v2 --output-dir results_v2_oracle
ctest --test-dir build --output-on-failure
```

## 1. Setup

### Gold grammar and oracle

`G = (E, Lex, Comp, F)`: a finite category set `E` (`|E| <= 8`), a lexicon
`Lex` mapping tokens to categories, an arbitrary partial composition relation
`Comp ⊆ E x E x E` (never assumed functional, total, or associative), and
accepting categories `F`. `Cats(s)` is the exact set of categories derivable
for `s` (CKY over all binary splits); `Accept(s) <=> Cats(s) ∩ F ≠ ∅`.

The learner sees **only** `Accept`. Gold categories, rules, and trees enter
evaluation only. Implementation-wise, one bottom-up DP table stores `Cats(s)`
for every string of length `<= L + K` over the family's vocabulary
(cross-validated against an independent CKY recognizer in
`test_parser_and_table_agree`), so every oracle query is an exact O(1) lookup
— there is no sampling and no approximation anywhere in the oracle path.

### External equivalence

Universe: every token string of length `1..L`. For a string `u`, the bounded
contextual signature is exactly the spec's

```text
Sig_k(u) = { (L, R, Accept(L u R)) : |L| + |R| <= k }
```

and `u ≡_k v <=> Sig_k(u) = Sig_k(v)` — exact set equality, no thresholds, no
similarity measures. Partitions are computed by refinement over context
weight `w = 0..K` (the weight-`w` slice refines the `≡_{w-1}` partition), and
`tests/test_oracle_v2.cpp::naive_signature_reference` verifies the refinement
against a literal, set-of-triples reimplementation of the definition.
Signatures do not depend on `L` (contexts are evaluated against the true
oracle), which the `restriction_consistency` test pins: the `L = 3` partition
is exactly the `L = 4` partition restricted to shorter strings.

### Grammar families

| family | E | Lex | Comp | accepted strings (len <= 10) |
|---|---|---|---|---|
| `simple_np_vp` | D N NP IV S | the→D; dog,cat→N; sleeps,runs→IV | D N→NP; NP IV→S | 4 |
| `transitive` | D N NP TV VP S | the→D; dog,cat→N; sees,likes→TV; sleeps→VP | D N→NP; TV NP→VP; NP VP→S | 10 |
| `recursive_modifier` | D A N NP IV S | the→D; big,red→A; dog→N; sleeps→IV | A N→N; D N→NP; NP IV→S | 255 |
| `observationally_equivalent_categories` | D Nm Nf NP IV S | the→D; dog→Nm; cat→Nf; sleeps→IV | D Nm→NP; D Nf→NP; NP IV→S | 2 |

`transitive` doubles as a category-unification test: `sleeps` is a *lexical*
VP, so external equivalence must place the length-1 string "sleeps" and the
length-3 derivation "sees the dog" in one class. `recursive_modifier` covers
the required recursive/modifier case (`A N → N`).

### Evaluation scopes

The gold partition labels every universe string with its exact category set
`Cats(u)`; `Cats(u) = ∅` (non-constituents) is a gold class like any other.
Metrics are reported on two scopes: `constituents` (strings with
`Cats(u) ≠ ∅` — the scope on which "category recovery" is well posed) and
`full` (the whole universe, where the relationship between external
equivalence and the category-set labeling is itself a finding; see §3).

## 2. Q1/Q2 — Convergence to gold categories and required context depth

Constituent scope, `L = 6` (columns from `category_recovery.csv`):

| family | k=0 ARI | k=1 | k=2 | k=3 | k=4 | first_stable_k (full universe) |
|---|---|---|---|---|---|---|
| simple_np_vp | 0.337 | 0.609 | **1.000** | 1.000 | 1.000 | 2 |
| transitive | 0.532 | 0.681 | **1.000** | 1.000 | 1.000 | **4** |
| recursive_modifier | 0.334 | 0.917 | **1.000** | 1.000 | 1.000 | 3 |
| observationally_equivalent_categories | 0.109 | 0.323 | 0.781 | 0.781 | 0.781 | 2 |

- **Convergence is exact.** Three families reach `ARI = NMI = 1.0` with
  bijective class correspondence; the fourth is exact modulo the flagged
  `{Nm} ~ {Nf}` merge (§4). This holds at *every* `L` in `2..6`: at `L = 2`
  the universe simply contains fewer gold categories (no `S`, no `VP`
  derivations), and the ones present are still recovered exactly.
- **`Lex` is recovered exactly.** The token-level restriction of the learned
  partition equals the gold lexical categorization in all four families
  (`lexicon recovery` tables in `oracle_summary.txt`), including the
  three-way `TV / VP / N` split of `transitive` and the merged `dog/cat`
  class in the observationally equivalent family.
- **Cross-length category unification works.** In `transitive` the final VP
  class is exactly `{"sleeps", "sees the dog", "sees the cat", "likes the
  dog", "likes the cat"}` — a lexical item and its phrasal congruents in one
  class, with no tree or category supervision.
- **Depth requirement = deepest embedding.** The full-universe partition
  stabilizes at the maximum, over categories, of the weight of the
  shallowest accepting context: `k = 2` for `simple_np_vp` (D needs
  `ε _ dog sleeps`), `k = 3` for `recursive_modifier` (A needs
  `the _ dog sleeps`), `k = 4` for `transitive` (TV needs
  `the N _ the N`). Stability is monotone and, once reached, persists
  through `K = 4` (partition hashes identical).
- **A scope caveat the numbers force us to state:** `transitive` reaches
  constituent-scope ARI 1.0 already at `k = 2`, but at `k = 2..3` the TV
  strings are still externally indistinguishable from dead strings — the
  constituent projection hides the mixing (`sees` and `likes` project to
  their own class either way). The honest convergence depth is the
  full-universe `first_stable_k = 4`; the regression suite pins
  `class("sees") == class("the the the")` at `k = 3` and their separation at
  `k = 4`.

## 3. The full-universe finding: external equivalence is Myhill–Nerode
structure, not category-set labeling

On the full universe, final ARIs are 0.50–0.89, and *every* deviation
decomposes into two principled phenomena, not noise:

1. **External equivalence is strictly finer than the gold labeling on
   non-constituents.** The gold class `Cats = ∅` splits into residue classes
   with real distributional identity: in `simple_np_vp` the final partition
   is `{D} {N} {IV} {NP} {S}` plus a "needs-a-determiner" class
   `{"dog sleeps", "cat runs", …}` and a dead class — the 4-member residue
   class is exactly the strings completable by a single left `the`. All
   `split_errors` in the full scope (e.g. 1,566,516 pairs in `transitive`)
   are of this kind: correct syntactic-congruence distinctions the gold
   category-set labeling cannot express.
2. **External equivalence can be coarser than the labeling where the grammar
   is recursive.** In `recursive_modifier` the final class of `the` also
   contains `the big`, `the big red`, … (`Cats = ∅`), and the class of `big`
   contains every adjective chain: `D · A* ≡ D` and `A · A* ≡ A` are true
   congruence facts of the language. These account for exactly the 310
   full-scope merge pairs and the 2 classes flagged as
   `classes mixing constituents and non-constituents`; in the constituent
   scope the same classes are pure `{D}` and `{A}`.

So on constituents the equivalence recovers `E` exactly; on the full universe
it recovers something *better-defined* than the gold labeling — the bounded
syntactic congruence — and the differences are enumerable and explainable.

## 4. Q3 — Observationally equivalent gold categories

In `observationally_equivalent_categories`, `dog` (gold `Nm`) and `cat` (gold
`Nf`) have identical distributions by construction: `D Nm → NP` and
`D Nf → NP` make every context accept both or neither, in the unbounded
language, not just the bounded one. Result at every `L`, every `k >= 2`:

```text
observationally_equivalent_gold_categories: [{Nm} ~ {Nf}]
merge_errors                = 1     (the dog–cat pair)
merge_errors_excl_obs_equiv = 0
split_errors                = 0
```

Detection is definitional, not heuristic: two gold classes are flagged when
both are fully contained in one learned class at the maximal context depth,
and pairs between flagged classes are excluded from the ordinary error count
(they remain visible in the unadjusted `merge_errors` column). No other
family triggers the flag: the `{D}/{A}`-chain merges of §3 are *not* flagged,
correctly, because the gold `∅` class is not fully contained in either
learned class.

Downstream, recovery degrades gracefully rather than collapsing: `Comp` is
still recovered with precision 1.0 and recall 1.0, because the merged class
carries the label set `{Nm, Nf}` and both gold rules `(D, Nm, NP)` and
`(D, Nf, NP)` are realized by the same learned triple.

## 5. Q4/Q5 — Comp recovery, functionality, and the congruence audit

From `composition_recovery.csv` at `L = 6` (`Comp(A,B,C)` recorded whenever
some `u ∈ A`, `v ∈ B` with `|uv| <= 6` has `uv ∈ C`):

| family | k | triples | precision | recall | nonfunctional pairs | congruence violations |
|---|---|---|---|---|---|---|
| simple_np_vp | 2..4 | 49 | 1.000 | 1.000 | 0 | 0 |
| transitive | 2 | 101 | 0.081 | 1.000 | 6 | 6 |
| transitive | 4 | 162 | **1.000** | 1.000 | **0** | **0** |
| recursive_modifier | 3..4 | 64 | 0.600 | 1.000 | 0 | 0 |
| observationally_equivalent_categories | 2..4 | 49 | 1.000 | 1.000 | 0 | 0 |

- **Q5 first: at the stable depth, external equivalence *is* a concatenation
  congruence over the observed universe.** `congruence_violations = 0` for
  every family once `k >= first_stable_k`. Below the stable depth it is
  provably not one; the earliest counterexample is pinned in the summary: at
  `k = 0`, `u = u' = "the"`, `v = "the"`, `v' = "dog sleeps"` satisfy
  `u ≡ u'`, `v ≡ v'` (both pairs non-accepted), yet `"the the"` is rejected
  while `"the dog sleeps"` is accepted — *composition inconsistency* of the
  too-coarse relation, measured, not assumed away.
- **Q4: the recovered `Comp` is functional on every observed input pair at
  the stable depth** (`nonfunctional_input_pairs = 0`), although
  functionality was never presupposed. A congruence violation and a
  nonfunctional input pair are definitionally the same event, and the CSV
  reports both columns agreeing at every single row — an internal consistency
  check of the implementation.
- **All witnessable gold rules are recovered at every family and depth**
  (recall 1.0), where witnessable means realizable by exact single-category
  strings within the length bound. At `L = 2` this denominator correctly
  shrinks (e.g. `NP IV → S` needs `|uv| = 3`): *finite-language truncation*
  visible as `witnessable_gold_rules` dropping from 2→1 (`simple_np_vp`) and
  3→1 (`transitive`), while everything witnessable is still found.
- **Precision at the stable depth is 1.0 except `recursive_modifier`
  (0.600)**, whose three correct rule triples are accompanied by exactly the
  two derived congruence facts of §3: `({A}, {A}, {A})` ("big" + "big") and
  `({D}, {A}, {D})` ("the" + "big"). The learned relation is the composition
  of the syntactic congruence; on recursive modifiers that relation genuinely
  extends the gold rule list. Low-`k` precision (down to 0.08 in
  `transitive`) is the informative degradation signal, while low-`k` recall
  stays 1.0 by the generosity of exists-semantics labeling over coarse
  classes — precision, not recall, is the metric to watch under insufficient
  depth.

## 6. Positive-only ablation

The corpus is the accepted language of length `<= L + K = 10`; a
deterministic seeded sample retains `ceil(coverage * N)` strings, and a
context now registers in a signature only if the whole string `L u R` was
*retained* — absence is never negative evidence. By construction, 100%
coverage is provably equivalent to the full oracle, and the runs confirm it:
`matches_oracle = 1` with identical partition hashes for every family, seed,
and `k` (also a regression test).

ARI vs the full-oracle partition at `k = 4`, `L = 6` (3 seeds):

| coverage | simple_np_vp (N=4) | transitive (N=10) | recursive_modifier (N=255) | obs_equiv (N=2) |
|---|---|---|---|---|
| 5% | 0.571 | 0.214–0.387 | 0.429–0.458 | 0.749 |
| 10% | 0.571 | 0.214–0.387 | 0.624–0.654 | 0.749 |
| 20% | 0.571 | 0.461–0.591 | 0.797–0.816 | 0.749 |
| 40% | 0.800–0.846 | 0.718–0.795 | 0.920–0.936 | 0.749 |
| 80% | 1.000 (all 4 retained) | 0.958 | 0.972–0.980 | 1.000 (both retained) |
| 100% | 1.000 = oracle | 1.000 = oracle | 1.000 = oracle | 1.000 = oracle |

Identifiability rises monotonically with coverage; only `recursive_modifier`
(255 accepted strings) yields a smooth curve, while the tiny languages make
coverage a step function of the retained-sentence count — the same
finite-sample phenomenon the v1.2.1 audit documented for coverage grids.
The 5% failure mode (*positive-only undersampling*) has two faces. Spurious
merges: every string occurring in no retained context is signature-empty and
collapses into the dead class (seed 1 of `simple_np_vp` retains only "the dog
runs", so `sleeps` and `cat` look dead; `transitive` seed 1 learns 7 classes
against the oracle's 13). Spurious splits: same-category strings with
disjoint observed contexts separate (`recursive_modifier` at 5% learns 40–45
classes against the oracle's 8, because different adjective chains survive in
different retained sentences).

## 7. Failure taxonomy with minimal counterexamples

The spec requires that any failure be classified. All five categories were
exhibited and localized:

| category | minimal synthetic counterexample | where measured |
|---|---|---|
| `insufficient context depth` | `transitive`, `k = 3`: `"sees" ≡ "the the the"` (dead string); separated at `k = 4` because TV's shallowest accepting context `the N _ the N` has weight 4 | `category_recovery.csv` (transitive, `refined_from_previous_k = 1` at `k = 4`); pinned in `test_transitive_context_depth` |
| `finite-language truncation` | `L = 2`: rule `NP IV → S` unwitnessable (`|uv| = 3 > L`); `witnessable_gold_rules` drops 2→1 / 3→1, and the `S`, `VP` classes are absent from the universe | `composition_recovery.csv`, `L = 2` rows |
| `observational non-identifiability` | `dog` (`Nm`) vs `cat` (`Nf`): identical distributions in the unbounded language; flagged `[{Nm} ~ {Nf}]`, excluded from ordinary errors | §4; `test_observational_equivalence_flagged` |
| `composition inconsistency` | `k = 0`: `u = u' = "the"`, `v = "the" ≡ v' = "dog sleeps"`, but `uv` rejected and `u'v'` accepted — the coarse relation is not a congruence | `composition_recovery.csv` (`congruence_violations > 0` for `k < first_stable_k`); pinned in `test_composition_recovery_simple` |
| `positive-only undersampling` | 5% coverage of `simple_np_vp` (1 of 4 sentences retained): unobserved-in-context strings collapse into the dead class, ARI 0.571 vs oracle; at the same coverage `recursive_modifier` over-splits (40–45 classes vs 8) | `positive_only_recovery.csv` |

One observed discrepancy deliberately does **not** fit the failure taxonomy:
the `recursive_modifier` precision 0.600 is caused by *true* congruence facts
(`A · A ≡ A`, `D · A ≡ D`) that the gold rule list simply does not contain.
Classifying it as a failure would misread the algebra; it is reported as
"labeled triples outside gold Comp" with witnesses.

## 8. Acceptance checklist

- Exact external-equivalence implementation — signature refinement verified
  against a literal set-of-triples reference (`naive_signature_reference`).
- Gold labels never participate in learning — the learner's inputs are
  `Accept` lookups (oracle path) or retained-string membership
  (positive-only path) exclusively.
- No heuristic thresholds — every equivalence is exact equality; every flag
  (observational equivalence, mixing, violations) is definitional.
- Category recovery metrics — ARI, NMI, pairwise precision/recall,
  merge/split pair counts, adjusted merges, partition hashes, two scopes.
- `Comp` recovery — triples, functionality census, precision/recall against
  witnessable gold rules.
- Congruence audit — violations counted and exemplified; zero at stable
  depth, positive below it.
- Context-depth convergence — `first_stable_k` recorded per family and `L`.
- Positive-only coverage experiment — 6 coverages x 3 seeds, with the
  provable 100%-equals-oracle anchor.
- Deterministic results — byte-identical reruns (tested), pinned
  cross-platform FNV partition hashes, seeded platform-independent sampling.
- All old tests continue to pass — the module is a separate library, tool,
  and test binary; `scf_tests` is untouched and green.

## 9. Final answer

Within a bounded universe and bounded contexts, **external distinguishability
is sufficient to recover `E` (on constituents, exactly), `Lex` (exactly), and
`Comp` (exactly on witnessable rules, functionally, and as a genuine
concatenation congruence)** — provided the context bound reaches the deepest
category embedding, and up to two principled exceptions that the pipeline
itself detects and labels: gold categories the language never distinguishes
(merged and flagged, by necessity), and derived composition facts of
recursive modifiers (the congruence knows more than the rule list). Every
observed failure below those bounds falls into the five-way taxonomy of §7
with a minimal counterexample.
