#!/bin/bash
# Downloads the pinned llvm-mingw release (checksum-verified) and unpacks it.
#   packaging/ci/fetch-llvm-mingw.sh DIR     -> prints the toolchain path (DIR/llvm-mingw-...)
set -euo pipefail
LLVM_MINGW_VERSION=20260826
LLVM_MINGW_SHA256=cee8d2ce3da5145ce4dc882e70d0b0719a783d53a99752c60948fc0659975a65
name=llvm-mingw-$LLVM_MINGW_VERSION-ucrt-ubuntu-22.04-x86_64
dir=${1:?usage: fetch-llvm-mingw.sh DIR}
mkdir -p "$dir"
if [ ! -x "$dir/$name/bin/x86_64-w64-mingw32-clang" ]; then
    if [ ! -f "$dir/$name.tar.xz" ] || ! echo "$LLVM_MINGW_SHA256  $dir/$name.tar.xz" | sha256sum -c --quiet - 2>/dev/null; then
        curl -fsSL --retry 3 -o "$dir/$name.tar.xz" \
            "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VERSION/$name.tar.xz" >&2
    fi
    echo "$LLVM_MINGW_SHA256  $dir/$name.tar.xz" | sha256sum -c --quiet - >&2
    tar -C "$dir" -xJf "$dir/$name.tar.xz"
fi
echo "$(cd "$dir/$name" && pwd)"
