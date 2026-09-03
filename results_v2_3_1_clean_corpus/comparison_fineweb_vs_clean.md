# FineWeb vs clean corpus (same v2.3 learner)

## v2.3 CLI FineWeb baseline reproduction

- scale 100000: identical on all shared columns

## Scale 100,000 nominal tokens

| metric | FineWeb / v2.3-D | FineWeb / v2.3.1 | peS2o / v2.3-D | peS2o / v2.3.1 |
|---|---|---|---|---|
| actual tokens | 89,078 | 100,041 | 85,333 | 100,022 |
| sentences | 5,219 | 5,644 | 4,458 | 3,253 |
| initial objects | 140,128 | 146,217 | 101,536 | 108,058 |
| local witnesses | 69,620 | 61,346 | 31,080 | 3,232 |
| merge candidates | 69,582 | 61,172 | 30,972 | 3,228 |
| accepted merges | 742 | 814 | 517 | 116 |
| rejected merges | 34,050 | 18,259 | 17,503 | 2,242 |
| accepted merge rate | 0.0107 | 0.0133 | 0.0167 | 0.0359 |
| rejected merge rate | 0.4894 | 0.2985 | 0.5651 | 0.6945 |
| resulting classes | 139,342 | 145,258 | 100,869 | 107,932 |
| largest class | 323 | 301 | 111 | 42 |
| largest class ratio | 0.0023 | 0.0021 | 0.0011 | 0.0004 |
| median / p95 class size | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| objects with (eps,eps) | 362 | 339 | 188 | 78 |
| empty-frame witness share | 0.9385 | 0.9339 | 0.5656 | 0.9291 |
| empty-frame candidate share | 0.9391 | 0.9366 | 0.5658 | 0.9294 |
| empty-frame accepted share | 0.4407 | 0.3784 | 0.2592 | 0.5086 |
| largest-class members with (eps,eps) | 260 | 284 | 110 | 42 |
| within-class POS purity | 0.9950 | 0.9976 | 0.9966 | 0.9994 |
| pairwise same-POS precision | 0.2093 | 0.1667 | 0.2121 | 0.3333 |
| runtime (s) | 114.7105 | 10.0542 | 5.5784 | 2.4600 |
| peak RSS (MB) | 107.9805 | 114.3086 | 103.8945 | 204.7383 |

Frame-type breakdown (witnesses / candidates / accepted / rejected, acceptance rate within type, accepted merges inside the largest class):

| frame type | FineWeb / v2.3-D | FineWeb / v2.3.1 | peS2o / v2.3-D | peS2o / v2.3.1 |
|---|---|---|---|---|
| empty_frame | 65,341 / 65,341 / 327 / 31,435 ; acc 0.005 ; largest 258 | 57,291 / 57,291 / 308 / 17,023 ; acc 0.005 ; largest 280 | 17,578 / 17,523 / 134 / 11,431 ; acc 0.008 ; largest 109 | 3,003 / 3,000 / 59 / 2,112 ; acc 0.020 ; largest 41 |
| left_boundary | 835 / 816 / 134 / 492 ; acc 0.164 ; largest 17 | 2,388 / 2,295 / 210 / 892 ; acc 0.092 ; largest 11 | 6,447 / 6,436 / 154 / 2,004 ; acc 0.024 ; largest 0 | 94 / 93 / 24 / 56 ; acc 0.258 ; largest 0 |
| right_boundary | 3,171 / 3,160 / 227 / 1,993 ; acc 0.072 ; largest 40 | 1,407 / 1,341 / 212 / 273 ; acc 0.158 ; largest 3 | 5,296 / 5,269 / 116 / 3,340 ; acc 0.022 ; largest 1 | 103 / 103 / 16 / 65 ; acc 0.155 ; largest 0 |
| internal_frame | 273 / 265 / 54 / 130 ; acc 0.204 ; largest 6 | 260 / 245 / 84 / 71 ; acc 0.343 ; largest 2 | 1,759 / 1,744 / 113 / 728 ; acc 0.065 ; largest 0 | 32 / 32 / 17 / 9 ; acc 0.531 ; largest 0 |

## Scale 200,000 nominal tokens

| metric | FineWeb / v2.3-D | FineWeb / v2.3.1 | peS2o / v2.3-D | peS2o / v2.3.1 |
|---|---|---|---|---|
| actual tokens | 179,028 | 200,002 | 172,837 | 200,020 |
| sentences | 10,391 | 10,800 | 8,876 | 6,630 |
| initial objects | 254,349 | 264,315 | 203,521 | 214,140 |
| local witnesses | 241,979 | 169,292 | 118,718 | 16,846 |
| merge candidates | 241,834 | 169,058 | 118,056 | 16,824 |
| accepted merges | 1,392 | 1,420 | 1,050 | 232 |
| rejected merges | 135,878 | 53,208 | 59,020 | 12,205 |
| accepted merge rate | 0.0058 | 0.0084 | 0.0089 | 0.0138 |
| rejected merge rate | 0.5619 | 0.3147 | 0.4999 | 0.7255 |
| resulting classes | 252,851 | 262,660 | 202,105 | 213,888 |
| largest class | 580 | 503 | 210 | 95 |
| largest class ratio | 0.0023 | 0.0019 | 0.0010 | 0.0004 |
| median / p95 class size | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| objects with (eps,eps) | 680 | 570 | 326 | 181 |
| empty-frame witness share | 0.9540 | 0.9579 | 0.4462 | 0.9670 |
| empty-frame candidate share | 0.9546 | 0.9592 | 0.4461 | 0.9670 |
| empty-frame accepted share | 0.4109 | 0.3669 | 0.2324 | 0.5517 |
| largest-class members with (eps,eps) | 451 | 471 | 207 | 94 |
| within-class POS purity | -1 | -1 | -1 | -1 |
| pairwise same-POS precision | -1 | -1 | -1 | -1 |
| runtime (s) | 4722.3711 | 47.1534 | 146.7905 | 8.3691 |
| peak RSS (MB) | 192.8867 | 202.0820 | 171.5898 | 183.8086 |

Frame-type breakdown (witnesses / candidates / accepted / rejected, acceptance rate within type, accepted merges inside the largest class):

| frame type | FineWeb / v2.3-D | FineWeb / v2.3.1 | peS2o / v2.3-D | peS2o / v2.3.1 |
|---|---|---|---|---|
| empty_frame | 230,860 / 230,860 / 572 / 128,805 ; acc 0.002 ; largest 449 | 162,165 / 162,165 / 521 / 51,177 ; acc 0.003 ; largest 466 | 52,975 / 52,661 / 244 / 31,209 ; acc 0.005 ; largest 205 | 16,290 / 16,269 / 128 / 11,835 ; acc 0.008 ; largest 93 |
| left_boundary | 3,177 / 3,118 / 336 / 1,673 ; acc 0.108 ; largest 38 | 3,170 / 3,074 / 351 / 1,169 ; acc 0.114 ; largest 16 | 25,542 / 25,472 / 272 / 5,909 ; acc 0.011 ; largest 1 | 169 / 168 / 47 / 101 ; acc 0.280 ; largest 1 |
| right_boundary | 7,430 / 7,357 / 384 / 5,121 ; acc 0.052 ; largest 80 | 2,993 / 2,917 / 343 / 706 ; acc 0.118 ; largest 9 | 27,603 / 27,419 / 252 / 17,472 ; acc 0.009 ; largest 1 | 331 / 331 / 30 / 253 ; acc 0.091 ; largest 0 |
| internal_frame | 512 / 499 / 100 / 279 ; acc 0.200 ; largest 10 | 964 / 902 / 205 / 156 ; acc 0.227 ; largest 6 | 12,598 / 12,504 / 282 / 4,430 ; acc 0.023 ; largest 0 | 56 / 56 / 27 / 16 ; acc 0.482 ; largest 0 |

## Scale 400,000 nominal tokens

| metric | peS2o / v2.3.1 |
|---|---|
| actual tokens | 400,036 |
| sentences | 13,054 |
| initial objects | 381,544 |
| local witnesses | 54,830 |
| merge candidates | 54,751 |
| accepted merges | 484 |
| rejected merges | 40,488 |
| accepted merge rate | 0.0088 |
| rejected merge rate | 0.7395 |
| resulting classes | 381,030 |
| largest class | 168 |
| largest class ratio | 0.0004 |
| median / p95 class size | 1 / 1 |
| objects with (eps,eps) | 327 |
| empty-frame witness share | 0.9721 |
| empty-frame candidate share | 0.9730 |
| empty-frame accepted share | 0.4793 |
| largest-class members with (eps,eps) | 166 |
| within-class POS purity | -1 |
| pairwise same-POS precision | -1 |
| runtime (s) | 111.0863 |
| peak RSS (MB) | 328.6602 |

Frame-type breakdown (witnesses / candidates / accepted / rejected, acceptance rate within type, accepted merges inside the largest class):

| frame type | peS2o / v2.3.1 |
|---|---|
| empty_frame | 53,301 / 53,273 / 232 / 39,479 ; acc 0.004 ; largest 165 |
| left_boundary | 842 / 811 / 98 / 587 ; acc 0.121 ; largest 1 |
| right_boundary | 517 / 509 / 76 / 366 ; acc 0.149 ; largest 0 |
| internal_frame | 170 / 158 / 78 / 56 ; acc 0.494 ; largest 1 |

## Scale 1,000,000 nominal tokens

| metric | peS2o / v2.3.1 |
|---|---|
| actual tokens | 1,000,009 |
| sentences | 33,225 |
| initial objects | 867,768 |
| local witnesses | 245,794 |
| merge candidates | 245,453 |
| accepted merges | 1,318 |
| rejected merges | 181,368 |
| accepted merge rate | 0.0054 |
| rejected merge rate | 0.7389 |
| resulting classes | 866,322 |
| largest class | 368 |
| largest class ratio | 0.0004 |
| median / p95 class size | 1 / 1 |
| objects with (eps,eps) | 694 |
| empty-frame witness share | 0.9783 |
| empty-frame candidate share | 0.9794 |
| empty-frame accepted share | 0.3710 |
| largest-class members with (eps,eps) | 353 |
| within-class POS purity | 0.9996 |
| pairwise same-POS precision | 0.3333 |
| runtime (s) | 2578.7493 |
| peak RSS (MB) | 894.6602 |

Frame-type breakdown (witnesses / candidates / accepted / rejected, acceptance rate within type, accepted merges inside the largest class):

| frame type | peS2o / v2.3.1 |
|---|---|
| empty_frame | 240,471 / 240,404 / 489 / 178,018 ; acc 0.002 ; largest 352 |
| left_boundary | 2,976 / 2,877 / 267 / 2,184 ; acc 0.093 ; largest 3 |
| right_boundary | 1,478 / 1,417 / 250 / 919 ; acc 0.176 ; largest 2 |
| internal_frame | 869 / 755 / 312 / 247 ; acc 0.413 ; largest 10 |

