#!/usr/bin/env python3
"""Build the v2.3 baseline vs clean-corpus comparison tables for v2.3.1.

Reads (never modifies):
  * the unchanged v2.3 output  results_v2_3_conservative/conservative_scaling.csv
  * the v2.3.1 frame-type diagnostics attached to the SAME v2.3 preprocessing
    (scf_clean_corpus --preprocess v23_condition_d), whose aggregate columns
    are asserted to equal the v2.3 CSV row for row exactly;
  * the v2.3.1 clean-corpus outputs (clean_corpus_scaling.csv,
    frame_type_metrics.csv) for one or more conditions.

Writes a Markdown fragment with the same-scale comparison used in the report.

Usage:
  python3 tools/compare_v231.py --baseline results_v2_3_conservative/conservative_scaling.csv \
      --baseline-frames results_v2_3_1_clean_corpus/baseline_v23_frames/ \
      --condition results_v2_3_1_clean_corpus/ [--condition DIR ...] --out FILE.md
"""

import argparse
import csv
import os
import sys


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def num(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fmt(value, digits=4):
    if value is None:
        return "n/a"
    if isinstance(value, float):
        if value == int(value) and abs(value) >= 1:
            return f"{int(value):,}"
        return f"{value:.{digits}f}"
    return str(value)


BASELINE_MAP = {
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
}


def assert_identity(baseline_rows, frame_scaling_rows):
    """The v2.3.1 v23_condition_d run must reproduce the v2.3 CSV exactly."""
    by_scale = {row["nominal_tokens"]: row for row in frame_scaling_rows}
    checked = []
    for row in baseline_rows:
        scale = row["nominal_tokens"]
        if scale not in by_scale:
            continue
        other = by_scale[scale]
        for old, new in BASELINE_MAP.items():
            if num(row[old]) != num(other[new]):
                raise SystemExit(
                    f"identity check failed at scale {scale}: {old}={row[old]} vs {new}={other[new]}")
        checked.append(scale)
    return checked


def scale_rows(rows):
    return {row["nominal_tokens"]: row for row in rows}


def frame_rows(rows):
    out = {}
    for row in rows:
        out.setdefault(row["nominal_tokens"], {})[row["frame_type"]] = row
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--baseline-frames", required=True,
                        help="directory with clean_corpus_scaling.csv/frame_type_metrics.csv "
                             "from --preprocess v23_condition_d")
    parser.add_argument("--condition", action="append", required=True,
                        help="directory with v2.3.1 outputs (repeatable)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    baseline = read_csv(args.baseline)
    base_scaling = read_csv(os.path.join(args.baseline_frames, "clean_corpus_scaling.csv"))
    base_frames = frame_rows(read_csv(os.path.join(args.baseline_frames, "frame_type_metrics.csv")))
    checked = assert_identity(baseline, base_scaling)
    base_by_scale = scale_rows(base_scaling)

    conditions = []
    for directory in args.condition:
        scaling = read_csv(os.path.join(directory, "clean_corpus_scaling.csv"))
        frames = frame_rows(read_csv(os.path.join(directory, "frame_type_metrics.csv")))
        label = scaling[0]["corpus"] + " / " + scaling[0]["preprocess"] if scaling else directory
        conditions.append((label, scale_rows(scaling), frames))

    lines = []
    lines.append(f"Identity check: v2.3.1 `v23_condition_d` reproduces the v2.3 CSV exactly at "
                 f"scale(s) {', '.join(checked)} (all {len(BASELINE_MAP)} shared columns).")
    lines.append("")
    metrics = [
        ("actual tokens observed", "actual_tokens", 0),
        ("sentences", "sentences", 0),
        ("initial objects", "initial_objects", 0),
        ("local witnesses", "local_witnesses", 0),
        ("merge candidates", "merge_candidates", 0),
        ("accepted merges", "accepted_merges", 0),
        ("rejected merges", "rejected_merges", 0),
        ("redundant candidates", "redundant_candidates", 0),
        ("induced unions", "induced_unions", 0),
        ("resulting classes", "resulting_classes", 0),
        ("largest class", "largest_class", 0),
        ("**accepted merge rate** (accepted / candidates)", "accepted_merge_rate", 4),
        ("**rejected merge rate** (rejected / candidates)", "rejected_merge_rate", 4),
        ("**largest class ratio** (largest / objects)", "largest_class_ratio", 6),
        ("median / p95 class size", None, 0),
        ("objects with an (eps,eps) frame", "objects_with_empty_frame", 0),
        ("empty-frame witness share", "empty_frame_witness_share", 4),
        ("**empty-frame candidate share** (only-witness)", "empty_frame_candidate_share", 4),
        ("**empty-frame accepted merge share** (only-witness)", "empty_frame_accepted_share", 4),
        ("largest class: complete-sentence members", "largest_class_complete_sentence_members", 0),
        ("largest class: single-token members", "largest_class_single_token_members", 0),
        ("largest class: members containing <num>", "largest_class_num_members", 0),
        ("runtime (s)", "runtime_seconds", 2),
        ("peak RSS (MB)", "peak_rss_mb", 1),
    ]
    all_scales = sorted({s for s in base_by_scale} |
                        {s for _, rows, _ in conditions for s in rows}, key=int)
    for scale in all_scales:
        cols = [("v2.3 baseline (condition D)", base_by_scale.get(scale), base_frames.get(scale, {}))]
        for label, rows, frames in conditions:
            cols.append((label, rows.get(scale), frames.get(scale, {})))
        if not any(c[1] for c in cols[1:]):
            continue
        lines.append(f"### Scale {int(scale):,} nominal tokens")
        lines.append("")
        lines.append("| metric | " + " | ".join(c[0] for c in cols) + " |")
        lines.append("|---|" + "---|" * len(cols))
        for name, key, digits in metrics:
            cells = []
            for _, row, _ in cols:
                if row is None:
                    cells.append("not run")
                elif key is None:
                    cells.append(f"{row['median_class_size']} / {row['p95_class_size']}")
                else:
                    value = num(row[key])
                    cells.append(fmt(value, digits) if digits else fmt(float(int(value)) if value is not None else None))
            lines.append(f"| {name} | " + " | ".join(cells) + " |")
        lines.append("")
        lines.append("Per frame type (candidates/accepted/rejected counted when ALL witnesses of the pair have that type):")
        lines.append("")
        lines.append("| frame type | " + " | ".join(
            f"{c[0]}: witnesses / candidates / accepted / rejected / in-largest" for c in cols) + " |")
        lines.append("|---|" + "---|" * len(cols))
        for ftype in ("empty_frame", "left_boundary", "right_boundary", "internal_frame", "mixed"):
            cells = []
            for _, row, frames in cols:
                f = frames.get(ftype)
                if f is None:
                    cells.append("not run")
                else:
                    cells.append(f"{int(f['witness_count']):,} / {int(f['candidate_count_only']):,} / "
                                 f"{int(f['accepted_only']):,} / {int(f['rejected_only']):,} / "
                                 f"{int(f['accepted_only_in_largest_class']):,}")
            lines.append(f"| {ftype} | " + " | ".join(cells) + " |")
        lines.append("")
        lines.append("Acceptance rate within each frame type (accepted_only / candidate_count_only):")
        lines.append("")
        lines.append("| frame type | " + " | ".join(c[0] for c in cols) + " |")
        lines.append("|---|" + "---|" * len(cols))
        for ftype in ("empty_frame", "left_boundary", "right_boundary", "internal_frame"):
            cells = []
            for _, row, frames in cols:
                f = frames.get(ftype)
                if f is None:
                    cells.append("not run")
                else:
                    c = int(f["candidate_count_only"])
                    cells.append("n/a" if c == 0 else f"{int(f['accepted_only']) / c:.4f}")
            lines.append(f"| {ftype} | " + " | ".join(cells) + " |")
        lines.append("")
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
