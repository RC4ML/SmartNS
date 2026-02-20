#!/usr/bin/env python3
"""Plot EXP4 curves from CSV produced by exp4_run_auto.py."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from svg_plot_lib import write_line_chart_svg


TYPE_ORDER = [
    "CPU-only",
    "CPU+CRC offload",
    "CPU+CRC offload+DSA",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot EXP4 results.")
    parser.add_argument("--input", type=Path, default=Path("test/results/exp4_results.csv"))
    parser.add_argument("--output", type=Path, default=Path("test/results/exp4_figure.svg"))
    parser.add_argument("--title", default="EXP4: Solar Application")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.exists():
        raise FileNotFoundError(f"Input CSV not found: {args.input}")

    data = defaultdict(lambda: defaultdict(list))
    with open(args.input, "r", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            label = row["type_label"]
            threads = int(row["threads"])
            mops = float(row["total_mops"])
            data[label][threads].append(mops)

    if not data:
        raise RuntimeError(f"No rows found in {args.input}")

    all_threads = sorted({threads for type_data in data.values() for threads in type_data.keys()})
    series_data = []
    for label in TYPE_ORDER:
        if label not in data:
            continue
        series_data.append(
            (
                label,
                {thread: sum(data[label][thread]) / len(data[label][thread]) for thread in data[label].keys()},
            )
        )

    write_line_chart_svg(
        output_path=args.output,
        title=args.title,
        x_label="Threads",
        y_label="Throughput (Mops)",
        x_ticks=all_threads,
        series_data=series_data,
    )
    print(f"[EXP4] figure saved to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
