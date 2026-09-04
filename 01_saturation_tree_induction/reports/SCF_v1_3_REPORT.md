# SCF v1.3 Report — Debiased Evidence, Correlated Benchmark, Real-Corpus Constraint Audit

Three lines of work, all measurement-first, none changing the default parser:

- **A.** an evidence-objective laboratory (`raw_count` baseline + three
  normalized candidates, switchable via `--evidence-objective(s)`);
- **B.** the correlated synthetic families promoted to primary benchmark,
  with observable-gold forced-span metrics;
- **C.** a real-corpus constraint-density smoke test (`scf_real_audit`) that
  measures whether realistic data provides dense substitution constraints —
  and explicitly does **not** measure parsing accuracy.

Reproduce everything with:

```bash
build/scf_audit objective-bias --K 2,3,4,5 --output-dir results/v1_3/bias
build/scf_audit objective-grid --seeds 20    --output-dir results/v1_3/objective_grid
build/scf_audit rho-objectives               --output-dir results/v1_3/rho
build/scf_real_audit --input data/real/tokenized.txt \
  --sample-sizes 100,500,1000,5000,10000 --seed 42 \
  --output-dir results/v1_3/real_audit
```

The real corpus is ~278k raw English sentences harvested locally from system
documentation (`/usr/share/doc`, `/usr/share/common-licenses`, CMake help) —
technical/legal register, whitespace-tokenized, no network download. Domain
bias is acknowledged; the audit's question (constraint geometry) does not
require balanced text, only genuine natural language.

---

## Q1 — Which normalized objective best removes opportunity-induced balance bias?

**All three remove it on the full Cartesian corpus; only `opportunity`
survives the rest of the criteria (see Q2–Q4).** On `nested_balanced`
(n = 4, K = 2, 3, 4, 5; `objective_bias.csv`):

| objective | balanced | left | right | argmax |
|---|---|---|---|---|
| raw_count | 2K² | K²+K | K²+K | 1 (balanced) |
| opportunity | 2.0 | 2.0 | 2.0 | **5 (all Catalan trees)** |
| conditional | 2.0 | 2.0 | 2.0 | 5 |
| jaccard | 2.0 | 2.0 | 2.0 | 5 |

`raw_count` reproduces the v1.2.1 bias exactly (Acceptance D); every
normalized objective achieves the ideal `balanced = left = right` — on an
observationally structureless corpus they correctly leave the full Catalan
set tied instead of manufacturing a bracketing. The quantized integer tie
(`round(strength·1e12)`) preserves all optima; no hidden floating-point
tie-break (test-guarded).

## Q2 — Does the chosen normalized objective preserve recovery on `simple_np_vp`?

**`opportunity`: yes, fully** (unique = exact = gold-in-argmax = 1.0 at high
coverage, K=4, 20 seeds). **`conditional`/`jaccard`: no** — at full coverage
they collapse to `unique = 0`: ratio normalization by |C(u)| awards a perfect
1.0 to the suffix span's weak 1-witness relation ("dog runs"~"dog sleeps",
|C|=1) just as to the subject span's strong 2-witness relation, erasing the
very asymmetry that identifies the structure. Their coverage curves are
*anti-learning*: unique rises to ~0.9 at coverage 0.2 and then falls to 0 as
coverage completes the symmetry.

## Q3 — Does it recover observable forced structure on correlated balanced/right/left grammars?

**`opportunity` (and `raw_count`): yes, exactly.** K=4, full coverage:

| grammar | objective | forced P (obs) | forced R (obs) | unique | gold∈argmax |
|---|---|---|---|---|---|
| correlated_balanced | raw / opportunity | 1.0 | 1.0 | 1.0 | 1.0 |
| correlated_right / _left | raw / opportunity | 1.0 | 1.0 | 0 (argmax 2) | 1.0 |
| any of the three | conditional / jaccard | 0.0 | 0.0 | — | 0 (chains) |

For the chains, `forced_precision_observable = forced_recall_observable = 1`
means precisely: the observable block-level split ([1,4) resp. [0,3)) is
forced across the entire optimal forest, while the frozen block's internal
bracket — for which the corpus carries no evidence — correctly stays
ambiguous (full-gold forced recall 0.5 by design, not by failure).
`conditional`/`jaccard` instead produce **UNIQUE_WRONG** trees on the chains
(gold excluded, forced precision 0): amplified 1/1 coincidences outvote the
K-witness block relations. This disqualifies them outright.

## Q4 — Does it remain ambiguous on genuinely symmetric population-level cases?

**Yes — all four objectives keep `symmetric_abc` at full coverage honest**:
gold-in-argmax 1.0, unique 0, argmax exactly 2 (Acceptance G). Symmetric
honesty is preserved by construction: normalization rescales pair strengths
symmetrically, so a population symmetry stays a tie.

## Q5 — How does `ForcedRecall_observable` change with coverage and rho?

- **Coverage** (correlated families, K=4, 20 seeds): under `raw_count` and
  `opportunity` the observable forced recall rises smoothly with coverage to
  1.0 (mirroring exact-match on the balanced family: 0 → 0.41 → 0.87 → 1.0
  across 0.2/0.4/0.6/0.8), with no spurious-wrong phase worse than
  `raw_count`'s (Property 6: their curves nearly coincide).
- **rho** (`rho_by_objective.csv`): under `raw_count` the sweep is smooth and
  monotone (forced recall observable 0 → 0.28 → 0.73 → 1.0 for rho 0 → 0.2 →
  0.6 → 1.0). Under `opportunity` the rho mechanism **stops working**
  (forced recall stays 0 at rho = 1.0, with a spurious-wrong dip mid-sweep):
  the marker sentences break the symmetry *by raising a witness count*, and
  count advantages are exactly what opportunity-normalization divides away
  (U_g grows in step with W_g). This is an honest, structural trade-off, not
  a bug to patch: count-based symmetry-breaking evidence is invisible to a
  count-normalized objective. Recorded as a v1.4 design constraint.

**Recommendation:** `opportunity` is the only normalized objective satisfying
Properties 1–5 with stability comparable to `raw_count` (Property 6), and it
is the recommended candidate going forward. Per the no-premature-switch rule,
**the default remains `raw_count`**; switching is a v1.4 decision that must
weigh the rho trade-off above.

---

## Q6 — On real corpora, does raw exact-context constraint density increase substantially with corpus size?

**Yes, steadily** (`real_audit_summary.csv`, N = 100 → 10,000 kept
sentences, len 2–10):

| N | contexts deg≥2 | yields w/ partner | proper spans w/ witness | witness pairs | density proxy |
|---|---|---|---|---|---|
| 100 | 1.8% | 8.4% | 8.9% | 1,060 | 0.47 |
| 500 | 3.0% | 16.8% | 15.1% | 31k | 3.2 |
| 1,000 | 3.6% | 22.5% | 18.3% | 125k | 6.8 |
| 5,000 | 5.0% | 40.5% | 26.9% | 3.1M | 41.7 |
| 10,000 | 5.6% | 47.8% | 30.6% | 12.4M | 89.7 |

Substitution structure accumulates: nearly half of all observed yields have
at least one substitution partner at N = 10,000, and 30.6% of proper span
occurrences carry at least one raw witness. (Density proxies are empirical,
not a proof of algebraic overdetermination or matrix rank.)

## Q7 — Does exact `(L,R)` remain too sparse even at N = 10,000?

**Yes.** 94.4% of raw contexts are still singletons (degree 1) at N =
10,000; mean context degree is only 1.26; the deg≥2 fraction climbs but is
at 5.6% after a 100× corpus increase. The claim "a large corpus already
provides plentiful substitution constraints" is **not** supported at the
exact-context level: constraints concentrate on a small repeated core while
the long tail of contexts never repeats.

## Q8 — Does strict-global saturation remain controlled or begin pathological collapse as N grows?

**Pathological collapse, with a sharp phase transition between N = 1,000 and
N = 5,000** (`saturation_real.csv`):

| N | collapse ratio | largest e-class ratio | rounds | unions |
|---|---|---|---|---|
| 100 | 0.089 | 1.1% | 2 | 231 |
| 1,000 | 0.213 | 4.4% | 3 | 4,325 |
| 5,000 | **0.931** | **78.9%** | 9 | 74,002 |
| 10,000 | **0.954** | **85.9%** | 7 | 138,603 |

Once enough accidental substitutions connect the graph, the congruence
cascade merges 86% of the observed string universe into one e-class. This is
the model-misspecification signal the v1.2 README predicted: strict global
monosemous equivalence does not survive real data at scale. Polysemy
pressure diagnostics (`top_polysemy_pressure_yields.tsv`) list high-context,
zero-overlap yields (e.g. corpus-specific terms appearing in hundreds of
contexts with no substitution partner) as future lexical-treatment
candidates — no split was performed (Acceptance C of the spec's §2.2).

## Q9 — Which regime best describes the real corpus?

```text
collapse-dominated
```

with strong exact-context-sparse features. Constraint density rises with N
(Regime-A trait), but the deg≥2 fraction stays near the 5% sparsity line
(Regime-B trait) while collapse becomes pathological far earlier than
density becomes rich (Regime-C, dominant). The rule-based classifier in
`REAL_CONSTRAINT_AUDIT.md` (auto-generated, deterministic) reports
`collapse-dominated`.

## Q10 — Which v1.4 direction is justified, on measurements only?

**C. both.**

- **Context abstraction / quotient contexts** is justified by Q7: exact
  `(L,R)` identity wastes 94% of contexts as singletons; the substitution
  signal that does exist (Q6) is trapped in a sparse exact-match relation.
- **Ambiguity-sensitive equivalence / lexical treatment** is justified by
  Q8: any abstraction built on top of strict-global equivalence would
  inherit an 86%-collapsed universe; the equivalence itself must become
  collapse-resistant first (the v1.2.1 ablation showed saturation is
  currently parse-inert, so this redesign is unblocked).

Additionally, the Q5 trade-off constrains the objective side: a v1.4
objective must keep opportunity-style geometry normalization for Cartesian
neutrality *without* discarding witness-count information entirely
(strength and confidence are already computed separately for exactly this
purpose). None of this is implemented in v1.3.

---

## Separation of failure sources (spec §39)

| phenomenon | classification |
|---|---|
| low-coverage dips on every family | sample insufficiency (more data helps) |
| `symmetric_abc` / frozen-block interiors at full coverage | population non-identifiability (more data cannot help) |
| raw_count's balanced preference on Cartesian data | objective bias (fixed by opportunity normalization, Q1) |
| conditional/jaccard UNIQUE_WRONG on chains | objective bias of a different kind (ratio amplification of weak evidence) |
| real-corpus e-class collapse at N ≥ 5,000 | model misspecification (strict global monosemy) |

## Acceptance checklist

**A** four switchable objectives (`--evidence-objective(s)`, all runnable) ✔ ·
**B** saturation semantics untouched, all prior tests pass (49/49) ✔ ·
**C** no polysemy implementation; diagnostics only ✔ ·
**D** Cartesian bias reproduced and compared, n=4, K=2..5 × 4 objectives ✔ ·
**E** correlated primary benchmark: 3 correlated families × K=4 × 20 seeds ×
7 coverages × 4 objectives (`objective_grid_runs.csv`, 2800 runs) ✔ ·
**F** `forced_precision_observable` / `forced_recall_observable` implemented
(per sentence + corpus means + `forced_span_metrics.csv`) ✔ ·
**G** symmetric honesty holds for all objectives ✔ ·
**H** real audit on a 278k-sentence local corpus at N=100..10,000 ✔ ·
**I** no parsing-accuracy claim anywhere in the real audit ✔ ·
**J** zero heuristic patches; `raw_count` remains the default ✔

## Answering the two v1.3 questions

> **Can substitution evidence be normalized so that tree ranking reflects
> structural regularity rather than context opportunity count?**

Yes — opportunity normalization (per-geometry witness fraction) removes the
q^(n−len) bias completely while preserving every genuine recovery the raw
count achieves, at the documented cost of blindness to count-based
symmetry-breaking evidence. Ratio-to-own-context normalizations
(conditional, Jaccard) are refuted: they amplify sparse coincidences into
confident wrong parses.

> **Are realistic corpora sufficiently constraint-rich for SCF-style
> induction, or is context abstraction required first?**

Not yet, on two independent grounds: exact contexts stay 94% singleton at
N=10,000, and strict-global equivalence collapses pathologically from
N≈5,000. Context abstraction *and* a collapse-resistant equivalence are
prerequisites (Q10: both), in that order of evidence strength.
