# ngx-fsr4

An experimental stand-in for NVIDIA's NGX runtime (`_nvngx.dll`) that answers
a game's **DLSS Super Resolution** calls with **AMD FSR 4** instead, for
RDNA4 GPUs on Linux (Proton / vkd3d-proton).

The game asks for DLSS as usual. This DLL says DLSS is available, takes the
game's frame inputs (colour, depth, motion vectors, jitter and so on), and runs
an FSR 4 upscale on the game's own command list through AMD's public
FidelityFX API.

> **Status: experiment.** Stage 3 is verified on a real GPU with a synthetic
> test (below). It has **not yet been tried in a real game**. Frame
> generation and ray reconstruction are refused for now, so games fall back
> cleanly. For everyday use, [OptiScaler](https://github.com/optiscaler/OptiScaler)
> is the mature project that does this and much more.

## How it works

| Piece | What it does |
|---|---|
| `src/ngx.c` | NGX entry points (`NVSDK_NGX_D3D12_*`), the parameter object, capability answers, the DLSS optimal-settings callback, logging |
| `src/fsr.cpp` | Loads `amd_fidelityfx_upscaler_dx12.dll`, picks the newest FSR 4.x it offers, maps DLSS inputs and flags to `ffxDispatchDescUpscale` |
| `test/harness.c` | CPU-only checks of the NGX surface under Wine |
| `test/gpu.c` | End-to-end check: real D3D12 device, a 1280×720 test card upscaled to 2560×1440, read back to `out.ppm` |

**The parameter object.** Games built with MSVC call it through a C++
vtable, so the slot order must match MSVC's, which is not the declaration
order. `src/ngx.c` builds that vtable by hand in C. Its order comes from
clang's own MSVC-ABI layout dump; `tools/dump-vtable.sh` reproduces it.

**Clean-room.** Written only from NVIDIA's public NGX SDK headers
([NVIDIA/DLSS](https://github.com/NVIDIA/DLSS)) and AMD's public FidelityFX
SDK. No NVIDIA binary was disassembled. NVIDIA's headers are not
redistributed: `tools/fetch-ngx-headers.sh` downloads them if you want them
for reference, though the build does not need them. AMD's FidelityFX API
headers (MIT) are vendored in `third_party/ffx`.

**DLSS → FSR mapping.**

| DLSS create flag | FSR enable flag |
|---|---|
| IsHDR | HIGH_DYNAMIC_RANGE |
| not MVLowRes | DISPLAY_RESOLUTION_MOTION_VECTORS |
| MVJittered | MOTION_VECTORS_JITTER_CANCELLATION |
| DepthInverted | DEPTH_INVERTED |
| AutoExposure | AUTO_EXPOSURE |

DLSS gives no camera data, so near, far and FOV use defaults. You can
override them with `NGX_FSR4_NEAR`, `NGX_FSR4_FAR` and `NGX_FSR4_FOV`
(radians).

## Build

Needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```sh
make                      # or: make LLVM_MINGW=/path/to/llvm-mingw
```

Output: `build/nvngx.dll`, `build/harness.exe`, `build/gpu.exe`.

## The FSR 4 runtime

Not included. Put AMD's `amd_fidelityfx_upscaler_dx12.dll` (FidelityFX
SDK 2.x, with FSR 4) next to `nvngx.dll`, or point `NGX_FSR4_FFX_DLL` at it.
`NGX_FSR4_VERSION=3.1` (any substring of a version name) picks a different
version from the ones the DLL offers.

## Test

```sh
# CPU checks (any Wine)
cd build && wine harness.exe nvngx.dll

# GPU check under Proton (vkd3d-proton), with the FSR DLL copied into build/
STEAM_COMPAT_DATA_PATH=$PWD/pfx STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam \
  /path/to/proton waitforexitandrun $PWD/gpu.exe
```

Results on an RX 9070 XT, Proton-tkg 11 and FSR 4.1.1b:

```
FSR: version offered: 4.1.1b / 3.1.5 / 2.3.4
FSR: using 4.1.1b
FSR: ffxCreateContext max render 2560x1440 -> 2560x1440 flags 0x40: rc=0
EvaluateFeature ×16 ... ReleaseFeature evals=16
```

The 2560×1440 output matches the test card (gradient, checkerboard, a
sharp 1-px diagonal).

## Logging

Every call goes to `ngx-fsr4.log` next to the DLL, flushed line by line:

- the first 3 frames in full, with each input's size and format;
- after that, one line every 600 frames.

`NGX_FSR4_QUIET=1` drops the per-parameter lines.

## Not done yet

- Trying a real game. That needs the game to see an NVIDIA GPU (e.g. dxvk-nvapi
  with GPU spoofing) and the NGX loader pointed at this DLL.
- Frame generation, ray reconstruction, D3D11 and Vulkan paths.
- Reactive and transparency masks (DLSS's bias mask could map to FSR's reactive mask).

## Licence

MIT for this project's code (see `LICENSE`). `third_party/ffx` is AMD's,
under its own MIT licence (`third_party/ffx/docs/license.md`). DLSS and NGX
are NVIDIA trademarks; FSR and FidelityFX are AMD trademarks. This project
is not affiliated with either company.
