#!/bin/bash
# Install-tests the release files in a FRESH Arch Linux container (archlinux:latest), as root:
#   packaging/ci/test.sh DIR VERSION        DIR holds the files from build.sh
# Runs from a checkout (for test/*.sh and src/ngx.c). No GPU needed; the Wine
# self-test runs when WINE_TEST=1 (default).
set -euo pipefail
dir=$(realpath "$1"); ver=$2
here=$(cd "$(dirname "$0")/../.." && pwd)
pkgs=( "$dir"/ngx-fsr4-git-*-any.pkg.tar.zst ); pkg=${pkgs[0]}
tarball=$dir/ngx-fsr4-$ver-proton.tar.gz
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }

extra=(binutils)
[ "${WINE_TEST:-1}" = 1 ] && extra+=(wine)
pacman -Syu --noconfirm --needed "${extra[@]}" >/dev/null
echo "=== install $(basename "$pkg")"
pacman -U --noconfirm "$pkg" >/dev/null
pacman -Q ngx-fsr4-git
case $(pacman -Q ngx-fsr4-git | cut -d' ' -f2) in "$ver".r*) ok "package version starts with $ver" ;; *) bad "package version" ;; esac
for f in /usr/bin/ngx-fsr4 /usr/lib/ngx-fsr4/nvngx.dll /usr/lib/ngx-fsr4/harness.exe /usr/lib/ngx-fsr4/gpu.exe \
    /usr/share/doc/ngx-fsr4/README.md /usr/share/doc/ngx-fsr4/USER-GUIDE.md /usr/share/doc/ngx-fsr4/CHANGELOG.md \
    /usr/share/doc/ngx-fsr4/CREDITS.md /usr/share/licenses/ngx-fsr4-git/LICENSE /usr/share/licenses/ngx-fsr4-git/FidelityFX-LICENSE.md; do
    [ -f "$f" ] && ok "installed $f" || bad "missing $f"
done
n=$(pacman -Qlq ngx-fsr4-git | grep -vc '/$')
[ "$n" = 10 ] && ok "package has exactly 10 files" || { bad "package has $n files"; pacman -Qlq ngx-fsr4-git; }
[ -x /usr/bin/ngx-fsr4 ] && ok "ngx-fsr4 is executable" || bad "ngx-fsr4 not executable"
[ "$(ngx-fsr4 --version)" = "ngx-fsr4 $ver" ] && ok "ngx-fsr4 --version = $ver" || bad "--version: $(ngx-fsr4 --version)"
ngx-fsr4 --help | grep -q 'ngx-fsr4 run APPID' && ok "ngx-fsr4 --help" || bad "--help"
ngx-fsr4 status | grep -q 'not enabled for any game' && ok "ngx-fsr4 status" || bad "status"
"$here/test/check-dll.sh" /usr/lib/ngx-fsr4/nvngx.dll objdump || bad "installed DLL check"
grep -aq "ngx-fsr4 $ver loaded" /usr/lib/ngx-fsr4/nvngx.dll && ok "DLL carries version $ver" || bad "DLL version string"

echo "=== helper against a fake Steam library (installed /usr/bin/ngx-fsr4, as a normal user)"
id tester >/dev/null 2>&1 || useradd -m tester
su tester -c "'$here/test/tool-test.sh' /usr/bin/ngx-fsr4" || bad "tool-test (installed)"

echo "=== tarball"
t=$(mktemp -d)
tar -C "$t" -xzf "$tarball"
d=$t/ngx-fsr4-$ver-proton
for f in nvngx.dll harness.exe gpu.exe tools/ngx-fsr4 README.md USER-GUIDE.md CHANGELOG.md CREDITS.md INSTALL.txt LICENSE FidelityFX-LICENSE.md; do
    [ -f "$d/$f" ] && ok "tarball has $f" || bad "tarball lacks $f"
done
[ "$(find "$d" -type f | wc -l)" = 11 ] && ok "tarball has exactly 11 files" || { bad "tarball file count"; find "$d" -type f; }
[ "$("$d/tools/ngx-fsr4" --version)" = "ngx-fsr4 $ver" ] && ok "tarball helper --version" || bad "tarball --version"
"$here/test/check-dll.sh" "$d/nvngx.dll" objdump || bad "tarball DLL check"
if cmp -s "$d/nvngx.dll" /usr/lib/ngx-fsr4/nvngx.dll; then ok "tarball and package DLLs are byte-identical"; else echo "note tarball and package DLLs differ (separate builds)"; fi
chown -R tester: "$t"
su tester -c "NGX_FSR4_LIB='$d' '$here/test/tool-test.sh' '$d/tools/ngx-fsr4'" >/dev/null && ok "tool-test (tarball helper)" || bad "tool-test (tarball)"

if [ "${WINE_TEST:-1}" = 1 ]; then
    echo "=== CPU self-test under Wine (no GPU)"
    w=$(mktemp -d); cp /usr/lib/ngx-fsr4/nvngx.dll /usr/lib/ngx-fsr4/harness.exe "$w/"; chown -R tester: "$w"
    if su tester -c "cd '$w' && WINEPREFIX='$w/pfx' WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=' timeout 300 wine harness.exe nvngx.dll" >"$w/out.txt" 2>"$w/err.txt"; then
        grep -E '^(ok|FAIL|PASSED|FAILED)' "$w/out.txt" | tail -3
        ok "harness.exe passed under Wine ($(grep -c '^ok' "$w/out.txt") checks)"
    else
        cat "$w/out.txt"; tail -20 "$w/err.txt"; bad "harness.exe under Wine"
    fi
fi

echo "$( [ $fails = 0 ] && echo PASSED || echo FAILED ): $fails failure(s)"
[ $fails = 0 ]
