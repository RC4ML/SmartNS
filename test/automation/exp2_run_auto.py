#!/usr/bin/env python3
"""Automate EXP2 (Unlimited-working-set In-Cache Processing RX Path)."""

from __future__ import annotations

import argparse
import csv
import re
import time
from datetime import datetime, timezone
from pathlib import Path

from ae_automation_lib import (
    Node,
    cleanup_binary,
    ensure_parent,
    extract_metric,
    extract_sum_by_regex,
    graceful_stop_remote,
    parse_int_list,
    start_background,
    start_capture,
    wait_capture,
)


METHODS = [
    ("rx_rdma_assisted", "RDMA-assisted RX"),
    ("rx_dma_assisted", "DMA-assisted RX"),
    ("rx_unlimited_working_set_in_cache", "Unlimited-working-set In-Cache RX"),
]

MEMORY_SAMPLE_REGEX = re.compile(
    r"Read:\s*([0-9]+(?:\.[0-9]+)?)\s*MB(?:/s)?\s*Write:\s*([0-9]+(?:\.[0-9]+)?)\s*MB(?:/s)?"
)


def parse_args() -> argparse.Namespace:
    default_root = Path(__file__).resolve().parent.parent.parent
    parser = argparse.ArgumentParser(description="Run EXP2 automatically across payload sizes and methods.")
    parser.add_argument("--workdir", type=Path, default=default_root, help="SmartNS root path shared by all machines.")
    parser.add_argument("--output", type=Path, default=Path("test/results/exp2_results.csv"), help="CSV output path.")
    parser.add_argument("--log-dir", type=Path, default=Path("test/results/exp2_logs"), help="Directory for run logs.")
    parser.add_argument("--payload-sizes", default="512,1024,2048,4096,8192")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--batch-size", type=int, default=1)
    parser.add_argument("--outstanding", type=int, default=32)
    parser.add_argument("--port-base", type=int, default=41000)
    parser.add_argument("--startup-delay", type=float, default=3.0)
    parser.add_argument("--cleanup-delay", type=float, default=1.0)
    parser.add_argument("--stop-timeout", type=float, default=6.0, help="Wait time (seconds) after sending stop signal.")
    parser.add_argument("--timeout", type=int, default=600, help="Timeout for the relay process in seconds.")
    parser.add_argument("--non-key-auto-exit-sec", type=int, default=360, help="Auto exit timeout for non-key roles.")
    parser.add_argument("--server-ip", default="10.0.0.201")
    parser.add_argument("--bf1", default="10.130.142.40")
    parser.add_argument("--bf2", default="10.130.142.41")
    parser.add_argument("--host2", default="10.130.142.27")
    parser.add_argument("--ssh-port", type=int, default=22)
    parser.add_argument("--ssh-user", default="eurosys26")
    parser.add_argument("--ssh-key", default="eurosys26_id_ed25519")
    parser.add_argument("--use-sudo", action="store_true", default=True)
    parser.add_argument("--no-sudo", dest="use_sudo", action="store_false")
    parser.add_argument("--host-device", default="mlx5_0")
    parser.add_argument("--bf-device", default="mlx5_2")

    parser.add_argument("--collect-memory", action="store_true", default=True, help="Collect Arm memory bandwidth during each run.")
    parser.add_argument("--no-collect-memory", dest="collect_memory", action="store_false")
    parser.add_argument("--memory-script", default="scripts/bf3_memory_bw.py", help="Relative path to Arm memory monitor script.")
    parser.add_argument("--memory-start-delay", type=float, default=2.0, help="Seconds to wait after starting memory monitor.")

    parser.add_argument("--memory-active-threshold-ratio", type=float, default=0.20, help="Active sample threshold ratio to peak total BW.")
    parser.add_argument(
        "--memory-active-threshold-min-mb",
        type=float,
        default=128.0,
        help="Minimum active threshold for total(read+write) bandwidth in MB/s.",
    )
    parser.add_argument("--memory-active-min-samples", type=int, default=2, help="Minimum number of active samples retained.")
    parser.add_argument("--memory-use-all-samples", action="store_true", help="Disable active-window filtering and use all samples.")
    return parser.parse_args()


def _pick_active_sample_indices(
    totals: list[float],
    use_all_samples: bool,
    threshold_ratio: float,
    threshold_min_mb: float,
    min_samples: int,
) -> tuple[list[int], float, float]:
    if not totals:
        raise RuntimeError("No memory samples found.")

    peak_total = max(totals)
    if use_all_samples:
        return list(range(len(totals))), 0.0, peak_total

    threshold = max(threshold_min_mb, peak_total * threshold_ratio)
    active_indices = [idx for idx, value in enumerate(totals) if value >= threshold]

    if len(active_indices) < min_samples:
        keep = min(min_samples, len(totals))
        active_indices = sorted(sorted(range(len(totals)), key=lambda idx: totals[idx], reverse=True)[:keep])

    return active_indices, threshold, peak_total


def parse_memory_log(
    log_path: Path,
    use_all_samples: bool,
    threshold_ratio: float,
    threshold_min_mb: float,
    min_samples: int,
) -> tuple[float, float, float, float, float, float, int, int, float, float]:
    if not log_path.exists():
        raise FileNotFoundError(f"Memory log not found: {log_path}")

    matches = MEMORY_SAMPLE_REGEX.findall(log_path.read_text(encoding="utf-8"))
    if not matches:
        raise RuntimeError(f"No memory samples found in {log_path}")

    read_values = [float(read_mb) for read_mb, _ in matches]
    write_values = [float(write_mb) for _, write_mb in matches]
    total_values = [read_values[i] + write_values[i] for i in range(len(read_values))]

    sample_count = len(total_values)
    avg_read = sum(read_values) / sample_count
    avg_write = sum(write_values) / sample_count
    avg_total = avg_read + avg_write

    active_indices, active_threshold, peak_total = _pick_active_sample_indices(
        totals=total_values,
        use_all_samples=use_all_samples,
        threshold_ratio=threshold_ratio,
        threshold_min_mb=threshold_min_mb,
        min_samples=min_samples,
    )
    active_count = len(active_indices)
    active_avg_read = sum(read_values[idx] for idx in active_indices) / active_count
    active_avg_write = sum(write_values[idx] for idx in active_indices) / active_count
    active_avg_total = active_avg_read + active_avg_write

    return (
        avg_read,
        avg_write,
        avg_total,
        active_avg_read,
        active_avg_write,
        active_avg_total,
        sample_count,
        active_count,
        active_threshold,
        peak_total,
    )


def main() -> int:
    args = parse_args()
    payload_sizes = parse_int_list(args.payload_sizes)
    ssh_key = str(Path(args.ssh_key).expanduser())

    bf1 = Node(args.bf1, args.ssh_user, ssh_key, args.ssh_port, args.use_sudo)
    bf2 = Node(args.bf2, args.ssh_user, ssh_key, args.ssh_port, args.use_sudo)
    host2 = Node(args.host2, args.ssh_user, ssh_key, args.ssh_port, args.use_sudo)

    ensure_parent(args.output)
    args.log_dir.mkdir(parents=True, exist_ok=True)

    run_index = 0
    with open(args.output, "w", newline="", encoding="utf-8") as csv_fp:
        writer = csv.writer(csv_fp)
        writer.writerow(
            [
                "timestamp_utc",
                "method",
                "method_label",
                "payload_size",
                "threads",
                "total_gbps",
                "memory_avg_read_mb",
                "memory_avg_write_mb",
                "memory_avg_total_mb",
                "memory_active_avg_read_mb",
                "memory_active_avg_write_mb",
                "memory_active_avg_total_mb",
                "memory_samples",
                "memory_active_samples",
                "memory_active_threshold_mb",
                "memory_peak_total_mb",
                "relay_log",
                "server_log",
                "client_log",
                "memory_log",
            ]
        )

        for method, method_label in METHODS:
            for payload_size in payload_sizes:
                port = args.port_base + run_index
                run_tag = f"{method}_payload{payload_size}_run{run_index}"
                relay_log = args.log_dir / f"{run_tag}_bf2_relay.log"
                server_log = args.log_dir / f"{run_tag}_host2_server.log"
                client_log = args.log_dir / f"{run_tag}_bf1_client.log"
                memory_log = args.log_dir / f"{run_tag}_bf2_memory.log"
                run_index += 1

                common_flags = [
                    "-deviceName",
                    args.bf_device,
                    "-batch_size",
                    str(args.batch_size),
                    "-outstanding",
                    str(args.outstanding),
                    "-threads",
                    str(args.threads),
                    "-payload_size",
                    str(payload_size),
                    "-port",
                    str(port),
                ]

                relay_cmd = [f"./build_dpu/{method}"] + common_flags + [
                    "-nodeType",
                    "1",
                    "-iterations",
                    str(args.iterations),
                ]
                server_cmd = [
                    f"./build_host/{method}",
                    "-deviceName",
                    args.host_device,
                    "-batch_size",
                    str(args.batch_size),
                    "-outstanding",
                    str(args.outstanding),
                    "-threads",
                    str(args.threads),
                    "-payload_size",
                    str(payload_size),
                    "-port",
                    str(port),
                    "-nodeType",
                    "2",
                    "-serverIp",
                    args.server_ip,
                    "-auto_exit_sec",
                    str(args.non_key_auto_exit_sec),
                ]
                client_cmd = [f"./build_dpu/{method}"] + common_flags + [
                    "-nodeType",
                    "0",
                    "-auto_exit_sec",
                    str(args.non_key_auto_exit_sec),
                ]

                print(f"[EXP2] method={method} payload={payload_size} threads={args.threads} port={port}")
                relay_proc = None
                server_proc = None
                client_proc = None
                memory_proc = None
                server_log_fp = None
                client_log_fp = None
                memory_log_fp = None
                total_gbps = None

                try:
                    if args.collect_memory:
                        memory_proc, memory_log_fp = start_background(
                            bf2,
                            args.workdir,
                            ["python3", args.memory_script],
                            memory_log,
                        )
                        time.sleep(args.memory_start_delay)

                    relay_proc = start_capture(bf2, args.workdir, relay_cmd)
                    time.sleep(args.startup_delay)

                    server_proc, server_log_fp = start_background(host2, args.workdir, server_cmd, server_log)
                    time.sleep(args.startup_delay)

                    client_proc, client_log_fp = start_background(bf1, args.workdir, client_cmd, client_log)

                    relay_rc, relay_output = wait_capture(relay_proc, args.timeout, relay_log)
                    if relay_rc != 0:
                        raise RuntimeError(f"Relay exited with code {relay_rc}. Check {relay_log}.")

                    total_gbps = extract_metric(relay_output, "total_gbps")
                    if total_gbps is None:
                        total_gbps = extract_sum_by_regex(relay_output, r"recv speed \[([0-9]+(?:\.[0-9]+)?)\] Gbps")
                    if total_gbps is None:
                        raise RuntimeError(f"Cannot parse throughput from relay output. Check {relay_log}.")
                finally:
                    graceful_stop_remote(bf1, args.workdir, method, client_proc, args.stop_timeout)
                    graceful_stop_remote(host2, args.workdir, method, server_proc, args.stop_timeout)
                    if args.collect_memory:
                        graceful_stop_remote(bf2, args.workdir, Path(args.memory_script).name, memory_proc, args.stop_timeout)
                    cleanup_binary(bf1, args.workdir, method)
                    cleanup_binary(bf2, args.workdir, method)
                    cleanup_binary(host2, args.workdir, method)
                    if server_log_fp is not None:
                        server_log_fp.close()
                    if client_log_fp is not None:
                        client_log_fp.close()
                    if memory_log_fp is not None:
                        memory_log_fp.close()
                    time.sleep(args.cleanup_delay)

                if total_gbps is None:
                    raise RuntimeError(f"Failed to parse throughput for {run_tag}")

                memory_avg_read = ""
                memory_avg_write = ""
                memory_avg_total = ""
                memory_active_avg_read = ""
                memory_active_avg_write = ""
                memory_active_avg_total = ""
                memory_samples = ""
                memory_active_samples = ""
                memory_active_threshold = ""
                memory_peak_total = ""
                memory_log_out = ""
                if args.collect_memory:
                    (
                        avg_read,
                        avg_write,
                        avg_total,
                        active_avg_read,
                        active_avg_write,
                        active_avg_total,
                        sample_count,
                        active_count,
                        active_threshold,
                        peak_total,
                    ) = parse_memory_log(
                        memory_log,
                        use_all_samples=args.memory_use_all_samples,
                        threshold_ratio=args.memory_active_threshold_ratio,
                        threshold_min_mb=args.memory_active_threshold_min_mb,
                        min_samples=args.memory_active_min_samples,
                    )
                    memory_avg_read = f"{avg_read:.6f}"
                    memory_avg_write = f"{avg_write:.6f}"
                    memory_avg_total = f"{avg_total:.6f}"
                    memory_active_avg_read = f"{active_avg_read:.6f}"
                    memory_active_avg_write = f"{active_avg_write:.6f}"
                    memory_active_avg_total = f"{active_avg_total:.6f}"
                    memory_samples = str(sample_count)
                    memory_active_samples = str(active_count)
                    memory_active_threshold = f"{active_threshold:.6f}"
                    memory_peak_total = f"{peak_total:.6f}"
                    memory_log_out = str(memory_log)
                    print(
                        f"[EXP2] memory method={method} payload={payload_size} "
                        f"active_avg_total={active_avg_total:.2f}MB/s "
                        f"active_samples={active_count}/{sample_count} "
                        f"threshold={active_threshold:.2f}MB/s peak={peak_total:.2f}MB/s"
                    )

                writer.writerow(
                    [
                        datetime.now(timezone.utc).isoformat(),
                        method,
                        method_label,
                        payload_size,
                        args.threads,
                        f"{total_gbps:.6f}",
                        memory_avg_read,
                        memory_avg_write,
                        memory_avg_total,
                        memory_active_avg_read,
                        memory_active_avg_write,
                        memory_active_avg_total,
                        memory_samples,
                        memory_active_samples,
                        memory_active_threshold,
                        memory_peak_total,
                        str(relay_log),
                        str(server_log),
                        str(client_log),
                        memory_log_out,
                    ]
                )
                csv_fp.flush()
                print(f"[EXP2] done method={method} payload={payload_size} total_gbps={total_gbps:.3f}")

    print(f"[EXP2] results saved to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
