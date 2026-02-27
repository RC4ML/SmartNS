#!/usr/bin/env python3
"""Plot two EXP1 figures from CSV produced by exp1_run_auto.py."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from svg_plot_lib import write_line_chart_svg, write_xy_line_chart_svg


METHOD_ORDER = [
    "RDMA-assisted TX",
    "DMA-assisted TX",
    "Header-only Offloading TX",
]

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot EXP1 throughput and memory-bandwidth figures.")
    parser.add_argument("--input", type=Path, default=Path("test/results/exp1_results.csv"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("test/results/exp1_figure.svg"),
        help="Base output path. The script writes <base>_a.svg (throughput) and <base>_b.svg (memory).",
    )
    parser.add_argument("--throughput-title", default="EXP1: Header-only Offloading TX Path")
    parser.add_argument("--memory-title", default="Figure 11.b: Arm Memory Bandwidth vs Throughput (EXP1)")
    return parser.parse_args()


def _pick_memory_value(row: dict[str, str]) -> float | None:
    field_candidates = ["memory_active_avg_total_mb", "memory_avg_total_mb", "llc_avg_total_mb"]
    for field in field_candidates:
        raw = row.get(field, "")
        if raw is None:
            continue
        raw = raw.strip()
        if raw:
            return float(raw)
    return None


def _derive_dual_outputs(base_output: Path) -> tuple[Path, Path]:
    stem = base_output.stem if base_output.suffix else base_output.name
    output_a = base_output.with_name(f"{stem}_a.svg")
    output_b = base_output.with_name(f"{stem}_b.svg")
    return output_a, output_b


def main() -> int:
    args = parse_args()
    if not args.input.exists():
        raise FileNotFoundError(f"Input CSV not found: {args.input}")
    output_a, output_b = _derive_dual_outputs(args.output)

    throughput_data = defaultdict(lambda: defaultdict(list))
    memory_data = defaultdict(lambda: defaultdict(list))
    with open(args.input, "r", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            label = row["method_label"]
            payload = int(row["payload_size"])
            throughput_data[label][payload].append(float(row["total_gbps"]))
            memory_value = _pick_memory_value(row)
            if memory_value is not None:
                memory_data[label][payload].append(memory_value)

    if not throughput_data:
        raise RuntimeError(f"No throughput rows found in {args.input}")

    all_payloads = sorted({payload for method_data in throughput_data.values() for payload in method_data.keys()})
    throughput_series_data = []
    for label in METHOD_ORDER:
        if label not in throughput_data:
            continue
        throughput_series_data.append(
            (
                label,
                {
                    payload: sum(throughput_data[label][payload]) / len(throughput_data[label][payload])
                    for payload in throughput_data[label].keys()
                },
            )
        )

    write_line_chart_svg(
        output_path=output_a,
        title=args.throughput_title,
        x_label="Payload Size (Bytes)",
        y_label="Throughput (Gbps)",
        x_ticks=all_payloads,
        series_data=throughput_series_data,
    )

    memory_series_points = []
    for label in METHOD_ORDER:
        if label not in memory_data or label not in throughput_data:
            continue
        points = []
        for payload in sorted(memory_data[label].keys()):
            if payload not in throughput_data[label]:
                continue
            throughput_value = sum(throughput_data[label][payload]) / len(throughput_data[label][payload])
            memory_value = sum(memory_data[label][payload]) / len(memory_data[label][payload])
            points.append((throughput_value, memory_value))
        if points:
            memory_series_points.append((label, points))

    if not memory_series_points:
        raise RuntimeError(f"No memory-bandwidth rows found in {args.input}")

    write_xy_line_chart_svg(
        output_path=output_b,
        title=args.memory_title,
        x_label="Throughput (Gbps)",
        y_label="Arm Memory Bandwidth (MB/s)",
        series_points=memory_series_points,
    )
    print(f"[EXP1] throughput figure saved to {output_a}")
    print(f"[EXP1] memory-vs-throughput figure saved to {output_b}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
