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

PIN_COMMIT="$(sed -n '2p' "${VERSION_FILE}" | tr -d '[:space:]')"
HEAD="$(git -C "${NRFXLIB_DIR}" rev-parse HEAD)"
if [[ -n "${PIN_COMMIT}" && "${HEAD}" != "${PIN_COMMIT}" ]]; then
    echo "WARN: nrfxlib HEAD $(git -C "${NRFXLIB_DIR}" rev-parse --short HEAD) != pin ${PIN_COMMIT:0:7}" >&2
    echo "      Update third_party/nrf54/VERSION_NRF802154 or re-run after fixing the tag." >&2
fi

exec "${REPO_ROOT}/script/verify-nrf802154-version.sh"
