# Credits

ngx-fsr4 is © Eugene Flanagin, MIT licence (`LICENSE`). It is written clean-room from public
material only. Nothing from NVIDIA is included: no NVIDIA headers, binaries or DLSS files.

## Included in this repository

| what | from | licence | changes |
|---|---|---|---|
| `third_party/ffx/` (FidelityFX API headers: `ffx_api.h`, `ffx_api_types.h`, `ffx_api_loader.h`, `dx12/ffx_api_dx12.h`, `ffx_upscale.h`, `docs/license.md`) | AMD, [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (`Kits/FidelityFX/`), © Advanced Micro Devices, Inc. | MIT (`third_party/ffx/docs/license.md`) | none: byte-identical copies, licence headers kept |

## Used, not included

| what | used for | licence |
|---|---|---|
| NVIDIA NGX SDK headers, [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) | the public interface this DLL implements (function names, parameter names, the parameter object's methods). Not redistributed: `tools/fetch-ngx-headers.sh` downloads them for reference; the build doesn't need them. | NVIDIA's DLSS SDK licence |
| AMD's FSR runtime, `amd_fidelityfx_upscaler_dx12.dll` | loaded at run time to do the upscaling. You supply it (FidelityFX SDK 2.x, or GOverlay's download). | AMD's licence |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) by Martin Storsjö (LLVM + mingw-w64) | cross-compiler for the Windows DLL and test programs; release builds use llvm-mingw 20260826. Parts of its runtime libraries are linked statically into the `.dll`/`.exe` files. | LLVM runtimes (libc++, compiler-rt): Apache-2.0 with LLVM exception; mingw-w64 runtime: ZPL 2.1 / public domain; winpthreads: MIT |

## Thanks

- [OptiScaler](https://github.com/optiscaler/OptiScaler): its public docs describe the NGXCore
  `EnableSignatureOverride` switch and the camera defaults used here, and its GPU spoofing works
  alongside ngx-fsr4 (`--optiscaler-spoof`).
- [dxvk-nvapi](https://github.com/jp7677/dxvk-nvapi) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), which make NVIDIA spoofing
  and DirectX 12 games work under Proton.
- [GOverlay](https://github.com/benjamimgois/goverlay), which downloads AMD's FSR 4 runtime.

NVIDIA, DLSS and NGX are trademarks of NVIDIA Corporation. AMD, Radeon, FidelityFX and FSR are
trademarks of Advanced Micro Devices, Inc. Steam and Proton are trademarks of Valve Corporation.
This project is not affiliated with, endorsed by, or supported by NVIDIA, AMD or Valve.
