#!/usr/bin/env bash
# Analyze libnrf-802154-sl.a external dependencies (variant C discovery).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_SL="${REPO_ROOT}/third_party/nrf54/nrfxlib/nrf_802154/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"
SL="${1:-${NRF54_SL_BINARY_PATH:-${DEFAULT_SL}}}"

if [[ ! -f "$SL" ]]; then
    echo "Usage: $0 [path/to/libnrf-802154-sl.a]" >&2
    echo "Binary not found: $SL" >&2
    exit 1
fi

echo "=== SL binary ==="
ls -la "$SL"
echo

echo "=== Archive members ==="
arm-none-eabi-ar t "$SL"
echo

echo "=== Undefined symbols (linker must provide) ==="
arm-none-eabi-nm -u "$SL" | grep ' U ' | awk '{print $2}' | sort -u | tee /tmp/sl-undef.txt
echo
echo "Count: $(wc -l < /tmp/sl-undef.txt)"
echo

echo "=== Groups ==="
for prefix in mpsl_cx nrf_raal sym_ nrf_802154_platform_sl_lptimer nrf_802154_platform_timestamper nrf_802154_wifi_coex nrf_802154_rsch_continuous gp_nrf; do
    echo "--- $prefix ---"
    grep "^${prefix}" /tmp/sl-undef.txt || true
done

echo
echo "=== sym_* in rsch_init (first calls) ==="
SL_ABS="$(readlink -f "$SL")"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
( cd "$tmpdir" && arm-none-eabi-ar x "$SL_ABS" nrf_802154_rsch.c.obj )
echo "(disassembly of nrf_802154_rsch_init — sym_* is typically the first call)"
arm-none-eabi-objdump -d "$tmpdir/nrf_802154_rsch.c.obj" 2>/dev/null \
    | sed -n '/<nrf_802154_rsch_init>:/,/^$/p' \
    | head -20 || true
