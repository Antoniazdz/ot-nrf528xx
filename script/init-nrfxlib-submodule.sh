#!/usr/bin/env bash
# Initialize sdk-nrfxlib submodule with sparse checkout (nrf_802154 only).
# Pin: VERSION_NRF802154 (default v3.4.0).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NRFXLIB_DIR="${REPO_ROOT}/third_party/nrf54/nrfxlib"
VERSION_FILE="${REPO_ROOT}/third_party/nrf54/VERSION_NRF802154"
TAG="$(sed -n '1p' "${VERSION_FILE}")"
URL="https://github.com/nrfconnect/sdk-nrfxlib.git"

if [[ ! -d "${NRFXLIB_DIR}/.git" ]]; then
    echo "Cloning sdk-nrfxlib (${TAG}) with sparse checkout nrf_802154..."
    git clone --filter=blob:none --sparse --branch "${TAG}" --depth 1 \
        "${URL}" "${NRFXLIB_DIR}"
    git -C "${NRFXLIB_DIR}" sparse-checkout set nrf_802154
else
    echo "Updating sdk-nrfxlib submodule (${TAG}), sparse nrf_802154..."
    git -C "${NRFXLIB_DIR}" fetch origin "refs/tags/${TAG}" --depth 1
    git -C "${NRFXLIB_DIR}" checkout "${TAG}"
    git -C "${NRFXLIB_DIR}" sparse-checkout init --cone
    git -C "${NRFXLIB_DIR}" sparse-checkout set nrf_802154
fi

SL_BIN="${NRFXLIB_DIR}/nrf_802154/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"
if [[ ! -f "${SL_BIN}" ]]; then
    echo "Brak binarki SL: ${SL_BIN}" >&2
    exit 1
fi

echo "OK: nrf_802154 @ $(git -C "${NRFXLIB_DIR}" rev-parse --short HEAD)"
echo "SL binary: ${SL_BIN} ($(stat -c '%s' "${SL_BIN}") B)"
