# SCF v2.3.1 — Clean-Corpus Replication on Wikipedia Body Text

Same algorithm, cleaner corpus. The v2.3 conservative learner
(`scf::v23::observe_sentences` + `scf::v23::ConservativeMerger`, Steps 3–5
unchanged, no file in the v2.3 module edited) is re-run on a
structure-preserving body-text corpus, and every witness, candidate,
accepted merge and rejected merge is classified by the boundary type of its
exact frame. The purpose is to decide how much of the v2.3 bad-merge
behaviour is corpus/preprocessing and how much is the witness semantics
`∃c: c[u], c[v] ∈ D ⇒ merge candidate`.

## 0. Status — what was run, what was substituted, what is bound

**Corpus substitution (forced, documented).** peS2o/S2ORC was the first
choice. This environment's egress policy denies `huggingface.co` (CONNECT
403) and every mirror tried (`hf-mirror.com`, `olmo-data.org`,
`dolma-artifacts.org`); the reachable AI2 bucket
`ai2-s2-research-public.s3.amazonaws.com` holds only the 2017–2019 Open
Research Corpus metadata releases (titles/abstracts), no full-text body
paragraphs, and no `s2orc`/`pes2o` prefix. `dumps.wikimedia.org` is denied
as well. The fallback named in the brief is therefore used: **clean English
Wikipedia body text**, from the gensim-data packaging of the 2017-10-01 dump
(GitHub release assets are reachable). No second corpus is mixed in.

**Baseline substitution (forced, documented).** The brief asks to read the
existing "FineWeb v2.3 100k" result. The repository contains no such
result: `results_v2_3_conservative/` held only `oracle_sanity.txt`, and the
v2.3 report states that the real ladder was wired but not executed. In
addition, the corpus that v2.1/v2.2 call "the FineWeb substitute" is
itself this same Wikipedia-2017 release (`tools/fetch_wiki_corpus.py`,
sha256 `fbecce93…4368`). The baseline used here is therefore **the unchanged
v2.3 CLI (`scf_conservative_merging`) run on the v2.3 corpus and
preprocessing (`wiki2017_head.txt`, v2.2 condition D) at 1e5 tokens**,
generated once in this session into `results_v2_3_conservative/` and then
only read. It is called "v2.3 baseline" below, never "FineWeb". The
patterns the brief lists for FineWeb v2.3 (`<num>` merged with unrelated
short utterances, `the` merged with heterogeneous phrases, boilerplate-like
complete spans, fragment-like terminal spans) all appear in this baseline
(§5), so the comparison is meaningful for the question asked.

**Scales.** 1e5 was run to completion for both corpora. 1e6 and 1e7 are
**resource-bound for the unchanged learner on both corpora** and are
reported as a measured bound (§6), not as numbers: the `(ε,ε)` hub makes the
candidate table C(d,2) in the number d of distinct complete short sentences,
and the v2.3 transaction cost grows super-linearly in that table. Following
v2.3 §7, no frequency, length or hub filter was introduced to make the
larger scales fit. The 1e5 prefix is nested inside the 1e6 and 1e7 prefixes
by construction (whole-sentence prefixes of one document-ordered stream).

## 1. Data and preprocessing

Both corpus variants are derived from the **same 419,430,400 raw bytes**
(fixed HTTP range `0-419430399` of `wiki-english-20171001.gz_00`, sha256
`8ed55189e51a6d083a43db019775f8729483e67b4af590eb0fd789ec678b18c2`),
decompressed as a truncated gzip stream: 100,332 articles in dump order.

| | v2.3 baseline corpus (`--mode legacy`) | clean body corpus (`--mode body`) |
|---|---|---|
| unit per line | one article: `title . section_1 section_2 …`, newlines flattened | one body paragraph; documents separated by an empty line |
| title | included as the first "sentence" | excluded (data-source field) |
| section headings | top-level headings are separate fields and were already excluded; `=== sub-headings ===` remained inside the text | top-level excluded (field); 444,817 `=== … ===` sub-heading lines excluded (fixed source-markup marker) |
| appendix sections | included | 230,909 sections dropped by exact title match on the source field (`See also`, `References`, `External links`, `Further reading`, `Notes`, `Bibliography`, `Sources`, `Footnotes`, `Citations`, `Notes and references`, `References and notes`, `Works cited`) |
| captions / table residue | included | **kept** — gensim's markup filter emits them as bare lines with no reliable field marker; excluding them would be a text heuristic (forbidden) |
| paragraphs | flattened into the article line | 7,332,413 paragraphs, boundaries preserved |
| sha256 of text | `fbecce93a0e0fd28f2e8964e717b37da5e3728c20f142b5377cb2fbc776a4368` (identical to the v2.1 report) | `aa222873f733aca8e739da72008169c552a64f42613e3e4b6897d284771ff81e` |

Shared, unchanged from v2.3: tokenizer (`v21::tokenize_line`: ASCII
lowercase, digit runs → `<num>`, single visible ASCII punctuation as one
token, 2+ apostrophes as separators, bytes ≥ 0x80 kept), sentence
segmentation at every `. ? !` token with the terminator consumed, substring
lengths 1..3, exact full-sentence frames, `<BOS>/<EOS>` as the empty trie
root only (never an object).

Changed in the clean condition: (i) documents/paragraphs preserved and body
text only, as above; (ii) **punctuation kept** (all non-terminal punctuation
tokens are ordinary tokens; in the baseline they are removed). A
paragraph end always closes a sentence, so no substring, frame or witness
crosses a sentence or paragraph boundary (checked by
`tests/test_clean_corpus.cpp`). Nothing is filtered by frequency, POS,
sentence length, grammaticality, embedding, similarity, hub size or
blacklist.

Interpretation note on "keep punctuation": the sentence terminator is the
segmentation signal and is absorbed into `<EOS>` (v2.2 condition A
representation); keeping it as a token would make `R=[.]` the right frame of
almost every sentence-final span and would change the meaning of the
`right_boundary` class relative to the baseline. A `--punctuation drop`
switch reproduces the condition-D removal on the structured corpus; it is
used only for the attribution run in §7.

## 2. Algorithm identity check

`scf_clean_corpus --preprocess v23_condition_d` rebuilds the v2.3 sentence
list (same read limit, same segmentation, punctuation dropped, prefixes in
condition-A tokens) and runs the same `observe_sentences`/`ConservativeMerger`
objects. At 1e5 it reproduces the unchanged v2.3 CLI row exactly on all 13
shared columns (`tools/compare_v231.py` asserts this before writing any
table):

```text
initial_objects 135661  local_witnesses 27149  merge_candidates 27107
accepted 442  rejected 12703  redundant 13962  induced_unions 55
classes 135164  largest 162  ratio 0.001194  median 1  p95 1
```

Only this identity makes the frame-type breakdown of the baseline (§4)
attributable to the v2.3 learner rather than to a re-implementation.

## 3. Same-scale results at 1e5 nominal tokens

Full tables: `results_v2_3_1_clean_corpus/comparison_with_v23_baseline.md`
(generated by `tools/compare_v231.py` from the CSVs; the baseline column is
read from the unchanged v2.3 output, `results_v2_3_conservative/conservative_scaling.csv`).

| metric | v2.3 baseline (flattened article, punct dropped) | clean Wikipedia body (paragraphs kept, punct kept) |
|---|---|---|
| actual tokens observed | 87,975 | 100,007 |
| sentences / documents | 4,450 / 15 | 7,136 / 15 (3,715 paragraphs) |
| initial objects | 135,661 | 140,764 |
| local witnesses | 27,149 | **707,503** |
| merge candidates | 27,107 | **707,196** |
| accepted merges | 442 | 2,018 |
| rejected merges | 12,703 | 252,154 |
| redundant candidates | 13,962 | 453,024 |
| induced unions | 55 | 153 |
| resulting classes | 135,164 | 138,593 |
| largest class | 162 | **984** |
| **accepted merge rate** | 0.0163 | 0.0029 |
| **rejected merge rate** | 0.4686 | 0.3566 |
| **largest class ratio** | 0.001194 | 0.006990 |
| median / p95 class size | 1 / 1 | 1 / 1 |
| #{u : (ε,ε) observed} | 216 | 1,180 |
| **empty-frame candidate share** | 0.8560 | 0.9833 |
| **empty-frame accepted merge share** | 0.4208 | 0.5391 |
| largest class: complete-sentence / single-token / `<num>` members | 162 / 8 / 35 | 947 / 166 / 24 |
| runtime (s), peak RSS (MB) | 4.97, 117 | 9,002.6, 150 |

Both corpora keep more than 95 % of all objects in singleton classes
(median = p95 = 1): the conservative learner does not over-merge the
lexicon. What differs is concentrated in a single structure, the `(ε,ε)`
hub, and the clean corpus makes that structure **larger**, not smaller.

## 4. Frame-type diagnostics

Counts are for candidates whose witnesses are **all** of the given type
(`only`); `mixed` collects pairs witnessed by two or more frame types, so the
rows partition the candidate table. "in-largest" = accepted merges of that
type whose two objects end in the largest class.

| frame type | v2.3 baseline: witnesses / candidates / accepted / rejected / in-largest | clean body: witnesses / candidates / accepted / rejected / in-largest |
|---|---|---|
| empty_frame `L=[] R=[]` | 23,220 / 23,204 / 186 / 10,108 / 161 | 695,610 / 695,386 / 1,088 / 246,689 / 930 |
| left_boundary `L=[] R≠[]` | 1,748 / 1,732 / 140 / 1,164 / 0 | 1,557 / 1,480 / 295 / 816 / 1 |
| right_boundary `L≠[] R=[]` | 1,348 / 1,327 / 66 / 862 / 0 | 9,774 / 9,561 / 513 / 4,296 / 24 |
| internal_frame `L≠[] R≠[]` | 833 / 825 / 50 / 550 / 0 | 562 / 520 / 112 / 171 / 1 |
| mixed | – / 19 / 0 / 19 / 0 | – / 249 / 10 / 182 / 0 |

Acceptance rate within type (accepted / candidates of that type):

| frame type | v2.3 baseline | clean body |
|---|---|---|
| empty_frame | 0.0080 | 0.0016 |
| left_boundary | 0.0808 | 0.1993 |
| right_boundary | 0.0497 | 0.0537 |
| internal_frame | 0.0606 | 0.2154 |

Accepted merges triggered by `(ε,ε)` alone: **186 of 442** in the baseline
(161 of them land in the largest class, which has 162 members), **1,088 of
2,018** in the clean corpus (930 land in the largest class of 984). The
listed rejected empty-frame candidates all have the same shape: a complete-sentence span
that also occurs inside longer sentences collides in composition with one
that does not (`Comp(a, is, a is) vs Comp(<num>, is, <num> is)`;
`Comp(,, anarchism, , anarchism) vs Comp(", anarchism, " anarchism)`).
The full lists are in `successful_merges_by_frame_type.txt` and
`rejected_merges_by_frame_type.txt` (20 per type per corpus); the
smaller-scale ladders (2e4–8e4) in `supplementary/` show the same
proportions.

## 5. Manual class audit (recorded, not corrected)

### 5.1 v2.3 baseline at 1e5 (`results_v2_3_conservative/class_examples.txt`, `results_v2_3_1_clean_corpus/baseline_v23_frames/`)

Largest class, size 162, **162/162 members are complete-sentence spans**,
8 single tokens, 35 contain `<num>`:

```text
anarchism | leader ruler cf | published in <num> | <num> to <num> | <num> – <num> | g | also m
| <num> march <num> | graeber david | an anarchist faq | scott james c | woodcock george ed
| <num> in <num> | an estimated <num> | unbalanced excitatory–inhibitory networks
| <num> deletion syndrome | by <num> months | more than <num> | preempted diagnoses | president …
```

161 of the 162 members entered through `(ε,ε)`-only accepted merges; the
class is the empty-frame hub, and it is exactly the mixture the brief
describes: bibliographic boilerplate (`published in <num>`, `woodcock george
ed`, `scott james c`), date/number fragments (`<num> march <num>`,
`<num> – <num>`), abbreviation fragments from the shared segmenter (`g`,
`also m`, `cf`), and list residue (`preempted diagnoses`). The `<num>`
pattern is there twice over: `<num>` itself heads a class of 15 complete
spans (`<num> | what is property | <num> january <num> | spanish and <num> |
… | surveys of u`), and the internal-frame class `to | jewish | open ocean |
worn asphalt | conifer forest summer | bare soil | … | lb` (size 24) is
built almost entirely from the one-token frame `L=[<num>] R=[<num>]` of an
albedo/statistics table. `the` is **not** in a large class in this baseline:
its candidates (`the <=> the <num>` in `according to _ u`, `the <=> surveys
of` in `in _ u`) are rejected by composition conflicts. Boundary classes:
`in | london pluto press | <num> per <num> | … | pangle lorraine smith`
(size 19, frame `[] _ <num>`), `e | in <num> | l | per <num> <num> | … | h`
(size 45, mixed `[] _ <num>` / `<num> _ []`), i.e. `in`/`e` merged with
heterogeneous phrases through single-anchor boundary frames.

### 5.2 Clean Wikipedia body at 1e5 (`results_v2_3_1_clean_corpus/largest_classes.txt`)

Largest class, size 984, **947/984 complete-sentence spans**, 166 single
tokens, 24 contain `<num>`:

```text
a | in <num> , | holiday | n | <num> to <num> | <num> – <num> | <num> , <num> | also m
| an estimated <num> | <num> deletion syndrome | more than <num> | they include :
| * genetic disorders | * intellectual disability | * preempted diagnoses | president pictures
| <num> - <num> | { | + sample albedos | typicalalbedo | fresh asphalt | open ocean | worn asphalt
| deciduous trees | bare soil | green grass | desert sand | new concrete | ocean ice | , )
| cretan | phoenician aleph | / cyrillic a | greek uncial | roman a | boeotioan <num> bc | … (944 more)
```

Of the accepted merges whose two objects end inside it, 930 are
`(ε,ε)`-only, 24 are right-boundary frames such as `L=[william] R=[]` /
`L=[hal pereira] R=[]` (Academy-Award art-director tables), one is
left-boundary and one internal; the remaining members arrive through
induced unions. Compared
with the baseline hub the *content* changed and the *kind* did not:
bibliographic boilerplate is gone (References sections were dropped by
field), and its place is taken by bulleted list items (`* genetic
disorders`), image captions (`president pictures`), table rows (`open
ocean … ocean ice`, `+ sample albedos`, `typicalalbedo`), infobox/letter-form
residue (`roman a`, `greek uncial`, `{`), and the same abbreviation fragments
(`n`, `also m`, `in <num> b`). Bare punctuation tokens that occur as
one-token paragraphs (`{`, `, )`) are new hub members created by keeping
punctuation.

The remaining top classes are cleaner than in the baseline and are **not**
hub classes: size 82 = film titles with `the` stripped (`awakening | patriot
| dove | love parade | … | ten commandments | apartment | hustler`, 0
complete-sentence members), size 32 = film titles of the form `X of Y`
(`wizard of oz | prisoner of zenda | sound of music | godfather part ii`),
sizes 45/39/26/20 = art directors' names (`ralph hammeras | fredric hope |
carroll clark | …`, `william | charles | franklin | lewis | …`). These come
from the Academy Award result tables that the body corpus preserves as
paragraphs with a fixed frame (`the _ hans dreier`, `_ cedric gibbons`), so
they are internally consistent categories produced by boundary/internal
frames — but they are table columns, not sentences. Two further classes
are heterogeneous in the baseline manner: size 47 (`- | g | per <num> , |
, <num> , | h | w m− | to + | egyptian | % in | % white ( | % asian , | …`,
the `<num> _ <num>` statistics frame) and size 29 (`in <num> | <num> per
<num> | <num> % asian | asian , <num> | … | hindu | it seats <num> | said :`).

Pattern check against the v2.3 list:

| pattern named for FineWeb v2.3 | v2.3 baseline (1e5) | clean body (1e5) |
|---|---|---|
| `<num>` merged with unrelated short utterances | yes — `<num>` class of 15 complete spans (`what is property`, `surveys of u`, `london duckworth <num>`) | `<num>`-containing complete spans are in the hub (`<num> to <num>`, `<num> – <num>`, `in <num> ,`, `an estimated <num>`, `more than <num>`); the token `<num>` is itself a complete-sentence object whose hub candidate with `a` is rejected by composition (`Comp(a, is, a is)` vs `Comp(<num>, is, <num> is)`); its final class is listed in `probe_object_classes.txt` (PROBE_NUM_CLEAN) |
| `the` merged with heterogeneous phrases | no — `the` stays a singleton (candidates `the <=> the <num>`, `the <=> surveys of` rejected) | no accepted merge involving `the` appears in the listed examples; 8 of its candidates are rejected (`several <=> the` in `[] _ u`, `the <=> the <num>`); its final class: PROBE_THE_CLEAN |
| boilerplate-like complete spans | yes — references (`published in <num>`, `woodcock george ed`) | yes, different boilerplate — list items, captions, table rows |
| fragment-like terminal spans | yes — `g`, `also m`, `cf`, `surveys of u`, `e`, `l`, `h` | yes — `n`, `also m`, `in <num> b`, `a`, `e`, `l`, `h`, `s`, `mr`, `jack d`, `lyle r` |
| `in`/`to`/`e` merged through one-token frames | yes — `in` (19), `to` (24), `e` (45) | yes — `-` (47, `<num> _ <num>`), `in` (29), `e` (45), `s` (26), `,`/`a` in the hub |


## 6. The `(ε,ε)` hub and the measured resource bound at 1e6 / 1e7

Every distinct complete sentence of length ≤ 3 is an object with the frame
`(ε,ε)`, and every pair of them is a direct witness, hence a merge candidate.
`scf_clean_corpus --hub-stats-only` measures d = #{u : (ε,ε) observed} per
nested prefix without running the learner (`results_v2_3_1_clean_corpus/hub_stats/`):

| nominal tokens | v2.3 baseline: d / C(d,2) | clean body, punct kept: d / C(d,2) | clean body, punct dropped: d / C(d,2) |
|---|---|---|---|
| 2e4 | 28 / 378 | 21 / 210 | — |
| 4e4 | 73 / 2,628 | 214 / 22,791 | — |
| 1e5 | 216 / 23,220 | 1,180 / 695,610 | 1,314 / 862,641 |
| 3e5 | 603 / 181,503 | 2,347 / 2,753,031 | — |
| 1e6 | 1,816 / 1,648,020 | 4,857 / 11,792,796 | 6,681 / 22,314,540 |
| 3e6 | 5,043 / 12,713,403 | 9,839 / 48,398,041 | — |
| 1e7 | 15,446 / 119,281,735 | 33,823 / 571,980,753 | 52,022 / 1,353,118,231 |

The structured corpus has a **larger** hub than the flattened one at every
scale ≥ 4e4, by 3–5×. The reason is visible in the audit files: with
paragraph boundaries preserved, every bulleted list item (`* genetic
disorders`), image caption (`president pictures`), table cell row (`open
ocean`, `worn asphalt`, `fresh snow` from an albedo table) and infobox
residue line becomes its own paragraph and therefore its own complete
sentence, whereas the flattened article line runs these fragments together
until the next `.`. The other hub source, abbreviation splitting by the
shared segmenter (`u`, `s`, `e`, `g`, `william b`, `also m`, `in <num> b`),
is common to both corpora.

Measured runtime of the unchanged learner on the clean corpus (separate
deterministic runs, `results_v2_3_1_clean_corpus/supplementary/small_scale_ladder.csv`):

| nominal tokens | hub d | candidates | largest class | runtime (s) | s per candidate |
|---|---|---|---|---|---|
| 2e4 | 21 | 230 | 18 | 0.17 | 7e-4 |
| 4e4 | 214 | 23,657 | 126 | 11.7 | 4.9e-4 |
| 6e4 | 238 | 29,173 | 138 | 32.7 | 1.1e-3 |
| 8e4 | 338 | 58,333 | 217 | 80.9 | 1.4e-3 |
| 1e5 | 1,180 | 707,196 | 984 | 9,002.6 | 1.3e-2 |

The cost per candidate itself grows with the hub class: a candidate whose
two objects sit in the hub class triggers `compare_behavior` over all
members of that class and, for each conflicting output pair,
`direct_witness_between` scans the witness neighbours of every member
(each hub member has ≈ d neighbours), so a rejected hub candidate costs
Θ(d²) and the table has Θ(d²) of them. Extrapolating the measured points
(time ≈ candidates^2 within the 4e4–1e5 range), 1e6 on the clean corpus
(1.18e7 candidates) needs on the order of 10^6–10^7 s and 1e7 (5.7e8
candidates, ≥ 6.9 GB of witness records alone) is out of reach; the baseline
corpus at 1e6 (1.65e6 candidates) is in the 10^4–10^5 s range. The unchanged
v2.3 CLI was started on the baseline corpus at 1e6 during this session;
its outcome is recorded in `results_v2_3_conservative/attempt_1e6/`
(ATTEMPT_1E6_OUTCOME). Per v2.3 §7 this bound is the result: reintroducing a
count, length or hub threshold would define a different experiment.

## 7. Corpus/preprocessing effect vs algorithm/witness-semantics effect

Two things changed between the baseline and the clean condition (structure
+ body-only, and punctuation). To attribute, all three preprocessings were
run at 4e4 tokens on the corresponding corpora (`supplementary/`):

| 4e4 tokens | v2.3 baseline (flattened, punct dropped) | clean body, punct **dropped** | clean body, punct **kept** |
|---|---|---|---|
| objects | 57,396 | 64,024 | 60,586 |
| witnesses | 4,967 | 34,870 | 23,706 |
| candidates | 4,953 | 34,786 | 23,657 |
| accepted / rejected | 147 / 2,800 | 386 / 18,053 | 322 / 15,516 |
| accepted rate | 0.0297 | 0.0111 | 0.0136 |
| rejected rate | 0.565 | 0.519 | 0.656 |
| largest class (ratio) | 53 (0.00092) | 204 (0.00319) | 126 (0.00208) |
| largest class = complete sentences | 53 / 53 | 180 / 204 | 124 / 126 |
| (ε,ε) objects d | 73 | 259 | 214 |
| empty-frame candidate share | 0.530 | 0.958 | 0.962 |
| empty-frame accepted share | 0.395 | 0.573 | 0.512 |
| internal-frame accepted / candidates | 34 / 687 | 26 / 248 | 60 / 293 |

Reading: moving from the flattened article line to preserved paragraphs
(column 1 → column 2, punctuation handling held fixed) multiplies the
`(ε,ε)` hub by 3.5 and makes it 96 % of all candidates; keeping punctuation
(column 2 → column 3) shrinks the hub slightly (fewer collisions between
short fragments once commas/parentheses distinguish them) and raises the
rejection rate, because punctuation tokens such as `,` and `)` become
hub members that are then blocked by composition conflicts. Neither change
touches the mechanism: in all three conditions the largest class is the
`(ε,ε)` hub and consists almost entirely of complete-sentence spans.

**Corpus/preprocessing effects** (they change with the corpus): the
composition of the hub (references and bibliographic fragments in the
baseline; list items, captions and table cells in the body corpus), the
size of the hub at a given token count, the presence of `<num>`-heavy table
rows (both), the prevalence of `u`/`s`/`e`/`g` fragments (shared segmenter,
both).

**Algorithm/witness-semantics effects** (they do not change with the
corpus): (a) one shared exact frame is sufficient to make a pair a
candidate, so the `(ε,ε)` frame connects every complete short sentence to
every other; (b) the transactional check only sees observed compositions,
and complete-sentence spans compose with almost nothing, so hub candidates
are accepted whenever both members are composition-inert (the 161/162 and
124/126 hub classes) and rejected only when one member also occurs inside a
longer sentence; (c) the one-token frames `<num> _ <num>`, `[] _ <num>`,
`<num> _ []` of 3-token sentences behave like the empty frame with one
anchor; (d) the cost of the learner is Θ(d²) candidates × Θ(d²) per
rejection.

## 8. Answers to the research questions

1. **Do the heterogeneous largest classes of the v2.3 baseline shrink on the
   cleaner corpus?** No. The largest class grows from 162 to 984 members
   (ratio 0.0012 → 0.0070), and it is the same object: the `(ε,ε)` hub of
   complete short "sentences" (947/984 vs 162/162). Removing titles,
   headings and reference sections removed the bibliographic boilerplate
   from the hub; preserving paragraph boundaries put list items, captions
   and table rows into it instead, in larger numbers. The clean corpus does
   produce a few internally coherent large classes (film titles, personal
   names from award tables), but they are table columns aligned by a fixed
   frame, not evidence of cleaner sentence-level categories.

2. **Does `(ε,ε)` still produce large numbers of wrong-looking merges?** Yes,
   more. 98.3 % of all witnesses and candidates are `(ε,ε)` (85.5 % in the
   baseline); 1,088 of 2,018 accepted merges (53.9 %, vs 42.1 %) are
   triggered by `(ε,ε)` alone, and 930 of them build the largest class. The
   20 listed empty-frame merges (`a <=> in <num> ,`, `a <=> * genetic
   disorders`, `a <=> president pictures`, `a <=> {`) are all wrong-looking.
   Within the empty-frame type the acceptance rate is low (0.16 %) only
   because the transaction mechanism rejects hub members that also occur
   inside longer sentences; members that occur nowhere else are accepted
   without opposition.

3. **Are internal exact-frame witnesses cleaner than boundary witnesses?**
   Partly, and not for the reason hoped. Internal frames have the highest
   within-type acceptance rate on the clean corpus (0.215 vs 0.0016 for
   `(ε,ε)`), contribute at most 1 merge to the largest class in either
   corpus, and yield the coherent name/title classes. But the listed
   internal-frame merges are dominated by `L=[<num>] R=[<num>]` from
   statistics tables (`- <=> per <num> ,`, `- <=> % asian ,`, baseline `to
   <=> open ocean`), i.e. one-token anchors in 3-token sentences that behave
   like an empty frame with a numeral on each side. Cleanliness tracks the
   amount of context in the frame, not the internal/boundary label as such;
   `left_boundary` with a long right context (`first <=> shakespeare in` in
   `[] _ love`) is as clean as any internal frame.

4. **Does the cleaner corpus significantly improve conservative merging
   without algorithm changes?** No. Accepted merge rate falls (0.0163 →
   0.0029), rejected rate falls (0.469 → 0.357) because redundant hub pairs
   dominate, largest-class ratio rises 5.9×, empty-frame shares rise, and
   runtime rises from 5 s to 9,003 s at the same token scale. The
   qualitative composition of the largest class improves in one respect
   (no reference-list boilerplate) and worsens in another (list/caption/table
   residue); the sentence-level lexicon is unchanged (median = p95 = 1 in
   both).

5. **If bad merges persist, does that point at the witness semantics rather
   than FineWeb preprocessing?** Yes, with one qualification. The
   corpus-dependent part is the *membership* of the hub (references vs list
   items) and its *size*; the corpus-independent part is the mechanism: a
   single shared exact frame `c` with `c[u], c[v] ∈ D` makes every pair of
   complete short spans a candidate, and the compositional check cannot
   reject pairs that compose with nothing. The 4e4 three-way run (§7) shows
   the hub dominates under all three preprocessings (53 %, 96 %, 96 % of
   candidates). The qualification: the `(ε,ε)` hub is fed by a shared
   preprocessing component — segmentation at every `.` and the treatment of
   list items/captions/table cells as sentences — so part of the volume is
   preprocessing after all, just not FineWeb-specific preprocessing. No
   structure-preserving, filter-free preprocessing tried here removes it.

6. **Is the clean-corpus evidence sufficient before changing merge
   semantics?** Yes for the diagnosis, with a stated boundary. Two corpora
   with different noise profiles, three preprocessings, and every scale from
   2e4 to 1e5 agree that the largest class is the `(ε,ε)` hub and that the
   hub is the single object that makes the learner infeasible above 1e5
   (C(d,2) candidates, d = 1,180 → 33,823 between 1e5 and 1e7). What the
   evidence does not settle is whether the fix belongs to witness semantics
   (how much frame is required for a witness) or to the observation
   representation (whether a complete short paragraph is a sentence at all):
   both would remove the hub, and this experiment cannot separate them
   because it held both fixed by design. That is a decision for v2.4, not
   for this control run.

## 9. Verification

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
  scf_tests                    Passed   (v1.x core, 56 checks)
  scf_oracle_v2_tests          Passed
  scf_real_v21_tests           Passed
  scf_conservative_v23_tests   Passed   (v2.3 unchanged)
  scf_clean_corpus_v231_tests  Passed   (new: reader structure, no cross-boundary
                                          objects + frame types, v2.3 reader identity,
                                          deterministic ladder outputs)
```

`-Wall -Wextra -Wpedantic` clean. Determinism: the 1e5 clean run was
executed twice (once before and once after adding the probe-object dump);
all CSV columns except runtime/RSS are byte-identical (DETERMINISM_NOTE).

## 10. Reproduction

```bash
curl -L -r 0-419430399 -o data/real/wiki-english-20171001.gz_00.head400m \
  https://github.com/piskvorky/gensim-data/releases/download/wiki-english-20171001/wiki-english-20171001.gz_00
python3 tools/extract_wiki_body.py --mode legacy data/real/wiki-english-20171001.gz_00.head400m data/real/wiki2017_head.txt
python3 tools/extract_wiki_body.py --mode body   data/real/wiki-english-20171001.gz_00.head400m data/real/wiki2017_body.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
build/scf_conservative_merging --input data/real/wiki2017_head.txt --scales 100000 --output-dir results_v2_3_conservative
build/scf_clean_corpus --input data/real/wiki2017_head.txt --preprocess v23_condition_d \
  --label v23_baseline_condition_d --scales 100000 --output-dir results_v2_3_1_clean_corpus/baseline_v23_frames
build/scf_clean_corpus --input data/real/wiki2017_body.txt --preprocess clean_body --punctuation keep \
  --label clean_wiki_body --scales 100000 --output-dir results_v2_3_1_clean_corpus      # ~2.5 h
build/scf_clean_corpus --hub-stats-only --input data/real/wiki2017_body.txt --preprocess clean_body \
  --scales 100000,1000000,10000000 --output-dir results_v2_3_1_clean_corpus/hub_stats
python3 tools/compare_v231.py --baseline results_v2_3_conservative/conservative_scaling.csv \
  --baseline-frames results_v2_3_1_clean_corpus/baseline_v23_frames \
  --condition results_v2_3_1_clean_corpus --out results_v2_3_1_clean_corpus/comparison_with_v23_baseline.md
```

Output files required by the brief: `SCF_V2_3_1_CLEAN_CORPUS_REPORT.md`,
`results_v2_3_1_clean_corpus/{clean_corpus_scaling.csv, frame_type_metrics.csv,
largest_classes.txt, successful_merges_by_frame_type.txt,
rejected_merges_by_frame_type.txt}`. Nothing in v2.4 direction was designed
and no category-equivalence theory was changed.
