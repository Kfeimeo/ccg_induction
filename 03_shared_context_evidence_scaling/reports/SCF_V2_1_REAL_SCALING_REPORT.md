# SCF v2.1 — Real Corpus Scaling Report

Question under test:

> **Holding the method fixed — exact token contexts, shared-context counting,
> no abstraction, no supervision, no new heuristics — how far does
> external-category evidence naturally converge as real English text grows
> from 1e5 to 1e8 tokens?**

**Verdict in one paragraph.** Scale delivers exactly what it can deliver:
occurrence coverage, repeated substitution evidence, held-out replication,
and neighborhood stability all rise monotonically over three orders of
magnitude, and repeated train evidence is a strong predictor of held-out
generalization (99.2% replication for 16+ pairs at 1e8 vs 57% for singleton
pairs). What scale does **not** deliver is category separation: exact-context
sparsity relents only logarithmically (74.7% of distinct contexts are still
singletons at 1e8), evidence mass concentrates into promiscuous hub contexts
(3.8% → 31.5% of occurrence records), the shared-context graph collapses into
a giant component at *every* fixed evidence threshold as N grows (even m=16
reaches a 68% giant component at 1e8), and POS purity of evidence pairs stays
flat (~1.5x baseline enrichment) rather than sharpening with more evidence.
The remaining bottleneck is **not data volume**: it is the combination of
context exactness and the raw shared-count evidence definition — the same
opportunity bias the v1.3 synthetic audit isolated, now reproduced on real
text at 1e8 tokens.

Raw data: `results_v2_1_real/scaling_metrics.csv`,
`pair_evidence_scaling.csv`, `heldout_replication.csv`,
`neighborhood_samples.txt`. Full ladder runtime: 213 s single-threaded, peak
RSS 3.73 GB.

## 0. Data (and a documented substitution)

The spec names FineWeb. This execution environment's network policy denies
`huggingface.co` outright (CONNECT 403), along with every other large-corpus
host tested (statmt, wikimedia dumps, commoncrawl, gutenberg). The closest
reachable real English corpus is the **English Wikipedia 2017-10-01 dump**
packaged by gensim-data as GitHub release assets, which the policy does
allow. `tools/fetch_wiki_corpus.py` performs a deterministic fetch — a fixed
400 MiB HTTP range of the first release part, decompressed as a truncated
gzip stream — yielding 100,332 articles / 1.154 GB of text (sha256
`fbecce93a0e0fd28f2e8964e717b37da5e3728c20f142b5377cb2fbc776a4368`). This is a substitution forced by the
environment, not a methodological choice; it is real, unlabeled,
markup-noisy English prose, which is what the experiment needs.

Fixed normalization (documented in `real_scaling.hpp`): lowercased letter
runs with internal apostrophes, digit runs → `<num>`, wiki bold/italic
apostrophe runs as separators, every other visible ASCII character a
single-character token, `<doc>` sentinels between articles. The experiment
consumes 121,005,199 tokens across 35,303 documents (vocabulary 985,402):
nested train prefixes of 1e5…1e8 tokens plus a document-disjoint 2e7-token
held-out shard that begins after the largest train prefix. Only the compact
uint32 id stream is ever held (~0.5 GB); the raw text is streamed once.

Per scale N: substrings of length 1–3 with
`min_count(N) = max(8, ceil(2e-6 · N))` (a constant relative-frequency
floor; 8 at 1e5–3e6, 20/60/200 at 1e7/3e7/1e8), exact single-token contexts
`C_N(u)`, and `I_N(u,v) = C_N(u) ∩ C_N(v)` built by inverting the context
index. Contexts hosting more than `hub_cap = 32` distinct substrings are
excluded from *pair generation* (never from the context statistics); they
are counted and their consequences measured (§4, §6). Unobserved strings are
never treated as negative evidence, and no global DSU/transitive merging is
performed anywhere — union-find appears only as a read-only diagnostic of
the shared-context graph.

## 1. Does exact-context sparsity relent with scale? (Q1)

| N | distinct contexts | singleton share | records in shared contexts | substrings with ≥1 shared context | mean contexts / substring |
|---|---|---|---|---|---|
| 1e5 | 70,699 | 88.9% | 31.0% | 92.8% | 31.3 |
| 1e6 | 690,454 | 83.2% | 49.4% | 98.6% | 38.9 |
| 1e7 | 5,029,375 | 79.3% | 62.4% | 99.9% | 106.4 |
| 1e8 | 29,021,887 | **74.7%** | **71.8%** | 99.99% | 834.6 |

Two truths at once. Measured over occurrence mass, sparsity relents
substantially: the share of context *records* that sit in a shared (degree
≥ 2) context goes 31% → 72%, and effectively every frequent substring has
substitution evidence by 1e7. Measured over context *types*, exact contexts
stay stubbornly sparse: the singleton share declines ~4–5 points per decade
(88.9 → 83.2 → 79.3 → 74.7) — extrapolating that log-linear trend, exact
contexts would remain majority-singleton until ~1e13 tokens. This is the
v1.3 real-audit finding ("94% singleton at N=1e4") extended four decades: the
mitigation is real, monotone, and far too slow for exactness to become cheap.

## 2. Do substitution neighborhoods stabilize? (Q2)

Mean top-20 neighborhood Jaccard for 28 fixed probes between consecutive
ladder steps (~3.3x data each): 0.31 → 0.44 → 0.56 → 0.64 → 0.74 → **0.76**
(3e7→1e8). Stability rises monotonically and has not saturated at 1e8.
Qualitatively (`neighborhood_samples.txt`) the neighborhoods are genuinely
distributional by 1e8:

- `was` ~ is, were, are, had (auxiliaries; interleaved with punctuation);
- `red` ~ white, black, blue, green at ranks 4–7 — behind `the`, `a`, `"`;
- `said` ~ stated, wrote, announced; `city` ~ town;
- `the` ~ a, his, an, their, its (determiners/possessives).

The pattern in those rankings is itself a finding: the top of every raw-|I|
neighborhood is claimed by promiscuous high-frequency items (punctuation,
determiners, `<num>`), with the categorially similar words just below. Raw
shared-context counts track opportunity (frequency), not category — on real
data at 1e8 tokens, exactly as the v1.3 span-length support law predicted on
synthetic data. `the` alone shares a context with 80,019 of the 92,191
substrings in the 1e8 inventory.

## 3. Does repeated train evidence predict held-out replication? (Q3 — core metric)

Held-out shard: 2e7 tokens, document-disjoint from every train prefix. Per
scale and train-evidence bucket, up to 2000 pairs sampled deterministically
in hash order; replication = the pair shares ≥ 1 exact context in the
held-out shard.

Replication rate by train-evidence bucket:

| N | m=1 | 2–3 | 4–7 | 8–15 | 16+ |
|---|---|---|---|---|---|
| 1e5 | 93.6% | 98.0% | 99.5% | 99.0% | 100% |
| 1e6 | 87.3% | 96.6% | 98.7% | 98.7% | 99.7% |
| 1e7 | 71.7% | 95.5% | 98.0% | 99.2% | 99.8% |
| 1e8 | **57.3%** | **81.9%** | **94.0%** | **97.3%** | **99.2%** |

**Yes — monotonically, at every scale.** Mean held-out shared-context counts
tell the same story with magnitude (1e8: 3.2 → 7.5 → 14.3 → 22.2 → 58.2).
The *decline down each column* is a base-rate effect, not a contradiction:
the held-out shard is fixed at 2e7 tokens while the train prefix grows, so
at 1e8 a singleton-evidence pair is a genuine tail event that a 2e7-token
sample often cannot re-witness, whereas at 1e5 every surviving substring is
so frequent that even singleton pairs replicate 94% of the time. Repeated
evidence (≥ 4 shared contexts) replicates at ≥ 94% everywhere — repeated
substitution evidence on real text is real signal, not noise.

## 4. Does a giant component emerge? (Q4)

Largest-component ratio of the shared-context graph (nodes = all frequent
substrings), by evidence threshold:

| N | m=1 | m=2 | m=4 | m=8 | m=16 |
|---|---|---|---|---|---|
| 1e5 | 0.910 | 0.489 | 0.136 | 0.046 | 0.019 |
| 1e6 | 0.962 | 0.583 | 0.200 | 0.068 | 0.026 |
| 1e7 | 0.986 | 0.871 | 0.529 | 0.243 | 0.102 |
| 1e8 | 0.995 | 0.987 | 0.960 | 0.874 | **0.684** |

**Giant collapse is not controlled at any fixed threshold.** At m=1 the graph
is a giant component from the start; the threshold needed to keep the graph
fragmented grows with N (m=4 sufficed at 1e6; m=16 no longer suffices at
1e8, where the graph is a single connected component at m=1). Since raising
m with N is precisely the "choose a threshold" move the spec forbids, the
honest conclusion is: under raw shared-context counting, more data makes the
graph *more* collapsed, not less — the real-corpus analogue of the v1.x
strict-global collapse, now measured as a function of scale. Any future
mechanism must make evidence frequency-calibrated (v1.3's `opportunity`
direction) rather than threshold-tuned.

The POS diagnostic (UD English EWT majority UPOS; labels never enter
discovery) confirms the mechanism: same-POS rate among labeled length-1
pairs is ~0.42–0.45 at m=1 against a 0.278 random baseline (~1.5x
enrichment), but does **not** rise with m at scale (1e8: 0.417 at m=1,
0.318 at m=16). High-|I| pairs are high-frequency pairs, and high-frequency
items disproportionately share promiscuous contexts across categories.
Evidence *quantity* is generalization signal (§3) but not category *purity*
signal — the two dissociate cleanly.

## 5. How much of the positive-only problem does data alone solve? (Q5)

The v2.0 oracle module isolated positive-only undersampling as spurious
merges (unobserved → indistinguishable) plus spurious splits (disjoint
observed contexts). Scaling real positive-only data:

- **Solved by data:** evidence *existence and stability* for the frequent
  inventory. 99.99% of frequent substrings have shared-context evidence at
  1e8; evidence on retained pairs keeps growing (mean 2.60 → 6.05 across the
  3e7→1e8 step); neighborhoods stabilize (Jaccard 0.76 and rising);
  repeated evidence replicates out-of-sample at 94–99%.
- **Not solved by data:** category *separation*. The giant component (§4)
  is the real-corpus face of the spurious-merge problem, and it worsens
  with N under exact raw counting. The inventory itself is also capped by
  the frequency floor: 985k observed token types vs ~22k frequent ones —
  the long tail never enters the evidence system at any tested scale.
- **Partially an artifact, honestly measured:** cross-scale pair churn.
  Between 3e7 and 1e8, 12.6M pairs persist (evidence up 2.3x), 38.0M appear,
  1.4M lose an endpoint to the rising frequency floor — and 9.0M (39% of the
  3e7 pair set) vanish because *every* sub-cap shared context they had
  crossed the hub cap as degrees grew. Exactness plus any fixed engineering
  cap makes pair membership unstable at scale; the cap's cost is measured,
  not hidden (hub record share 3.8% → 31.5%).

## 6. What is the remaining bottleneck? (Q6)

Ranked by the evidence above:

1. **The evidence definition (algorithm).** Raw |I| counts track frequency
   and opportunity, not category: giant components at every fixed m, POS
   purity flat in m, promiscuous items topping every neighborhood. This is
   the dominant bottleneck and it is *not* curable by more data — the curves
   move the wrong way.
2. **Context exactness (representation).** Singleton context share declines
   only ~4–5 points per decade; evidence mass migrates into hub contexts;
   39% pair churn per ladder step at the top scale is the direct price of
   exact contexts interacting with a degree cap. Some abstraction over
   contexts (explicitly out of scope here, and the v1.4/v1.5 question) is
   where the leverage is.
3. **Data sparsity (residual).** Real but least binding: it governs the tail
   (the m=1 replication decline, the frequency floor's 22k-token ceiling),
   while the frequent inventory is evidence-saturated by 1e7–1e8.

## 7. Engineering summary

Single-threaded, deterministic end to end (byte-identical reruns modulo
runtime/RSS fields, regression-tested): fixed tokenizer → uint32 id stream
(the raw corpus is never resident); Apriori-bounded substring counting
(every part of a frequent substring is frequent), dense sub-2^21 frequent
token ids packing bigram/trigram keys into one uint64; context inversion by
sort+unique of (l, r, u) records; pair generation only from contexts with
degree ≤ 32 (hubs counted); components via read-only union-find; held-out
sampling by smallest `mix64(key)` (no RNG state). Ladder totals: 213 s, peak
RSS 3.73 GB, all seven scales. Per-scale numbers are in
`scaling_metrics.csv` (`runtime_seconds`, `peak_rss_mb` columns); the top
scale alone: 103 s, 77M distinct context records, 123M pair emissions, 50.6M
distinct pairs.

The definitions were not adjusted to improve any result: the hub cap is the
single engineering concession, its effects are quantified (§5, §6), and
every reported failure mode — giant collapse, flat POS purity, slow
sparsity decline, cap-induced churn — is left standing as measured.

## 8. Verification

`tests/test_real_scaling.cpp` (8 tests, all offline): the full per-scale
model — frequent substrings, exact context sets, hub exclusion, pair
counts, threshold curves, component structure, transition counts, held-out
bucket statistics — is checked for exact equality against a naive
string-based reimplementation on a deterministic synthetic corpus; plus
tokenizer cases, hub-cap semantics pinned exactly, POS diagnostic sanity,
byte-level determinism, and pinned `mix64` values. The v1.x and v2.0 suites
are untouched; `ctest` runs all three green.
