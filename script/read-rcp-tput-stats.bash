#!/usr/bin/env bash
# Read g_nrf54_rcp_tput_stats from a running/halted nRF54L15 ot-rcp via nrfjprog.
#
# Usage:
#   ./script/read-rcp-tput-stats.bash --sn 1057766367 \
#     --elf build-nrf54l15-uart/bin/ot-rcp
#
# During iperf: halt after 10s transfer, read stats, compare BM vs NCS or before/after patch.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="${ROOT}/../tests/test-fw-nrfconnect-thread/tests_morty/functional/nrf54_rcp_tput_stats.py"

SN=""
ELF=""
OUT=""
JSON=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sn) SN="$2"; shift 2 ;;
        --elf) ELF="$2"; shift 2 ;;
        -o|--output) OUT="$2"; shift 2 ;;
        --json) JSON="--json"; shift ;;
        -h|--help)
            sed -n '2,10p' "$0"
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$SN" ]]; then
    echo "missing --sn" >&2
    exit 1
fi

cmd=(python3 "$PY" --sn "$SN")
if [[ -n "$ELF" ]]; then
    cmd+=(--elf "$ELF")
fi
if [[ -n "$JSON" ]]; then
    cmd+=("$JSON")
fi
if [[ -n "$OUT" ]]; then
    cmd+=(-o "$OUT")
fi

exec "${cmd[@]}"
