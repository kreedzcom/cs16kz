#!/usr/bin/env bash
# AMXX Metamod string API guard.
#
# MF_GetAmxString / MF_GetAmxStringNull return pointers into AMXX-managed buffers.
# The third argument is a buffer slot id (0, 1, …), not a char[] — never pass sizeof().
# Wrong (HLSDK-style): MF_GetAmxString(amx, params[1], mapname, sizeof(mapname))
# Right:              char* mapname = MF_GetAmxString(amx, params[1], 0, &len)
#
# See kz_api_get_map_details in src/kz_global_api/kz_natives.cpp.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATTERN='MF_GetAmxString(Null)?.*sizeof'

if matches=$(grep -rEn -- "${PATTERN}" "${ROOT}/src/"); then
  echo "::error::MF_GetAmxString misuse — returns AMXX-managed char*; do not pass a local buffer or sizeof (see kz_api_get_map_details in src/kz_global_api/kz_natives.cpp)"
  printf '%s\n' "${matches}"
  exit 1
else
  status=$?
  if [ "${status}" -ne 1 ]; then
    echo "::error::AMXX string API lint failed to scan source (grep exit ${status})" >&2
    exit "${status}"
  fi
fi

echo "AMXX string API lint: ok"
