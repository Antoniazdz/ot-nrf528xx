#!/bin/bash
#
#  Copyright (c) 2020, The OpenThread Authors.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the name of the copyright holder nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#
# Flash NCS Thread CLI on nRF52840 DK, flash ot-cli-ftd on nRF54L15 DK,
# bring up a network (52840 leader), join 54L15 as CSL child, sleep 5s, exit.
# Serial output from both boards is streamed with [leader] / [child] prefixes.
#
# Usage:
#   ./script/run-nrf52-leader-nrf54-csl-child-test.sh
#
# Environment:
#   OT_CMAKE_BUILD_DIR     nRF54L15 build tree (default: build-nrf54l15-uart)
#   CSL_PERIOD_US          CSL period in microseconds (default: 500000 = 500 ms).
#                          Must be a multiple of 160; minimum on this stack is ~16000 µs.
#                          (500 µs is below the minimum — use 500000 for 500 ms.)
#   NRF52840_SERIAL        Override J-Link serial for nRF52840 DK
#   NRF54L15_SERIAL        Override J-Link serial for nRF54L15 DK
#   SKIP_FLASH=1           Skip programming (only run Thread/CSL steps)
#   POST_START_SLEEP       Seconds after child thread start (default: 5)
#   CHILD_ATTACH_TIMEOUT   Max wait for Child Update / CSL sync (default: 120)
#   Pass --quiet to the Python step to hide live serial (see script --help).

set -euo pipefail

OT_SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OT_SRCDIR

POST_START_SLEEP="${POST_START_SLEEP:-5}"
CSL_PERIOD_US="${CSL_PERIOD_US:-500000}"
OT_CMAKE_BUILD_DIR="${OT_CMAKE_BUILD_DIR:-build-nrf54l15-uart}"
export OT_CMAKE_BUILD_DIR

die()
{
    echo " ** ERROR: $1" >&2
    exit 1
}

require_cmd()
{
    command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

nrfutil_serial_for_board()
{
    local board="$1"
    nrfutil device list --json 2>/dev/null | python3 -c "
import json, sys
board = sys.argv[1]
for line in sys.stdin:
    try:
        o = json.loads(line)
    except json.JSONDecodeError:
        continue
    if o.get('type') != 'info':
        continue
    for d in o.get('data', {}).get('devices', []):
        if (d.get('devkit') or {}).get('boardVersion') == board:
            sn = d.get('serialNumber', '')
            print(sn.lstrip('0') or sn)
            sys.exit(0)
sys.exit(1)
" "${board}" || die "No ${board} DK found"
}

flash_nrf52840_ncs()
{
    echo "=== Flash nRF52840 DK ==="
    if [[ -n "${NRF52840_SERIAL:-}" ]]; then
        "${OT_SRCDIR}/script/flash-nrf52840-ncs-thread-cli" "${NRF52840_SERIAL}"
    else
        "${OT_SRCDIR}/script/flash-nrf52840-ncs-thread-cli"
    fi
}

flash_nrf54l15_cli()
{
    local serial="${NRF54L15_SERIAL:-$(nrfutil_serial_for_board PCA10156)}"
    echo "=== Flash nRF54L15 DK (serial ${serial}, ${OT_CMAKE_BUILD_DIR}) ==="
    "${OT_SRCDIR}/script/flash-nrf54l15-cli-ftd-csl-debug" "${serial}"
}

main()
{
    require_cmd nrfutil
    require_cmd python3

    python3 -c "import serial" 2>/dev/null || die "Python package 'pyserial' required (pip install pyserial)"

    if [[ "${SKIP_FLASH:-0}" != "1" ]]; then
        flash_nrf52840_ncs
        flash_nrf54l15_cli
        echo "Waiting for boards to boot..."
        sleep 3
    fi

    echo "=== Thread + CSL test (CSL_PERIOD_US=${CSL_PERIOD_US}) ==="
    python3 "${OT_SRCDIR}/script/run-nrf52-leader-nrf54-csl-child-test.py" \
        --csl-period-us "${CSL_PERIOD_US}" \
        --post-start-sleep "${POST_START_SLEEP}" \
        "$@"
}

main "$@"
