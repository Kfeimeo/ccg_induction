# SCF v2.4 — Counterexample-Guided Closed-World Refinement

## 0. Status and scope

v2.4 replaces the v2.3 discovery rule "one shared exact context makes a
merge candidate" by closed-world contextual equivalence over the observed
language, computed by partition refinement. The v2.3 transactional merger
is not used for discovery any more; it is run only as the comparison
baseline on the same sentence prefixes.

Everything is implemented in the new research line
`05_closed_world_refinement/` (namespace `scf::v24`, library
`scf_closed_world_v24`, CLI `scf_closed_world`, test binary
`scf_closed_world_v24_tests`). The v2.3.1 structured corpus loader, the
frame-type classification and the v2.1 tokenizer are reused unchanged; the
v2.3 observation table (witnesses, compositions) is not built.

Corpus: the same peS2o v2 full-text body sample and `.scs` file as v2.3.1
(`scs` SHA-256 `4a05562940a8d86dad54a70574bfa37bce66be9905dd42feec45dddaedcfca81`,
1,371,122 sentences, 32.6 M whitespace tokens), same structure-preserving
preprocessing, same nested-by-sentence prefixes. No new corpus ablation.
POS labels (UD English EWT, majority UPOS per lowercased form) are loaded
after every partition is final and used for evaluation only.

All numbers below are produced by
`05_closed_world_refinement/results/pes2o_structured/` (main ladder, three
context universes, 1e5 – 2e7) and
`05_closed_world_refinement/results/pes2o_structured_v23_comparison/`
(1e5 – 1e6 with the v2.3 merger run on the same prefixes). Both runs are
deterministic; the regression suite re-runs a ladder twice and compares the
output files byte for byte.

## 1. Definitions

Empirical acceptance over the observed sentence set `D`:

```text
Accept_D(s) = 1  iff  s ∈ D,   otherwise 0.
```

Objects are the observed substrings of length 1..3 (the v2.3 inventory,
unchanged). For a bounded context universe `C_D` of exact full-sentence
frames `(L, R)`:

```text
u ≡_D v   iff   ∀ (L, R) ∈ C_D :  Accept_D(L u R) = Accept_D(L v R).
```

There is no frequency, threshold or similarity score anywhere in the
definition or in the implementation.

Semantics that are explicit in this version:

- *not observed* is a closed-world negative for the current scale;
- the relation is an equivalence over the empirical language `D`; nothing
  is claimed about natural-language grammaticality;
- every scale is recomputed from scratch, and the cross-scale comparison
  counts separately the pairs a larger corpus splits (new distinctions) and
  re-unites (repaired closed-world negatives);
- `(ε,ε)` is one terminal test bit — complete span or not. It can split a
  block; it cannot create a pairwise clique of complete-sentence spans,
  because no pairwise table exists.

### 1.1 Context universes (bounded, no abstraction)

| name | frames | 1e6: contexts |
|---|---|---|
| `all_frames` (A) | every observed exact frame, `(ε,ε)` and one-sided boundary frames included | 2,842,063 |
| `internal_only` (B) | `L ≠ ε` and `R ≠ ε` | 2,655,648 |
| `boundary_frames` (C) | frames that include a sentence boundary: `L = ε` or `R = ε`, `(ε,ε)` included | 186,415 |

A is the disjoint union of B and C. (The prompt's "C. all frames including
sentence boundaries" is read as "frames that include a sentence boundary";
read as "everything", it coincides with A, which is already run.)

## 2. Algorithm

### 2.1 Observation table

`build_observation_table` stores, per sentence prefix, the objects, the
exact frames and the positive relation `R = {(c, u) : L u R ∈ D}` as a
compressed sparse index in both directions (`contexts_of(u)`,
`objects_of(c)`, both sorted). Frame identity is exact: a 128-bit content
hash of `(L, R)` plus lengths is verified token-by-token against an
exemplar occurrence, with a collision chain (never a wrong id). The
membership query `Accept_D(L u R)` is a binary search in `contexts_of(u)`;
a negative is the absence of the record. No negative example is ever
materialised, and no witness, pair or composition table is built. The
test `table_matches_v23_observation_records` checks that the object set
and the `(L, u, R)` relation are identical to what `v23::observe_sentences`
produces on the same sentences.

### 2.2 Refinement

All objects start in one block. Every universe context `c` is a test
`T_c(u) = [L u R ∈ D]`; for every block `B` touched by `c` with
`B1 = {u ∈ B : T_c(u) = 1}` and `B0 = B \ B1` both non-empty, `c` is a
distinguishing context and `B → B1 ⊔ B0` is executed. A pass processes
every universe context once (order: descending number of positive objects,
then ascending id — an ordering choice for the search only); passes repeat
until a pass splits nothing. Because a split by `c` separates exactly the
objects that differ on `c`, and any two objects with different signatures
are separated by the first context on which they differ when it is
processed, one productive pass already yields the signature partition; the
second pass is the fixed-point check (`refinement_rounds = 2` everywhere).

Cost per pass is `O(Σ_c |objects_of(c)|)`: for context `c` only its
positive objects are visited, their blocks are counted, and only blocks
with `0 < count < |B|` are split by moving the positive members into a
fresh block (swap-remove, O(1) per move). Members of a touched block that
are negative for `c` are never enumerated. Every split is recorded as
`(c, u ∈ B1, v ∈ B0, |B|, |B1|, round)` — the counterexample.

Metrics reported per (scale, universe): `context_tests` = number of
(block, context) tests evaluated, `effective_splitters` = contexts that
split at least one block, `block_splits`, `refinement_rounds`,
`membership_queries` = positive-index lookups performed.

### 2.3 Brute-force references

`signature_partition` groups objects by their exact set of positive
universe contexts; `signature_partition_dense` materialises the full bit
vector `[Accept_D(L u R)]_{(L,R) ∈ C_D}` per object (small inputs only).
The tool compares the refinement partition with the sparse reference at
every real scale and universe; the tests compare it with the dense
enumeration on synthetic corpora.

## 3. Synthetic oracle tests

`scf_closed_world --oracle-only` and the test binary run the six required
deterministic cases (`oracle_comparison.txt`, synthetic section; every
line ends with PASS/FAIL, 44 checks, all PASS):

1. `dog`/`cat` identical on every bounded context → same class in all
   three universes (also `the dog`/`the cat`, `dog sleeps`/`cat sleeps`).
2. `mary`/`swimming` share the positive context `(ε, is fun)` but
   `(ε, runs)` is a `(1,0)` distinguishing context → different classes; the
   split is recorded with `Accept(L mary R) = 1`, `Accept(L swimming R) = 0`;
   `mary`/`john` (identical behaviour) stay together.
3. `introduction`, `<num>`, `conclusions` all satisfy `(ε,ε) = 1` but differ
   elsewhere → three different classes; the terminal test alone never
   merges. Objects whose *only* frame is `(ε,ε)` (`conclusions`,
   `<num> mice ran`, `see section <num>`) are indistinguishable in `D` and
   share a class — by definition, not by a hub mechanism.
4. Observationally indistinguishable pair (`alpha`/`beta`, plus
   `x alpha`/`x beta`, `alpha y`/`beta y`) → merged in all universes; no
   distinguishing context exists.
5. `D_small = {the dog sleeps, the cat sleeps, a dog runs}` splits `dog`/`cat`
   on the closed-world negative `(a, runs)`; `D_large = D_small ∪ {a cat runs}`
   merges them; `compare_partitions` reports `pairs_merged = 1`,
   `pairs_split = 0`.
6. Refinement vs brute-force dense signature enumeration on
   pseudo-random corpora (3 seeds × 3 universes in the tool, 4 seeds × 3
   universes in the tests, plus the sparse reference): identical partitions,
   identical class counts; every recorded split is a genuine `(1,0)`
   counterexample on a universe context; `block_splits + 1 = final_classes`.

Additional regression tests: the table equals the v2.3 observation
records; 200 one-token complete spans plus one internal use of one of
them produce `membership_queries ≤ 2 × records` (no quadratic clique),
199 terminal-only objects in one class and `h7` separated; POS and
terminal diagnostics on a hand-checkable corpus; the v2.3 comparison
separates a `(ε,ε)`-only merge; the ladder is deterministic and writes all
four output files with `oracle_identical = 1` in every row. All six
existing suites (v1.x core, v2.0, v2.1, v2.3, v2.3.1) still pass.

## 4. Real corpus: scaling (peS2o, structured, nested prefixes)

`initial_objects` is identical to the v2.3.1 table at every shared scale
(108,058 / 214,140 / 381,544 / 867,768). Refinement cost is linear in the
positive records; the table build dominates.

### 4.1 `all_frames`

| nominal | objects | observed frames | universe contexts | context tests | effective splitters | block splits | rounds | membership queries | final classes | non-trivial classes (objects) | largest | ratio | median / p95 | build s | refine s | peak RSS MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1e5 | 108,058 | 287,853 | 287,853 | 108,020 | 107,993 | 107,997 | 2 | 576,054 | 107,998 | 23 (83) | 35 | 3.2e-4 | 1 / 1 | 0.48 | 0.07 | 180 |
| 2e5 | 214,140 | 571,538 | 571,538 | 214,058 | 214,005 | 214,019 | 2 | 1,143,830 | 214,020 | 39 (159) | 77 | 3.6e-4 | 1 / 1 | 1.1 | 0.14 | 268 |
| 4e5 | 381,544 | 1,146,724 | 1,146,724 | 381,418 | 381,292 | 381,348 | 2 | 2,295,134 | 381,349 | 63 (258) | 118 | 3.1e-4 | 1 / 1 | 2.2 | 0.30 | 390 |
| 1e6 | 867,768 | 2,842,063 | 2,842,063 | 867,569 | 867,138 | 867,289 | 2 | 5,688,864 | 867,290 | 182 (660) | 238 | 2.7e-4 | 1 / 1 | 6.1 | 0.92 | 698 |
| 1e7 | 6,321,550 | 28,387,854 | 28,387,854 | 6,318,852 | 6,314,476 | 6,316,983 | 2 | 56,834,370 | 6,316,984 | 1,502 (6,068) | 1,832 | 2.9e-4 | 1 / 1 | 136.5 | 11.7 | 4,375 |
| 2e7 | 11,212,285 | 56,808,023 | 56,808,023 | 11,206,306 | 11,197,420 | 11,202,776 | 2 | 113,745,770 | 11,202,777 | 2,931 (12,439) | 3,428 | 3.1e-4 | 1 / 1 | 330.4 | 30.0 | 9,676 |

### 4.2 `internal_only` and `boundary_frames`

| nominal | universe | universe contexts | effective splitters | final classes | non-trivial classes | empty-signature objects (one class) | largest class excluding it | terminal-only objects | refine s |
|---|---|---|---|---|---|---|---|---|---|
| 1e5 | internal | 269,269 | 102,547 | 102,549 | 12 | 5,499 (5.1 %) | 2 | 0 | 0.06 |
| 1e5 | boundary | 18,584 | 9,851 | 9,855 | 23 | 98,129 (90.8 %) | 45 | 45 | 0.02 |
| 2e5 | internal | 534,070 | 203,070 | 203,073 | 18 | 11,051 (5.2 %) | 2 | 0 | 0.14 |
| 2e5 | boundary | 37,468 | 19,304 | 19,317 | 41 | 194,663 (90.9 %) | 103 | 103 | 0.05 |
| 4e5 | internal | 1,073,275 | 362,485 | 362,497 | 24 | 19,025 (5.0 %) | 2 | 0 | 0.27 |
| 4e5 | boundary | 73,449 | 33,991 | 34,030 | 81 | 347,224 (91.0 %) | 171 | 171 | 0.10 |
| 1e6 | internal | 2,655,648 | 824,350 | 824,378 | 89 | 43,291 (5.0 %) | 7 | 0 | 0.69 |
| 1e6 | boundary | 186,415 | 78,381 | 78,498 | 188 | 788,595 (90.9 %) | 369 | 369 | 0.23 |
| 1e7 | internal | 26,463,372 | 5,999,199 | 5,999,679 | 641 | 321,095 (5.1 %) | 12 | 0 | 10.8 |
| 1e7 | boundary | 1,924,482 | 606,008 | 607,989 | 1,682 | 5,706,475 (90.3 %) | 2,979 | 2,979 | 2.8 |
| 2e7 | internal | 52,952,811 | 10,642,883 | 10,644,068 | 1,269 | 566,590 (5.1 %) | 21 | 0 | 22.5 |
| 2e7 | boundary | 3,855,212 | 1,093,884 | 1,098,004 | 3,275 | 10,099,411 (90.1 %) | 5,580 | 5,580 | 5.6 |

In `internal_only` the objects with no internal frame at all (every
object that occurs only at a sentence edge or as a complete span — a
constant ≈ 5 % of the inventory) have the empty signature and form one
class by definition; in `boundary_frames` the same happens to the ≈ 91 %
of objects that never touch a boundary. Those two classes are
"no evidence" classes, not categories, and are reported separately.

### 4.3 Structure of the partition

The closed-world partition is almost the identity. The non-singleton
classes are, with three exceptions at 1e6, classes of objects that were
each observed in exactly **one** frame, the same frame:

| nominal | universe | objects with exactly one universe context | non-trivial classes sharing one context | non-trivial classes sharing ≥ 2 contexts (objects) |
|---|---|---|---|---|
| 1e5 | all | 81,604 (75.5 %) | 23 | 0 |
| 2e5 | all | 165,453 (77.3 %) | 39 | 0 |
| 4e5 | all | 291,354 (76.4 %) | 63 | 0 |
| 1e6 | all | 658,990 (75.9 %) | 179 | 3 (6) |
| 1e6 | internal | 628,215 | 85 | 3 (6) |
| 1e6 | boundary | 63,084 | 187 | 0 |
| 1e7 | all | 4,729,100 (74.8 %) | 1,480 | 22 (44) |
| 2e7 | all | 8,303,311 (74.1 %) | 2,891 | 40 (87) |

The three multi-context classes at 1e6 come from one paper's parallel
hypotheses (`effect on basic` / `effect on extra`,
`hedonic motives will` / `eudaimonic motives will`, three shared frames
each); the 22 at 1e7 are of the same kind — parallel figure captions
(`) sonolysis` / `) sonophotocatalysis`, four shared frames), parallel
section titles (`& future work` / `and future direction` after
`conclusion` / `conclusions`), typographic variants (`−udc` / `−u dc`),
all of size 2; the 40 at 2e7 (87 objects) add the first classes larger
than a pair — five parallel subsection titles of one review
(`surgical resection :` / `rfa :` / `rfa complications :` / `sbrt :` /
`sbrt complications :`, shared frames `(ε, lung metastases)` and
`(ε, liver metastases)`). Three quarters of all objects are hapax
observations (one frame), and two hapax objects are equivalent iff they
were observed in the same frame. Objects with two or more observations
are equivalent to something else only inside such template-parallel
fragments of a single document: 6 / 44 / 87 objects at 1e6 / 1e7 / 2e7.

The largest class in `all_frames` is at every scale exactly the set of
**terminal-only objects** — objects whose only observed frame is `(ε,ε)`
(35 → 77 → 118 → 238 → 1,832 → 3,428 of the 78 → 181 → 327 → 694 →
5,929 → 11,414 objects that carry `(ε,ε)`): `| gravitropism | | phototropism | histopathology and
immunohistochemistry | j . clin | | introduction | | participants | …`,
i.e. Grobid section headers and one-line fragments that never occur inside
a sentence. They are indistinguishable in `D` by definition. Every
`(ε,ε)` object that occurs anywhere else is a singleton: the 694 terminal
objects at 1e6 fall into 457 classes (238 in the terminal-only class, 456
alone).

### 4.4 Partition change across scales (common objects, `all_frames`)

| transition | common objects | pairs split (new distinctions) | pairs merged (repaired) | changed-pair share |
|---|---|---|---|---|
| 1e5 → 2e5 | 108,058 | 105 | 0 | 1.8e-8 |
| 2e5 → 4e5 | 214,140 | 231 | 0 | 1.0e-8 |
| 4e5 → 1e6 | 381,544 | 1,039 | 0 | 1.4e-8 |
| 1e6 → 1e7 | 867,768 | 11,688 | 0 | 3.1e-8 |
| 1e7 → 2e7 | 6,321,550 | 322,104 | 1 | 1.6e-8 |

`internal_only`: 1.87 M / 7.22 M / 34.9 M / 455 M / 10.3 B pairs split, 0 / 0 / 0 / 0 / 2 merged (the
empty-signature class loses members as they acquire an internal frame).
`boundary_frames`: 128 M / 474 M / 2.42 B / 45.0 B / 637 B split, 169 / 670 / 1,925 / 28,229 / 976,977 merged
(objects that acquire their first boundary frame move from the
empty-signature class into a boundary class, occasionally the
terminal-only class).

### 4.5 POS diagnostics (evaluation only)

| nominal | universe | labeled single-token objects | within-class purity | labeled within-class pairs | pairwise same-POS precision |
|---|---|---|---|---|---|
| 1e5 – 1e6 | all | 3,253 – 7,630 | 1.000 | **0** | undefined |
| 1e7 | all | 11,389 | 0.9999 | 1 | 0.000 |
| 2e7 | all | 12,163 | 1.000 | 0 | undefined |
| 1e5 | internal | 3,253 | 0.990 | 2,016 | 0.277 |
| 1e6 | internal | 7,630 | 0.994 | 4,656 | 0.341 |
| 1e7 | internal | 11,389 | 0.996 | 3,160 | 0.320 |
| 2e7 | internal | 12,163 | 0.997 | 2,556 | 0.282 |
| 1e5 | boundary | 3,253 | 0.552 | 2,717,952 | 0.265 |
| 1e6 | boundary | 7,630 | 0.663 | 8,374,294 | 0.266 |
| 1e7 | boundary | 11,389 | 0.779 | 7,176,421 | 0.248 |
| 2e7 | boundary | 12,163 | 0.810 | 5,710,539 | 0.247 |

In `all_frames` no two labeled tokens share a class at any scale, so the
purity of 1.0 is vacuous and the pairwise precision is undefined. The only
labeled within-class pairs in the other universes lie inside the
empty-signature classes (`internal`: 756 lexical members at 1e6 with UPOS
`NOUN=53 PROPN=16 VERB=9 ADJ=9 …`; `boundary`: 21,001 lexical members),
and their same-POS precision (0.27 – 0.34) is what a random pairing of
that class's labels gives. No linguistically meaningful lexical class is
produced by any universe.

## 5. Comparison with the v2.3 merger on the same prefixes

`--compare-v23-max-scale 1000000` runs `v23::observe_sentences` and
`ConservativeMerger` on each prefix and maps its objects onto the v2.4
table (identical substrings). The v2.3 numbers (classes, largest class,
accepted merges) reproduce the v2.3.1 CSV exactly at every scale. The
v2.3 merger at 1e6 took 5,432 s on this machine (v2.3.1 measured 2,579 s
on its hardware); the driver at the time re-ran it once per universe, so
the comparison run was stopped after the `all_frames` 1e6 row (the
`internal_only` / `boundary_frames` 1e6 rows are absent from that CSV; the
driver now runs the v2.3 merger once per scale).

| nominal | v2.3 classes / largest | v2.3 accepted merges | separated by v2.4 (`all`) | empty-frame merges separated | internal-frame merges separated | v2.3 same-class pairs → separated by v2.4 | v2.4 (`all`) same-class pairs → separated by v2.3 | v2.3 runtime s |
|---|---|---|---|---|---|---|---|---|
| 1e5 | 107,932 / 42 | 116 | 104 | 59 / 59 | 11 / 17 | 986 → 360 | 628 → 2 | 3.1 |
| 2e5 | 213,888 / 95 | 232 | 212 | 128 / 128 | 18 / 27 | 4,727 → 1,895 | 2,983 → 151 | 14.9 |
| 4e5 | 381,030 / 168 | 484 | 449 | 232 / 232 | 67 / 78 | 14,679 → 7,674 | 7,019 → 14 | 233.5 |
| 1e6 | 866,322 / 368 | 1,318 | 1,205 | 489 / 489 | 265 / 312 | 69,870 → 41,326 | 28,585 → 41 | 5,432 |

Every empty-frame merge accepted by v2.3 (`<num> ⇔ conclusions`,
`<num> ⇔ | introduction`, `<num> ⇔ j . clin`, …) is separated: `<num>`
has thousands of other frames, and the headers have none or others. The
12 / 20 / 35 / 113 v2.3 merges that survive at 1e5 / 2e5 / 4e5 / 1e6 are
pairs with identical signatures (single-frame parallel phrases such as
`differentially modulate auxin` / `differentially modulate photosynthetic`).
Conversely, almost every v2.4 class is inside a v2.3 class (628 → 2,
2,983 → 151, 7,019 → 14, 28,585 → 41): the closed-world partition is a refinement of
the v2.3 partition up to a few pairs — the exceptions are terminal-only
objects that v2.3's transactional check had rejected on a compositional
conflict — but it removes the v2.3 hub — v2.3's largest class (42 → 95 → 168 → 368) is
replaced by the terminal-only class (35 → 77 → 118 → 238), which is its
subset of objects with no other frame.

## 6. Distinguishing contexts (`distinguishing_contexts.txt`)

The file lists, per scale and universe, the first 20 block splits in
refinement order, the probe pairs, and the classes of the `(ε,ε)` objects.
Format: `u | v | L | R | Accept(LuR) | Accept(LvR)`. Twenty examples
from the 1e6 `all_frames` run (block size and `|B1|` in the file):

```text
the            | one          | L=[]                 R=[]            | 1 | 0   # (eps,eps): 694 of 867,768
(              | one          | L=[]                 R=[<num>]       | 1 | 0
abell          | the          | L=[]                 R=[<num>]       | 1 | 0   # splits the (eps,eps) block
phylogenetic   | one          | L=[]                 R=[analysis]    | 1 | 0
data           | the          | L=[]                 R=[analysis]    | 1 | 0
results        | the          | L=[|]                R=[]            | 1 | 0
decrease       | one          | L=[the]              R=[]            | 1 | 0
<num>          | the          | L=[]                 R=[. <num>]     | 1 | 0
availability   | one          | L=[data]             R=[]            | 1 | 0
<num> . <num>  | <num>        | L=[(]                R=[)]           | 1 | 0
table s <num>  | the          | L=[(]                R=[)]           | 1 | 0
results        | general discussion | L=[study]      R=[]            | 1 | 0
<num> . <num>  | one          | L=[(]                R=[)]           | 1 | 0   # internal_only, first split
amount         | one          | L=[( ids :]          R=[)]           | 1 | 0
has several    | one          | L=[this study]       R=[limitations] | 1 | 0
algorithm runs | one          | L=[the]              R=[as follows]  | 1 | 0
,              | one          | L=[<num>]            R=[<num>]       | 1 | 0
some           | one          | L=[this study has]   R=[limitations] | 1 | 0
```

Probe pairs (1e6, `all_frames`; the ones v2.3/v2.3.1 put into one class):

```text
u                     v                      L                                           R                              Accept(LuR) Accept(LvR)
<num>                 conclusions            phytochromes ( phys ) are ... ( chen & chory ,   ; franklin & quail , <num> )   1   0
<num>                 introduction           same frame                                                                     1   0
introduction          conclusions            discussion and                              (eps)                          0   1
introduction          discussion             following the                               in section <num> . <num> , ...  0   1
results               discussion             in contrast to arabidopsis , mutation of phyb <num> in tomato   only in ... 1   0
<num>                 the                    one important family of plant genes are     phytochromes                   0   1
<num>                 and                    plants use both internal                    external cues as signals ...    0   1
the                   and                    one important family of plant genes are     phytochromes                   1   0
the                   a                      one important family of plant genes are     phytochromes                   1   0
<num>                 [ <num> ]              phytochromes ( phys ) are ... ( chen & chory ,   ; franklin & quail , <num> )   1   0
<num>                 <num> . <num>          same frame                                                                     1   0
materials and methods conclusions            discussion and                              (eps)                          0   1
conflict of interest  acknowledgments        the authors declare no                      associated with the work ...    1   0
is                    was                    compared to arabidopsis , much less         known about the functions ...   1   0
in                    of                     one important family                        plant genes are the phytochromes 0   1
```

At 1e5 `introduction` and `conclusions` are separated by `L=[|] R=[]`
(`| introduction` is a Grobid header line, `conclusions` is not); in
`internal_only` at 1e5 they are in the same class because neither has an
internal frame there (both are in the empty-signature class); at 1e6 they
are separated in every universe (`abstract _ unhealthy lifestyle …` for
`internal_only`). `<num>` and the short complete spans never share a
class in any universe at any scale.

## 7. Efficiency

| nominal | v2.3.1 merger (witness table + transactions) | v2.4 table build | v2.4 refinement (`all`) | v2.4 peak RSS |
|---|---|---|---|---|
| 1e5 | 2.5 s | 0.48 s | 0.07 s | 180 MB |
| 2e5 | 8.4 s | 1.1 s | 0.14 s | 268 MB |
| 4e5 | 111 s | 2.2 s | 0.30 s | 390 MB |
| 1e6 | 2,579 s | 6.1 s | 0.92 s | 698 MB |
| 1e7 | not completed (> 70 h extrapolated, stopped at 7 GB) | 136.5 s | 11.7 s | 4,375 MB |
| 2e7 | — | 330.4 s | 30.0 s | 9,676 MB |

`membership_queries` is exactly the number of positive records per pass,
two passes (`5,688,864 = 2 × 2,844,432` at 1e6); no `O(|objects|²)` pair
table exists. The 694
`(ε,ε)` objects at 1e6 cost 694 lookups per pass and one block split, instead of
the 240,471 pairwise witnesses and 178,018 rejected transactions of
v2.3.1. Membership results are not cached across contexts because each
positive record is read exactly once per pass; the index itself is the
cache.

## 8. Answers

1. **Does closed-world contextual equivalence eliminate the v2.3 false-merge
   mechanism?** Yes. A shared context is no longer evidence for anything;
   only agreement on *every* universe context merges. On the same prefixes
   v2.4 separates 104 / 212 / 449 / 1,205 of the 116 / 232 / 484 / 1,318
   v2.3 accepted merges at 1e5 / 2e5 / 4e5 / 1e6 (every empty-frame merge:
   59 / 128 / 232 / 489; 11 / 18 / 67 / 265 of the 17 / 27 / 78 / 312
   internal-frame merges), and the v2.4 partition is, up to
   2 / 151 / 14 / 41 pairs, a refinement of v2.3's (§5).
   The prompt's named cases (`<num>` / `conclusions` / `introduction` /
   short complete spans) are separated at every scale by an explicit
   `(1,0)` context (§6).
2. **Is the `(ε,ε)` hub gone?** As a mechanism, yes: `(ε,ε)` is one bit,
   processed once per pass against the blocks of its 78 – 694 positive
   objects; it splits the initial block once and never generates a pair.
   What remains is the class of objects whose *only* frame is `(ε,ε)`
   (35 / 77 / 118 / 238 / 1,832 / 3,428 at 1e5 – 2e7, ≈ 0.03 % of objects,
   ≈ 1/3 of the `(ε,ε)` objects). It is the largest class of the `all_frames` partition
   and it grows linearly with the prefix, because every new header line
   that never recurs inside a sentence joins it. It is not a false merge
   under the definition — its members are literally indistinguishable in
   `D` — but it is also not a category; it is the closed-world image of
   "unobserved elsewhere".
3. **Do linguistically meaningful classes form without any frequency?** No.
   The exact closed-world equivalence over full-sentence frames is almost
   the discrete partition: 867,290 classes for 867,768 objects at 1e6,
   6,316,984 for 6,321,550 at 1e7, 11,202,777 for 11,212,285 at 2e7, one
   labeled token pair in one class across all scales (a different-POS
   pair at 1e7), and every non-singleton class
   except three consists of hapax objects that happen to share their
   single frame (§4.3). The distinguishing contexts are real and
   linguistically sensible (`is` vs `was`, `in` vs `of`, `the` vs `a` are
   separated by ordinary frames), but equality of complete exact-frame
   signatures requires two strings to co-occur in every frame either of
   them was seen in, which real text at 1e5 – 2e7 tokens provides only for
   template-parallel fragments of one document. The mechanism removes the
   false merges by removing (almost) all merges.
4. **Internal-only vs all-frame universe?** Removing the boundary frames
   removes the terminal-only class (its members now have the empty
   signature) but puts every object without an internal frame — a constant
   ≈ 5 % of the inventory, 43,291 objects at 1e6, including complete
   spans, sentence-initial-only and sentence-final-only substrings — into a
   single empty-signature class, i.e. a larger and less interpretable
   class than the one it removes. Excluding that class, `internal_only`
   is as discrete as `all_frames` (largest class 2 – 7). The boundary-only
   universe is the mirror image: 91 % of objects have no boundary frame and
   share the empty signature; the remaining 9 % are split by 18k – 186k
   boundary frames into the same terminal-only class plus singletons.
   The choice of universe therefore moves objects between "no evidence"
   and "unique evidence"; it does not produce classes of the kind v2.3
   was hoping for.
5. **With more data, are false splits repaired or do new distinctions
   appear?** New distinctions, exclusively. In `all_frames` the number of
   previously-equal pairs that a larger prefix splits is 105 / 231 / 1,039 / 11,688 / 322,104
   per step and the number of repaired pairs is 0 / 0 / 0 / 0 / 1
   (§4.4): one pair in 6.3 M common objects is re-united between 1e7 and
   2e7. Every additional occurrence of an
   object adds a frame that is (almost always) unique to it, so growth
   makes signatures more specific faster than it fills closed-world
   negatives. The synthetic case 5 shows repair is possible in principle;
   the corpus shows it does not happen at these scales.
6. **Is the optimized refinement equal to the brute-force signature
   partition?** Yes, exactly: the sparse full-signature partition is
   identical at every scale and universe (`oracle_identical = 1` in all
   rows, same class counts), and the dense enumeration is identical on
   every synthetic case (§3, case 6). The refinement is provably the
   signature partition (§2.2); the comparison is a check, not a
   heuristic.

## 9. Files

```text
05_closed_world_refinement/
  include/scf/closed_world.hpp        module interface
  src/closed_world.cpp                table, refiner, references, diagnostics, oracle cases, driver
  tools/scf_closed_world.cpp          CLI
  tests/test_closed_world.cpp         13 tests (six oracle cases included)
  results/pes2o_structured/           closed_world_scaling.csv, distinguishing_contexts.txt,
                                      class_examples.txt, oracle_comparison.txt (1e5 - 2e7, 3 universes)
  results/pes2o_structured_v23_comparison/   same files, 1e5 - 1e6, with the v2.3 columns filled
  reports/SCF_V2_4_CLOSED_WORLD_REFINEMENT_REPORT.md   this report
```

Verification:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
ctest --test-dir build --output-on-failure
  scf_tests, scf_oracle_v2_tests, scf_real_v21_tests,
  scf_conservative_v23_tests, scf_clean_corpus_v231_tests   passed (unchanged)
  scf_closed_world_v24_tests                                13/13 passed
```

GCC 13.3, `-std=c++20 -Wall -Wextra -Wpedantic`, no warnings in the new
line.
