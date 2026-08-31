#!/usr/bin/env python3
#
#  Copyright (c) 2020, The OpenThread Authors.
#  SPDX-License-Identifier: BSD-3-Clause
#
"""OpenThread CLI automation: nRF52840 leader + nRF54L15 CSL child."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass

import serial


@dataclass
class BoardSerial:
    serial: str
    port: str
    board: str


def die(message: str) -> None:
    print(f" ** ERROR: {message}", file=sys.stderr)
    sys.exit(1)


class TestFailure(Exception):
    """Test step failed; shutdown should still run before exit."""


def fail(message: str) -> None:
    print(f" ** ERROR: {message}", file=sys.stderr)
    raise TestFailure(message)


def nrfutil_devices() -> list[dict]:
    raw = subprocess.check_output(
        ["nrfutil", "device", "list", "--json"], text=True, stderr=subprocess.DEVNULL
    )
    for line in raw.splitlines():
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if obj.get("type") != "info":
            continue
        devices = obj.get("data", {}).get("devices")
        if devices is not None:
            return devices
    die("Could not parse nrfutil device list JSON")
    return []


def pick_board(devices: list[dict], board_version: str) -> BoardSerial:
    for dev in devices:
        devkit = dev.get("devkit") or {}
        if devkit.get("boardVersion") != board_version:
            continue
        ports = dev.get("serialPorts") or []
        if not ports:
            die(f"No serial ports for {board_version}")
        # Prefer highest vcom index (OT CLI on nRF54L15 DK is usually vcom 1).
        port = sorted(ports, key=lambda p: p.get("vcom", 0))[-1]["path"]
        sn = dev.get("serialNumber", "")
        sn_short = sn.lstrip("0") or sn
        return BoardSerial(serial=sn_short, port=port, board=board_version)
    die(f"No connected device with board {board_version}")
    return BoardSerial("", "", "")


class OtCli:
    def __init__(
        self,
        name: str,
        port: str,
        baud: int,
        prefix: str,
        rtscts: bool = False,
        log_output: bool = True,
    ) -> None:
        self._name = name
        self._prefix = prefix
        self._log_output = log_output
        self._ser = serial.Serial(port, baud, rtscts=rtscts, timeout=0.2)
        self._stop = threading.Event()
        self._cmd_active = False
        self._cmd_buf: list[str] = []
        self._cmd_done = threading.Event()
        self._cmd_lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_loop, name=f"serial-{name}", daemon=True)
        time.sleep(0.5)
        self._ser.reset_input_buffer()
        self._wake()
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        self._reader.join(timeout=2.0)
        self._ser.close()

    def _wake(self) -> None:
        self._ser.write(b"\r\n")
        time.sleep(0.2)

    @staticmethod
    def _is_ot_cli_done(output: str) -> bool:
        """True when OpenThread CLI finished a command (Done line or Error N:)."""
        if OtCli._has_cli_error(output):
            return True
        # Done as its own line; allow CR/LF quirks from UART.
        if re.search(r"(?:^|\r?\n)Done(?:\r?\n|\r|$)", output):
            return True
        # Chunk may end mid-line before trailing newline arrives.
        return output.rstrip("\r\n").endswith("Done")

    @staticmethod
    def _is_handoff_stats_complete(output: str) -> bool:
        return "nrf54_handoff_end=1" in output

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(4096)
            except serial.SerialException:
                break
            if not chunk:
                continue
            text = chunk.decode("utf-8", errors="replace")
            if self._log_output:
                sys.stdout.write(f"[{self._name}] {text}")
                sys.stdout.flush()
            with self._cmd_lock:
                if self._cmd_active:
                    self._cmd_buf.append(text)
                    joined = "".join(self._cmd_buf)
                    if (
                        "Done" in text
                        or self._is_ot_cli_done(joined)
                        or self._is_handoff_stats_complete(joined)
                    ):
                        self._cmd_done.set()

    @staticmethod
    def _has_cli_error(output: str) -> bool:
        return re.search(r"Error \d+:", output) is not None

    def command(self, cmd: str, timeout: float = 15.0, allow_error: bool = False) -> str:
        with self._cmd_lock:
            self._cmd_active = True
            self._cmd_buf = []
            self._cmd_done.clear()
        # Drop stale RX so a prior Done/prompt cannot complete the wrong command.
        self._ser.reset_input_buffer()
        line = f"{self._prefix}{cmd}\r\n"
        self._ser.write(line.encode())
        self._ser.flush()
        if not self._cmd_done.wait(timeout=timeout):
            with self._cmd_lock:
                partial = "".join(self._cmd_buf)
                self._cmd_active = False
            hint = partial[-200:] if partial else "(no bytes captured)"
            fail(
                f"Timeout waiting for CLI response to '{cmd}' on {self._name} "
                f"(last RX: {hint!r})"
            )
        with self._cmd_lock:
            text = "".join(self._cmd_buf)
            self._cmd_active = False
            self._cmd_done.clear()
        if not allow_error and self._has_cli_error(text):
            fail(f"CLI error for command '{cmd}' on {self._name}:\n{text}")
        return text

    def wait_state(self, wanted: str, timeout: float) -> str:
        deadline = time.time() + timeout
        last = ""
        while time.time() < deadline:
            out = self.command("state", timeout=5)
            last = out
            if re.search(rf"(^|\n){re.escape(wanted)}(\n|$)", out, re.IGNORECASE):
                return out
            if re.search(rf"(^|\n){wanted}(\n|$)", out):
                return out
            # Response body is often "...> state\nleader\nDone"
            m = re.search(r"state\r?\n([a-z]+)", out, re.IGNORECASE)
            if m and m.group(1).lower() == wanted.lower():
                return out
            time.sleep(0.5)
        fail(f"Timed out waiting for state '{wanted}'. Last output:\n{last}")
        return last

    @staticmethod
    def parse_thread_state(output: str) -> str | None:
        m = re.search(r"state\r?\n([a-z]+)", output, re.IGNORECASE)
        if m:
            return m.group(1).lower()
        for line in output.splitlines():
            token = line.strip().lower()
            if token in ("disabled", "detached", "child", "router", "leader"):
                return token
        return None

    def factory_reset(self, boot_sleep: float = 3.0, *, strict: bool = True) -> None:
        """Wipe OpenThread NV settings (for clean state on the next test run)."""
        self.command("thread stop", allow_error=True, timeout=10)
        self.command("ifconfig down", allow_error=True, timeout=10)
        print(f"> {self._prefix}factoryreset")
        with self._cmd_lock:
            self._cmd_active = False
        self._ser.write(f"{self._prefix}factoryreset\r\n".encode())
        self._ser.flush()
        time.sleep(boot_sleep)
        self._ser.reset_input_buffer()
        self._wake()

        deadline = time.time() + 20.0
        last = ""
        while time.time() < deadline:
            try:
                out = self.command("state", timeout=5, allow_error=True)
            except TestFailure:
                time.sleep(0.5)
                self._wake()
                continue
            last = out
            state = self.parse_thread_state(out)
            if state in ("disabled", "detached"):
                print(f"  {self._name}: settings cleared (state={state}).")
                return
            time.sleep(0.5)
            self._wake()

        message = f"{self._name} not quiescent after factoryreset. Last output:\n{last}"
        if strict:
            fail(message)
        print(f" ** WARNING: {message}", file=sys.stderr)


def extract_dataset_hex(output: str) -> str:
    # dataset active -x returns a long hex string (may include whitespace).
    hex_chars = re.findall(r"[0-9a-fA-F]+", output)
    if not hex_chars:
        fail(f"No dataset hex in output:\n{output}")
    # Longest chunk is the TLV hex payload.
    return max(hex_chars, key=len)


_IPV6_LINE = re.compile(r"^[0-9a-fA-F:]+$")


def extract_mleid(output: str) -> str:
    for line in output.splitlines():
        candidate = line.strip()
        if _IPV6_LINE.fullmatch(candidate) and candidate.count(":") >= 2:
            return candidate
    fail(f"No mesh-local IPv6 (MLEID) in output:\n{output}")
    return ""


def extract_rloc16(output: str) -> str:
    m = re.search(r"0x([0-9a-fA-F]{4})\b", output)
    if m:
        return m.group(1).lower()
    for line in output.splitlines():
        token = line.strip().lower()
        if re.fullmatch(r"[0-9a-f]{4}", token):
            return token
    fail(f"No RLOC16 in output:\n{output}")
    return ""


_CSL_SYNC_OK = re.compile(r"CSL Synchronized:\s*1\b", re.IGNORECASE)


def leader_reports_csl_synchronized(leader: OtCli, rloc16: str) -> tuple[bool, str]:
    rloc = rloc16.lower().removeprefix("0x")
    detail = leader.command(f"child 0x{rloc}", timeout=10, allow_error=True)
    return _CSL_SYNC_OK.search(detail) is not None, detail


def wait_child_update_complete(child: OtCli, leader: OtCli, timeout: float) -> str:
    """
    Wait until the leader lists the child as CSL-synchronized (MLE Child Update done).

    Fails if the child leaves the 'child' role before that (typical Child Update failure).
    """
    deadline = time.time() + timeout
    poll_interval = 0.5
    seen_child_role = False
    last_child_state = "unknown"
    last_leader_child = ""

    print(
        "  Waiting for leader 'child' entry with CSL Synchronized: 1 "
        f"(timeout {timeout}s)..."
    )

    while time.time() < deadline:
        state_out = child.command("state", timeout=5)
        state = OtCli.parse_thread_state(state_out)
        last_child_state = state or "unknown"

        if state == "child":
            seen_child_role = True
            rloc = extract_rloc16(child.command("rloc16", timeout=5))
            synced, last_leader_child = leader_reports_csl_synchronized(leader, rloc)
            if synced:
                print(f"  Child Update OK: RLOC 0x{rloc}, CSL synchronized on leader.")
                return rloc
        elif seen_child_role:
            fail(
                "Child Update failed: child left 'child' role "
                f"(now '{last_child_state}') before leader reported "
                "CSL Synchronized: 1.\n"
                f"Last leader 'child' output:\n{last_leader_child}"
            )

        time.sleep(poll_interval)

    fail(
        "Timed out waiting for Child Update (leader CSL Synchronized: 1). "
        f"Last child state: {last_child_state}\n"
        f"Last leader 'child' output:\n{last_leader_child}"
    )
    return ""


def assert_ping_success(output: str) -> None:
    stats = re.search(
        r"(\d+) packets transmitted, (\d+) packets received",
        output,
    )
    if stats:
        received = int(stats.group(2))
        if received < 1:
            fail(f"Ping failed (0 replies):\n{output}")
        return
    if re.search(r"\d+ bytes from [0-9a-fA-F:]+: icmp_seq=", output):
        return
    fail(f"Unexpected ping output:\n{output}")


def dump_child_nrf54_stats(child: OtCli) -> None:
    """Dump g_nrf54_debug_stats on the nRF54 child (UART + RTT). Uses diag nrf54stats."""
    print("\n=== Child: nrf54 debug stats ===")
    diag_started = False
    try:
        print("> diag start")
        child.command("diag start", allow_error=True, timeout=10)
        diag_started = True
        print("> diag nrf54stats handoff")
        try:
            # Handoff counters only, one UART transaction per line. Full: diag nrf54stats full
            child.command("diag nrf54stats handoff", allow_error=True, timeout=60.0)
        except TestFailure as exc:
            print(f" ** WARNING: 'diag nrf54stats' failed: {exc}", file=sys.stderr)
            print(
                " ** Hint: reflash child with ./script/flash-nrf54l15-cli-ftd-csl-debug",
                file=sys.stderr,
            )
    except TestFailure as exc:
        print(f" ** WARNING: diag start failed: {exc}", file=sys.stderr)
    finally:
        if diag_started:
            print("> diag stop")
            try:
                child.command("diag stop", allow_error=True, timeout=10)
            except TestFailure as exc:
                print(f" ** WARNING: diag stop failed: {exc}", file=sys.stderr)


def shutdown_boards(child: OtCli | None, leader: OtCli | None) -> None:
    if child is None and leader is None:
        return
    print("\n=== Shutdown ===")
    for label, cli, prefix in (
        ("Child", child, ""),
        ("Leader", leader, "ot "),
    ):
        if cli is None:
            continue
        for cmd in ("thread stop", "ifconfig down"):
            print(f"> {prefix}{cmd} ({label})")
            try:
                cli.command(cmd, allow_error=True, timeout=10)
            except TestFailure as exc:
                print(f" ** WARNING: shutdown '{cmd}' on {label} failed: {exc}", file=sys.stderr)

    if child is not None:
        dump_child_nrf54_stats(child)
        print("\n=== Child: factoryreset (clean state for next run) ===")
        try:
            child.factory_reset(strict=False)
        except TestFailure as exc:
            print(f" ** WARNING: child factoryreset failed: {exc}", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--channel",
        type=int,
        default=int(__import__("os").environ.get("THREAD_CHANNEL", "23")),
        help="Thread operational channel (11–26). Default 23.",
    )
    parser.add_argument(
        "--csl-period-us",
        type=int,
        default=int(__import__("os").environ.get("CSL_PERIOD_US", "500000")),
        help="CSL period in microseconds (multiple of 160, min ~16000). Default 500000 (500 ms).",
    )
    parser.add_argument("--leader-timeout", type=float, default=120.0)
    parser.add_argument(
        "--child-attach-timeout",
        type=float,
        default=float(__import__("os").environ.get("CHILD_ATTACH_TIMEOUT", "120")),
        help="Max seconds to wait for MLE Child Update / CSL sync on leader (default: 120).",
    )
    parser.add_argument(
        "--ping-count",
        type=int,
        default=1,
        help="ICMPv6 echo requests from leader to child MLEID (default: 1).",
    )
    parser.add_argument(
        "--ping-timeout",
        type=float,
        default=15.0,
        help="Per-ping session timeout passed to OpenThread CLI (seconds).",
    )
    parser.add_argument("--post-start-sleep", type=float, default=5.0)
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Do not print live serial logs (only script markers and errors).",
    )
    args = parser.parse_args()

    if not 11 <= args.channel <= 26:
        die(f"Thread channel must be 11–26, got {args.channel}")
    if args.csl_period_us <= 0 or args.csl_period_us % 160 != 0:
        die(f"CSL period must be a positive multiple of 160 us, got {args.csl_period_us}")
    if args.csl_period_us < 16000:
        die(f"CSL period {args.csl_period_us} us is below stack minimum (~16000 us)")
    if args.ping_count < 1:
        die(f"ping-count must be >= 1, got {args.ping_count}")
    if args.ping_timeout <= 0:
        die(f"ping-timeout must be positive, got {args.ping_timeout}")
    if args.child_attach_timeout <= 0:
        die(f"child-attach-timeout must be positive, got {args.child_attach_timeout}")

    leader: OtCli | None = None
    child: OtCli | None = None
    exit_code = 0

    try:
        devices = nrfutil_devices()
        nrf52 = pick_board(devices, "PCA10056")
        nrf54 = pick_board(devices, "PCA10156")

        print(f"nRF52840 DK: serial {nrf52.serial}, CLI {nrf52.port} @ 115200 (ot prefix)")
        print(f"nRF54L15 DK: serial {nrf54.serial}, CLI {nrf54.port} @ 1000000")

        leader = OtCli("leader", nrf52.port, 115200, prefix="ot ", log_output=not args.quiet)

        print(f"\n=== Leader (nRF52840): form network on channel {args.channel} ===")
        for cmd in (
            "thread stop",
            "ifconfig down",
            "dataset init new",
            f"dataset channel {args.channel}",
            "dataset commit active",
            "ifconfig up",
            "thread start",
        ):
            print(f"> ot {cmd}")
            leader.command(cmd, allow_error=cmd == "thread stop")

        print(f"\nWaiting for leader (timeout {args.leader_timeout}s)...")
        leader.wait_state("leader", args.leader_timeout)
        print("Leader role confirmed.")

        print("\n=== Export active dataset ===")
        ds_out = leader.command("dataset active -x", timeout=20)
        dataset_hex = extract_dataset_hex(ds_out)
        print(f"Dataset length: {len(dataset_hex)} hex chars")

        print("\n=== Child (nRF54L15): join with CSL ===")
        child = OtCli("child", nrf54.port, 1000000, prefix="", rtscts=False, log_output=not args.quiet)

        for cmd in (
            "thread stop",
            "ifconfig down",
            f"dataset set active {dataset_hex}",
            "ifconfig up",
            "mode -",
            f"csl period {args.csl_period_us}",
            "thread start",
        ):
            print(f"> {cmd}" if not cmd.startswith("dataset set") else "> dataset set active <...>")
            allow_err = cmd in ("thread stop", "ifconfig down")
            child.command(
                cmd,
                timeout=30 if cmd.startswith("dataset set") else 15,
                allow_error=allow_err,
            )

        print(f"\nWaiting for Child Update (timeout {args.child_attach_timeout}s)...")
        child_rloc = wait_child_update_complete(child, leader, args.child_attach_timeout)

        mleid_out = child.command("ipaddr mleid", timeout=10)
        child_mleid = extract_mleid(mleid_out)
        print(f"Child RLOC 0x{child_rloc}, MLEID {child_mleid}")

        ping_cmd = f"ping {child_mleid} 16 {args.ping_count} 1 64 {args.ping_timeout}"
        print(f"\n=== Leader ping child ({args.ping_count} echo request(s)) ===")
        print(f"> ot {ping_cmd}")
        ping_out = leader.command(
            ping_cmd,
            timeout=args.ping_timeout * args.ping_count + 30,
        )
        assert_ping_success(ping_out)
        print("Ping succeeded.")

        print(f"\nRunning for {args.post_start_sleep}s...")
        time.sleep(args.post_start_sleep)

        print("\n=== Final state ===")
        print("> state / csl (child)")
        child.command("state")
        child.command("csl")
        print("> ot state (leader)")
        leader.command("state")

    except TestFailure:
        exit_code = 1
    finally:
        shutdown_boards(child, leader)
        if child is not None:
            child.close()
        if leader is not None:
            leader.close()

    if exit_code == 0:
        print("\nDone.")
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
