#!/usr/bin/env python3
"""Automate EXP4 (Solar application) across threads and type."""

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
    graceful_stop_remote,
    parse_int_list,
    run_capture,
    start_background,
)


TYPE_LABELS = {
    0: "CPU-only",
    1: "CPU+CRC offload",
    2: "CPU+CRC offload+DSA",
}


def parse_args() -> argparse.Namespace:
    default_root = Path(__file__).resolve().parent.parent.parent
    parser = argparse.ArgumentParser(description="Run EXP4 automatically for all threads and type combinations.")
    parser.add_argument("--workdir", type=Path, default=default_root, help="SmartNS root path shared by all machines.")
    parser.add_argument("--output", type=Path, default=Path("test/results/exp4_results.csv"), help="CSV output path.")
    parser.add_argument("--log-dir", type=Path, default=Path("test/results/exp4_logs"), help="Directory for run logs.")
    parser.add_argument("--thread-start", type=int, default=1)
    parser.add_argument("--thread-end", type=int, default=12)
    parser.add_argument("--types", default="0,1,2")
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--num-pack", type=int, default=51200)
    parser.add_argument("--payload-size", type=int, default=4096)
    parser.add_argument("--port-base", type=int, default=42000)
    parser.add_argument("--startup-delay", type=float, default=3.0)
    parser.add_argument("--cleanup-delay", type=float, default=1.0)
    parser.add_argument("--stop-timeout", type=float, default=6.0, help="Wait time (seconds) after sending stop signal.")
    parser.add_argument("--timeout", type=int, default=600, help="Timeout for the client process in seconds.")
    parser.add_argument("--server-ip", default="10.0.0.200")
    parser.add_argument("--host1", default="10.130.142.26")
    parser.add_argument("--host2", default="10.130.142.27")
    parser.add_argument("--ssh-port", type=int, default=22)
    parser.add_argument("--ssh-user", default="eurosys26")
    parser.add_argument("--ssh-key", default="eurosys26_id_ed25519")
    parser.add_argument("--use-sudo", action="store_true", default=True)
    parser.add_argument("--no-sudo", dest="use_sudo", action="store_false")
    parser.add_argument("--host-device", default="mlx5_0")
    return parser.parse_args()


def parse_mops(output: str) -> float:
    metric = extract_metric(output, "total_mops")
    if metric is not None:
        return metric

    match = re.search(r"Total Speed:\s*([0-9]+(?:\.[0-9]+)?)\s*Mops", output)
    if match is None:
        raise RuntimeError("Cannot parse Mops from client output.")
    return float(match.group(1))


def main() -> int:
    args = parse_args()
    type_values = parse_int_list(args.types)
    thread_values = list(range(args.thread_start, args.thread_end + 1))
    ssh_key = str(Path(args.ssh_key).expanduser())

    host1 = Node(args.host1, args.ssh_user, ssh_key, args.ssh_port, args.use_sudo)
    host2 = Node(args.host2, args.ssh_user, ssh_key, args.ssh_port, args.use_sudo)

    ensure_parent(args.output)
    args.log_dir.mkdir(parents=True, exist_ok=True)

    run_index = 0
    with open(args.output, "w", newline="", encoding="utf-8") as csv_fp:
        writer = csv.writer(csv_fp)
        writer.writerow(
            [
                "timestamp_utc",
                "type",
                "type_label",
                "threads",
                "payload_size",
                "total_mops",
                "client_log",
                "server_log",
            ]
        )

        for test_type in type_values:
            if test_type not in TYPE_LABELS:
                raise ValueError(f"Unsupported type: {test_type}")

            for threads in thread_values:
                port = args.port_base + run_index
                run_tag = f"type{test_type}_threads{threads}_run{run_index}"
                server_log = args.log_dir / f"{run_tag}_host2_server.log"
                client_log = args.log_dir / f"{run_tag}_host1_client.log"
                run_index += 1

                server_cmd = [
                    "./build_host/solar_bench",
                    "-deviceName",
                    args.host_device,
                    "-is_server",
                    "-iterations",
                    str(args.iterations),
                    "-numPack",
                    str(args.num_pack),
                    "-payload_size",
                    str(args.payload_size),
                    "-threads",
                    str(threads),
                    "-type",
                    str(test_type),
                    "-port",
                    str(port),
                ]
                client_cmd = [
                    "./build_host/solar_bench",
                    "-deviceName",
                    args.host_device,
                    "-serverIp",
                    args.server_ip,
                    "-iterations",
                    str(args.iterations),
                    "-numPack",
                    str(args.num_pack),
                    "-payload_size",
                    str(args.payload_size),
                    "-threads",
                    str(threads),
                    "-type",
                    str(test_type),
                    "-port",
                    str(port),
                ]

                print(f"[EXP4] type={test_type} threads={threads} port={port}")
                server_proc = None
                server_log_fp = None
                try:
                    server_proc, server_log_fp = start_background(host2, args.workdir, server_cmd, server_log)
                    time.sleep(args.startup_delay)

                    client_rc, client_output = run_capture(host1, args.workdir, client_cmd, args.timeout, client_log)
                    if client_rc != 0:
                        raise RuntimeError(f"Client exited with code {client_rc}. Check {client_log}.")

                    total_mops = parse_mops(client_output)

                    writer.writerow(
                        [
                            datetime.now(timezone.utc).isoformat(),
                            test_type,
                            TYPE_LABELS[test_type],
                            threads,
                            args.payload_size,
                            f"{total_mops:.6f}",
                            str(client_log),
                            str(server_log),
                        ]
                    )
                    csv_fp.flush()
                    print(f"[EXP4] done type={test_type} threads={threads} total_mops={total_mops:.3f}")
                finally:
                    graceful_stop_remote(host2, args.workdir, "solar_bench", server_proc, args.stop_timeout)
                    cleanup_binary(host1, args.workdir, "solar_bench")
                    cleanup_binary(host2, args.workdir, "solar_bench")
                    if server_log_fp is not None:
                        server_log_fp.close()
                    time.sleep(args.cleanup_delay)

    print(f"[EXP4] results saved to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
