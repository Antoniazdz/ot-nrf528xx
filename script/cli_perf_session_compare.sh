#!/usr/bin/env bash
# Local wrapper — runs the Morty session compare script from this repo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/notes/cli_perf_morty/run_nrf54l15_cli_perf_compare.bash" "$@"
