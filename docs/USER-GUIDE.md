# ngx-fsr4 — User Guide

When a game asks for DLSS Super Resolution, ngx-fsr4 answers and runs AMD FSR 4 instead.
It is for AMD Radeon GPUs on Linux, with Steam's Proton.

This guide is for people installing and using ngx-fsr4, from first-time Proton users to people
who want to debug a game's upscaler calls. It starts in plain words, then gives the detail, then
every option. A glossary is at the end.

> **Status: experimental.** It works for DLSS **Super Resolution** in the games tested so far
> (section 9). Frame generation, ray reconstruction, DirectX 11 and Vulkan games are **not**
> handled. For everyday use, [OptiScaler](https://github.com/optiscaler/OptiScaler) is the
> mature, much larger project that does this and more. ngx-fsr4 is a small, readable
> alternative that you can audit.

---

## 1. In plain words

Many games include NVIDIA DLSS but not AMD's newest upscaler, FSR 4. DLSS needs an NVIDIA
card. When a game wants DLSS, it loads NVIDIA's helper library `_nvngx.dll` (the "NGX runtime")
and asks it to upscale each frame.

ngx-fsr4 is a **stand-in for that one library**. It:

1. tells the game "yes, DLSS Super Resolution is available";
2. takes what the game hands over every frame (the low-resolution image, depth, motion vectors,
   camera jitter, exposure);
3. runs **FSR 4** on those inputs, using AMD's own FidelityFX runtime DLL, on the game's own GPU
   command list.

The game thinks it is running DLSS. What actually runs is FSR 4. You pick the DLSS quality
modes (Quality, Balanced, Performance, ...) in the game's own menu as usual.

The package also has a helper command, `ngx-fsr4`, that puts the files into one Steam game's
Proton prefix, records every change, and can undo them all.

### Who it is for

- Owners of AMD **Radeon RX 9000 series** (RDNA 4) cards who play DirectX 12 games that offer
  DLSS but not FSR 4.
- People who want to see exactly what a game sends to DLSS: the log records every call.
- Developers curious how the NGX interface works. The code is small and was written only from
  public headers.

---

## 2. Requirements

| need | detail |
|---|---|
| GPU | **AMD RDNA 4** (Radeon RX 9070 / 9070 XT / 9060 family) for FSR 4. FSR 4 is AMD's machine-learning upscaler and needs RDNA 4. Older Radeon cards can only use the older FSR 3.1 path that the same AMD DLL offers (`NGX_FSR4_VERSION=3.1`, untested). |
| OS | Linux, any distribution with Steam. The helper command and package target **Arch Linux**. The DLL itself works anywhere Proton does. |
| Proton | A Proton with **vkd3d-proton** (all current Valve Proton, Proton-GE and proton-tkg builds). `ngx-fsr4 run` uses the Proton Steam is set to use for the game; `--proton DIR` picks another (section 6). |
| Game | A **DirectX 12** game that offers **DLSS Super Resolution**, installed through Steam and started once (so its Proton prefix exists). |
| FSR runtime | AMD's `amd_fidelityfx_upscaler_dx12.dll` from FidelityFX SDK 2.x, a version that includes FSR 4 (tested: 4.1.1b). **Not included** (section 4). |
| Tools | `bash`, `python3`, `sha256sum`, `pgrep` for the helper. To build: `git` and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (AUR: `llvm-mingw`, installed at `/opt/llvm-mingw`). |

Tested on: Radeon RX 9070 XT, proton-tkg (Wine 11), FSR 4.1.1b, Arch Linux, kernel 7.3.

---

## 3. Install

### 3a. Arch Linux (recommended)

**Prebuilt:** download `ngx-fsr4-git-<version>-1-any.pkg.tar.zst` from the
[Releases page](https://github.com/eugenesflanagin-spec/ngx-fsr4/releases), check it against
`SHA256SUMS` (`sha256sum -c SHA256SUMS --ignore-missing`), then:

```sh
sudo pacman -U ngx-fsr4-git-*-any.pkg.tar.zst
```

**From source:** the repository includes a PKGBUILD. It is not on the AUR; build it yourself:

```sh
git clone https://github.com/eugenesflanagin-spec/ngx-fsr4.git
cd ngx-fsr4/packaging
makepkg -si
```

`makepkg` can only install dependencies from the official repositories, and `llvm-mingw` is
on the AUR. **Install it first** (`yay -S llvm-mingw`), then run `makepkg -si`. The package is
named `ngx-fsr4-git` and installs:

| path | what |
|---|---|
| `/usr/bin/ngx-fsr4` | the helper command |
| `/usr/lib/ngx-fsr4/nvngx.dll` | the stand-in DLL |
| `/usr/lib/ngx-fsr4/harness.exe`, `gpu.exe` | test programs (section 7) |
| `/usr/share/doc/ngx-fsr4/` | README, this guide, CHANGELOG, CREDITS |
| `/usr/share/licenses/ngx-fsr4-git/` | MIT licence + AMD FidelityFX licence |

### 3b. Prebuilt tarball (any distribution)

Download `ngx-fsr4-<version>-proton.tar.gz` from the
[Releases page](https://github.com/eugenesflanagin-spec/ngx-fsr4/releases) and unpack it. It holds
the DLL, the two test programs, the helper and the docs. Use the helper from there:

```sh
tar xf ngx-fsr4-*-proton.tar.gz && cd ngx-fsr4-*-proton
NGX_FSR4_LIB=$PWD ./tools/ngx-fsr4 enable APPID
```

`INSTALL.txt` inside it shows how to install it system-wide instead.

### 3c. From source (any distribution)

```sh
git clone https://github.com/eugenesflanagin-spec/ngx-fsr4.git
cd ngx-fsr4
make                               # uses /opt/llvm-mingw
make LLVM_MINGW=/path/to/llvm-mingw   # if it lives elsewhere
```

Output: `build/nvngx.dll`, `build/harness.exe`, `build/gpu.exe`. `make check` runs the checks
that need no GPU and no Wine (the DLL's exports, and the helper against a fake Steam library).

To use the helper without the package, point it at your build folder:

```sh
NGX_FSR4_LIB=$PWD/build ./tools/ngx-fsr4 enable APPID
```

---

## 4. Get the FSR 4 runtime

ngx-fsr4 doesn't contain FSR. It loads AMD's signed runtime DLL,
`amd_fidelityfx_upscaler_dx12.dll`, which is distributed by AMD. Ways to get it:

- **GOverlay** downloads it into `~/.local/share/goverlay/FSR4/<version>/`.
- AMD's **FidelityFX SDK 2.x** release packages (GPUOpen) ship the prebuilt signed DLL.
- Some games ship it themselves.

`ngx-fsr4 enable` looks for it in this order: `--ffx PATH`, the `NGX_FSR4_FFX` variable,
`~/.local/share/ngx-fsr4/amd_fidelityfx_upscaler_dx12.dll`,
`/usr/lib/ngx-fsr4/amd_fidelityfx_upscaler_dx12.dll`, then the newest version folder under
`~/.local/share/goverlay/FSR4/`. It prints which file it used. If it finds none it stops and
tells you where to put one; the simplest is to copy it to `~/.local/share/ngx-fsr4/`.

The helper copies it next to the stand-in under a **unique name**, `ngx-fsr4-upscaler.dll`.
That matters: if another module of the same name is already loaded in the game (OptiScaler
loads one), Windows/Wine would hand back that copy instead of ours.

---

## 5. Everyday use

The game is identified by its **Steam App ID**: the number in its store URL, or in
`steamapps/appmanifest_<APPID>.acf`. Examples below use `1420640` (Boundary: Benchmark).

### The usual sequence

```sh
ngx-fsr4 enable 1420640            # install into the game's prefix (game must be closed)
ngx-fsr4 run 1420640               # launch it with an NVIDIA identity so the DLSS menu appears
# in the game: graphics settings -> DLSS Super Resolution -> pick a quality mode
ngx-fsr4 log 1420640               # in another terminal: follow the log
ngx-fsr4 disable 1420640           # when you are done: put everything back
```

### If the game hides DLSS (and you have OptiScaler from GOverlay)

Many games only show DLSS when they see an NVIDIA GPU. `ngx-fsr4 run` already sets Proton's
NVIDIA spoofing (section 6). If that isn't enough and the game folder has OptiScaler (for
example installed by GOverlay), use:

```sh
ngx-fsr4 enable 1420640 --optiscaler-spoof
ngx-fsr4 run 1420640
```

This **keeps** OptiScaler, but only for its GPU spoofing. It turns off all eight of its input
hooks (`EnableDlssInputs`, `EnableXeSSInputs`, `EnableFsr2Inputs`, `UseFsr2Inputs`,
`EnableFsr3Inputs`, `UseFsr3Inputs`, `EnableFfxInputs`, `UseFfxInputs` → `false`), so neither the
game's DLSS calls nor ngx-fsr4's FSR calls pass through it. OptiScaler being present also makes
the game's NGX loader honour the signature override in some games (Boundary was one).

Launch with `ngx-fsr4 run`, **not** the Steam Play button. Launchers such as GOverlay's rewrite
`OptiScaler.ini` at every start. `run` re-applies the "hooks off" settings each time before it
launches.

### What `enable` changes

Before it changes anything, `enable` writes a **manifest** under
`~/.local/share/ngx-fsr4/backups/<APPID>-<date>-<time>/` and backs up every file it will
replace. It then:

1. copies the stand-in into the prefix as `drive_c/windows/system32/_nvngx.dll`, and the FSR
   runtime as `system32/ngx-fsr4-upscaler.dll`;
2. for every game folder that contains `nvngx_dlss.dll` or `sl.interposer.dll`, also copies the
   stand-in there as `nvngx.dll`, plus `ngx-fsr4-upscaler.dll`. Many NGX loaders look beside
   the game first; in the tested games this is the copy that actually loads;
3. adds `"EnableSignatureOverride"=dword:00000001` under
   `HKLM\Software\NVIDIA Corporation\Global\NGXCore` in the prefix's `system.reg` (and creates
   `FullPath=C:\Windows\System32` if the key doesn't exist). The manifest records exactly
   which of the two it did, and a copy of `system.reg` from before is kept in the backup. Without this, NVIDIA's loader
   ignores an unsigned `_nvngx.dll`. It is the same switch OptiScaler ships as
   `EnableSignatureOverride.reg`;
4. without `--optiscaler-spoof`: **moves aside** OptiScaler's `dxgi.dll`, `OptiScaler.ini`,
   `OptiScaler.log`, and fakenvapi's `nvapi64.dll`, `fakenvapi.ini`, `fakenvapi.log`, because
   they would answer DLSS calls too. A `dxgi.dll` or `nvapi64.dll` is only moved if it really is
   OptiScaler's / fakenvapi's. The game's own files are never touched.

It refuses to run while the game is running, or if it is already enabled for that game.

### What `disable` does

It reads the manifest **backwards**: it deletes added files, puts moved files back, restores
replaced files, removes the registry value it added, and moves the `ngx-fsr4.log` files from
beside each copy of the DLL into the backup folder (`logs/`). Before restoring each backup it checks the SHA-256 checksum. If any backup is
missing or doesn't match, it stops, says which, and keeps the backup folder. Backups are kept
after a successful disable too, so you can delete old ones by hand.

Two things to know:

- In `system.reg` only the `EnableSignatureOverride` value is removed (and the `NGXCore` key,
  if `enable` created it and nothing else was added to it since). Everything else Wine or the
  game wrote there in the meantime stays. Backups made by older versions of ngx-fsr4 still
  restore the whole file, as those versions did.
- If GOverlay manages the game, its launcher copies OptiScaler back on its next launch anyway.

---

## 6. Reference: the `ngx-fsr4` command

```
ngx-fsr4 enable APPID [--ffx PATH] [--optiscaler-spoof]
ngx-fsr4 disable APPID
ngx-fsr4 status [APPID]
ngx-fsr4 run APPID [--proton DIR] [--exe PATH] [--dry-run]
ngx-fsr4 log APPID
ngx-fsr4 --version
ngx-fsr4 help | -h | --help
```

| command / option | meaning |
|---|---|
| `enable APPID` | Install into that game's prefix (section 5). |
| `--ffx PATH` | FSR runtime to use. Default: found automatically (section 4). |
| `--optiscaler-spoof` | Keep OptiScaler for GPU spoofing only; switch off its input hooks. |
| `disable APPID` | Undo everything `enable` did, using its manifest. |
| `status [APPID]` | List games where it is enabled, with their backup folder. Prints `not enabled for any game` otherwise. |
| `run APPID` | Launch the game directly through Proton with NVIDIA spoofing. Steam should be running. |
| `--proton DIR` | Proton folder to use (the one containing the `proton` script). Default: `$NGX_FSR4_PROTON`, else the compatibility tool Steam is set to use for this game (or Steam's global default), else the only Proton installed in `compatibilitytools.d` or `steamapps/common/Proton*`. If there are several and Steam names none, it lists them and asks you to choose. |
| `--exe PATH` | Game executable. Default: the single `*.exe` in the game's top folder (crash handlers, setup and uninstall programs are ignored). If there are several, it lists them and asks for `--exe`. |
| `--dry-run` | With `run`: print the Proton and executable that would be used, launch nothing. |
| `--version` | Print the helper's version. |
| `log APPID` | Follow (`tail -F`) the newest `ngx-fsr4.log`: it checks every place `enable` put a copy of the DLL, the prefix's `system32` and the game folder, and names any older logs it found. |

Running `ngx-fsr4` with no arguments prints the usage and exits with status 1.

### What `run` sets

| variable | value | why |
|---|---|---|
| `STEAM_COMPAT_DATA_PATH`, `STEAM_COMPAT_CLIENT_INSTALL_PATH` | the game's compatdata, your Steam folder | what Proton expects |
| `SteamAppId`, `SteamGameId` | the App ID | Steam API / overlay |
| `PROTON_FORCE_NVAPI=1`, `DXVK_NVAPI_ALLOW_OTHER_DRIVERS=1` | | enable dxvk-nvapi on an AMD card |
| `DXVK_NVAPI_GPU_ARCH` | `AD100` unless you set it | report an Ada (RTX 40) GPU |
| `DXVK_NVAPI_DRIVER_VERSION` | `58000` unless you set it | report an NVIDIA driver version |
| `DXVK_CONFIG` | `dxgi.hideAmdGpu=True;dxgi.customVendorId=10de;dxgi.customDeviceId=2684` + yours | appear as an RTX 4090 |

It doesn't turn on Proton logging, which costs real frame time. Set `PROTON_LOG=1` yourself if
you need it.

### Helper settings (environment)

| variable | default | meaning |
|---|---|---|
| `NGX_FSR4_LIB` | `/usr/lib/ngx-fsr4` | where `nvngx.dll` is taken from |
| `XDG_DATA_HOME` | `~/.local/share` | backups and state go in `$XDG_DATA_HOME/ngx-fsr4/` |
| `STEAM_ROOT` | `~/.local/share/Steam` | your Steam folder. Extra libraries are read from `steamapps/libraryfolders.vdf`. |
| `NGX_FSR4_FFX` | (search, section 4) | FSR runtime for `enable` when `--ffx` isn't given |
| `NGX_FSR4_PROTON` | (Steam's setting) | Proton folder for `run` when `--proton` isn't given |

State files: `~/.local/share/ngx-fsr4/enabled/<APPID>` holds the path of the active backup
folder. Each manifest is a tab-separated text file with one line per change:
`added|replaced|moved <path> <backup> <sha256>`, plus `log <path>` for each log location,
`regvalue <system.reg> value|section` for the registry change, and `snapshot <system.reg>
<copy> <sha256>` for the pre-`enable` copy (kept for manual recovery, not restored).

---

## 7. Reference: settings for the DLL itself

These are read **inside the game process**, so they must reach Wine. Under `ngx-fsr4 run`,
put them in front of the command (`NGX_FSR4_QUIET=1 ngx-fsr4 run APPID`). Under a Steam launch,
put them in the game's launch options (`NGX_FSR4_QUIET=1 %command%`).

| variable | default | meaning |
|---|---|---|
| `NGX_FSR4_FFX_DLL` | *(unset)* | Full Windows path of the FidelityFX DLL to load. If unset, the DLL looks beside itself for `ngx-fsr4-upscaler.dll`, then `amd_fidelityfx_upscaler_dx12.dll`, then `amd_fidelityfx_dx12.dll`. |
| `NGX_FSR4_VERSION` | newest 4.x | Choose a different upscaler version from those the AMD DLL offers; any part of the version name works, e.g. `3.1` or `4.0.2`. The log lists the offered versions. |
| `NGX_FSR4_QUIET` | `0` | `1` drops the per-parameter lines from the log. |
| `NGX_FSR4_NEAR` | `0.1` | Camera near plane. DLSS doesn't pass camera data, but FSR wants it. |
| `NGX_FSR4_FAR` | `100000` | Camera far plane. |
| `NGX_FSR4_FOV` | `1.0471976` (60°) | Vertical field of view, in **radians**. |
| `NGX_FSR4_JITTER_SIGN` | `1` | Multiplies the jitter offset. Try `-1` if the image shimmers or looks smeared in a way FSR normally fixes. Some engines use the opposite sign convention. |

The camera defaults match OptiScaler's. Most games look right with them. Change them only if
you see artefacts at a distance or on thin geometry.

### DLSS → FSR mapping

| DLSS create flag | FSR flag |
|---|---|
| IsHDR | HIGH_DYNAMIC_RANGE |
| motion vectors not low-res | DISPLAY_RESOLUTION_MOTION_VECTORS |
| MVJittered | MOTION_VECTORS_JITTER_CANCELLATION |
| DepthInverted | DEPTH_INVERTED (near/far are swapped to match) |
| AutoExposure | AUTO_EXPOSURE |

DLSS sharpness above 0 turns on FSR's sharpening at the same strength. The frame time is taken
from the game. A value under 0.1 is treated as seconds and converted, because some games pass
seconds where DLSS expects milliseconds. If the game passes none, it is measured.

### Quality modes (render scale the game is told to use)

| DLSS mode | scale per axis | 2560×1440 output renders at |
|---|---|---|
| DLAA | 1.00 | 2560×1440 |
| Ultra Quality | 0.77 | 1971×1109 |
| Quality | 0.667 | 1707×960 |
| Balanced | 0.58 | 1485×835 |
| Performance | 0.50 | 1280×720 |
| Ultra Performance | 0.333 | 853×480 |

Dynamic resolution is allowed down to one-third of the output size.

### What the DLL answers

- `SuperSampling.Available = 1`, no driver update needed.
- `FrameGeneration.Available`, `RayReconstruction.Available` and
  `SuperSamplingDenoising.Available` are all 0, and creating any feature other than Super
  Resolution fails with "feature not supported". Games then fall back cleanly.
- All D3D11 and CUDA NGX entry points return "platform not supported". DirectSR and a few
  query functions are declined.

---

## 8. Check that it works

1. **The log.** Every call is logged to `ngx-fsr4.log` in the folder of the copy of the DLL
   that was loaded. That is usually the **game folder**, next to the game's `nvngx_dlss.dll`
   (for example `.../Binaries/Win64/ngx-fsr4.log`). It can also be the prefix's
   `drive_c/windows/system32/`. `ngx-fsr4 log APPID` finds the newest one in either place and
   follows it. The first line names the ngx-fsr4 version that loaded. If it says `no log yet`, the DLL hasn't loaded yet (start the game and pick DLSS).

   A working start looks like:

   ```
   === ngx-fsr4 0.1.0 loaded into C:\...\Game-Win64-Shipping.exe (pid 1234)
   NVSDK_NGX_D3D12_Init_with_ProjectID project=... engine=...
   NVSDK_NGX_D3D12_Init_with_ProjectID app=0 sdk=0x13 device=... data=...
   FSR: loaded C:\...\ngx-fsr4-upscaler.dll
   FSR: version offered: 4.1.1b (id 0x...)
   FSR: version offered: 3.1.5 (id 0x...)
   FSR: using 4.1.1b
   FSR: ffxCreateContext max render 2560x1440 -> 2560x1440 flags 0x40: rc=0
   ```

   The first 3 frames are logged in full, with each input's size and format. After that, one
   line every 600 frames.

2. **In the game.** DLSS options appear and can be selected. The image is sharp at Quality or
   Performance and the frame rate rises the way an upscaler should make it rise.

3. **CPU self-test (any Wine, no GPU work):**

   ```sh
   cd /usr/lib/ngx-fsr4      # or build/
   wine harness.exe nvngx.dll
   ```

4. **GPU self-test (short, renders one test card).** It upscales a 1280×720 test image to
   2560×1440 and writes `out.ppm`. Copy the FSR DLL into the same folder first:

   ```sh
   cd build
   cp /path/to/amd_fidelityfx_upscaler_dx12.dll ngx-fsr4-upscaler.dll
   STEAM_COMPAT_DATA_PATH=$PWD/pfx STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam \
     /path/to/proton waitforexitandrun $PWD/gpu.exe
   ```

   Expected: `FSR: using 4.1.1b`, 16 `EvaluateFeature` calls, and an `out.ppm` showing the
   gradient, checkerboard and a sharp one-pixel diagonal.

---

## 9. Tested games and known limits

| game (App ID) | result |
|---|---|
| Boundary: Benchmark (1420640) | DLSS SR → FSR 4.1.1b working, stable over thousands of frames. Needed `--optiscaler-spoof` and `ngx-fsr4 run`. |
| Black Myth: Wukong Benchmark Tool (3132990) | DLSS SR → FSR 4.1.1b working. Frame rates matched the game's DLSS path through OptiScaler, but were well below the game's own built-in FSR 4 option: use that when a game has it. |

Limits:

- **Super Resolution only.** No frame generation, no ray reconstruction (games that require
  RR for path tracing will refuse or fall back).
- **DirectX 12 only.** DX11 and Vulkan DLSS games are not handled.
- **No reactive / transparency mask.** DLSS's bias mask is not mapped yet, so particles and
  transparent effects may ghost a little more than with native FSR 4.
- **No camera data from DLSS.** Defaults are used (section 7).
- **Proton only.** It is a Windows DLL. Native Linux games don't use NGX.
- **Steam only** for the helper. Non-Steam games need the files copied by hand (section 12).
- **Online games and anti-cheat.** Replacing `_nvngx.dll`, adding `nvngx.dll` to the game
  folder and setting `EnableSignatureOverride` are exactly the kinds of change anti-cheat
  systems look for. **Don't use this in online or competitive games.** You could be kicked or
  banned. Use it in single-player games and benchmarks.

---

## 10. Troubleshooting

| symptom | cause and fix |
|---|---|
| `app N is not installed in any Steam library` | Wrong App ID, or the library isn't in `libraryfolders.vdf`. Set `STEAM_ROOT` if Steam lives elsewhere (e.g. `~/.steam/steam`). Flatpak Steam: `STEAM_ROOT=~/.var/app/com.valvesoftware.Steam/.local/share/Steam`. |
| `... has no Proton prefix yet` | Start the game once from Steam with Proton, then quit. |
| `FSR runtime not found` | Pass `--ffx PATH` (section 4). |
| `/usr/lib/ngx-fsr4/nvngx.dll missing` | The package isn't installed. Install it, or set `NGX_FSR4_LIB`. |
| `already enabled for ...` | Run `ngx-fsr4 disable APPID` first. |
| `... is running; quit it first` | Close the game (and its launcher). |
| `no Proton found ...; pass --proton DIR` | No Proton in `compatibilitytools.d` or `steamapps/common`. Pass it: `--proton ~/.local/share/Steam/steamapps/common/"Proton - Experimental"` or `--proton ~/.local/share/Steam/compatibilitytools.d/GE-Proton…`. |
| `several Proton builds found and Steam names none` | Steam has no compatibility tool set for this game (or globally). Pick one from the list with `--proton DIR`, or set `NGX_FSR4_PROTON`. |
| `several .exe files in ...; pass --exe PATH` | The game folder has more than one program. Pick the game's own `.exe` from the list with `--exe PATH`. `ngx-fsr4 run APPID --dry-run` shows what would be used without starting anything. |
| `warning: a wineserver is running` | Another Wine program is running and may rewrite `system.reg` when it exits, losing the override. Close it and re-run `disable` + `enable`. |
| DLSS greyed out / missing in the menu | The game doesn't see an NVIDIA GPU. Launch with `ngx-fsr4 run`. If that isn't enough, use `--optiscaler-spoof` with OptiScaler present. |
| "Ray tracing not supported" or similar errors with spoofing | Seen in Boundary without OptiScaler: the loader didn't honour the override. Use `--optiscaler-spoof`. |
| No `ngx-fsr4.log` anywhere | The DLL never loaded: the game uses something else for DLSS (Streamline-only path, OptiScaler still answering), or the game isn't DX12. Check that `nvngx.dll` is beside the game's `nvngx_dlss.dll`. |
| Log shows `could not load the FidelityFX upscaler DLL` | The runtime is missing beside the loaded copy. Re-run `enable` with `--ffx`, or set `NGX_FSR4_FFX_DLL`. |
| Log shows `using 3.1.5` or `(default)` | The AMD DLL doesn't offer FSR 4 (old DLL), or the GPU isn't RDNA 4. Use a 4.x DLL. |
| Log shows `ffxDispatch rc=...` | FSR rejected an input. Send the first 3 frames of the log with your report. |
| Smearing, ghosting or shimmer | Try `NGX_FSR4_JITTER_SIGN=-1`. Check the game's motion-blur and sharpening settings. |
| `checksum mismatch` / `missing backup` on disable | A backup was edited or deleted. Nothing was overwritten for that entry; fix by hand from the backup folder (paths are in the manifest). |
| Settings reset to OptiScaler after a Steam launch | GOverlay's launcher rewrote them. Use `ngx-fsr4 run`. |

When you report a problem, include: the game and App ID, Proton version, FSR DLL version, and
the first ~200 lines of `ngx-fsr4.log`.

---

## 11. Updating

Prebuilt: `sudo pacman -U` the newer package from the Releases page. From source:

```sh
cd ngx-fsr4/packaging && makepkg -si      # rebuilds from the latest commit
```

`ngx-fsr4 --version` shows the helper's version; the first line of `ngx-fsr4.log` shows the
DLL's.

After updating, **re-run `enable`** for each game (`disable` then `enable`), because `enable`
**copies** the DLL into the prefix and game folder. The copies don't update themselves.

## 12. Uninstall

1. For every game listed by `ngx-fsr4 status`: `ngx-fsr4 disable APPID`.
2. `sudo pacman -R ngx-fsr4-git`.
3. Optional: `rm -rf ~/.local/share/ngx-fsr4` (old backups and state).
4. Logs: `disable` moves the `ngx-fsr4.log` files into the backup folder, so step 3 removes
   them too. (Games enabled with an older version have no log entries in their manifest;
   delete those logs by hand if you like.)

### Manual install (non-Steam or other launchers)

Copy `nvngx.dll` next to the game's `nvngx_dlss.dll` and, as `_nvngx.dll`, into the prefix's
`drive_c/windows/system32/`. Copy the FSR runtime beside each as `ngx-fsr4-upscaler.dll`.
Import a `.reg` that sets `EnableSignatureOverride=dword:1` under
`HKLM\Software\NVIDIA Corporation\Global\NGXCore`. Keep backups yourself; nothing will undo it
for you.

---

## 13. FAQ

**Is this DLSS?** No. No NVIDIA code or DLSS model runs. The game *talks* DLSS. The upscaling
is AMD's FSR 4.

**Does it include NVIDIA or AMD files?** No NVIDIA files at all. From AMD, only the MIT-licensed
FidelityFX API **headers** are included (for building). The FSR runtime DLL comes from AMD
separately.

**Is it better than OptiScaler?** It does far less. OptiScaler supports many more inputs and
outputs, frame generation, an in-game menu, DX11/Vulkan, and many games. ngx-fsr4 is small,
single-purpose and fully logged. In the tested games, image and frame rate matched OptiScaler's
DLSS path.

**Can I use it on an NVIDIA card?** There is no reason to: you have real DLSS.

**Can I use it on RDNA 2/3?** FSR 4 needs RDNA 4. `NGX_FSR4_VERSION=3.1` may give FSR 3.1
through the same AMD DLL; this is untested.

**Why does it need a registry change?** NVIDIA's loader only loads a signed `_nvngx.dll` unless
the NGXCore signature override is set. The change is inside the game's own Proton prefix only
and is undone by `disable`.

**Will Steam updates undo it?** A game update can replace files in the game folder. Run
`disable` and `enable` again if DLSS stops working after an update.

---

## 14. Glossary

- **DLSS** — NVIDIA Deep Learning Super Sampling, NVIDIA's upscaler. **DLSS SR** = Super
  Resolution, the upscaling part.
- **NGX** — NVIDIA's runtime framework that games call to use DLSS. `_nvngx.dll` is its core
  library. `nvngx_dlss.dll` is the DLSS model library a game ships.
- **FSR 4** — AMD FidelityFX Super Resolution 4, AMD's machine-learning upscaler for RDNA 4.
- **FidelityFX API / SDK** — AMD's public interface (`ffxCreateContext`, `ffxDispatch`, ...)
  and the signed runtime DLL that implements it.
- **Proton / Wine / vkd3d-proton** — Steam's compatibility layer for Windows games, the Windows
  API layer it is built on, and the part that translates DirectX 12 to Vulkan.
- **Prefix** — the private Windows folder tree Proton keeps for each game
  (`steamapps/compatdata/<APPID>/pfx`).
- **App ID** — Steam's number for a game.
- **dxvk-nvapi / NVAPI** — NVIDIA's driver API and the open-source implementation in Proton.
  Here it is used to make the game believe an NVIDIA GPU is present ("spoofing").
- **OptiScaler** — an open-source upscaler bridge for Windows games; GOverlay can install it.
- **fakenvapi** — a small NVAPI stand-in some setups use with OptiScaler.
- **Signature override** — the NGXCore registry switch that lets an unsigned `_nvngx.dll` load.
- **Motion vectors / jitter / exposure** — per-frame data an upscaler needs: how each pixel
  moved, the sub-pixel camera offset of this frame, and the image brightness scale.
- **Reactive mask** — an optional image telling the upscaler where history should count less
  (particles, transparency).
- **DLAA** — DLSS at 100% resolution, used only as anti-aliasing.
- **Manifest** — ngx-fsr4's record of every file it added, replaced or moved, with checksums.

---

## 15. Credits and licence

- ngx-fsr4: MIT licence (`LICENSE`), © Eugene Flanagin.
- `third_party/ffx`: AMD FidelityFX API headers, © Advanced Micro Devices, Inc., MIT licence
  (`third_party/ffx/docs/license.md`). Vendored unmodified.
- Full list of sources: `CREDITS.md`.
- Written clean-room from NVIDIA's **public** NGX SDK headers
  ([NVIDIA/DLSS](https://github.com/NVIDIA/DLSS)) and AMD's public FidelityFX SDK. No NVIDIA
  binary was disassembled. NVIDIA's headers are not redistributed; `tools/fetch-ngx-headers.sh`
  downloads them for reference only (the build doesn't need them).
  `tools/dump-vtable.sh` reproduces the MSVC parameter-object layout from those headers with
  clang.
- Thanks to the OptiScaler project, whose public documentation of the signature-override switch
  and camera defaults informed this one, and to the dxvk-nvapi and vkd3d-proton developers.

NVIDIA, DLSS and NGX are trademarks of NVIDIA Corporation. AMD, Radeon, FidelityFX and FSR are
trademarks of Advanced Micro Devices, Inc. Steam and Proton are trademarks of Valve
Corporation. This project is not affiliated with, endorsed by, or supported by NVIDIA, AMD or
Valve.
