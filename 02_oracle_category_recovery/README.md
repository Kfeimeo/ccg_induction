# Research line 02 — Oracle category recovery (v2.0)

**Direction.** Change the question from *tree induction* to *category
recovery*: given only a membership oracle `Accept(s)` for a small synthetic
CCG-like grammar, do bounded contextual signatures
`Sig_k(u) = {(L, R, Accept(LuR)) : |L|+|R| <= k}` recover the true
categories, lexicon and composition relation? No thresholds, no
heuristics, gold labels never participate.

**Status.** Complete. Recovery is exact once the signature bound `k`
reaches the deepest category embedding; observationally equivalent gold
categories are merged and flagged rather than counted as errors. This
line established the *bounded contextual signature* as the object of
study; lines 03–05 take it to real corpora, where there is no oracle and
`Accept` must come from the data.

| what | where |
|---|---|
| module | `include/scf/oracle_v2.hpp`, `src/oracle_v2.cpp` (namespace `scf::v2`, library `scf_oracle_v2`) |
| tool | `tools/scf_oracle_v2.cpp` (binary `scf_oracle_v2`) |
| tests | `tests/test_oracle_v2.cpp` (`scf_oracle_v2_tests`) |
| results | `results/v2_oracle/` |
| report | `reports/SCF_V2_ORACLE_REPORT.md` |

## v2.0: oracle category recovery

Full results in `reports/SCF_V2_ORACLE_REPORT.md`. v2.0 is an **independent
experimental module** (`scf::v2`; new library `scf_oracle_v2`, tool
`scf_oracle_v2`, test binary `scf_oracle_v2_tests`) that leaves the v1.x core
untouched and asks a different question than the tree-induction mainline:

> Do externally indistinguishable string equivalence classes recover the true
> categories `E`, lexicon `Lex`, and composition relation `Comp` of a small
> synthetic CCG-like grammar, given only the membership oracle `Accept(s)`?

A gold grammar `G = (E, Lex, Comp, F)` defines `Accept` exactly (a bottom-up
category table over every string of length `<= L + K`, cross-validated
against an independent CKY recognizer). The learner partitions all strings of
length `<= L` by exact equality of the bounded contextual signature
`Sig_k(u) = {(L, R, Accept(LuR)) : |L|+|R| <= k}` — no thresholds, no
heuristics, gold labels never participate. Four families
(`simple_np_vp`, `transitive`, `recursive_modifier`,
`observationally_equivalent_categories`) are swept over `L = 2..6`,
`k = 0..4`, with category-recovery metrics (ARI, NMI, pairwise
precision/recall, merge/split pairs, partition hashes), `Comp` recovery with
a congruence audit, an observational-equivalence flag for gold categories the
language never distinguishes, and a positive-only coverage ablation
(5%..100% of accepted strings, where absence is never negative evidence and
100% provably equals the oracle).

Headline results: on constituents, recovery is exact (`ARI = 1.0`) for every
family once `k` reaches the deepest category embedding (2 for
`simple_np_vp`, 3 for `recursive_modifier`, 4 for `transitive`); the designed
`{Nm} ~ {Nf}` pair is merged and flagged `observationally_equivalent_gold_categories`
rather than counted as error; the recovered `Comp` is functional and a true
concatenation congruence at stable `k`, and on recursive modifiers it
correctly extends the gold rule list with derived facts (`A·A ≡ A`,
`D·A ≡ D`). Run everything with:

```bash
build/02_oracle_category_recovery/scf_oracle_v2 --output-dir 02_oracle_category_recovery/results/v2_oracle
```

which writes `category_recovery.csv`, `composition_recovery.csv`,
`positive_only_recovery.csv`, and `oracle_summary.txt` (committed under
`02_oracle_category_recovery/results/v2_oracle/`).

