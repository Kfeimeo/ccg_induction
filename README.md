# SCF — Symbolic Context Factorization v1.1

SCF is a correctness-first C++20 research prototype for discovering anonymous string equivalence and maximum-evidence projective binary structure from tokenized sentences. It uses no tags, labels, probabilities, embeddings, neural models, or external runtime libraries.

## Build and run

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The project test launcher also makes the requested shorter command work after the Release build, including with Visual Studio's multi-configuration generator:

```bash
ctest --test-dir build --output-on-failure
```

Run the supplied corpus:

```bash
build/Release/scf_cli --input data/synthetic/simple.txt --stats --dump-classes --dump-witnesses --dump-evidence --dump-trees
```

On Linux or a single-configuration Windows generator, the executable is normally `build/scf_cli`. The independent generator recreates all supplied controlled corpora:

```bash
scf_synthetic_generator data/synthetic
```

Important CLI options are `--max-len`, `--lowercase`, `--deduplicate`, `--config`, `--stats`, `--dump-classes`, `--dump-proofs`, `--dump-witnesses`, `--dump-evidence`, `--dump-optimal-forest`, and `--dump-trees`. Supplying `--output-dir DIR` writes:

- `saturation.csv`
- `pair_witnesses.csv`
- `span_evidence.csv`
- `sentence_analysis.csv`
- `eclasses.txt`
- `proofs.txt`

Input is one ASCII-whitespace-tokenized sentence per line. Empty lines are ignored. The defaults in `config/default.cfg` impose length 10, preserve case, deduplicate sentence types, disable global occurrence consistency, use `strict_global` equivalence, count exact raw surface contexts, aggregate with `max_pair_support`, and exclude leaf/root scores.

## Research semantics

### Finite observed universe

For every retained sentence type, preprocessing interns every non-empty contiguous substring plus one distinguished epsilon string. This finite set is the complete universe. Saturation only merges IDs already in that set; it never synthesizes a new non-empty surface string.

Every non-empty span creates an immutable occurrence `(sentence, begin, end, yield, left_context, right_context)`. Logical context triples are deduplicated by `(left, right, yield)` while retaining all source occurrence IDs. The whole-sentence context `(epsilon, epsilon)` is excluded. Every distinct observed string also contributes all of its internal binary surface decompositions as deduplicated concat triples.

### Context substitution

At the start of each context phase, records are keyed dynamically by:

```text
(find(left_context), find(right_context))
```

All distinct yields in one bucket are star-unioned. Literal context strings are not treated as permanently different: later equality can make two context signatures coincide.

### Concatenation congruence

Every observed decomposition `left + right = result` is keyed by:

```text
(find(left), find(right))
```

Results in one bucket are star-unioned. A concat triple is an algebraic surface fact, not a proposed syntax split. Congruence propagates equality upward only to observed result strings.

### Fixed point and context representation

Context and concat records are canonicalized again every round. Contexts deliberately have no separate union-find: context equivalence is exactly the product of the two current string e-classes. This is why a new string equality can merge context buckets on a later round and cause another equality, which can in turn enable more concat congruence.

With `M` observed strings, every successful union reduces the number of e-classes by one. There are at most `M-1` successful unions, no surface strings are generated, and a final no-change round terminates the algorithm.

### Provenance

Every successful DSU merge stores its two surface endpoints, rule kind, source record pair, canonical bucket key, and round. These edges form a proof forest over each final e-class. `proof_chain()` recovers a trace between any two equivalent strings; `--dump-proofs` and `proofs.txt` expose the individual edges.

## Contextual equivalence is not constituency

Equivalence is global over surface strings, but `u ≡ v` does not imply that occurrences of `u` and `v` must be constituents. Saturation may derive equality by canonical-context and concatenation cascades; those derived equalities remain valid algebraic facts but do not create tree evidence. Concat decompositions are likewise never interpreted as syntax.

Corpus substitution evidence is not converted to hard spans. The hard-constraint API is retained for explicit external axioms and tests; only crossing explicit hard spans make `hard_consistent=false`.

## Repeated substitutability as evidence

For exact surface context `c=(L,R)`, define `W(u,v)` as the set of distinct raw contexts in which both distinct yields occur. `support(u,v)=|W(u,v)|`. These contexts use original `StringId` identity, never final DSU representatives; repeated occurrences and duplicate sentences do not increase support.

Evidence remains occurrence-local. An occurrence `(u,c)` is a candidate only when `c ∈ W(u,v)` for some `v≠u`, and its score is:

```text
max over v with c in W(u,v) of support(u,v)
```

The builder records every maximizing alternative and its raw witness contexts as provenance. It never scores by e-class size, frequency, or a sum over alternatives.

## Maximum-evidence binary trees

The CKY-style DP maximizes the sum of evidence over proper nontrivial spans. Leaf and root evidence is always ignored. Every maximizing split is retained, and `optimal_tree_count` counts all trees in the packed optimal forest with checked `uint64_t` arithmetic. Crossing candidates are competing hypotheses: projectivity ensures that a tree selects at most one, without declaring an inconsistency.

`FORCED_OPTIMAL` spans occur in every maximum-scoring tree, not in every suboptimal or merely hard-consistent tree. They are computed by intersecting span sets over all optimal split alternatives. A concrete bracket tree is reconstructed only when `optimal_tree_count == 1`; otherwise the result remains explicitly ambiguous. Tests compare best score, optimal count, and forced spans with exhaustive Catalan enumeration.

## Structural identifiability

`simple.txt` has repeated evidence of support 2 for the left two-token phrases and support 1 for the competing right phrases, so all four sentences have a unique left-branching optimum. In contrast, the complete `C × D × B` design in `deep.txt` gives symmetric support to `[0,2)` and `[1,3)`. Both binary trees are optimal for every sentence, `optimal_tree_count == 2`, and there are no proper forced-optimal spans. This is correct non-identifiability under the stated symbolic objective, not a conflict to repair with a branching heuristic.

## Scope and known risk

SCF v1.1 intentionally does not implement CCG, PCFG probabilities, POS or constituency labels, neural representations, learned confidence, thresholds, branching bias, biclique/FCA or MDL objectives, Max-CSP, beam search, tries, GPUs, or distributed processing. It is single-threaded and uses contiguous integer-ID records, sort/group passes, and a DSU.

`strict_global` is deliberately strong. Lexical ambiguity, polysemy, syntactic ambiguity, or accidental substitution may initiate a congruence cascade and catastrophic e-class collapse. V1.1 does not repair equivalence with frequency, context-sensitive classes, or learned scores because its purpose is to test the symbolic axioms on controlled corpora.

## Why no dynamically updated Trie

Sentence length is at most 10 and all spans must already be enumerated. Token-vector interning plus immutable concat records keeps the observed-universe invariant explicit and inspectable. A dynamic Trie would complicate the correctness argument without changing v1.1 semantics. Prefix/reverse tries, suffix indexes, radix/external sorting, mmap storage, worklists, and parallel canonicalization are future scaling work.

## Layout

Public interfaces live under `include/scf`, implementations under `src`, controlled data under `data/synthetic`, the generator under `tools`, and the assert-based regression suite under `tests`. `IMPLEMENTATION_NOTES.md` documents data structures, complexity, correctness caveats, and specification inconsistencies found during implementation.
