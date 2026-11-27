#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if (( $# != 1 )); then
    echo "Usage: bash scripts/run-e2e.sh <api-checkout>" >&2
    exit 2
fi
KZ_E2E_API_CHECKOUT=$(cd -- "$1" && pwd)
test -f "$KZ_E2E_API_CHECKOUT/Dockerfile"
docker info --format '{{.OSType}}' | grep -Fx linux

work=$(mktemp -d)
project="kz-e2e-$(openssl rand -hex 6)"
export KZ_E2E_API_CHECKOUT
export KZ_E2E_RUNTIME="$work"
export KZ_E2E_TOKEN
KZ_E2E_TOKEN=$(openssl rand -hex 16)
compose=(docker compose -p "$project" -f "$repo/scripts/e2e/compose.yml")
build_game=false
if [[ -z ${KZ_E2E_GAME_IMAGE:-} ]]; then
    KZ_E2E_GAME_IMAGE=kz-e2e-game:local
    build_game=true
fi
export KZ_E2E_GAME_IMAGE

stage="prepare runtime"
last_passed="none"
cleanup() {
    local status=$?
    trap - EXIT
    if (( status != 0 )); then
        local logs
        logs=${KZ_E2E_LOG_DIR:-$(mktemp -d)}
        mkdir -p "$logs"
        printf 'Failed during %s\n' "$stage" >&2
        printf 'Last passed scenario: %s\nFailed during %s\n' "$last_passed" "$stage" > "$logs/result.txt"
        "${compose[@]}" logs --no-color --tail 400 game api proxy postgres 2>&1 |
            sed "s/$KZ_E2E_TOKEN/[REDACTED]/g" > "$logs/services.log" || true
        printf 'Diagnostics saved in %s\n' "$logs" >&2
    fi
    "${compose[@]}" down --volumes --remove-orphans --rmi local >/dev/null || true
    rm -rf -- "$work"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -m 755 "$work/game"
stage="build module and read build fingerprint"
if [[ -n ${KZ_E2E_MODULE_DIR:-} ]]; then
    module_dir="$KZ_E2E_MODULE_DIR"
else
    (
        cd "$repo"
        bash build.sh --prefix "$work/build"
    ) 2>&1 | tee "$work/build.log"
    module_dir="$work/build/lib"
    awk '$1 == "kz_global_api" && $3 == "md5" { print $2, $4 }' \
        "$work/build.log" > "$module_dir/kz-e2e-build.txt"
fi
read -r version checksum < "$module_dir/kz-e2e-build.txt"
[[ -n "$version" && "$checksum" =~ ^[0-9a-f]{32}$ ]]
install -m 644 "$module_dir/kz_global_api_amxx_i386.so" "$work/game/"

stage="generate test TLS certificate"
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj '/CN=KZ E2E CA' \
    -keyout "$work/ca.key" -out "$work/game/ca.pem" 2>"$work/openssl.log"
openssl req -new -newkey rsa:2048 -nodes -subj '/CN=proxy' \
    -keyout "$work/server.key" -out "$work/server.csr" 2>>"$work/openssl.log"
printf 'subjectAltName=DNS:proxy\nextendedKeyUsage=serverAuth\n' > "$work/server.ext"
openssl x509 -req -in "$work/server.csr" -CA "$work/game/ca.pem" -CAkey "$work/ca.key" \
    -CAcreateserial -days 2 -extfile "$work/server.ext" -out "$work/server.pem"
chmod 644 "$work/game/ca.pem"

stage="build game and API images"
if "$build_game"; then
    docker build --platform linux/amd64 -t "$KZ_E2E_GAME_IMAGE" "$repo/scripts/e2e"
fi
"${compose[@]}" build api

stage="start API and seed fixtures"
"${compose[@]}" up -d --wait --wait-timeout 120 postgres api proxy
"${compose[@]}" exec -T postgres psql -X -U kz_e2e -d kz_e2e \
    -v ON_ERROR_STOP=1 -v token="$KZ_E2E_TOKEN" -v version="$version" -v checksum="$checksum" \
    < "$repo/scripts/e2e/fixtures.sql"

game_log_until() {
    local pattern=$1 threshold=$2 timeout_seconds=$3 logs count
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if [[ -z $("${compose[@]}" ps --status running -q game) ]]; then
            echo "Game server exited" >&2
            return 1
        fi
        logs=$("${compose[@]}" logs --no-color game) || return 1
        count=$(grep -Ec -- "$pattern" <<< "$logs") || true
        if (( count >= threshold )); then
            return 0
        fi
        sleep 1
    done
    printf 'Timed out waiting for game log pattern %s (count %s)\n' "$pattern" "$threshold" >&2
    return 1
}

lookup() {
    local scenario=$1
    "${compose[@]}" exec -T game timeout 5 bash -c 'printf "%s\n" "$1" > /tmp/kz-e2e-console' \
        _ "kz_e2e_$scenario" || return 1
    game_log_until "\\[KZ-E2E\\] $scenario (PASS|FAIL)" 1 30 || return 1
    "${compose[@]}" logs --no-color game > "$work/game.log" || return 1
    if grep -Fq "[KZ-E2E] $scenario FAIL callback " "$work/game.log"; then
        return 42
    fi
    grep -Fq "[KZ-E2E] $scenario PASS" "$work/game.log"
}

hello_pattern='\[kz_ws_ack_hello\] Heartbeat interval:'
stage="authenticate game server and verify first Pawn callback"
"${compose[@]}" up -d --no-deps --pull never game
game_log_until '\[KZ-E2E\] READY' 1 60
game_log_until "$hello_pattern" 1 60
lookup initial
last_passed="authenticated round trip"
echo 'Authenticated round trip passed.'

stage="restart API and verify second Pawn callback"
"${compose[@]}" restart --timeout 10 api
game_log_until "$hello_pattern" 2 90
lookup reconnect
last_passed="round trip after reconnect"
echo 'Round trip after reconnect passed.'

stage="verify the runner rejects incorrect callback metadata"
mismatch_status=0
lookup mismatch || mismatch_status=$?
if [[ "$mismatch_status" != 42 ]] ||
    ! grep -Fq '[KZ-E2E] mismatch FAIL callback kz_e2e_first 1 2 3' "$work/game.log"; then
    echo 'Expected the deliberate mismatch to fail at the Pawn assertion.' >&2
    exit 1
fi
echo 'Deliberate callback mismatch rejected.'
