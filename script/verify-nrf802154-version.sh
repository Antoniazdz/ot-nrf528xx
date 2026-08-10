#!/usr/bin/env bash
# Verify driver sources + SL binary match VERSION_NRF802154 pin.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION_FILE="${REPO_ROOT}/third_party/nrf54/VERSION_NRF802154"
NRFXLIB_DIR="${REPO_ROOT}/third_party/nrf54/nrfxlib"
SL_BIN="${NRFXLIB_DIR}/nrf_802154/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"

fail() {
    echo "verify-nrf802154-version: FAIL — $*" >&2
    exit 1
}

ok() {
    echo "verify-nrf802154-version: OK — $*"
}

[[ -f "${VERSION_FILE}" ]] || fail "missing ${VERSION_FILE}"

PIN_TAG="$(sed -n '1p' "${VERSION_FILE}" | tr -d '[:space:]')"
PIN_COMMIT="$(sed -n '2p' "${VERSION_FILE}" | tr -d '[:space:]')"

[[ -n "${PIN_TAG}" ]] || fail "empty tag in ${VERSION_FILE}"
[[ -n "${PIN_COMMIT}" ]] || fail "empty commit in ${VERSION_FILE}"

[[ -d "${NRFXLIB_DIR}/.git" ]] || fail "nrfxlib submodule not initialized — run script/init-nrfxlib-submodule.sh"

HEAD="$(git -C "${NRFXLIB_DIR}" rev-parse HEAD)"
SHORT="$(git -C "${NRFXLIB_DIR}" rev-parse --short HEAD)"

if [[ "${HEAD}" != "${PIN_COMMIT}" ]]; then
    fail "nrfxlib HEAD ${SHORT} != pin ${PIN_COMMIT:0:7} (tag ${PIN_TAG})"
fi

if ! git -C "${NRFXLIB_DIR}" describe --tags --exact-match HEAD >/dev/null 2>&1; then
    echo "verify-nrf802154-version: WARN — HEAD is not exactly tagged ${PIN_TAG} (commit pin still matches)"
else
    TAG_AT_HEAD="$(git -C "${NRFXLIB_DIR}" describe --tags --exact-match HEAD)"
    if [[ "${TAG_AT_HEAD}" != "${PIN_TAG}" ]]; then
        fail "nrfxlib tag at HEAD is ${TAG_AT_HEAD}, expected ${PIN_TAG}"
    fi
fi

if git -C "${REPO_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    SUPERLINK="$(git -C "${REPO_ROOT}" ls-tree HEAD third_party/nrf54/nrfxlib 2>/dev/null | awk '{print $3}')"
    if [[ -n "${SUPERLINK}" && "${SUPERLINK}" != "${PIN_COMMIT}" ]]; then
        fail "superproject gitlink ${SUPERLINK:0:7} != pin ${PIN_COMMIT:0:7}"
    fi
fi

[[ -f "${SL_BIN}" ]] || fail "SL binary missing: ${SL_BIN}"

DRIVER_CMAKE="${NRFXLIB_DIR}/nrf_802154/driver/CMakeLists.txt"
[[ -f "${DRIVER_CMAKE}" ]] || fail "driver sources missing under nrfxlib"

SL_SIZE="$(stat -c '%s' "${SL_BIN}")"
ok "tag=${PIN_TAG} commit=${SHORT} driver+SL from same nrfxlib tree (SL ${SL_SIZE} B)"
