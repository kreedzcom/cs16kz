#!/usr/bin/env bash
set -euo pipefail

addons=cstrike/addons
amxx=$addons/amxmodx
runtime=/run/kz-e2e

printf 'linux addons/amxmodx/dlls/amxmodx_mm_i386.so\n' > "$addons/metamod/plugins.ini"
printf 'kz_global_api\n' > "$amxx/configs/modules.ini"
printf 'kz_e2e.amxx\n' > "$amxx/configs/plugins.ini"
cp "$runtime/kz_global_api_amxx_i386.so" "$amxx/modules/"

mkdir -p "$amxx/data/kz_global"
cp "$runtime/ca.pem" "$amxx/data/kz_global/cacert.pem"
umask 077
cat > "$amxx/configs/kz_global.cfg" <<EOF
kz_api_log_mode 0
kz_api_log_send 0
kz_api_log_recv 0
kz_api_url "wss://proxy:8443/ws/game"
kz_api_token "$KZ_E2E_TOKEN"
EOF
umask 022

cp /opt/kz-e2e/kz_e2e.sma "$amxx/scripting/"
cp /opt/kz-e2e/include/*.inc "$amxx/scripting/include/"
(
    cd "$amxx/scripting"
    LD_LIBRARY_PATH="$PWD" ./amxxpc kz_e2e.sma -o../plugins/kz_e2e.amxx
    test -s ../plugins/kz_e2e.amxx
)

mkfifo /tmp/kz-e2e-console
# Keep a writer open so the engine never sees EOF between commands.
exec 3<>/tmp/kz-e2e-console
export LD_LIBRARY_PATH="$PWD"
exec ./hlds_linux -game cstrike -dll addons/metamod/dlls/metamod_i386.so \
    -console -insecure -nomaster +sv_lan 1 +maxplayers 4 +map de_dust2 <&3 3<&-
