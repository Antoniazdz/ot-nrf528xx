#!/usr/bin/env python3
"""Aggregate a cli_perf session_compare directory into summary CSV with medians."""

from __future__ import annotations

import csv
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

REPORT = re.compile(r"^perf: (?P<role>server|client) (?P<fields>.*)$", re.MULTILINE)
TIMING = re.compile(r"perf: timing (.+)", re.DOTALL)


def parse_report(text: str, role: str) -> dict[str, str]:
    match = REPORT.search(text)
    if match is None or match.group("role") != role:
        return {}
    return dict(item.split("=", 1) for item in match.group("fields").split() if "=" in item)


def load_run(variant_dir: Path, size: int) -> dict | None:
    """Read one ctf results_*.json.

    The Morty test suffixes every per-variant session_data key with the variant
    name ("perf_goodput_kbps_200B"); only a few keys such as
    perf_firmware_stack are global.
    """
    results = list(variant_dir.glob("outcomes/results/results_*.json"))
    if not results:
        return None
    data = json.loads(results[0].read_text())
    suffix = f"_{size}B"

    for suite in data.get("suites", {}).values():
        for test in suite.get("tests", []):
            sd = test.get("session_data", {})

            def field(base: str):
                if base + suffix in sd:
                    return sd[base + suffix]
                return sd.get(base)

            row = {
                "status": test.get("status"),
                "duration_s": test.get("duration"),
                "perf_count": field("perf_count"),
                "perf_interval_ms": field("perf_interval_ms"),
                "perf_goodput_kbps": field("perf_goodput_kbps"),
                "perf_offered_kbps": field("perf_offered_kbps"),
                "perf_tail_ms": field("perf_tail_ms"),
                "perf_submit_span_ms": field("perf_submit_span_ms"),
                "perf_packets_sent": field("perf_sent"),
                "perf_packets_lost": field("perf_lost"),
                "perf_client_nobufs": field("perf_client_nobufs"),
                "perf_timing_client_probe": field("perf_timing_client_probe") or "",
                "firmware_stack": field("perf_firmware_stack"),
                "run_index": (variant_dir / "run_index.txt").read_text().strip()
                if (variant_dir / "run_index.txt").exists()
                else "1",
            }
            probe = row.pop("perf_timing_client_probe", "")
            if probe:
                m = re.search(r"gap_all avg_us=(\d+|-)", probe)
                if m and m.group(1) != "-":
                    row["gap_all_avg_us"] = m.group(1)
                m = re.search(r"tx_done=(\d+)", probe)
                if m:
                    row["tx_done"] = m.group(1)
            return row
    return None


def fnum(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <session_dir>", file=sys.stderr)
        return 2

    session = Path(sys.argv[1])
    rows: list[dict] = []

    for variant_dir in sorted(session.glob("*/*/variant_*B")):
        parts = variant_dir.relative_to(session).parts
        stack, mode, variant = parts[0], parts[1], parts[2]
        size = int(variant.replace("variant_", "").replace("B", ""))
        # Repeated sessions nest run_1..run_N; single-run sessions do not.
        run_dirs = sorted(variant_dir.glob("run_*")) or [variant_dir]
        for path in run_dirs:
            row = load_run(path, size)
            if row is None:
                continue
            row.update(
                {
                    "profile": stack,
                    "perf_mode": mode,
                    "packet_size_B": size,
                    "result_json": str(path.relative_to(session)),
                }
            )
            rows.append(row)

    if not rows:
        print("no results found", file=sys.stderr)
        return 1

    summary_path = session / "summary.csv"
    fieldnames = sorted({key for row in rows for key in row})
    with summary_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    groups: dict[tuple, list[dict]] = defaultdict(list)
    for row in rows:
        groups[(row["profile"], row["perf_mode"], int(row["packet_size_B"]))].append(row)

    def median_key(key: str, items: list[dict]) -> float | None:
        vals = [fnum(str(r.get(key))) for r in items]
        vals = [v for v in vals if v is not None]
        return statistics.median(vals) if vals else None

    for mode in ("paced", "burst"):
        out = session / f"comparison_{mode}.csv"
        with out.open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(
                [
                    "packet_size_B",
                    "bare_metal_goodput_median",
                    "ncs_goodput_median",
                    "delta_kbps",
                    "delta_percent",
                    "bare_metal_tail_ms_median",
                    "ncs_tail_ms_median",
                    "bare_metal_nobufs_median",
                    "ncs_nobufs_median",
                    "runs",
                ]
            )
            for size in (100, 200, 500, 1000, 1200):
                bm = groups.get(("bare-metal", mode, size), [])
                ncs = groups.get(("ncs", mode, size), [])
                if not bm and not ncs:
                    continue
                bm_g = median_key("perf_goodput_kbps", bm)
                ncs_g = median_key("perf_goodput_kbps", ncs)
                delta = None
                pct = None
                if bm_g is not None and ncs_g is not None and bm_g != 0:
                    delta = ncs_g - bm_g
                    pct = 100.0 * delta / bm_g
                writer.writerow(
                    [
                        size,
                        f"{bm_g:.2f}" if bm_g is not None else "",
                        f"{ncs_g:.2f}" if ncs_g is not None else "",
                        f"{delta:.2f}" if delta is not None else "",
                        f"{pct:.1f}" if pct is not None else "",
                        median_key("perf_tail_ms", bm),
                        median_key("perf_tail_ms", ncs),
                        median_key("perf_client_nobufs", bm),
                        median_key("perf_client_nobufs", ncs),
                        max(len(bm), len(ncs)),
                    ]
                )

    print(f"wrote {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
