# SCF v2.2 — Terminal × Punctuation Ablation Report

Question under test:

> **How much of category discovery is carried by sentence-terminal
> information, and how much by punctuation — holding the v2.1 method
> (exact contexts, raw shared-context counting, thresholds, hub cap,
> ladder) completely fixed?**

**Verdict in one paragraph.** Both signals are real, both are small, and
neither rescues the v2.1 failure modes. The terminal anchor is genuine
signal: terminal-behavior purity of evidence pairs *rises* with evidence
threshold and with scale (C at 1e8: 0.650 at m=1 → 0.732 at m=16, against a
0.525 baseline) — the one purity metric in the whole v2.x series that
sharpens with more evidence — and the anchor adds ~1–3 points of POS purity
and ~2–3 points of held-out replication. Punctuation adds replicable
evidence mass (+3.6 points of held-out replication in the 4–7 bucket) but
*degrades* POS purity at high evidence thresholds (−1.4 to −10.4 points),
because punctuation tokens intrude into evidence pairs across categories.
Sentence-final `.?!` kept as ordinary tokens (condition E) recovers most of
the terminal signal (capable share 0.576 vs 0.613, purity within ~3–5
points of C) but less cleanly (complete-sentence observations 0.077 vs
0.124). Decisively for Q5: with the terminal anchor in place, the giant
component still stands at 0.995 (m=1) and 0.680 (m=16) at 1e8, and POS
purity still falls with m — **v2.1's failure is raw-count bias, not the
missing sentence anchor.**

Data: `results_v2_2_ablation/terminal_punctuation_ablation.csv`
(per-condition rows plus `delta_terminal` and `delta_punct` rows) and
`ablation_neighborhood_samples.txt`. Total runtime ≈ 10 min for all five
conditions over 4 scales, single-threaded.

## 0. Design

Same corpus and same split as v2.1 (English Wikipedia 2017 via gensim-data;
FineWeb remains unreachable under this environment's network policy).
Sentence segmentation is computed once on the base token stream — every
`. ? !` token ends a sentence, as does a document boundary — and the
train/held-out split is the *same set of sentences* for every condition
(scale prefixes rounded up to whole sentences, measured in condition-A
tokens; the held-out shard starts at the first new document after the
largest train prefix: 6,214,262 sentences total, 868,115 held out).

| condition | terminal | punctuation | stream |
|---|---|---|---|
| A | none | aware | final `.?!` consumed by segmentation; internal punctuation kept |
| B | none | free | additionally drops internal punctuation tokens |
| C | anchor | aware | `<s>` sentinel around every sentence |
| D | anchor | free | both |
| E | none | aware | final `.?!` kept as ordinary tokens (leak probe) |

`<s>` behaves exactly like `<doc>`: it appears in exact contexts (a
sentence-final token observes right context `<s>`; a complete sentence span
of length ≤ 3 observes `(<s>, <s>)`) but never inside a substring and never
in the lexical inventory. Nothing else changes: same exact contexts, same
raw `|I_N(u,v)|` counting, same thresholds m ∈ {1,2,4,8,16}, same hub cap
32, same held-out sampling. Note the two token budgets: for the same
sentences, punctuation-free conditions see ~14% fewer tokens (86.1M vs
100.0M at nominal 1e8) and E ~4% more (104.4M) — the ablation holds *text*
constant, not token count, and actual counts are reported per row.

Two new exact observables (no thresholds): `final_capable(u)` — u occurs at
least once with a terminal right context — and `complete_span(u)` — u
occurs at least once with terminal contexts on both sides; pair-level
terminal purity is the rate at which evidence pairs agree on
`final_capable`, against the random-pair baseline p² + (1−p)².

## 1. Q1 — Does the terminal anchor reduce giant-component collapse?

Largest-component ratio at 1e8 tokens:

| m | A (no term) | C (anchor) | B | D | Δ_terminal |
|---|---|---|---|---|---|
| 1 | 0.995 | 0.995 | 0.998 | 0.998 | −0.000 |
| 4 | 0.968 | 0.957 | 0.981 | 0.976 | −0.008 |
| 16 | 0.704 | 0.680 | 0.717 | 0.684 | −0.029 |

**No.** The anchor shaves at most 2.9 points off the giant component at the
highest threshold and changes nothing at m=1. The collapse dynamics across
scales are also unchanged (C's m=16 ratio grows 0.015 → 0.026 → 0.103 →
0.680, tracking A almost exactly). Two second-order effects are worth
recording: the anchor *adds* hub mass (Δ_terminal on hub record share
+0.012 at 1e8 — sentence-initial and sentence-final positions are
themselves promiscuous contexts), and it slightly shrinks the substring
inventory (−2,554 at 1e8) by removing cross-sentence-boundary bigrams and
trigrams.

## 2. Q2 — Does punctuation independently provide grammatical signal?

Punctuation contributes **evidence mass, not category signal**:

- Shared-context coverage: Δ_punct +0.045 → +0.023 (1e5 → 1e8) — commas,
  parentheses, and quotes create many genuinely shared contexts.
- Held-out replication at 1e8: the punctuation-aware conditions replicate
  better in the low/mid buckets (bucket 4–7: A 0.936 vs B 0.889, Δ_punct
  +0.036) — that added evidence is real and generalizes.
- POS purity: negative where it matters. At m=16, punctuation-aware runs
  score 3–10 points *below* punctuation-free at small scales (1e5: A 0.488
  vs B 0.577) and remain below at 1e8 (0.306 vs 0.322, Δ_punct −0.014);
  qualitatively, the top of A/C's "was" neighborhood is `is, ","` — the
  comma outranks `were` — while B/D's is a clean `is, and, were, are, in,
  as, had`. Punctuation tokens are extreme distributional promiscuers: they
  form high-evidence pairs across every category (they are also themselves
  a UPOS class, PUNCT, and their cross-class pairs are exactly the
  high-|I| errors).
- Hub mass: Δ_punct +0.028 at 1e8 — punctuation feeds the hub problem.

So punctuation-as-token is a double-edged observable under raw counting:
more (replicable) evidence, worse category purity at the top of the
evidence distribution. It does not act as a stand-in grammatical signal.

## 3. Q3 — Do `.?!` as ordinary tokens approximate the terminal anchor?

Largely yes, with a measurable purity gap (all at 1e8):

| observable | C (`<s>` anchor) | E (`.?!` proxy) |
|---|---|---|
| terminal-capable share | 0.613 | 0.576 |
| complete-span share | **0.124** | **0.077** |
| terminal purity m=1 / m=4 / m=16 | 0.650 / 0.709 / 0.732 | 0.619 / 0.666 / 0.678 |
| neighborhood terminal completion | 0.988 | 0.938 |
| general metrics (LCR, coverage, POS) | ≈ A ± anchor deltas | ≈ A |

Sentence-final punctuation leaks most of the terminal signal — E's terminal
observables land within 3–5 points of the explicit anchor — which explains
why v2.1 (whose stream was exactly condition E) already behaved almost
identically to the anchored runs on every general metric. The residual gap
has a structural cause: in E, `.` is an ordinary token, so substrings can
span sentence boundaries ("dog . the"-type bigrams/trigrams exist, complete
sentences are under-observed by ~40%), and `.` itself participates in
evidence pairs. The anchor is the same information, delivered cleanly.

## 4. Q4 — Which contributes more: terminal or punctuation?

Δ rows at 1e8 (positive favors the first-named setting):

| metric | Δ_terminal = (C+D)/2 − (A+B)/2 | Δ_punct = (A+C)/2 − (B+D)/2 |
|---|---|---|
| POS purity m=1 | **+0.011** | +0.019 |
| POS purity m=16 | **+0.023** | **−0.014** |
| held-out replication, bucket 1 | **+0.028** | +0.016 |
| held-out replication, bucket 4–7 | +0.016 | **+0.036** |
| neighborhood Jaccard stability | −0.015 | +0.006 |
| giant component m=16 | **−0.029** (smaller) | −0.009 |
| hub record share | +0.012 (worse) | +0.028 (worse) |

- **For category purity: terminal.** The anchor's POS-purity contribution
  is small but *consistently positive and growing with m* (and its own
  terminal purity rises with both m and N — from 0.60 at 1e5 to 0.73–0.78
  at 1e8 in C/D, the only evidence-sharpened purity signal in v2.x).
  Punctuation's contribution flips sign at high m.
- **For raw replicable evidence: punctuation.** It buys the largest
  mid-bucket held-out replication gain; the anchor's replication gain is
  concentrated in the singleton bucket.
- **Stability:** neither moves probe-neighborhood Jaccard beyond noise
  (±0.015 against a 0.63–0.67 base).
- The strongest single condition on terminal/POS purity is **D**
  (anchor + punctuation-free): terminal purity 0.703/0.770/0.781 over
  m=1/4/16 — both interventions point the same way, and their effects are
  roughly additive, not interactive.

## 5. Q5 — Raw-count bias, or missing terminal anchor?

**Raw-count bias.** The decisive observation is what the anchor does *not*
change: with `<s>` in place (C), the shared-context graph is still one
giant component at m=1 (0.995), still 68% giant at m=16, POS purity still
*falls* from m=1 to m=16 (0.419 → 0.332), the comma still outranks `were`
in the neighborhood of `was`, and the hub-mass concentration is slightly
*worse*. The terminal anchor adds a genuinely learnable, evidence-sharpened
observable (terminal purity is the proof that the machinery can extract a
category-like signal when one is cleanly marked), but it perturbs the v2.1
failure metrics by at most ~3 points. Meanwhile v2.1's stream was already
E, which carries most of the terminal information — so no meaningful part
of the v2.1 outcome is attributable to the missing anchor. The binding
bottleneck remains the evidence definition itself (raw |I| counts tracking
frequency/opportunity), exactly as the v2.1 report concluded; v2.2 closes
the loophole that sentence-terminal supervision-by-observation might have
been the missing ingredient.

## 6. Verification

The v2.1 pipeline was extracted into a parameterized ladder engine; the
v2.1 runner's outputs were verified **bit-identical** (modulo runtime/RSS
fields) against the pre-refactor binary on real data, and the existing
naive-reference suite passes unchanged. Four new deterministic tests cover
the ablation: exact condition-stream vectors for all five conditions on a
hand corpus (including `<s>` placement and E ≡ base), hand-checked
C-condition terminal metrics (capable share 0.5, purity 1.0, the exact pair
set), complete-span observation, and CSV structure plus byte-level
determinism of a full rerun. `ctest` runs all three suites green
(v1.x, v2.0, v2.1+v2.2).
