# ngx-fsr4

An experimental stand-in for NVIDIA's NGX runtime (`_nvngx.dll`) that answers
a game's **DLSS Super Resolution** calls with **AMD FSR 4** instead, for
RDNA4 GPUs on Linux (Proton / vkd3d-proton).

The game asks for DLSS as usual. This DLL says DLSS is available, takes the
game's frame inputs (colour, depth, motion vectors, jitter and so on), and runs
an FSR 4 upscale on the game's own command list through AMD's public
FidelityFX API.

> **Status: experiment.** Verified on one system (RX 9070 XT, Proton-tkg,
> FSR 4.1.1b): the synthetic GPU test below, and two real games' own DLSS
> paths - *Boundary* (stable over a long session) and the *Black Myth: Wukong*
> benchmark tool. In the Black Myth benchmark it ran slower than the game's
> built-in FSR 4 option, so treat it as a compatibility route for DLSS-only
> games, not a speed-up. Frame generation and ray reconstruction are refused
> for now, so games fall back cleanly. For everyday use, [OptiScaler](https://github.com/optiscaler/OptiScaler)
> is the mature project that does this and much more.

**Current version: 0.1.0** ([what changed](CHANGELOG.md)). Step-by-step instructions,
every option and troubleshooting: **[docs/USER-GUIDE.md](docs/USER-GUIDE.md)**.

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

The jitter offset is passed through unchanged by default. If a game's image
shimmers or smears in a way that looks like the jitter is applied the wrong way
round, `NGX_FSR4_JITTER_SIGN=-1` flips its sign (the value multiplies both jitter
components; default `1`).

All of these are read from the game's environment, so set them for the launch,
e.g. `NGX_FSR4_JITTER_SIGN=-1 ngx-fsr4 run APPID`.

## Build

Needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```sh
make                      # or: make LLVM_MINGW=/path/to/llvm-mingw
```

Output: `build/nvngx.dll`, `build/harness.exe`, `build/gpu.exe`.
`make check` runs the checks that need no GPU and no Wine (the DLL's exports, and
the helper against a fake Steam library). `make dist` makes the release tarball.

## Install

Prebuilt files are on the [Releases page](https://github.com/eugenesflanagin-spec/ngx-fsr4/releases)
(check them with `sha256sum -c SHA256SUMS --ignore-missing`):

- **Arch Linux:** `sudo pacman -U ngx-fsr4-git-*-any.pkg.tar.zst`
- **Any distribution:** `ngx-fsr4-<version>-proton.tar.gz`, use it in place; see `INSTALL.txt` inside.

Or build the Arch package from this repo:

```sh
cd packaging && makepkg -si      # needs llvm-mingw (AUR) installed first
```

Either way it installs `/usr/lib/ngx-fsr4/nvngx.dll` (plus the two test programs),
the `ngx-fsr4` command and the docs in `/usr/share/doc/ngx-fsr4/`. Remove it with
`ngx-fsr4 disable APPID` for each game in `ngx-fsr4 status`, then `pacman -R ngx-fsr4-git`.
After an update, `disable` and `enable` each game again: `enable` copies the DLL.

## Use it in a game

```sh
ngx-fsr4 enable APPID [--ffx /path/to/amd_fidelityfx_upscaler_dx12.dll]
ngx-fsr4 run APPID [--proton DIR] [--exe PATH] [--dry-run]
                          # launches with NVIDIA spoofing so the game offers DLSS
ngx-fsr4 log APPID        # follow the newest log
ngx-fsr4 disable APPID    # put everything back
```

`enable` finds the FSR runtime in this order: `--ffx PATH`, `$NGX_FSR4_FFX`,
`~/.local/share/ngx-fsr4/amd_fidelityfx_upscaler_dx12.dll`,
`/usr/lib/ngx-fsr4/amd_fidelityfx_upscaler_dx12.dll`, then the newest version
folder under `~/.local/share/goverlay/FSR4/`. If none exists it stops and says
where to put the file.

`run` picks Proton in this order: `--proton DIR`, `$NGX_FSR4_PROTON`, the
compatibility tool Steam is set to use for this game (or Steam's global default),
then the only Proton build installed (`compatibilitytools.d/*` or
`steamapps/common/Proton*`). With several builds and no Steam setting it lists
them and asks for `--proton`. The game executable is the one `.exe` in the game
folder (crash handlers, setup and uninstall programs ignored); if there are
several, it lists them and asks for `--exe`. `--dry-run` prints the Proton and
executable it would use and launches nothing.

`log` follows the newest `ngx-fsr4.log`: the DLL writes it beside whichever copy
was loaded (the game folder or the prefix's `system32`), so it checks every place
`enable` put a copy, plus `system32` and the game folder.

`enable` makes four kinds of change:

- copies the DLL into the game's Proton prefix as `system32/_nvngx.dll`, and beside the
  game as `nvngx.dll` (the NGX loader checks there first);
- copies the FSR runtime beside it;
- sets `EnableSignatureOverride=1` under `NGXCore` in the prefix registry.
  NVIDIA's loader otherwise ignores an unsigned `_nvngx.dll`. This is the
  same switch OptiScaler ships as `EnableSignatureOverride.reg`;
- moves OptiScaler's `dxgi.dll` and fakenvapi's `nvapi64.dll` out of the
  game folder, because both also answer DLSS calls.

Many games only offer DLSS when they see an NVIDIA GPU. If Proton's
spoofing is not enough (it wasn't for Boundary), use
`ngx-fsr4 enable APPID --optiscaler-spoof`. This keeps OptiScaler for the
spoofing (it also makes the game's loader honour the signature override) but
turns off all its input hooks (DLSS, XeSS, FSR 2/3, FidelityFX), so neither the
game's DLSS calls nor this DLL's FSR calls go through it. Launch with `ngx-fsr4 run APPID`: launchers such as
GOverlay's rewrite OptiScaler.ini at every start, and `run` re-applies the
setting each time.

Before changing anything, it records each change in a manifest under
`~/.local/share/ngx-fsr4/backups/`. `disable` replays that manifest in
reverse and checks each file's checksum. Files that belong to the game
itself are left alone. In the registry, `disable` removes only the value
`enable` added (and the `NGXCore` key itself if `enable` created it and nothing
else was added since); the rest of `system.reg` is not touched. A copy of
`system.reg` from before `enable` stays in the backup folder for manual
recovery. The `ngx-fsr4.log` files beside each DLL copy are moved into the backup
folder (`logs/`). Backups made by older versions restored the whole `system.reg`
file; `disable` still does that for them.

## The FSR 4 runtime

Not included. Put AMD's `amd_fidelityfx_upscaler_dx12.dll` (FidelityFX SDK 2.x, with FSR 4)
next to `nvngx.dll` as `ngx-fsr4-upscaler.dll` (the tool does this), or point
`NGX_FSR4_FFX_DLL` at it. The unique name matters: another module of the same
name already loaded in the game (OptiScaler's) would be used instead.
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

Every call goes to `ngx-fsr4.log` next to the copy of the DLL that loaded (usually
the game folder, sometimes the prefix's `system32`), flushed line by line. The
first line names the ngx-fsr4 version. `ngx-fsr4 log APPID` finds and follows the newest.

- the first 3 frames in full, with each input's size and format;
- after that, one line every 600 frames.

`NGX_FSR4_QUIET=1` drops the per-parameter lines.

## Not done yet

- Testing on more games and GPUs (so far: two games, one RX 9070 XT).
- Frame generation, ray reconstruction, D3D11 and Vulkan paths.
- Reactive and transparency masks (DLSS's bias mask could map to FSR's reactive mask).

## Credits

- AMD's FidelityFX API headers in `third_party/ffx` (MIT, unmodified) from the
  [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK).
- NVIDIA's public NGX SDK headers ([NVIDIA/DLSS](https://github.com/NVIDIA/DLSS)) as the
  interface reference; not redistributed.
- Built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).
- [OptiScaler](https://github.com/optiscaler/OptiScaler) for documenting the signature
  override and camera defaults; [dxvk-nvapi](https://github.com/jp7677/dxvk-nvapi) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton);
  [GOverlay](https://github.com/benjamimgois/goverlay) for the FSR 4 runtime download.

Details and licences: [CREDITS.md](CREDITS.md).

## Licence

MIT for this project's code (see `LICENSE`). `third_party/ffx` is AMD's,
under its own MIT licence (`third_party/ffx/docs/license.md`). DLSS and NGX
are NVIDIA trademarks; FSR and FidelityFX are AMD trademarks. This project
is not affiliated with either company.
