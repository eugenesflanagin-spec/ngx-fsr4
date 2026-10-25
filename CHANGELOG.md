# Changelog

## 0.1.0 - 2026-10-25

First release. **Experimental:** tested on one PC (Radeon RX 9070 XT, Arch Linux,
proton-tkg, FSR 4.1.1b) with a synthetic GPU test and two games' own DLSS paths
(*Boundary: Benchmark* and the *Black Myth: Wukong* benchmark tool).

### What it is

A stand-in for NVIDIA's NGX runtime (`_nvngx.dll`). When a DirectX 12 game under Proton asks
for **DLSS Super Resolution**, it answers and runs **AMD FSR 4** instead, through AMD's public
FidelityFX API, on the game's own frames. The `ngx-fsr4` command puts it into one Steam game's
Proton prefix, records every change, and undoes them all with `disable`.

In the Black Myth benchmark it was as fast as the game's DLSS path through OptiScaler, but
slower than the game's own built-in FSR 4 option. Use it for games that only offer DLSS; it is
a compatibility route, not a speed-up.

### Changes since the development builds (`ngx-fsr4-git` r10 and earlier)

- **`disable` no longer copies back the whole registry file.** `enable` changes one value
  (`EnableSignatureOverride` under `NGXCore`); `disable` now removes exactly that value, and the
  `NGXCore` key only if `enable` created it. Anything Wine or the game wrote to `system.reg` in
  the meantime is kept. A copy of the old `system.reg` stays in the backup folder. Backups made
  by older builds are still restored the old way.
- Fixed: when the `NGXCore` key was the last one in `system.reg`, `enable` wrote the value just
  outside it, and `disable` then left it behind.
- **Log files are cleaned up.** The DLL writes `ngx-fsr4.log` beside whichever copy loaded;
  `disable` now moves those logs into the backup folder (`logs/`) instead of leaving them in the
  game folder.
- **`ngx-fsr4 log` finds the right log.** It used to read only the prefix's `system32` copy;
  games usually load the copy beside the game. It now follows the newest of all of them.
- **`ngx-fsr4 run` finds Proton and the game on any setup:** the Proton Steam is set to use for
  the game (or Steam's default), else the only one installed; with several and no Steam setting
  it lists them and asks for `--proton`. The game `.exe` skips crash handlers, setup and
  uninstall programs; with several it lists them and asks for `--exe`. New `--dry-run` shows
  what it would start.
- **`enable` finds the FSR runtime on any setup:** `--ffx`, `$NGX_FSR4_FFX`,
  `~/.local/share/ngx-fsr4/`, `/usr/lib/ngx-fsr4/`, then the newest GOverlay download; if none,
  it says where to get one.
- `disable` warns when a wineserver is running (it may rewrite `system.reg` on exit).
- `ngx-fsr4 --version`; the DLL's log now starts with its version.
- `make check`: checks that need no GPU and no Wine (the DLL's exports, and the helper against
  a fake Steam library). The Arch package runs them while building.
- Documented `NGX_FSR4_JITTER_SIGN` (flip the jitter sign if an image shimmers or smears).
- New user guide (`docs/USER-GUIDE.md`), `CREDITS.md`, release tarball for any distribution.

### Known limits

- DirectX 12 and DLSS Super Resolution only: no frame generation, ray reconstruction, D3D11 or
  Vulkan. Games fall back cleanly.
- No reactive or transparency mask yet; no camera data from DLSS (defaults are used).
- Many games only offer DLSS when they see an NVIDIA GPU. `ngx-fsr4 run` spoofs one; some games
  (Boundary) also need `enable --optiscaler-spoof`.
- Don't use it in online games with anti-cheat.
