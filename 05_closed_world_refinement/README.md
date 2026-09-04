# Research line 05 — Counterexample-guided closed-world refinement (v2.4)

**Direction.** Stop treating a shared context as evidence *for* a merge.
The observed corpus `D` defines a closed-world acceptance function
`Accept_D(s) = [s ∈ D]`, and two candidate strings are equivalent iff they
behave identically under every frame of a bounded, explicitly enumerated
context universe:

```text
u ≡_D v   ⇔   ∀ (L, R) ∈ C_D :  Accept_D(L u R) = Accept_D(L v R)
```

No frequency, threshold or similarity score appears anywhere. The partition
is computed by **partition refinement**: every observed positive frame
`c = (L, R)` is a test `T_c(u) = [L u R ∈ D]`; a block `B` with
`B1 = {u ∈ B : T_c(u) = 1}` and `B0 = B \ B1` both non-empty is split by the
distinguishing context `c`; splitting repeats until no context splits any
block. The result is provably the partition by full contextual signature,
which the tool verifies against a brute-force reference at every scale.

**Status.** Current line. Full write-up, all numbers and the answers to
the six research questions:
`reports/SCF_V2_4_CLOSED_WORLD_REFINEMENT_REPORT.md`.

## Semantics made explicit

- *Not observed* is a closed-world negative at the current corpus scale.
  This is an equivalence over the empirical language `D`, not a claim about
  natural-language grammaticality.
- Every scale is recomputed from scratch: a split found at 1e5 is not an
  axiom at 1e6 (`pairs_merged_prev` in the CSV counts exactly the pairs
  that a larger corpus re-unites).
- `(ε,ε)` is one terminal test bit (complete span or not). It can only
  *split* a block; there is no pairwise witness table, so the
  complete-sentence clique of v2.3 cannot exist. Objects whose only observed
  frame is `(ε,ε)` are, by definition, indistinguishable in `D` and remain
  one class (`terminal_only_objects`); this class is reported, not hidden.
- Three bounded context universes are run at every scale, with no context
  abstraction:

  | name | frames |
  |---|---|
  | `all_frames` | every observed exact frame, `(ε,ε)` and one-sided boundary frames included |
  | `internal_only` | `L ≠ ε` and `R ≠ ε` |
  | `boundary_frames` | frames that include a sentence boundary (`L = ε` or `R = ε`) |

  `all_frames` is the disjoint union of the other two.

## Efficiency

The observation table stores the positive relation `{(c, u) : L u R ∈ D}`
as a compressed sparse index in both directions (exact frame identity via a
128-bit content hash *verified* against an exemplar; substrings of length
1..3). Refinement processes each universe context once per pass and touches
only the blocks of its positive objects, so the cost per pass is
`O(Σ_c |objects_of(c)|)`; members that are negative for `c` are never
enumerated and no negative example is ever materialised. There is no
`O(|objects|²)` pair generation and no witness table: the quadratic
empty-frame clique of v2.3/v2.3.1 is gone (1e6 tokens: 0.6 s refinement
versus 43 min for the v2.3 merger on the same prefix).

## Layout

| what | where |
|---|---|
| module | `include/scf/closed_world.hpp`, `src/closed_world.cpp` (namespace `scf::v24`, library `scf_closed_world_v24`) |
| tool | `tools/scf_closed_world.cpp` (binary `scf_closed_world`) |
| tests | `tests/test_closed_world.cpp` (`scf_closed_world_v24_tests`, 13 tests incl. the six required synthetic oracle cases) |
| results | `results/pes2o_structured/` (main ladder 1e5–1e7, three universes), `results/pes2o_structured_v23_comparison/` (1e5–1e6 with the v2.3 merger run on the same prefixes) |
| report | `reports/SCF_V2_4_CLOSED_WORLD_REFINEMENT_REPORT.md` |

Dependencies: `scf_clean_corpus_v231` (line 04) for the structured corpus
loader, frame-type classification and the v2.3 merger used in the comparison;
through it the v2.1 tokenizer (line 03) and the platform layer.

## Running

```bash
cmake --build build --target scf_closed_world scf_closed_world_v24_tests
build/05_closed_world_refinement/scf_closed_world_v24_tests
build/05_closed_world_refinement/scf_closed_world --oracle-only            # six synthetic cases

build/05_closed_world_refinement/scf_closed_world \
  --input data/real/pes2o_body.scs --corpus pes2o \
  --scales 100000,200000,400000,1000000,10000000 \
  --universes all,internal,boundary \
  --ud data/real/en_ewt-ud-train.conllu \
  --output-dir 05_closed_world_refinement/results/pes2o_structured

# same ladder plus the v2.3 conservative merger on every prefix <= 1e6
build/05_closed_world_refinement/scf_closed_world \
  --input data/real/pes2o_body.scs --corpus pes2o \
  --scales 100000,200000,400000,1000000 --compare-v23-max-scale 1000000 \
  --ud data/real/en_ewt-ud-train.conllu \
  --output-dir 05_closed_world_refinement/results/pes2o_structured_v23_comparison
```

Other options: `--examples N` (splits / probe pairs listed per scale),
`--largest-classes N`, `--probe "u|v"` (add a probe pair),
`--no-oracle-check` (skip the sparse brute-force comparison),
`--max-substring-length 3`.

## Output files (per run directory)

- `closed_world_scaling.csv` — one row per (scale, universe): objects,
  observed contexts and positive records, universe size, context tests,
  effective splitters, block splits, refinement rounds, membership queries,
  final / singleton / non-trivial classes, largest class and ratio,
  median and p95 class size, terminal-frame diagnostics, partition change
  against the previous scale (`pairs_split_prev` = new distinctions,
  `pairs_merged_prev` = repaired closed-world splits), POS diagnostics,
  oracle agreement, v2.3 comparison columns, runtime and peak RSS.
- `distinguishing_contexts.txt` — per (scale, universe): the first block
  splits in refinement order as `u | v | L | R | Accept(LuR) | Accept(LvR)`,
  the probe pairs (`<num>` / `conclusions` / `introduction` / …) with the
  context that separates them, and the classes of the `(ε,ε)` objects.
- `class_examples.txt` — the largest classes with their shared signature,
  member lists, lexical-member and terminal-member counts, UPOS histogram
  of labeled members, and a class-size histogram.
- `oracle_comparison.txt` — the six synthetic cases (PASS/FAIL per check)
  followed by the refinement-vs-signature comparison at every real scale.
