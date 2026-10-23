#!/bin/sh
# NVIDIA's NGX SDK headers are not redistributed here; this fetches them
# from NVIDIA's own public repo for reference (the build does not need them).
set -e
dir=$(dirname "$0")/../third_party/ngx
base=https://raw.githubusercontent.com/NVIDIA/DLSS/main
mkdir -p "$dir"
for f in nvsdk_ngx.h nvsdk_ngx_defs.h nvsdk_ngx_params.h nvsdk_ngx_helpers.h nvsdk_ngx_helpers_d3d.h; do
    curl -fsSL -o "$dir/$f" "$base/include/$f"
done
curl -fsSL -o "$dir/LICENSE.txt" "$base/LICENSE.txt"
echo "headers in $dir"
