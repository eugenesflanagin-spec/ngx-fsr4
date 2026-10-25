#!/bin/bash
# Checks that build/nvngx.dll is a 64-bit Windows DLL that exports exactly the
# NGX functions src/ngx.c defines. No GPU or Wine needed.
#   test/check-dll.sh DLL [OBJDUMP]      (OBJDUMP: llvm-objdump or a PE-aware objdump)
set -euo pipefail
dll=$1
objdump=${2:-objdump}
src=$(dirname "$0")/../src/ngx.c
fail() { echo "FAIL $*"; exit 1; }

[ "$(head -c2 "$dll")" = MZ ] || fail "$dll does not start with MZ"
out=$("$objdump" -p "$dll")
grep -Eq 'Magic[[:space:]]+020b' <<<"$out" || fail "not PE32+ (64-bit)"
grep -Eq 'Characteristics.*0x[0-9a-f]+' <<<"$out" || fail "no PE header characteristics"
grep -q 'DLL' <<<"$out" || fail "not marked as a DLL"
echo "ok   $dll is a PE32+ x86-64 DLL"

want=$( { grep -oE '^(API|PEXP|GEXP) [A-Za-z0-9_]+' "$src" | cut -d' ' -f2
          grep -oE '^(REFUSE|DECLINE|ACCEPT)\([A-Za-z0-9_]+\)' "$src" | sed 's/.*(\(.*\))/\1/'; } | sort -u)
# export table lines look like "  [   0] +base[   1]  0000 NAME" (llvm) or "  [   0] NAME" (GNU)
have=$(sed -n '/[Ee]xport [Tt]able/,/^$/p; /Ordinal\/Name Pointer/,/^$/p' <<<"$out" |
       grep -oE 'NVSDK_NGX_[A-Za-z0-9_]+' | sort -u)
[ -n "$want" ] || fail "found no exports in $src"
if [ "$want" != "$have" ]; then
    diff <(echo "$want") <(echo "$have") | sed 's/^</  missing:/; s/^>/  extra:  /'
    fail "export list differs from src/ngx.c"
fi
echo "ok   exports match src/ngx.c ($(wc -l <<<"$want") functions)"
for imp in $(sed -n 's/^[[:space:]]*DLL Name: //p' <<<"$out" | sort -u); do
    case ${imp,,} in
    nvngx*.dll | nvngx_dlss*.dll) fail "imports an NVIDIA DLL: $imp" ;;
    esac
done
echo "ok   imports: $(sed -n 's/^[[:space:]]*DLL Name: //p' <<<"$out" | sort -u | tr '\n' ' ')"
