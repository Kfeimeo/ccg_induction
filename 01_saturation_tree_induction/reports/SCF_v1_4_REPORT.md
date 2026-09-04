# SCF v1.4 Report — Context-Indexed Equivalence

v1.4 replaces the global yield equivalence `u ~ v` by a family of
context-indexed local relations `u ~_(eL,eR) v` built from a recursively
refined context abstraction `A_t`, keeps the legacy global DSU strictly as a
baseline, changes no default parser, performs no lexical splitting, and uses
no thresholds — class formation depends on exact complete-profile equality
only (Acceptance H/I/J). All numbers reproduce from:

```bash
build/scf_audit v14-synthetic --output-dir results/v1_4/synthetic
build/scf_real_audit --input data/real/tokenized.txt \
  --sample-sizes 100,500,1000,5000,10000 --seed 42 --output-dir results/v1_4/real_audit
```

Beyond the requested measurements, the audit surfaced **two provable
theorems** about the exact prefix/suffix context universe that reshape the
hypotheses; both are stated with proof sketches below and pinned by the
100-seed naive-reference property test.

---

## Q1 — Does context-indexed equivalence eliminate the catastrophic giant-class collapse?

**Yes, completely.** (`global_vs_indexed.csv`, N = 10,000 kept sentences):

| N | legacy global largest ratio | indexed largest ContextAbstractionClass | indexed max LocalRoleBlock |
|---|---|---|---|
| 1,000 | 0.044 | 0.049 | 0.012 |
| 5,000 | **0.789** | 0.062 | 0.016 |
| 10,000 | **0.859** | 0.067 | **0.018** |

The legacy phase transition (v1.3: 86% of the string universe in one class)
simply does not occur in the indexed model: the largest local role block
holds 1.8% of the universe at N = 10,000 and the largest abstraction class
6.7% (dominated by the empty-profile bucket of whole-sentence-only strings).
H1 and H3 confirmed. The `ambiguous_lexicon` family, which drives legacy
collapse to 0.88, shows local-relation precision **1.0** under context
indexing.

## Q2 — Where does legacy collapse arise?

**From discarding the context index and taking a global transitive closure
(plus the concat congruence cascade) — not from raw connectivity.**
(`collapse_attribution.csv`):

| N | Stage A: raw direct giant | Stage B: indexed projection giant | Stage C: legacy DSU giant | indexed max local block |
|---|---|---|---|---|
| 1,000 | 0.012 | 0.012 | 0.044 | 0.012 |
| 5,000 | 0.036 | 0.036 | 0.789 | 0.016 |
| 10,000 | 0.208 | 0.208 | 0.859 | 0.018 |

Stage A equals Stage B exactly (a corollary of the relation-invariance
theorem, Q4): context abstraction adds **zero** connectivity. The gap
0.208 → 0.859 at N = 10,000 is produced entirely inside the legacy engine —
global unconditional union over canonical-context buckets plus concat
congruence. Attribution: `loss of context index + transitive closure`
(dominant, Case 3), amplified by the `concat congruence cascade`; `direct
raw connectivity` accounts for at most 0.208 and `context abstraction` for
none of it.

## Q3 — Does recursive contraction still occur for more than one genuine round?

**Two-part answer, sharper than the hypothesis.**

- **`context_only`: provably no — the operator is one-round idempotent.**
  Every context coordinate is a full sentence prefix/suffix with its own
  observed occurrence, so from `P0(L1) = P0(L2)`:
  `(L1,R) ∈ P0(x) ⟺ sentence L1·x·R observed ⟺ (ε, x·R) ∈ P0(L1) = P0(L2)
  ⟺ sentence L2·x·R observed ⟺ (L2,R) ∈ P0(x)` — profiles that become equal
  after round-1 renaming were already exactly equal, hence `A* = A_1`.
  Verified on 100 random small corpora (naive reference agrees on
  partition, blocks, and round count) and on every synthetic family and
  every real-N (`context_partition_rounds = 1` throughout).
- **`context_plus_concat`: yes — a genuine ≥ 2-round cascade exists.** The
  decomposition signature `D_t` is not protected by the argument above, and
  the fixed minimal corpus `recursive_context_cascade` ("w a m" / "w b m")
  contracts over **three** productive rounds (trace in
  `recursive_cascade_trace.txt`): round 1 `a ~ b`; round 2 `"a m" ~ "b m"`
  and `"w a" ~ "w b"` (their D-components only match after round 1); round 3
  the two sentences. Acceptance C satisfied — with the honest caveat that
  the recursion lives in the *abstraction classes*, not in the relations
  (Q4).

## Q4 — How much additional local relation / evidence coverage does recursive context abstraction create?

**None — and this is a theorem, not a sampling accident.** By induction,
every pair of strings that ever merges (either signature; `plus_concat`
only delays merges) has exactly equal round-0 profiles; the same
sentence-existence chase then shows the exact blocks of merged coordinates
contain identical yield sets. Hence merging keys never fuses blocks with
different yields:

- `distinct_pairs_round0 == distinct_pairs_final` on all 10 synthetic
  families × 2 signatures and in the 100-seed property test;
- `indexed_evidence_coverage == raw_evidence_coverage` (coverage_gain
  0.000000) on every synthetic family and at every real N
  (`real_indexed_metrics.csv`);
- real relation-pair counts even dip slightly (−219 at N = 10,000) as
  `(key, pair)` duplicates merge.

**H4 is refuted for exact prefix/suffix contexts.** Recursive contraction
reorganizes the index (fewer, coarser keys: 233k → 128k at N = 10,000) but
cannot mint new substitution relations; that would require a genuinely
coarser context notion than exact prefix/suffix strings — a v1.5 question,
deliberately not implemented.

## Q5 — Does local-relation precision remain high on synthetic grammars with known latent roles?

**Precision = 1.0 on every family, both signatures**
(`local_role_metrics.csv`): all Cartesian, correlated, `simple_np_vp`,
`symmetric_abc`, `ambiguous_surface_roles`, `ambiguous_lexicon`, `ccg_lite`.
Recall over enumerable same-role token pairs is 1.0 for the ambiguity
families and mean/weighted context-key purity is 1.0 throughout. Notably
`ambiguous_lexicon` — legacy collapse ratio 0.88 — has *perfectly sound*
local relations once the context index is kept.

## Q6 — Can one surface form participate in multiple context-conditioned roles without a global merge?

**Yes (H5 confirmed).** In `ambiguous_surface_roles`, token `x` joins both
N-blocks (`x ~_c n1, n2`) and V-blocks (`x ~_c' v1, v2`) with `c ≠ c'`; no
split is performed, no key ever relates `n*` to `v*`, and their abstraction
classes stay distinct (`ambiguous_surface_roles.txt`; regression-tested).
The cross-context bridge regression holds: `u ~_c1 v`, `v ~_c2 w`,
`u ≁ w` locally and by class — while the legacy baseline merges u,v,w
through the bridge, and the diagnostic projection graph connects them
(exactly the object that must not be read as an equivalence class). On real
data, 4,409 surface forms occupy ≥ 2 substitution-active local roles at
N = 10,000 (mean 1.13, p95 = 2).

## Q7 — Does `context_plus_concat` improve abstraction quality or merely overconstrain?

**It neither improves relations (theorem, Q4) nor purity (already 1.0); it
is a strictly more conservative merge schedule.** On
`hierarchical_correlated_balanced` it blocks all round-0 merges
(0 productive rounds vs 1 under `context_only`); on the cascade corpus it
stretches the same final partition over three rounds. Its value is
diagnostic — it exhibits the genuine recursive structure that `context_only`
provably cannot — not qualitative. It remains non-default.

## Q8 — Real-data scaling (N = 100 … 10,000)

| N | largest abstraction class ratio | max local block ratio | projection giant ratio | coverage gain | multi-role surfaces | rounds | runtime |
|---|---|---|---|---|---|---|---|
| 100 | 0.038 | 0.011 | 0.011 | 0 | 0 | 1 | 2 ms |
| 1,000 | 0.049 | 0.012 | 0.012 | 0 | 9 | 1 | 32 ms |
| 5,000 | 0.062 | 0.016 | 0.036 | 0 | 1,270 | 1 | 169 ms |
| 10,000 | 0.067 | 0.018 | 0.208 | 0 | 4,409 | 1 | 331 ms |

All indexed collapse indicators grow slowly and stay small; the projection
giant grows fastest (it *is* the raw substitution graph) but never touches
the model's classes. The indexed fixed point costs 0.33 s at N = 10,000
versus ~2.5 min for the legacy baseline it replaces as the object of study
(complexity instrumentation in `real_indexed_metrics.csv`).

## Q9 — Projection giant vs small local blocks: was v1.3 collapse caused mainly by discarding context indices?

**Yes — answered affirmatively and quantitatively.** At N = 10,000 the
unindexed projection has a 20.8% giant component while every local block
stays ≤ 1.8% and every abstraction class ≤ 6.7%. Moreover the legacy DSU
giant (85.9%) exceeds even the projection giant by another factor of four —
its extra connectivity comes from canonical-context re-keying plus concat
congruence on top of the projection. The v1.3 catastrophic collapse was an
artifact of projecting context-indexed relations onto one global
equivalence and closing transitively; the relations themselves are sound
(Q5) and small (Q1).

## Q10 — What should v1.5 do, based only on these measurements?

**E — a combination with explicit priority: B ≫ D > A; C not yet.**

1. **B. Refine context abstraction semantics (top priority).** The
   relation-invariance theorem shows exact prefix/suffix contexts make
   abstraction evidence-neutral: any real gain requires a coarser context
   object (bounded-window or quotient contexts) whose profiles are *not*
   forced into exact equality by the sentence-existence argument. This is
   also what the v1.3 sparsity finding (94% singleton contexts) demands.
2. **D. Contextual concat congruence (second).** `context_plus_concat`
   proves decomposition information genuinely recurses; a *contextual*
   congruence (indexed, not global — the legacy global version is the
   proven collapse amplifier, Q2) is the natural candidate source of new
   relations that Q4 shows pure context abstraction cannot provide.
3. **A. Parser integration (deferred).** The experimental indexed-shadow
   evidence is currently *worse* than raw/opportunity on the correlated
   chains (UNIQUE_WRONG at full coverage;
   `indexed_shadow_parse_metrics.csv`) while matching them on Cartesian
   neutrality and symmetric honesty; integrating an evidence source that
   the theorem says equals raw coverage buys nothing yet.
4. **C. Lexical/sense splitting — not needed.** Multi-role membership
   already represents surface ambiguity losslessly (Q6) with precision 1.0;
   no measurement motivates splitting.

None of this is implemented in v1.4.

---

## Hypotheses scoreboard

| hypothesis | verdict |
|---|---|
| H1 cross-context bridge collapse prevented | **confirmed** (regression + real data) |
| H2 genuine recursion after round 1 | **split**: impossible for `context_only` (theorem); real for `context_plus_concat` (3-round cascade) — at class level only |
| H3 no giant local semantic class on real data | **confirmed** (max block 1.8% vs legacy 85.9%) |
| H4 abstraction increases usable evidence coverage | **refuted by theorem + all data** (gain ≡ 0 for exact prefix/suffix contexts) |
| H5 multi-role surfaces without global splitting | **confirmed** (synthetic + 4,409 real surfaces) |

## The §45 verdict

`legacy_global_largest_ratio (0.859) ≫ indexed_max_local_block_ratio
(0.018)` holds decisively, and `synthetic_local_relation_precision = 1.0`
throughout — so the data supports `u ~_(eL,eR) v` as the correct core
object of SCF over the old global `u ~ v`. The two caveats the audit adds:
`recursive_relation_gain = 0` (H4) means the *evidence* side of the story
needs a coarser context notion before parser integration, and the genuine
recursive contraction that motivates the whole construction lives in the
concat-aware signature. Both point v1.5 at direction B before anything else.

## Acceptance checklist

**A** `u ~_(eL,eR) v` queryable (`locally_related`, `final_key_for`,
`keys_of_yield`) ✔ · **B** cross-context bridge regression ✔ · **C**
`recursive_context_cascade` ≥ 2 genuine rounds (3, `plus_concat`; trace) ✔ ·
**D** one surface in several LocalRoleBlocks ✔ · **E** legacy baseline kept,
v1.3 collapse reproduced (0.859 @ N=10,000) ✔ · **F** N = 100..10,000
indexed/global comparison ✔ · **G** four-way collapse attribution ✔ · **H**
exact-profile classes, no thresholds ✔ · **I** no lexical split ✔ · **J**
default parser unchanged ✔ · **K** deterministic partition/relation/trace
hashes ✔ · **L** all v1.3 regressions pass (56/56 tests) ✔

## Deliverables map

`SCF_v1_4_REPORT.md` (this file) · `results/v1_4/real_audit/`:
`context_indexed_rounds.csv`, `collapse_attribution.csv`,
`global_vs_indexed.csv`, `real_indexed_metrics.csv` ·
`results/v1_4/synthetic/`: `synthetic_indexed_metrics.csv`,
`local_role_metrics.csv`, `recursive_cascade_trace.txt`,
`ambiguous_surface_roles.txt`, `indexed_shadow_parse_metrics.csv`. A second
natural-English corpus is not present in the repository:
`second_corpus_not_available` (no network download; primary corpus
replication only).
