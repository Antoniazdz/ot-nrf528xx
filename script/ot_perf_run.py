#!/usr/bin/env python3
"""Drive the `perf` CLI command on two boards and report UDP throughput.

The PC only orchestrates: it is not part of the Thread network. Both boards run ot-cli-ftd, one
as the receiver and one as the sender, and the traffic never leaves the mesh.

    ./script/ot_perf_run.py --server /dev/ttyACM0 --client /dev/ttyACM1 \
        --size 1232 --count 2000 --interval 0

Add --server-prefix ot (and/or --client-prefix ot) for a board running the NCS CLI sample, where
the OpenThread interpreter sits behind the Zephyr shell's `ot` command.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")

PROMPT_DONE = re.compile(r"^(Done|Error \d+: .*)$", re.MULTILINE)
REPORT = re.compile(r"^perf: (?P<role>server|client) (?P<fields>.*)$", re.MULTILINE)


class Cli:
    """Line oriented access to one OpenThread CLI over a serial port."""

    def __init__(self, port: str, baudrate: int, prefix: str = "", timeout: float = 10.0):
        self.prefix = f"{prefix} " if prefix else ""
        self.timeout = timeout
        self.serial = serial.Serial(port, baudrate, timeout=0.1)
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.serial.close()

    def command(self, line: str) -> str:
        self.serial.reset_input_buffer()
        self.serial.write(f"{self.prefix}{line}\r\n".encode())
        self.serial.flush()

        deadline = time.monotonic() + self.timeout
        output = ""

        while time.monotonic() < deadline:
            output += self.serial.read(512).decode(errors="replace")

            if PROMPT_DONE.search(output):
                return output

        raise TimeoutError(f"no CLI result for {line!r}, got: {output!r}")

    def report(self, role: str) -> dict[str, str]:
        match = REPORT.search(self.command(f"perf {role} report"))

        if match is None:
            raise RuntimeError(f"{role} did not answer with a perf report")

        fields = dict(item.split("=", 1) for item in match.group("fields").split() if "=" in item)
        fields["role"] = match.group("role")

        return fields


def mleid(cli: Cli) -> str:
    for line in cli.command("ipaddr mleid").splitlines():
        line = line.strip()

        if ":" in line and not line.startswith(("Done", "Error")):
            return line

    raise RuntimeError("could not read the mesh-local EID; is the node attached?")


def wait_for_state(cli: Cli, role: str, deadline: float, poll_s: float = 0.5) -> dict[str, str]:
    fields: dict[str, str] = {}

    while time.monotonic() < deadline:
        fields = cli.report(role)

        if fields.get("state") in ("done", "stopped"):
            return fields

        # draining: FIN sent, radio queue still emptying

        time.sleep(poll_s)

    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, help="serial port of the receiving node")
    parser.add_argument("--client", required=True, help="serial port of the sending node")
    parser.add_argument("--server-prefix", default="", help="'ot' for an NCS/Zephyr shell")
    parser.add_argument("--client-prefix", default="", help="'ot' for an NCS/Zephyr shell")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--port", type=int, default=5001, help="UDP port")
    parser.add_argument("--size", type=int, default=1232, help="UDP payload bytes, 20..1232")
    parser.add_argument("--count", type=int, default=1000, help="packets to send")
    parser.add_argument(
        "--interval",
        type=int,
        default=0,
        help="milliseconds between packets; 0 sends as fast as the stack accepts",
    )
    parser.add_argument("--label", default="", help="free-form tag stored in the CSV row")
    parser.add_argument("--csv", type=Path, help="append the result to this CSV file")
    args = parser.parse_args()

    server = Cli(args.server, args.baudrate, args.server_prefix)
    client = Cli(args.client, args.baudrate, args.client_prefix)

    try:
        destination = mleid(server)
        print(f"receiver mleid: {destination}")

        server.command("perf server stop")
        server.command(f"perf server start {args.port}")

        client.command(
            f"perf client start {destination} {args.port} {args.size} {args.count} {args.interval}"
        )

        # Worst case the run is paced, plus a margin for the FIN retries and the idle deadline.
        budget = 40.0 + args.count * max(args.interval, 1) / 1000.0
        deadline = time.monotonic() + budget

        client_fields = wait_for_state(client, "client", deadline)
        server_fields = wait_for_state(server, "server", deadline + 5.0)
        server.command("perf server stop")
    finally:
        client.close()
        server.close()

    print(f"client: {client_fields}")
    print(f"server: {server_fields}")
    print(
        f"goodput {server_fields.get('goodput_kbps', '?')} kbps"
        f" over {server_fields.get('dur_ms', '?')} ms,"
        f" tail {server_fields.get('tail_ms', '?')} ms,"
        f" {server_fields.get('lost', '?')}/{server_fields.get('expected', '?')} lost,"
        f" offered {client_fields.get('offered_kbps', '?')} kbps,"
        f" submit_span {client_fields.get('submit_span_ms', '?')} ms,"
        f" nobufs {client_fields.get('nobufs', '?')}"
    )

    if args.csv:
        row = {"label": args.label, "size": args.size, "count": args.count, "interval_ms": args.interval}
        row.update({f"client_{k}": v for k, v in client_fields.items()})
        row.update({f"server_{k}": v for k, v in server_fields.items()})

        new_file = not args.csv.exists()

        with args.csv.open("a", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(row))

            if new_file:
                writer.writeheader()

            writer.writerow(row)

    return 0 if server_fields.get("state") == "done" else 1


if __name__ == "__main__":
    sys.exit(main())
