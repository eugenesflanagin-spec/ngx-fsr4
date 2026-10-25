#!/bin/bash
# Exercises the ngx-fsr4 helper against a fake Steam library: no Steam, no
# GPU, no Wine. Checks enable -> status -> run --dry-run -> log -> disable puts
# every file and the registry back exactly as they were.
#   test/tool-test.sh [PATH-TO-ngx-fsr4]     (default: tools/ngx-fsr4)
set -euo pipefail
tool=$(realpath "${1:-$(dirname "$0")/../tools/ngx-fsr4}")
T=$(mktemp -d)
[ -n "${KEEP:-}" ] && echo "keeping $T" || trap 'rm -rf "$T"' EXIT
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }

export STEAM_ROOT=$T/steam XDG_DATA_HOME=$T/data NGX_FSR4_LIB=$T/lib
unset NGX_FSR4_FFX NGX_FSR4_PROTON
ID=4242
lib=$STEAM_ROOT/steamapps
game=$lib/common/FakeGame
sys=$lib/compatdata/$ID/pfx/drive_c/windows/system32
mkdir -p "$NGX_FSR4_LIB" "$game/Bin" "$sys" "$STEAM_ROOT/config"
echo "stand-in dll" >"$NGX_FSR4_LIB/nvngx.dll"
echo "fsr runtime" >"$T/amd_fidelityfx_upscaler_dx12.dll"
cat >"$lib/appmanifest_$ID.acf" <<ACF
"AppState"
{
	"appid"		"$ID"
	"name"		"Fake Game"
	"installdir"		"FakeGame"
}
ACF
echo exe >"$game/Game.exe"
echo exe >"$game/unins000.exe"
echo exe >"$game/Bin/Game-Win64.exe"
echo dlss >"$game/Bin/nvngx_dlss.dll"
echo "OptiScaler build" >"$game/Bin/dxgi.dll"
printf '[Inputs]\nEnableDlssInputs = true\n' >"$game/Bin/OptiScaler.ini"
reg=$sys/../../../system.reg
printf 'WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n[Software\\\\Foo] 1700000000\n#time=1\n"a"="b"\n' >"$reg"
cp "$reg" "$T/system.reg.orig"
cp "$game/Bin/dxgi.dll" "$T/dxgi.orig"
for p in Proton-A Proton-B; do
    mkdir -p "$STEAM_ROOT/compatibilitytools.d/$p"
    printf '#!/bin/sh\necho fake proton "$@"\n' >"$STEAM_ROOT/compatibilitytools.d/$p/proton"
    chmod +x "$STEAM_ROOT/compatibilitytools.d/$p/proton"
    printf '"compatibilitytools"\n{\n  "compat_tools"\n  {\n    "%s-internal"\n    {\n    }\n  }\n}\n' "$p" \
        >"$STEAM_ROOT/compatibilitytools.d/$p/compatibilitytool.vdf"
done

out=$("$tool" --version)
check "--version prints a version ($out)" grep -Eq '^ngx-fsr4 [0-9]+\.[0-9]+\.[0-9]+' <<<"$out"
check "--help lists the commands" bash -c '"$1" --help | grep -q "ngx-fsr4 enable APPID"' _ "$tool"
check "status with nothing enabled" bash -c '"$1" status | grep -q "not enabled for any game"' _ "$tool"
check "unknown app id is refused" bash -c '! "$1" enable 999 2>/dev/null' _ "$tool"

# run --dry-run: two Protons and no Steam setting -> asks; Steam setting -> uses it
check "run --dry-run asks which Proton when Steam names none" \
    bash -c '"$1" run '$ID' --dry-run 2>&1 | grep -q "several Proton builds"' _ "$tool"
printf '"InstallConfigStore"\n{\n\t"Software"\n\t{\n\t\t"Valve"\n\t\t{\n\t\t\t"Steam"\n\t\t\t{\n\t\t\t\t"CompatToolMapping"\n\t\t\t\t{\n\t\t\t\t\t"%s"\n\t\t\t\t\t{\n\t\t\t\t\t\t"name"\t\t"Proton-B-internal"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n' "$ID" \
    >"$STEAM_ROOT/config/config.vdf"
out=$("$tool" run $ID --dry-run)
check "run --dry-run uses Steam's Proton for the game" grep -q "proton: $STEAM_ROOT/compatibilitytools.d/Proton-B (Steam" <<<"$out"
check "run --dry-run picks the one game exe (not the uninstaller)" grep -q "exe:    $game/Game.exe" <<<"$out"

# enable
check "enable without an FSR runtime explains where to get it" \
    bash -c '"$1" enable '$ID' 2>&1 | grep -q "not shipped with ngx-fsr4"' _ "$tool"
"$tool" enable $ID --ffx "$T/amd_fidelityfx_upscaler_dx12.dll" >"$T/enable.out"
check "enable: _nvngx.dll in system32" cmp -s "$sys/_nvngx.dll" "$NGX_FSR4_LIB/nvngx.dll"
check "enable: FSR runtime in system32" cmp -s "$sys/ngx-fsr4-upscaler.dll" "$T/amd_fidelityfx_upscaler_dx12.dll"
check "enable: nvngx.dll beside the game's DLSS dll" cmp -s "$game/Bin/nvngx.dll" "$NGX_FSR4_LIB/nvngx.dll"
check "enable: OptiScaler dxgi.dll moved aside" test ! -e "$game/Bin/dxgi.dll"
check "enable: registry override set" grep -q '"EnableSignatureOverride"=dword:00000001' "$reg"
check "enable: other registry keys untouched" grep -q '^"a"="b"$' "$reg"
check "status shows the game enabled" bash -c '"$1" status | grep -q "^'$ID'  Fake Game  enabled"' _ "$tool"
check "enable twice is refused" bash -c '! "$1" enable '$ID' --ffx "$2" 2>/dev/null' _ "$tool" "$T/amd_fidelityfx_upscaler_dx12.dll"
out=$("$tool" run $ID --dry-run)
check "run --dry-run when enabled" grep -q "would launch Fake Game" <<<"$out"

# log: newest of the logs beside each DLL copy
echo old >"$sys/ngx-fsr4.log"
touch -d '2001-01-01' "$sys/ngx-fsr4.log"
echo "=== ngx-fsr4 loaded" >"$game/Bin/ngx-fsr4.log"
out=$(timeout 3 "$tool" log $ID 2>&1 || true)
check "log follows the newest log" grep -q "following $game/Bin/ngx-fsr4.log" <<<"$out"

# disable: everything back
"$tool" disable $ID >"$T/disable.out"
check "disable: system32 copies removed" test ! -e "$sys/_nvngx.dll" -a ! -e "$sys/ngx-fsr4-upscaler.dll"
check "disable: game-folder copies removed" test ! -e "$game/Bin/nvngx.dll" -a ! -e "$game/Bin/ngx-fsr4-upscaler.dll"
check "disable: dxgi.dll restored unchanged" cmp -s "$game/Bin/dxgi.dll" "$T/dxgi.orig"
check "disable: system.reg identical to before" cmp -s "$reg" "$T/system.reg.orig"
check "disable: logs moved into the backup" bash -c 'ls "$1"/ngx-fsr4/backups/*/logs/*-ngx-fsr4.log >/dev/null' _ "$XDG_DATA_HOME"
check "disable: game's own DLSS dll left alone" test -f "$game/Bin/nvngx_dlss.dll"
check "status after disable" bash -c '"$1" status | grep -q "not enabled for any game"' _ "$tool"

# an NGXCore key that was already there keeps everything but our value
printf '\n[Software\\\\NVIDIA Corporation\\\\Global\\\\NGXCore] 1700000000\n#time=1\n"FullPath"="C:\\\\Windows\\\\System32"\n"Other"=dword:00000002\n' >>"$reg"
cp "$reg" "$T/system.reg.orig2"
"$tool" enable $ID --ffx "$T/amd_fidelityfx_upscaler_dx12.dll" --optiscaler-spoof >/dev/null
check "--optiscaler-spoof keeps dxgi.dll" test -f "$game/Bin/dxgi.dll"
check "--optiscaler-spoof turns DLSS inputs off" grep -q '^EnableDlssInputs = false' "$game/Bin/OptiScaler.ini"
"$tool" disable $ID >/dev/null
check "disable keeps a pre-existing NGXCore key as it was" cmp -s "$reg" "$T/system.reg.orig2"
check "disable restores OptiScaler.ini" grep -q '^EnableDlssInputs = true' "$game/Bin/OptiScaler.ini"

# ... also when the key sits between other keys
printf 'WINE REGISTRY Version 2\n\n[Software\\\\NVIDIA Corporation\\\\Global\\\\NGXCore] 1700000000\n#time=1\n"Other"=dword:00000002\n\n[Software\\\\Zed] 1700000000\n#time=1\n"z"="1"\n' >"$reg"
cp "$reg" "$T/system.reg.orig3"
"$tool" enable $ID --ffx "$T/amd_fidelityfx_upscaler_dx12.dll" >/dev/null
check "enable puts the value inside a mid-file NGXCore key" \
    python3 -c 'import sys; t=open(sys.argv[1]).read(); b=t[t.index("NGXCore]"):t.index("[Software\\\\Zed]")]; sys.exit("EnableSignatureOverride" not in b)' "$reg"
"$tool" disable $ID >/dev/null
check "disable restores a mid-file NGXCore key exactly" cmp -s "$reg" "$T/system.reg.orig3"

echo "$( [ $fails = 0 ] && echo PASSED || echo FAILED ): $fails failure(s)"
[ $fails = 0 ]
