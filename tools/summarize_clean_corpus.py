#!/usr/bin/env python3
"""SCF v2.3.1 -- aggregate the per-run outputs of scf_clean_corpus.

Reads every run directory under results_v2_3_1_clean_corpus/ (one per
corpus x preprocessing pair), concatenates the per-run CSVs into the
top-level clean_corpus_scaling.csv / frame_type_metrics.csv, copies the
main-experiment (peS2o, structured) audit files to the top level, verifies
that the unchanged v2.3 CLI's FineWeb run (results_v2_3_conservative/
fineweb_baseline/conservative_scaling.csv) is reproduced number-for-number
by the v23d preprocessing mode, and writes comparison_fineweb_vs_clean.md.

Usage: python3 tools/summarize_clean_corpus.py [results_v2_3_1_clean_corpus]
"""

import csv
import os
import shutil
import sys

RUNS = ["fineweb_v23d", "fineweb_structured", "pes2o_v23d", "pes2o_structured"]
MAIN_RUN = "pes2o_structured"
BASELINE = "results_v2_3_conservative/fineweb_baseline/conservative_scaling.csv"
AUDIT_FILES = ["largest_classes.txt", "successful_merges_by_frame_type.txt",
               "rejected_merges_by_frame_type.txt"]
SHARED = {  # v2.3 CSV column -> v2.3.1 CSV column
    "actual_condition_d_tokens": "actual_tokens",
    "initial_objects": "initial_objects",
    "local_witnesses": "local_witnesses",
    "merge_candidates": "merge_candidates",
    "accepted_merges": "accepted_merges",
    "rejected_merges": "rejected_merges",
    "redundant_candidates": "redundant_candidates",
    "induced_unions": "induced_unions",
    "resulting_eclasses": "resulting_classes",
    "largest_eclass": "largest_class",
    "largest_eclass_ratio": "largest_class_ratio",
    "median_class_size": "median_class_size",
    "p95_class_size": "p95_class_size",
    "pos_labeled_objects": "pos_labeled_objects",
    "within_class_pos_purity": "within_class_pos_purity",
    "pairwise_same_pos_precision": "pairwise_same_pos_precision",
}


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, rows):
    with open(path, "w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def fmt(value, digits=4):
    try:
        number = float(value)
    except ValueError:
        return value
    if number.is_integer() and abs(number) >= 1:
        return f"{int(number):,}"
    return f"{number:.{digits}f}"


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "results_v2_3_1_clean_corpus"
    scaling, frames = [], []
    for run in RUNS:
        run_dir = os.path.join(root, run)
        if not os.path.isdir(run_dir):
            print(f"missing run directory {run_dir}")
            continue
        scaling += read_csv(os.path.join(run_dir, "clean_corpus_scaling.csv"))
        frames += read_csv(os.path.join(run_dir, "frame_type_metrics.csv"))
    write_csv(os.path.join(root, "clean_corpus_scaling.csv"), scaling)
    write_csv(os.path.join(root, "frame_type_metrics.csv"), frames)
    for name in AUDIT_FILES:
        shutil.copyfile(os.path.join(root, MAIN_RUN, name), os.path.join(root, name))

    lines = ["# FineWeb vs clean corpus (same v2.3 learner)", ""]

    # 1. Baseline reproduction check.
    if os.path.exists(BASELINE):
        baseline = {r["nominal_tokens"]: r for r in read_csv(BASELINE)}
        lines.append("## v2.3 CLI FineWeb baseline reproduction")
        lines.append("")
        for row in scaling:
            if row["corpus"] != "fineweb" or row["preprocessing"] != "v23d":
                continue
            base = baseline.get(row["nominal_tokens"])
            if base is None:
                continue
            diffs = [k for k, v in SHARED.items() if base[k] != row[v]]
            lines.append(f"- scale {row['nominal_tokens']}: "
                         + ("identical on all shared columns" if not diffs
                            else "DIFFERS on " + ", ".join(diffs)))
        lines.append("")

    # 2. Per-scale comparison tables.
    by_key = {(r["corpus"], r["preprocessing"], r["nominal_tokens"]): r for r in scaling}
    frame_by_key = {}
    for r in frames:
        frame_by_key.setdefault((r["corpus"], r["preprocessing"], r["nominal_tokens"]), {})[
            r["frame_type"]] = r
    scales = sorted({r["nominal_tokens"] for r in scaling}, key=int)
    metrics = [
        ("actual tokens", "actual_tokens"),
        ("sentences", "sentences"),
        ("initial objects", "initial_objects"),
        ("local witnesses", "local_witnesses"),
        ("merge candidates", "merge_candidates"),
        ("accepted merges", "accepted_merges"),
        ("rejected merges", "rejected_merges"),
        ("accepted merge rate", "accepted_merge_rate"),
        ("rejected merge rate", "rejected_merge_rate"),
        ("resulting classes", "resulting_classes"),
        ("largest class", "largest_class"),
        ("largest class ratio", "largest_class_ratio"),
        ("median / p95 class size", None),
        ("objects with (eps,eps)", "objects_with_empty_frame"),
        ("empty-frame witness share", "empty_frame_witness_share"),
        ("empty-frame candidate share", "empty_frame_candidate_share"),
        ("empty-frame accepted share", "empty_frame_accepted_share"),
        ("largest-class members with (eps,eps)", "largest_class_members_with_empty_frame"),
        ("within-class POS purity", "within_class_pos_purity"),
        ("pairwise same-POS precision", "pairwise_same_pos_precision"),
        ("runtime (s)", "runtime_seconds"),
        ("peak RSS (MB)", "peak_rss_mb"),
    ]
    columns = [("fineweb", "v23d", "FineWeb / v2.3-D"),
               ("fineweb", "structured", "FineWeb / v2.3.1"),
               ("pes2o", "v23d", "peS2o / v2.3-D"),
               ("pes2o", "structured", "peS2o / v2.3.1")]
    for scale in scales:
        present = [c for c in columns if (c[0], c[1], scale) in by_key]
        if not present:
            continue
        lines.append(f"## Scale {int(scale):,} nominal tokens")
        lines.append("")
        lines.append("| metric | " + " | ".join(c[2] for c in present) + " |")
        lines.append("|---|" + "---|" * len(present))
        for label, key in metrics:
            cells = []
            for c in present:
                row = by_key[(c[0], c[1], scale)]
                if key is None:
                    cells.append(f"{row['median_class_size']} / {row['p95_class_size']}")
                else:
                    cells.append(fmt(row[key]))
            lines.append(f"| {label} | " + " | ".join(cells) + " |")
        lines.append("")
        lines.append("Frame-type breakdown (witnesses / candidates / accepted / rejected, "
                     "acceptance rate within type, accepted merges inside the largest class):")
        lines.append("")
        lines.append("| frame type | " + " | ".join(c[2] for c in present) + " |")
        lines.append("|---|" + "---|" * len(present))
        for frame_type in ["empty_frame", "left_boundary", "right_boundary", "internal_frame"]:
            cells = []
            for c in present:
                r = frame_by_key[(c[0], c[1], scale)][frame_type]
                cells.append(f"{fmt(r['witness_count'])} / {fmt(r['candidate_count'])} / "
                             f"{fmt(r['accepted_merge_count'])} / {fmt(r['rejected_merge_count'])}"
                             f" ; acc {fmt(r['acceptance_rate_within_type'], 3)} ; "
                             f"largest {fmt(r['largest_class_accepted_merges'])}")
            lines.append(f"| {frame_type} | " + " | ".join(cells) + " |")
        lines.append("")
    with open(os.path.join(root, "comparison_fineweb_vs_clean.md"), "w",
              encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
