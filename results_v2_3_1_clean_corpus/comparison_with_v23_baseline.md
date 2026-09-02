Identity check: v2.3.1 `v23_condition_d` reproduces the v2.3 CSV exactly at scale(s) 100000 (all 13 shared columns).

### Scale 100,000 nominal tokens

| metric | v2.3 baseline (condition D) | clean_wiki_body / clean_body_keep_punct |
|---|---|---|
| actual tokens observed | 87,975 | 100,007 |
| sentences | 4,450 | 7,136 |
| initial objects | 135,661 | 140,764 |
| local witnesses | 27,149 | 707,503 |
| merge candidates | 27,107 | 707,196 |
| accepted merges | 442 | 2,018 |
| rejected merges | 12,703 | 252,154 |
| redundant candidates | 13,962 | 453,024 |
| induced unions | 55 | 153 |
| resulting classes | 135,164 | 138,593 |
| largest class | 162 | 984 |
| **accepted merge rate** (accepted / candidates) | 0.0163 | 0.0029 |
| **rejected merge rate** (rejected / candidates) | 0.4686 | 0.3566 |
| **largest class ratio** (largest / objects) | 0.001194 | 0.006990 |
| median / p95 class size | 1 / 1 | 1 / 1 |
| objects with an (eps,eps) frame | 216 | 1,180 |
| empty-frame witness share | 0.8553 | 0.9832 |
| **empty-frame candidate share** (only-witness) | 0.8560 | 0.9833 |
| **empty-frame accepted merge share** (only-witness) | 0.4208 | 0.5391 |
| largest class: complete-sentence members | 162 | 947 |
| largest class: single-token members | 8 | 166 |
| largest class: members containing <num> | 35 | 24 |
| runtime (s) | 4.97 | 9002.61 |
| peak RSS (MB) | 116.9 | 150.1 |

Per frame type (candidates/accepted/rejected counted when ALL witnesses of the pair have that type):

| frame type | v2.3 baseline (condition D): witnesses / candidates / accepted / rejected / in-largest | clean_wiki_body / clean_body_keep_punct: witnesses / candidates / accepted / rejected / in-largest |
|---|---|---|
| empty_frame | 23,220 / 23,204 / 186 / 10,108 / 161 | 695,610 / 695,386 / 1,088 / 246,689 / 930 |
| left_boundary | 1,748 / 1,732 / 140 / 1,164 / 0 | 1,557 / 1,480 / 295 / 816 / 1 |
| right_boundary | 1,348 / 1,327 / 66 / 862 / 0 | 9,774 / 9,561 / 513 / 4,296 / 24 |
| internal_frame | 833 / 825 / 50 / 550 / 0 | 562 / 520 / 112 / 171 / 1 |
| mixed | 0 / 19 / 0 / 19 / 0 | 0 / 249 / 10 / 182 / 0 |

Acceptance rate within each frame type (accepted_only / candidate_count_only):

| frame type | v2.3 baseline (condition D) | clean_wiki_body / clean_body_keep_punct |
|---|---|---|
| empty_frame | 0.0080 | 0.0016 |
| left_boundary | 0.0808 | 0.1993 |
| right_boundary | 0.0497 | 0.0537 |
| internal_frame | 0.0606 | 0.2154 |

