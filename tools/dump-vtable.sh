#!/bin/sh
# Prints the MSVC vtable order of NVSDK_NGX_Parameter, the authority for
# the slot order hand-built in src/ngx.c. Needs tools/fetch-ngx-headers.sh.
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
sed -n '/^typedef struct NVSDK_NGX_Parameter$/,/^} NVSDK_NGX_Parameter;/p' "$top/third_party/ngx/nvsdk_ngx_params.h" >"$tmp/p.inc"
cat >"$tmp/vt.cpp" <<'CPP'
struct ID3D11Resource; struct ID3D12Resource;
typedef int NVSDK_NGX_Result;
#include "p.inc"
#include "impl.inc"
NVSDK_NGX_Parameter *make() { return new Impl; }
CPP
{ echo 'struct Impl : NVSDK_NGX_Parameter {'
  grep 'virtual' "$tmp/p.inc" | sed 's/ = 0;/ override;/'
  echo '};'; } >"$tmp/impl.inc"
clang++ -target x86_64-pc-windows-msvc -c -o /dev/null -Xclang -fdump-vtable-layouts "$tmp/vt.cpp" 2>&1 |
    sed -n "/VFTable indices for 'NVSDK_NGX_Parameter'/,/^\$/p"
