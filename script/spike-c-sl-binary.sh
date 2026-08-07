#!/usr/bin/env bash
# Build variant C gate spike: radio-spike-sl-binary-nrf54l15
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# nRF54L15 app core + ot-nrf54xx toolchain (arm-none-eabi.cmake) = hard-float.
SL_DEFAULT="${REPO_ROOT}/third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"
SL_SRC="${NRF54_SL_BINARY_SRC:-$SL_DEFAULT}"
SL_DST="${REPO_ROOT}/third_party/nrf54/bin/libnrf-802154-sl.a"
BUILD_DIR="${REPO_ROOT}/build-spike-c"

if [[ ! -f "$SL_SRC" ]]; then
    echo "Brak libnrf-802154-sl.a dla nRF54L15 (hard-float)." >&2
    echo "" >&2
    echo "Szukaj w repo:" >&2
    echo "  third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/" >&2
    echo "" >&2
    echo "Albo z NCS nrfxlib (ten sam tag co driver _nowy):" >&2
    echo "  ~/NCS_workspace/nrfxlib/nrf_802154/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a" >&2
    echo "" >&2
    echo "Ustaw: export NRF54_SL_BINARY_SRC=/sciezka/do/libnrf-802154-sl.a" >&2
    exit 1
fi

mkdir -p "${REPO_ROOT}/third_party/nrf54/bin"
cp -f "$SL_SRC" "$SL_DST"
echo "Copied SL binary -> $SL_DST"

echo "=== Pre-build: analyze undefined symbols ==="
"${REPO_ROOT}/script/analyze-sl-binary.sh" "$SL_DST"

cmake -B "$BUILD_DIR" -GNinja \
    -DNRF_PLATFORM=nrf54l15 \
    -DOT_PLATFORM=external \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/src/nrf54l15/arm-none-eabi.cmake" \
    -DOT_APP_RCP=OFF \
    -DOT_APP_CLI=OFF \
    -DOT_APP_NCP=OFF \
    -DNRF54_SPIKE_SL_BINARY=ON \
    -DNRF54_SL_BINARY_PATH="$SL_DST" \
    -DNRF54_POC_MINIMAL_TIMERS=ON

ninja -C "$BUILD_DIR" radio-spike-sl-binary-nrf54l15

ELF="${BUILD_DIR}/bin/radio-spike-sl-binary-nrf54l15"
HEX="${ELF}.hex"
echo
echo "=== G1 link check: remaining undefined symbols ==="
if arm-none-eabi-nm "$ELF" | grep ' U '; then
    echo "FAIL G1: unresolved symbols above"
    exit 1
fi
echo "PASS G1: no unresolved symbols"

arm-none-eabi-objcopy -O ihex "$ELF" "$HEX"

echo
arm-none-eabi-size "$ELF"
echo
echo "Flash (G2 on hardware):"
echo "  nrfjprog --chiperase --program ${HEX} --reset"
echo "  RTT Viewer -> expect: spike-c: PASS — rsch_init OK (sym_* gate)"
