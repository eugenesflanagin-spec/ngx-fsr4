#!/bin/bash
# Builds the release files in a clean Arch Linux container (archlinux:base-devel), as root:
#   packaging/ci/build.sh SRC LLVM_MINGW OUT
# SRC        a git checkout (with tags) of the commit to build
# LLVM_MINGW the llvm-mingw toolchain directory (packaging/ci/fetch-llvm-mingw.sh)
# OUT        gets ngx-fsr4-VERSION-proton.tar.gz and ngx-fsr4-git-*.pkg.tar.zst
# The package is built from SRC (not from GitHub) so it is exactly the checked-out commit.
set -euo pipefail
src=$(realpath "$1"); llvm=$(realpath "$2"); out=$3
mkdir -p "$out"; out=$(realpath "$out")
pacman -Syu --noconfirm --needed git python procps-ng findutils binutils >/dev/null
id builder >/dev/null 2>&1 || useradd -m builder
work=$(mktemp -d); chown builder: "$work"
su builder -c "git config --global --add safe.directory '*'"
# a clean clone: no stray files from the checkout end up in the build
su builder -c "git clone -q --no-local '$src' '$work/src'"
ver=$(cat "$work/src/VERSION")

echo "=== make dist + make check (llvm-mingw $(basename "$llvm"))"
su builder -c "cd '$work/src' && make -s LLVM_MINGW='$llvm' dist check"
cp "$work/src/build/ngx-fsr4-$ver-proton.tar.gz" "$out/"

echo "=== makepkg"
mkdir "$work/pkg"
sed "s|^source=.*|source=(\"\$pkgname::git+file://$work/src\")|" "$work/src/packaging/PKGBUILD" >"$work/pkg/PKGBUILD"
chown -R builder: "$work/pkg"
# -d: llvm-mingw comes from LLVM_MINGW, not from a package
su builder -c "cd '$work/pkg' && LLVM_MINGW='$llvm' PKGDEST='$work/pkg' makepkg -f --noconfirm -d"
cp "$work"/pkg/ngx-fsr4-git-*.pkg.tar.zst "$out/"
chmod 644 "$out"/*
ls -l "$out"
