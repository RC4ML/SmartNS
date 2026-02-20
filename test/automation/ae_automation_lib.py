#!/usr/bin/env python3
"""Shared helpers for AE automation scripts."""

from __future__ import annotations

import re
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


LOCAL_HOSTS = {"", "local", "localhost", "127.0.0.1"}


@dataclass(frozen=True)
class Node:
    host: str
    user: str
    ssh_key: str
    ssh_port: int
    use_sudo: bool

    def _is_local(self) -> bool:
        return self.host in LOCAL_HOSTS

    def build_cmd(self, workdir: Path, argv: List[str]) -> List[str]:
        full_argv = (["sudo", "-n"] if self.use_sudo else []) + argv
        shell_cmd = "cd {} && exec {}".format(shlex.quote(str(workdir)), shlex.join(full_argv))
        if self._is_local():
            return ["bash", "-lc", shell_cmd]

        ssh_cmd = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new"]
        ssh_cmd.extend(["-p", str(self.ssh_port)])
        if self.ssh_key:
            ssh_cmd.extend(["-i", str(Path(self.ssh_key).expanduser())])
        target = f"{self.user}@{self.host}" if self.user else self.host
        ssh_cmd.extend([target, shell_cmd])
        return ssh_cmd


def parse_int_list(raw: str) -> List[int]:
    values = [item.strip() for item in raw.split(",") if item.strip()]
    return [int(item) for item in values]


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def start_background(node: Node, workdir: Path, argv: List[str], log_path: Path) -> Tuple[subprocess.Popen, object]:
    ensure_parent(log_path)
    log_fp = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(
        node.build_cmd(workdir, argv),
        stdout=log_fp,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return proc, log_fp


def run_capture(node: Node, workdir: Path, argv: List[str], timeout: int, log_path: Path) -> Tuple[int, str]:
    ensure_parent(log_path)
    proc = subprocess.Popen(
        node.build_cmd(workdir, argv),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        output, _ = proc.communicate()
        raise RuntimeError(f"Command timeout after {timeout}s: {' '.join(argv)}")
    with open(log_path, "w", encoding="utf-8") as fp:
        fp.write(output)
    return proc.returncode, output


def start_capture(node: Node, workdir: Path, argv: List[str]) -> subprocess.Popen:
    return subprocess.Popen(
        node.build_cmd(workdir, argv),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def wait_capture(proc: subprocess.Popen, timeout: int, log_path: Path) -> Tuple[int, str]:
    ensure_parent(log_path)
    try:
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        output, _ = proc.communicate()
        raise RuntimeError(f"Command timeout after {timeout}s")
    with open(log_path, "w", encoding="utf-8") as fp:
        fp.write(output)
    return proc.returncode, output


def stop_process(proc: Optional[subprocess.Popen]) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def signal_binary(node: Node, workdir: Path, binary_name: str, signal_name: str = "TERM") -> None:
    argv = ["pkill", f"-{signal_name}", "-f", binary_name]
    subprocess.run(
        node.build_cmd(workdir, argv),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )


def wait_process(proc: Optional[subprocess.Popen], timeout: float) -> bool:
    if proc is None:
        return True
    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        return False


def graceful_stop_remote(
    node: Node,
    workdir: Path,
    binary_name: str,
    proc: Optional[subprocess.Popen],
    wait_sec: float = 5.0,
) -> None:
    signal_binary(node, workdir, binary_name, "INT")
    if not wait_process(proc, wait_sec):
        signal_binary(node, workdir, binary_name, "TERM")
        if not wait_process(proc, wait_sec):
            signal_binary(node, workdir, binary_name, "KILL")
    stop_process(proc)


def force_cleanup_binaries(node: Node, workdir: Path, binary_names: Iterable[str]) -> None:
    for binary_name in binary_names:
        cleanup_binary(node, workdir, binary_name)


def cleanup_binary(node: Node, workdir: Path, binary_name: str) -> None:
    signal_binary(node, workdir, binary_name, "TERM")


def parse_result_fields(line: str) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    for part in line.strip().split("|")[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            fields[key] = value
    return fields


def extract_metric(output: str, key: str) -> Optional[float]:
    for line in output.splitlines():
        if not line.startswith("RESULT|"):
            continue
        fields = parse_result_fields(line)
        if key in fields:
            return float(fields[key])
    return None


def extract_sum_by_regex(output: str, pattern: str) -> Optional[float]:
    regex = re.compile(pattern)
    values = [float(match.group(1)) for match in regex.finditer(output)]
    if not values:
        return None
    return sum(values)
