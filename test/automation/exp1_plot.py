#!/usr/bin/env python3
"""Plot EXP1 throughput curves from CSV produced by exp1_run_auto.py."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from svg_plot_lib import write_line_chart_svg


METHOD_ORDER = [
    "RDMA-assisted TX",
    "DMA-assisted TX",
    "Header-only Offloading TX",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot EXP1 results.")
    parser.add_argument("--input", type=Path, default=Path("test/results/exp1_results.csv"))
    parser.add_argument("--output", type=Path, default=Path("test/results/exp1_figure.svg"))
    parser.add_argument("--title", default="EXP1: Header-only Offloading TX Path")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.exists():
        raise FileNotFoundError(f"Input CSV not found: {args.input}")

    data = defaultdict(lambda: defaultdict(list))
    with open(args.input, "r", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            label = row["method_label"]
            payload = int(row["payload_size"])
            gbps = float(row["total_gbps"])
            data[label][payload].append(gbps)

    if not data:
        raise RuntimeError(f"No rows found in {args.input}")

    all_payloads = sorted({payload for method_data in data.values() for payload in method_data.keys()})
    series_data = []
    for label in METHOD_ORDER:
        if label not in data:
            continue
        series_data.append(
            (
                label,
                {payload: sum(data[label][payload]) / len(data[label][payload]) for payload in data[label].keys()},
            )
        )

    write_line_chart_svg(
        output_path=args.output,
        title=args.title,
        x_label="Payload Size (Bytes)",
        y_label="Throughput (Gbps)",
        x_ticks=all_payloads,
        series_data=series_data,
    )
    print(f"[EXP1] figure saved to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
