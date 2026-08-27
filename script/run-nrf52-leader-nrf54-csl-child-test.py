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
                    if "Done" in text or self._has_cli_error(text):
                        self._cmd_done.set()

    @staticmethod
    def _has_cli_error(output: str) -> bool:
        return re.search(r"Error \d+:", output) is not None

    def command(self, cmd: str, timeout: float = 15.0, allow_error: bool = False) -> str:
        with self._cmd_lock:
            self._cmd_active = True
            self._cmd_buf = []
            self._cmd_done.clear()
        line = f"{self._prefix}{cmd}\r\n"
        self._ser.write(line.encode())
        if not self._cmd_done.wait(timeout=timeout):
            with self._cmd_lock:
                self._cmd_active = False
            die(f"Timeout waiting for CLI response to '{cmd}' on {self._name}")
        with self._cmd_lock:
            text = "".join(self._cmd_buf)
            self._cmd_active = False
        if not allow_error and self._has_cli_error(text):
            die(f"CLI error for command '{cmd}' on {self._name}:\n{text}")
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
        die(f"Timed out waiting for state '{wanted}'. Last output:\n{last}")
        return last


def extract_dataset_hex(output: str) -> str:
    # dataset active -x returns a long hex string (may include whitespace).
    hex_chars = re.findall(r"[0-9a-fA-F]+", output)
    if not hex_chars:
        die(f"No dataset hex in output:\n{output}")
    # Longest chunk is the TLV hex payload.
    return max(hex_chars, key=len)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--csl-period-us",
        type=int,
        default=int(__import__("os").environ.get("CSL_PERIOD_US", "500000")),
        help="CSL period in microseconds (multiple of 160, min ~16000). Default 500000 (500 ms).",
    )
    parser.add_argument("--leader-timeout", type=float, default=120.0)
    parser.add_argument("--post-start-sleep", type=float, default=5.0)
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Do not print live serial logs (only script markers and errors).",
    )
    args = parser.parse_args()

    if args.csl_period_us <= 0 or args.csl_period_us % 160 != 0:
        die(f"CSL period must be a positive multiple of 160 us, got {args.csl_period_us}")
    if args.csl_period_us < 16000:
        die(f"CSL period {args.csl_period_us} us is below stack minimum (~16000 us)")

    devices = nrfutil_devices()
    nrf52 = pick_board(devices, "PCA10056")
    nrf54 = pick_board(devices, "PCA10156")

    print(f"nRF52840 DK: serial {nrf52.serial}, CLI {nrf52.port} @ 115200 (ot prefix)")
    print(f"nRF54L15 DK: serial {nrf54.serial}, CLI {nrf54.port} @ 1000000")

    leader = OtCli("leader", nrf52.port, 115200, prefix="ot ", log_output=not args.quiet)

    try:
        print("\n=== Leader (nRF52840): form network and become leader ===")
        for cmd in (
            "thread stop",
            "ifconfig down",
            "dataset init new",
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
        try:
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

            print(f"\nRunning for {args.post_start_sleep}s...")
            time.sleep(args.post_start_sleep)

            print("\n=== Final state ===")
            print("> state / csl (child)")
            child.command("state")
            child.command("csl")
            print("> ot state (leader)")
            leader.command("state")

            print("\n=== Shutdown ===")
            for label, cli, prefix in (
                ("Child", child, ""),
                ("Leader", leader, "ot "),
            ):
                for cmd in ("thread stop", "ifconfig down"):
                    print(f"> {prefix}{cmd} ({label})")
                    cli.command(cmd, allow_error=True)
        finally:
            child.close()

    finally:
        leader.close()

    print("\nDone.")


if __name__ == "__main__":
    main()
