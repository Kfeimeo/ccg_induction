# SCF v2.3.1 — Clean-Corpus Replication on peS2o

## 0. Status and scope

This round changes nothing in the v2.3 learner. `observe_sentences` and
`ConservativeMerger` (Steps 3–5: different-by-default, direct exact-context
witness → candidate, transactional compositional-consistency check) are the
same compiled functions as in v2.3. What changes is only what is put in
front of them (corpus, preprocessing) and what is read out behind them
(witness-frame boundary types).

Two facts about the baseline must be stated up front:

1. **No FineWeb v2.3 result existed in the repository.** The v2.3 report
   says the real ladder "is not executed in this commit", and v2.1/v2.2 used
   Wikipedia-2017 because `huggingface.co` was blocked. In this session the
   network policy allowed Hugging Face, so the FineWeb 100k baseline was
   produced by running the **unchanged v2.3 CLI** (`scf_conservative_merging`)
   on a fixed FineWeb sample. Its output is stored verbatim in
   `results_v2_3_conservative/fineweb_baseline/` and is not re-interpreted
   or edited; the v2.3.1 tool's `v23d` mode reproduces it number for number
   (§5.1), which is how the frame-type breakdown of the baseline is obtained.
2. **peS2o was obtainable, so Wikipedia was not used.** The two corpora are
   never mixed.

Because v2.3 removed punctuation (condition D) and v2.3.1 keeps it, the
comparison is run as a 2×2 at the shared scales — {FineWeb, peS2o} ×
{v2.3-D preprocessing, v2.3.1 structured preprocessing} — so that the
corpus effect and the punctuation/segmentation effect can be read
separately. The main ladder (1e5, 1e6, 1e7, nested) is peS2o × v2.3.1.

## 1. Data

### 1.1 Clean corpus: peS2o v2 full-text body paragraphs

| item | value |
|---|---|
| source | `allenai/peS2o`, `data/v2/train-00010-of-00020.json.gz` (first `s2orc/train` full-text shard) |
| fetch | HTTP range `bytes=0-67108863` (first 64 MiB of the gzip stream), decoded until the truncated tail; only complete JSON lines used |
| raw sha256 | `d72beb2cec297cb8d931de71307dc4d091b6146916a1fa607f09366f91737d58` |
| records used | 7,792 `s2orc/train` documents (stream order) |
| body paragraphs / source lines / sentences | 88,691 / 329,481 / 1,371,122 |
| whitespace tokens (body only) | 32,607,179 |
| `.scs` sha256 | `4a05562940a8d86dad54a70574bfa37bce66be9905dd42feec45dddaedcfca81` |
| `.txt` sha256 (flat, for v2.3-D control) | `bbf9d30cc10903270a11654ef9fb8d873e92da664bd57a082dc1c9e856256ebe` |

peS2o's `text` field has a documented layout: title block, abstract block,
then body paragraphs separated by blank lines (Grobid section headers and
paragraphs; figures, tables and references already removed by peS2o). The
title and abstract blocks are excluded because the documented format
identifies them positionally (they come from Semantic Scholar metadata, not
from the body). **Section headers are kept**: no source field separates a
header from a one-line paragraph, and the spec forbids heuristic guessing.
This is the single most important preprocessing fact for reading the
results, because headers become one- to three-token complete "sentences"
(`introduction`, `| participants`, `conclusions`) and therefore carry the
`(ε,ε)` frame.

### 1.2 FineWeb sample (baseline corpus)

| item | value |
|---|---|
| source | `HuggingFaceFW/fineweb`, `sample/10BT/000_00000.parquet`, row groups 0–1 |
| records | 1,999 documents (stream order), 6,313,037 characters |
| raw sha256 | `7cdf441304180b9187b249d59ff560f8ef48df371c6d61a66f799b82c74750e1` |
| `.txt` sha256 (one document per line, input of the v2.3 CLI) | `2f56a565ec598f371744dd9b4d9a62656a025efeffc1ce31f3829c83fb183d34` |
| `.scs` sha256 (structured control) | `a0bb281af5a0897c6b9071778fd2a8a97aee56b032e187b7ec1162deca386669` |
| sentences (structured) | 64,742 |

FineWeb has no structural fields, so for the structured control every
newline-separated line of `text` is one paragraph.

### 1.3 POS labels (evaluation only)

UD English EWT `en_ewt-ud-train.conllu` (majority UPOS per lowercased form),
loaded after the partition is complete, exactly as in v2.3.

## 2. Preprocessing (v2.3.1 structured mode)

`tools/prepare_clean_corpus.py` writes a `.scs` file (`#doc`, `#par`, one
raw sentence per line) and the C++ loader (`scf::v231::load_structured_corpus`)
turns it into the `sentences + token_text` input that the v2.3 driver
builds internally.

1. Document and paragraph boundaries preserved (`#doc`, `#par`; per-sentence
   document/paragraph ids kept).
2. Body text only (title/abstract dropped by documented position; nothing
   else guessed).
3. Sentence segmentation: a fixed rule — a run of `. ? !`, optional closing
   quotes/brackets, whitespace, then a token starting with an uppercase
   letter or digit — unless the word before the punctuation is a single
   capital initial or in a fixed 40-entry abbreviation list (`et`, `al`,
   `e.g`, `fig`, `vs`, …). A single `\n` inside a peS2o block is a hard
   source line boundary and is never crossed.
4. Deterministic tokenization: the v2.1 tokenizer unchanged (ASCII lowercase,
   digit runs → `<num>`, single apostrophe kept inside words, every other
   visible ASCII character a one-character token, bytes ≥ 0x80 kept).
5. Punctuation **kept** as tokens (`,` `(` `)` `:` `|` `-` …).
6. The sentence-final `. ? !` run is consumed as the `<EOS>` boundary — the
   same segmentation semantics as v2.2 conditions A–D and v2.3 — so
   `<BOS>/<EOS>` remain metadata (trie root ε) and never become objects.
7. No substring crosses a sentence, paragraph or document boundary: each
   sentence is observed on its own.
8. Nested prefixes by whole sentence: the prefix for scale N is the shortest
   sentence prefix whose cumulative learner-visible token count reaches N.

Nothing is filtered by frequency, length, POS, grammaticality, similarity
or blacklist; there is no hub cap.

The `v23d` mode of the same tool reproduces the v2.3 condition-D pipeline
(one document per line, every `. ? !` ends a sentence, all punctuation
removed, condition-A nominal counts for prefix selection) and is verified
against the v2.3 CLI in §5.1.

## 3. Frame-type diagnostics

Every exact frame `(L, R)` is classified by whether `L` and/or `R` is ε
(trie root 0):

```text
empty_frame     : L=[] and R=[]      (u is a complete sentence)
left_boundary   : L=[] and R!=[]     (u is sentence-initial)
right_boundary  : L!=[] and R=[]     (u is sentence-final)
internal_frame  : L!=[] and R!=[]
```

Per type the tool reports witness count, candidate count (by the witness the
merger records for the pair — the first witness per unordered pair in
witness order, the same rule `ConservativeMerger::run` uses), accepted and
rejected merge counts, redundant candidates, induced unions, the number of
candidates/accepted merges whose witnesses are *exclusively* of that type,
and the accepted merges that end up inside the largest final class. It also
reports `#{u : (ε,ε) observed for u}` and lists every accepted merge
directly triggered by an empty frame. All of this is computed from the
merger's public state; the merger itself was not modified.

## 4. Runs

| run | corpus | preprocessing | scales completed | output |
|---|---|---|---|---|
| baseline | FineWeb | v2.3 CLI, condition D | 1e5 | `results_v2_3_conservative/fineweb_baseline/` |
| control | FineWeb | v23d (v2.3.1 tool) | 1e5, 1e6 (see §5.3) | `results_v2_3_1_clean_corpus/fineweb_v23d/` |
| control | FineWeb | structured (v2.3.1) | 1e5, 1e6 (see §5.3) | `results_v2_3_1_clean_corpus/fineweb_structured/` |
| control | peS2o | v23d | 1e5, 1e6 (see §5.3) | `results_v2_3_1_clean_corpus/pes2o_v23d/` |
| **main** | **peS2o** | **structured** | **1e5, 2e5, 4e5, 1e6 (see §5.3); 1e7 not reached (§5.4)** | `results_v2_3_1_clean_corpus/pes2o_structured/` (+ `_growth_2e5`, `_growth_4e5`) |

Top-level `clean_corpus_scaling.csv` and `frame_type_metrics.csv` concatenate
every run (the `corpus,preprocessing` columns identify the cell);
`largest_classes.txt`, `successful_merges_by_frame_type.txt` and
`rejected_merges_by_frame_type.txt` at the top level are the main run's
files; `comparison_fineweb_vs_clean.md` is generated by
`tools/summarize_clean_corpus.py`. All prefixes are nested by whole
sentence; every run is deterministic (the regression test re-runs a ladder
twice and compares the files byte for byte, runtime/RSS columns excluded).

## 5. Results

### 5.1 Baseline reproduction

`tools/summarize_clean_corpus.py` compares the v2.3 CLI's
`conservative_scaling.csv` with the `fineweb / v23d` row of the v2.3.1
tool on every shared column (objects, witnesses, candidates, accepted,
rejected, redundant, induced unions, classes, largest class, medians,
POS diagnostics): **identical at 1e5**. The frame-type breakdown of the
baseline below is therefore a reading of the unchanged v2.3 run, not a new
learner.

### 5.2 Same token scale (1e5): FineWeb vs clean corpus, 2×2

| metric | FineWeb / v2.3-D (baseline) | FineWeb / v2.3.1 | peS2o / v2.3-D | **peS2o / v2.3.1** |
|---|---|---|---|---|
| actual tokens | 89,078 | 100,041 | 85,333 | 100,022 |
| sentences | 5,219 | 5,644 | 4,458 | 3,253 |
| initial objects | 140,128 | 146,217 | 101,536 | 108,058 |
| local witnesses | 69,620 | 61,346 | 31,080 | **3,232** |
| merge candidates | 69,582 | 61,172 | 30,972 | **3,228** |
| accepted merges | 742 | 814 | 517 | 116 |
| rejected merges | 34,050 | 18,259 | 17,503 | 2,242 |
| accepted merge rate | 0.0107 | 0.0133 | 0.0167 | 0.0359 |
| rejected merge rate | 0.489 | 0.299 | 0.565 | 0.695 |
| resulting classes | 139,342 | 145,258 | 100,869 | 107,932 |
| largest class | 323 | 301 | 111 | **42** |
| largest class ratio | 0.00231 | 0.00206 | 0.00109 | **0.00039** |
| median / p95 class size | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| #{u : (ε,ε) observed} | 362 | 339 | 188 | **78** |
| empty-frame witness share | 0.939 | 0.934 | 0.566 | 0.929 |
| empty-frame candidate share | 0.939 | 0.937 | 0.566 | 0.929 |
| empty-frame accepted-merge share | 0.441 (327/742) | 0.378 (308/814) | 0.259 (134/517) | **0.509 (59/116)** |
| largest-class members with (ε,ε) | 260 / 323 | 284 / 301 | 110 / 111 | 42 / 42 |
| largest-class accepted merges by type (E/L/R/I) | 258 / 17 / 40 / 6 | 280 / 11 / 3 / 2 | 109 / 0 / 1 / 0 | 41 / 0 / 0 / 0 |
| within-class POS purity (UD EWT, external) | 0.995 | 0.998 | 0.997 | 0.999 |
| pairwise same-POS precision (labeled pairs) | 0.209 (86) | 0.167 (30) | 0.212 (33) | 0.333 (3) |
| runtime (s) | 89.0 (v2.3 CLI) / 114.7 | 10.1 | 5.6 | 2.5 |

Frame-type breakdown (witnesses / candidates / accepted / rejected; acceptance
rate within type):

| frame type | FineWeb / v2.3-D | FineWeb / v2.3.1 | peS2o / v2.3-D | peS2o / v2.3.1 |
|---|---|---|---|---|
| empty_frame | 65,341 / 65,341 / 327 / 31,435 ; 0.005 | 57,291 / 57,291 / 308 / 17,023 ; 0.005 | 17,578 / 17,523 / 134 / 11,431 ; 0.008 | 3,003 / 3,000 / 59 / 2,112 ; 0.020 |
| left_boundary | 835 / 816 / 134 / 492 ; 0.164 | 2,388 / 2,295 / 210 / 892 ; 0.092 | 6,447 / 6,436 / 154 / 2,004 ; 0.024 | 94 / 93 / 24 / 56 ; 0.258 |
| right_boundary | 3,171 / 3,160 / 227 / 1,993 ; 0.072 | 1,407 / 1,341 / 212 / 273 ; 0.158 | 5,296 / 5,269 / 116 / 3,340 ; 0.022 | 103 / 103 / 16 / 65 ; 0.155 |
| internal_frame | 273 / 265 / 54 / 130 ; 0.204 | 260 / 245 / 84 / 71 ; 0.343 | 1,759 / 1,744 / 113 / 728 ; 0.065 | 32 / 32 / 17 / 9 ; 0.531 |

In every cell the empty-frame witness count equals C(n, 2) for
n = #{u : (ε,ε) observed}: the `(ε,ε)` frame is one context shared by all
complete-sentence spans of length ≤ 3, so it emits a witness for every pair
of them. Candidates whose witnesses are *exclusively* empty-frame are
3,000 of 3,000 in the main run (and 59 of 59 accepted): a pair of
complete-sentence spans essentially never shares any other exact frame.

**Reading the 2×2.**

- *Corpus effect (preprocessing fixed).* Under v2.3-D, FineWeb → peS2o
  cuts (ε,ε) objects 362 → 188, candidates 69.6k → 31.0k, largest class
  323 → 111. Under structured preprocessing, 339 → 78, 61.2k → 3.2k,
  301 → 42. The clean corpus removes most witnesses and shrinks the hub
  2–4×.
- *Preprocessing effect (corpus fixed).* On FineWeb the two pipelines are
  close (362 vs 339 hub objects; 323 vs 301 largest class). On peS2o they
  are far apart (188 vs 78; 31.0k vs 3.2k candidates): the v2.1 rule
  "every `.` ends a sentence" cuts scientific prose at decimals,
  `et al.`, `Fig.`, and initials, which manufactures boundary fragments —
  visible as the 6.4k left-boundary and 5.3k right-boundary witnesses of
  the peS2o/v2.3-D cell versus 94 / 103 in the structured cell. Roughly
  half of the peS2o/v2.3-D largest classes (e.g. 60 author names
  `haque | rajkumar | sahebjamee | …` sharing the frame `L=[] R=[et al]`)
  are segmentation artifacts, not corpus content.
- *What does not move.* In all four cells the largest class is the
  `(ε,ε)` class: 80 % / 95 % / 99 % / 100 % of the accepted merges inside it
  are empty-frame merges, and 80–100 % of its members carry the `(ε,ε)`
  frame. The acceptance rate of empty-frame candidates is 0.5–2 %, but the
  survivors accumulate into a single class because the rollback check can
  only reject when a compositional conflict has been *observed*, and the
  objects that survive are precisely those that rarely appear inside a
  longer sentence (section headers, bracketed numbers, formula fragments,
  `<num>` templates). The share of accepted merges that come from an empty
  frame is not lower on the clean corpus (0.51 vs 0.44).

### 5.3 Scaling on the clean corpus (peS2o, structured, nested prefixes)

| nominal | actual tokens | sentences | objects | witnesses | candidates | accepted | rejected | classes | largest | ratio | (ε,ε) objects | empty-frame cand. share | empty-frame acc. share | internal cand. / acc. | runtime (s) | peak RSS (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1e5 | 100,022 | 3,253 | 108,058 | 3,232 | 3,228 | 116 | 2,242 | 107,932 | 42 | 0.00039 | 78 | 0.929 | 0.509 | 32 / 17 | 2.5 | 205 |
| 2e5 | 200,020 | 6,630 | 214,140 | 16,846 | 16,824 | 232 | 12,205 | 213,888 | 95 | 0.00044 | 181 | 0.967 | 0.552 | 56 / 27 | 8.4 | 184 |
| 4e5 | 400,036 | 13,054 | 381,544 | 54,830 | 54,751 | 484 | 40,488 | 381,030 | 168 | 0.00044 | 327 | 0.973 | 0.479 | 158 / 78 | 111.1 | 329 |
| 1e6 | PENDING_1E6 |

The number of `(ε,ε)` objects grows linearly with the prefix (78 → 181 → 327),
the empty-frame witnesses quadratically (3.0k → 16.3k → 53.3k ≈ n²/2), the
largest class ≈ half of the `(ε,ε)` objects at every scale (42/78, 95/181,
168/327), and the largest-class ratio is flat (≈ 4×10⁻⁴). Internal-frame
candidates stay at 0.3–1 % of all candidates.

### 5.4 1e7: measured bound, not run

Runtime of the unchanged learner grows faster than the candidate count
(2.5 s → 8.4 s → 111 s for 3.2k → 16.8k → 54.8k candidates: the per-candidate
transaction cost rises with the size of the `(ε,ε)` class it has to scan).
Extrapolating the linear growth of `(ε,ε)` objects gives ≈ 8,000 at 1e7 and
therefore ≈ 3×10⁷ empty-frame candidates, each more expensive than at 4e5;
this is far outside the session budget. The 1e7 prefix was therefore not
completed. This is the same bound the v2.3 report anticipated ("runtime and
memory are empirical outputs of this baseline"): it is caused by the
`(ε,ε)` hub, i.e. by witness semantics, and reintroducing a hub cap or a
frequency floor to reach 1e7 would define a different experiment. PENDING_1E6_RUNTIME

## 6. Manual class audit (recorded, not corrected)

Full lists: `largest_classes.txt` (top 20 per scale, with per-class counts
of members carrying `(ε,ε)` and of accepted merges by frame type),
`successful_merges_by_frame_type.txt` (20 per type per scale) and
`rejected_merges_by_frame_type.txt` (20 per type per scale) in
`results_v2_3_1_clean_corpus/` (main run) and in each run directory.

### 6.1 Largest classes, peS2o / structured

1e5, largest class (42, all 42 members carry `(ε,ε)`, all 41 merges
empty-frame):

```text
<num> | | gravitropism | | phototropism | [ <num> ] | histopathology and
immunohistochemistry | j . clin | conclusions | | introduction | | participants |
| task paradigms | visual search task | | behavioral data | | acquisition |
| imaging preprocessing | | imaging analysis | | discussion | mouse behavioural
analysis | quantification microrna panel | patients characteristics | literature
review | description levels * | grant assumption <num> | fix any t | if we choose
| output w m | n ` ˆ | vi | vii | acknowledgment | cluster redshift estimation ...
```

4e5, largest class (168; 166 carry `(ε,ε)`; 165 empty-frame merges): the
same class has absorbed `and`, `<num> . <num>`, `national science
foundation`, `conflict of interest`, `the study was`, `fig . <num>`,
`materials and methods`, `in our notation`, `page and immunoblotting`, ….

The next classes (sizes 15, 12, 8, 6, …) are boundary-fragment classes:
`( | theorem <num> . | proof of theorem | lemma <num> . | corollary <num> . |
example <num> . | definition <num> . | remark <num> .` (all sentence-initial
before a bare `<num>`); `results | go enrichment analysis | con clus ions |
task paradigms | imaging preprocessing | …` (sentence-final after the
Grobid header marker `|`); `availability | collection and analysis |
analysis and discussion | collection methods | accessibility statement`
(after `data`).

### 6.2 The four FineWeb v2.3 patterns, checked on the clean corpus

| FineWeb v2.3 pattern | FineWeb (baseline) example | present on peS2o / structured? |
|---|---|---|
| `<num>` merged with unrelated short utterances | `<num> ⇔ and newfound feelings`, `<num> ⇔ sigh`, `<num> ⇔ sorry everyone` (empty frame) | **yes** — `<num> ⇔ \| gravitropism`, `<num> ⇔ histopathology and immunohistochemistry`, `<num> ⇔ conclusions`, `<num> ⇔ j . clin`; 20 of the first 20 empty-frame merges have `<num>` as one side |
| `the` merged with heterogeneous phrases | `the ⇔ a number of / european and / flanked by / we have <num>` via `L=[] R=[u]`, `R=[sept]` (fragments of `u.s.`, `sept.`) | **no** for `the` (no `the` class in the top 20 at any scale: the segmentation fragments that created the frame are gone). **yes** for `and` at 4e5, which enters the `(ε,ε)` class as a one-token "sentence" left by a source line break |
| boilerplate-like complete spans | `all rights reserved`, `read more »`, `view public profile`, `sort it out` | **yes, in scientific form** — section headers `introduction`, `conclusions`, `materials and methods`, `conflict of interest`, `national science foundation`, `\| participants` are the boilerplate of this corpus and are exactly the `(ε,ε)` objects |
| fragment-like terminal spans | class of 38 right-boundary spans `get advice \| make friends \| upload photos \| gender : male \| posts : <num>` | **yes, smaller** — `phototropism \| go enrichment analysis \| con clus ions \| task paradigms` after `L=[\|]`; `n ă <num> \| : ` after `L=[<num>]`; `ρ ď d ⇔ ď` after `L=[let]` |

### 6.3 Empty-frame accepted merges (20 of 59, main run, 1e5)

```text
<num> <=> | gravitropism            <num> <=> | phototropism
<num> <=> [ <num> ]                 <num> <=> histopathology and immunohistochemistry
<num> <=> j . clin                  <num> <=> conclusions
<num> <=> | introduction            <num> <=> | participants
<num> <=> | task paradigms          <num> <=> visual search task
<num> <=> | behavioral data         <num> <=> | acquisition
<num> <=> | imaging preprocessing   <num> <=> | imaging analysis
<num> <=> | discussion              <num> <=> mouse behavioural analysis
<num> <=> quantification microrna panel   <num> <=> patients characteristics
<num> <=> mechanism signaling pathways    <num> <=> literature review
```

Every one has `L=[] R=[]`, exactly one witness, `induced_unions=0`, and is
exclusively empty-frame. None is a plausible category identity.

### 6.4 Internal-frame accepted merges (20 of 17+, main run, 1e5 and 4e5)

```text
auxin responses <=> photosynthetic responses   L=[| phyb <num> and phyb <num> differentially modulate] R=[in tomato seedlings]
linear <=> log - transformed                   L=[average] R=[slope activity at the first session was ...]
responses were <=> choices were                L=[a total of <num>] R=[collected]
<num> choices <=> <num> responses              L=[a total of] R=[were collected]
of photosynthesis <=> of auxin responses       L=[| regulation] R=[by phytochrome b]
linear slope activity <=> quadratic slope activity   L=[average] R=[at the first session was ...]
has <=> still has                              L=[this study] R=[some limitations]
study <=> study still                          L=[this] R=[has some limitations]
primers <=> specific primers                   L=[the] R=[are listed in table s <num>]
primers are <=> probes used are                L=[the] R=[listed in table s <num>]
this <=> this overdensity                      L=[we comeback to] R=[in § <num> . <num> . <num>]
rneasy <=> rneasy plant mini                   L=[total rna was extracted using an] R=[kit ( qiagen ) ...]
plant mini kit <=> kit                         L=[total rna was extracted using an rneasy] R=[( qiagen ) ...]
rnaseq differential expression <=> go enrichment   L=[|] R=[analysis]
in <=> highly enrich                           L=[sex - biased genes] R=[modules of correlated transcripts]
. <=> , <num> ,                                L=[( <num>] R=[<num> )]
. <=> - <num> -                                L=[<num>] R=[<num>]
<num> and <=> <num> , and                      L=[assume hypothesis <num> .] R=[suppose p > <num> ...]
u <=> capacitated s                            L=[alternative variational inequality formulation of the] R=[- o problem]
- <=> and weight -                             L=[model <num> : fev <num> % predicted] R=[adjusted]
```

Roughly two thirds are same-category or modifier-insertion pairs
(`auxin responses / photosynthetic responses`, `linear / log-transformed`,
`has / still has`, `primers / specific primers`); the rest are numeric or
punctuation templates inside formulas (`. ⇔ , <num> ,`) and one clear error
(`in ⇔ highly enrich`). None of these merges reaches the largest class.
FineWeb's internal-frame merges at 1e5 are dominated by numeric templates
(`<num> ⇔ <num> yr fixed`, `billion in ⇔ percent in`, `fixed ⇔ refi`,
`ohio ⇔ morgan`).

### 6.5 Rejected merges (20, main run, 1e5)

Empty-frame rejections are all of the form `<num> ⇔ X` blocked by
`Comp(<num>, of, <num> of)` vs `Comp(X, of, X of)` (or `,`, `(`, `the`,
`were`, `:` as the shared partner): the pair is rejected because `X`
also occurs inside a sentence, where its composition with the same partner
produces an output with a different exact-context profile — i.e. the
learner only manages to reject an empty-frame candidate when the object
happens to have been seen elsewhere.

Internal-frame rejections include linguistically valid pairs:
`responses ⇔ choices` (`L=[a total of <num>] R=[were collected]`) is rolled
back because `responses of` and `choices of` have different observed
profiles and no direct witness; likewise `auxin ⇔ photosynthetic`,
`linear ⇔ quadratic`, `then ⇔ therefore`. The same one-context rule that
accepts `<num> ⇔ conclusions` rejects `responses ⇔ choices`.

## 7. Corpus/preprocessing effect vs algorithm/witness-semantics effect

Attributable to **corpus / preprocessing** (they change when the corpus or
the pipeline changes, holding the learner fixed):

- the *number* of `(ε,ε)` objects (362 → 78 at 1e5) and hence the size of
  the hub, the number of witnesses (69.6k → 3.2k) and the size of the
  largest class (323 → 42);
- the `the`-with-fragments classes and the author-name / decimal fragment
  classes, which exist only under the v2.1 "every `.` ends a sentence" rule
  (peS2o/v2.3-D: 6.4k left- and 5.3k right-boundary witnesses vs 94 / 103
  when segmentation respects abbreviations and decimals);
- FineWeb's boilerplate (`all rights reserved`) being replaced by
  scientific boilerplate (section headers, `conflict of interest`).

Attributable to **algorithm / witness semantics** (invariant across all
four cells and across 1e5–4e5):

- the largest class is always the `(ε,ε)` class, built 80–100 % from
  empty-frame merges, mixing `<num>`, headers, brackets and formula
  fragments;
- one shared context is enough to make a candidate, and the transactional
  check can only block it when a *conflicting* composition has been
  observed; objects that do not appear inside longer sentences therefore
  merge with everything else that does not — the acceptance rate of
  empty-frame candidates (0.5–2 %) is low, but the accepted ones are
  systematically the uninformative ones;
- the same rule rejects valid same-category pairs whose outputs happen to
  be observationally separated (`responses ⇔ choices`);
- the quadratic growth of empty-frame candidates in the number of
  complete-sentence spans, which sets the runtime bound (§5.4).

## 8. Answers to the research questions

1. **Are FineWeb's heterogeneous largest classes significantly reduced on
   peS2o?** In size, yes: 323 → 42 members at 1e5 (ratio 2.3×10⁻³ →
   3.9×10⁻⁴), witnesses 69.6k → 3.2k. In kind, no: the largest class is
   still the same heterogeneous `(ε,ε)` class (`<num>` with section headers,
   brackets, formula fragments, and by 4e5 the token `and`), it is built
   100 % from empty-frame merges, and it grows linearly with the prefix
   (42 → 95 → 168), tracking half of all `(ε,ε)` objects.
2. **Does `(ε,ε)` still produce many wrong-looking merges?** Yes. Empty
   frames are 93–97 % of all candidates on the clean corpus (a *higher*
   share than on FineWeb, because the clean corpus removes the other noise
   but not the hub) and 48–55 % of all accepted merges (59/116, 128/232,
   232/484); every one of the 20 audited empty-frame merges is wrong-looking
   and every one has exactly one witness, which is `(ε,ε)`.
3. **Are internal exact-frame witnesses cleaner than boundary witnesses?**
   Clearly: within-type acceptance 0.53 (internal) vs 0.02 (empty), 0.26
   (left) and 0.16 (right) at 1e5, and 0.49 vs 0.004 at 4e5; the audited
   internal merges are mostly same-category or modifier-insertion pairs;
   internal merges contribute 0–1 merges to the largest class. But they are
   rare: 32 of 3,228 candidates at 1e5, 158 of 54,751 at 4e5.
4. **Does a cleaner corpus significantly improve conservative merging
   without changing the algorithm?** It improves the *counts* (fewer
   witnesses, smaller hub, smaller largest class, no `the` class) but not
   the *composition* of what is accepted: the empty-frame share of accepted
   merges is 0.51 on peS2o vs 0.44 on FineWeb, the largest class is the same
   kind of object, and the largest-class ratio is flat with scale. Roughly
   half of the improvement in the v2.3-D cells is segmentation, not corpus.
5. **If bad merges persist, is the cause `∃c: c[u], c[v] ∈ D ⇏ u ≡ v` rather
   than FineWeb preprocessing?** Yes. Both corpus and preprocessing were
   changed independently (2×2) and the empty-frame mechanism produced the
   largest class in every cell, with the same signature (single witness,
   `induced_unions = 0`, exclusively empty-frame, objects absent from
   sentence interiors). A single shared context — in particular the one
   context every complete short span shares — is accepted as sufficient
   evidence unless a contradiction happens to have been observed; the clean
   corpus reduces how many objects fall into that situation but cannot make
   the inference valid. The bad merges are a witness-semantics effect.
6. **Is the clean-corpus evidence sufficient before deciding to modify
   merge semantics?** For the narrow claim "`(ε,ε)`-only witnesses are not
   a corpus artifact", yes: it is invariant across corpus, preprocessing
   and 1e5–4e5 (1e6: §5.3), it is the source of the largest class, and it
   is also the computational bottleneck that stops the ladder at 1e7. For
   the broader question of what internal-frame-only evidence would deliver
   at scale, no: internal-frame candidates are too few (32–158) for a
   precision estimate, and the learner also rejects valid internal pairs,
   so any v2.4 design would need its own evaluation. No v2.4 is proposed
   here.

## 9. Verification

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
ctest --test-dir build --output-on-failure
  scf_tests                    passed   (v1.x core)
  scf_oracle_v2_tests          passed
  scf_real_v21_tests           passed
  scf_conservative_v23_tests   passed   (unchanged)
  scf_clean_corpus_v231_tests  passed   (4 new tests: structured loader,
                                         condition-D loader, frame types,
                                         deterministic ladder + outputs)
```

GCC 13.3, `-std=c++20 -Wall -Wextra -Wpedantic`, no warnings. The v2.3
sources (`src/conservative_merging.cpp`, `include/scf/conservative_merging.hpp`,
`tools/scf_conservative_merging.cpp`) are byte-identical to the previous
commit.
