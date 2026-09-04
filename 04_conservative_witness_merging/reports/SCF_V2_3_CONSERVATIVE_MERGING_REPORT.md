# SCF v2.3 — Conservative Evidence-Driven Category Merging

## Status

The v2.3 baseline is implemented as an independent C++20 module and CLI.
The synthetic oracle sanity check passes all four required cases. The real
`1e5, 1e6, 1e7, 1e8` condition-D ladder is wired end to end, but it is not
executed in this commit because the 100M-token Wikipedia source used by v2.2
is intentionally not stored in the repository. No real-scale values are
fabricated here.

The implementation's invariant is:

> absence of evidence keeps objects separate; it is never recorded as a
> proof that the objects are unequal.

## 1. What changed

New targets:

```text
scf_conservative_v23          library
scf_conservative_merging      experiment CLI
scf_conservative_v23_tests    regression suite
```

The discovery path contains no occurrence counts, shared-count thresholds,
Jaccard/Dice/PMI/conditional probabilities, embeddings, POS features, or LLM
scores. There is also no hub cap. POS is loaded only after the partition is
finished.

Condition D is reproduced from v2.2: sentence-terminal boundaries are
explicit in the observation representation and punctuation tokens are
removed. Prefixes are nested by whole sentence using the same condition-A
nominal token boundaries as v2.2; the actual condition-D token count is
reported separately.

## 2. Exact evidence representation

For every observed candidate substring `u` of length `1..max_len` (default
3), the learner records the exact complete-sentence frame `(L,R)` in
`L u R`. Prefix and suffix tries give exact sequence identity without hash
collisions and without storing duplicate context strings.

Each distinct `(u,v,L,R)` witness is emitted once even if either sentence is
repeated. The local witness table and the global partition are separate data
structures. A pair enters `MergeCandidate` iff at least one direct local
witness exists; candidates are deduplicated by unordered object pair. There
is no connected-component pass over the witness graph.

Observed concatenations are stored as a relation:

```text
Comp(object_left, object_right, object_result)
```

No totality, associativity, or global functionality assumption is encoded.

## 3. Conservative transactional merge

Every candidate is evaluated in a rollback transaction.

1. Tentatively identify its two current classes.
2. Compare only observed left- and right-composition behavior whose partner
   is already in the same current class. Missing behavior is ignored; it is
   not negative evidence.
3. If the quotient makes the same input expose two output classes and those
   outputs have a direct witness, enqueue the output merge. Repeat to a fixed
   point.
4. If the outputs have identical observed exact-context profiles, keep both
   outputs in the `Comp` relation. This is the explicit non-functional case.
5. If the outputs are observationally separated in the current positive
   table and have no direct witness permitting an output merge, roll back the
   whole transaction and emit the exact two conflicting `Comp` constraints.

Step 5 is deliberately a conservative learner decision, not a theorem that
the outputs are unequal in the language. More data may add a direct witness
and make a previously blocked closure possible.

## 4. Synthetic oracle sanity check

Pinned output (`results_v2_3_conservative/oracle_sanity.txt`):

```text
same-class lexical items dog/cat: 1
observationally indistinguishable alpha/beta merged: 1
single-context false friends p/q rejected: 1
low-frequency rare1/rare2 separate before second witness: 1
low-frequency rare1/rare2 merged after more data: 1
learned-vs-oracle lexical pair precision: 1
learned-vs-oracle lexical pair recall: 1
learned-vs-oracle TP/FP/FN: 4/0/0
objects=65 witnesses=76 candidates=73 accepted=18 rejected=1 classes=41
```

The `p/q` case is the critical guard against `one context -> DSU union`.
They share the frame `a _ z`, but `p·t` and `q·t` have distinct observed
external behavior and no direct witness relating the outputs, so the primary
merge is rolled back. `alpha/beta` exercises quotient propagation: merging
the lexical pair induces the witnessed merges of `x alpha/x beta`,
`alpha y/beta y`, and the complete sentences. `rare1/rare2` demonstrates the
monotone positive-evidence interpretation: both exist early but remain
separate; the later matching frame unlocks the merge.

The learned lexical partition is compared pairwise with the oracle external-
equivalence partition, including the `N`, `IV`, observationally equivalent,
and low-frequency classes. Precision and recall are both 1.0; oracle labels
are evaluation-only.

## 5. Real-scaling outputs

Running the CLI writes:

```text
results_v2_3_conservative/
  conservative_scaling.csv
  successful_merges.txt
  rejected_merges.txt
  class_examples.txt
  oracle_sanity.txt
```

`conservative_scaling.csv` includes, per nested prefix:

```text
initial objects
distinct local witnesses
distinct merge candidates
accepted / rejected / redundant candidates
induced quotient unions
resulting E-classes
largest-class ratio
median / p95 class size
pairwise partition change from the previous scale
within-class POS purity (external only)
pairwise same-POS precision (external only)
runtime
```

The example files emit up to the requested limit per scale (default 20).
Rejected examples include the candidate's exact `(L,R)` witness and the two
concrete conflicting composition triples.

Reproduction after preparing the same real corpus used for v2.2:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target scf_conservative_merging
build/scf_conservative_merging \
  --input data/real/wiki_text.txt \
  --scales 100000,1000000,10000000,100000000 \
  --output-dir results_v2_3_conservative \
  --ud data/real/en_ewt-ud-train.conllu
```

For the oracle alone:

```bash
build/scf_conservative_merging --oracle-only
```

## 6. Verification

The local environment did not provide `cmake`, so the same source sets were
compiled directly with GCC 13.3 using `-std=c++20 -Wall -Wextra -Wpedantic`.
Results:

```text
v2.3 conservative tests:  5/5
v1.x core tests:         56/56
v2.0 oracle tests:       13/13
v2.1/v2.2 tests:         12/12
```

The full v2.3 CLI was also smoke-tested on the checked-in synthetic corpus,
including CSV and diagnostic-file generation.

## 7. Scaling caveat to measure, not hide

Removing the v2.1/v2.2 frequency floor means the inventory contains every
distinct observed length-1..3 substring. Exact full-sentence contexts also
remove the old immediate-token hub explosion, but the observation table can
still be much larger than the previous frequent-substring table. Runtime and
memory at `1e8` are therefore empirical outputs of this baseline, not
silently controlled by reintroducing a count threshold. If the top scale is
resource-bound, the correct report is the measured bound; changing the
inventory by frequency would define a different experiment.
