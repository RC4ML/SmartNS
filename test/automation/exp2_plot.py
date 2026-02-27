#!/usr/bin/env python3
"""Plot two EXP2 figures from CSV produced by exp2_run_auto.py."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from svg_plot_lib import write_line_chart_svg


METHOD_ORDER = [
    "RDMA-assisted RX",
    "DMA-assisted RX",
    "Unlimited-working-set In-Cache RX",
]
BYTES_PER_MIB = 1024 * 1024

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot EXP2 throughput and memory-bandwidth figures.")
    parser.add_argument("--input", type=Path, default=Path("test/results/exp2_results.csv"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("test/results/exp2_figure.svg"),
        help="Base output path. The script writes <base>_a.svg (throughput) and <base>_b.svg (memory).",
    )
    parser.add_argument("--throughput-title", default="EXP2: Throughput vs Working Set Size")
    parser.add_argument("--memory-title", default="EXP2: Arm Memory Bandwidth vs Working Set Size")
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


def _pick_working_set_size(row: dict[str, str]) -> int | None:
    raw_working_set = (row.get("working_set_size", "") or "").strip()
    if raw_working_set:
        return int(float(raw_working_set))

    raw_nb_rxd = (row.get("nb_rxd", "") or "").strip()
    raw_payload = (row.get("payload_size", "") or "").strip()
    raw_threads = (row.get("threads", "") or "").strip()
    if raw_nb_rxd and raw_payload and raw_threads:
        return int(raw_nb_rxd) * int(raw_payload) * int(raw_threads)
    return None


def _bytes_to_mib(value_bytes: int) -> float:
    return value_bytes / BYTES_PER_MIB


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
            working_set_bytes = _pick_working_set_size(row)
            if working_set_bytes is None:
                raise RuntimeError(
                    "Missing working-set fields in CSV row. Need either working_set_size "
                    "or (nb_rxd, payload_size, threads)."
                )
            working_set_mib = _bytes_to_mib(working_set_bytes)
            throughput_data[label][working_set_mib].append(float(row["total_gbps"]))
            memory_value = _pick_memory_value(row)
            if memory_value is not None:
                memory_data[label][working_set_mib].append(memory_value)

    if not throughput_data:
        raise RuntimeError(f"No throughput rows found in {args.input}")

    all_working_sets = sorted({ws for method_data in throughput_data.values() for ws in method_data.keys()})
    throughput_series_data = []
    for label in METHOD_ORDER:
        if label not in throughput_data:
            continue
        throughput_series_data.append(
            (
                label,
                {
                    ws: sum(throughput_data[label][ws]) / len(throughput_data[label][ws])
                    for ws in throughput_data[label].keys()
                },
            )
        )

    write_line_chart_svg(
        output_path=output_a,
        title=args.throughput_title,
        x_label="Working Set Size (MiB)",
        y_label="Throughput (Gbps)",
        x_ticks=all_working_sets,
        series_data=throughput_series_data,
    )

    all_memory_working_sets = sorted({ws for method_data in memory_data.values() for ws in method_data.keys()})
    memory_series_data = []
    for label in METHOD_ORDER:
        if label not in memory_data:
            continue
        memory_series_data.append(
            (
                label,
                {
                    ws: sum(memory_data[label][ws]) / len(memory_data[label][ws])
                    for ws in memory_data[label].keys()
                },
            )
        )

    if not memory_series_data:
        raise RuntimeError(f"No memory-bandwidth rows found in {args.input}")

    write_line_chart_svg(
        output_path=output_b,
        title=args.memory_title,
        x_label="Working Set Size (MiB)",
        y_label="Arm Memory Bandwidth (MB/s)",
        x_ticks=all_memory_working_sets,
        series_data=memory_series_data,
    )
    print(f"[EXP2] throughput figure saved to {output_a}")
    print(f"[EXP2] memory figure saved to {output_b}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
