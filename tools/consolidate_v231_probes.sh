#!/usr/bin/env bash
# Collects the small-scale probe ladders (separate deterministic runs of
# scf_clean_corpus at 20k..100k tokens) into one supplementary CSV pair so the
# runtime law of the unchanged v2.3 learner can be read from a single table.
set -euo pipefail
root=results_v2_3_1_clean_corpus
out=$root/supplementary
mkdir -p "$out"
first=1
for dir in "$@"; do
  if [ $first -eq 1 ]; then
    head -1 "$dir/clean_corpus_scaling.csv" > "$out/small_scale_ladder.csv"
    head -1 "$dir/frame_type_metrics.csv" > "$out/small_scale_frame_type_metrics.csv"
    first=0
  fi
  tail -n +2 "$dir/clean_corpus_scaling.csv" >> "$out/small_scale_ladder.csv"
  tail -n +2 "$dir/frame_type_metrics.csv" >> "$out/small_scale_frame_type_metrics.csv"
done
echo "wrote $out/small_scale_ladder.csv and $out/small_scale_frame_type_metrics.csv"
