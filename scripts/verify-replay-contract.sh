#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND="$(cd "${1:?Usage: bash scripts/verify-replay-contract.sh /path/to/kz-global-api}" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf -- "$WORK"' EXIT

"${CXX:-c++}" -std=c++17 -I"$ROOT/src/kz_global_api/include" \
  "$ROOT/scripts/contract/make-replays.cpp" \
  "$ROOT/src/kz_global_api/krp_format.cpp" \
  "$ROOT/src/kz_global_api/krp_header_validate.cpp" \
  -lzstd -lz -o "$WORK/make-replays"
"$WORK/make-replays" "$WORK/fixtures"

(cd "$ROOT" && zig build --fetch)
sqlite_package="$(sed -n '/^[[:space:]]*\.sqlitecpp = \.{/,/^[[:space:]]*},/s/^[[:space:]]*\.hash = "\([^"]*\)".*/\1/p' "$ROOT/build.zig.zon")"
sqlite="$ROOT/zig-pkg/$sqlite_package"
"${CC:-cc}" -DSQLITE_ENABLE_COLUMN_METADATA -c "$sqlite/sqlite3/sqlite3.c" -o "$WORK/sqlite3.o"
"${CXX:-c++}" -std=c++17 -pthread -I"$ROOT/src/kz_global_api/include" -I"$sqlite/include" -I"$sqlite/sqlite3" \
  "$ROOT/scripts/contract/test-outbox.cpp" "$sqlite"/src/*.cpp "$WORK/sqlite3.o" -ldl -o "$WORK/test-outbox"
"$WORK/test-outbox" "$WORK/outbox.sqlite"

mkdir -p "$WORK/backend"
git -C "$BACKEND" archive HEAD | tar -x -C "$WORK/backend"
cp "$ROOT/scripts/contract/NativeReplayContractTest.kt" \
  "$WORK/backend/src/test/kotlin/kz/global/api/ws/handlers/NativeReplayContractTest.kt"
cd "$WORK/backend"
KZ_CONTRACT_FIXTURES="$WORK/fixtures" bash gradlew test --no-daemon --tests '*NativeReplayContractTest'
