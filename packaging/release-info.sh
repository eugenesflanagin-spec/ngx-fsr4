#!/bin/bash
# Release bookkeeping, used by the release workflow and by hand:
#   packaging/release-info.sh version        print the version (from VERSION)
#   packaging/release-info.sh check vX.Y.Z   fail unless the tag, VERSION, the helper's
#                                            VERSION= and a CHANGELOG section all agree
#   packaging/release-info.sh title vX.Y.Z   release title
#   packaging/release-info.sh notes vX.Y.Z   release notes (Markdown) for that version
set -euo pipefail
cd "$(dirname "$0")/.."
ver=$(tr -d ' \n' <VERSION)
repo=https://github.com/eugenesflanagin-spec/ngx-fsr4

changelog_section() {
    awk -v v="$1" '/^## /{p = ($2 == v)} p' CHANGELOG.md | sed '1d'
}

case ${1:-} in
version) echo "$ver" ;;
check)
    tag=${2:?tag}
    err=0
    [ "$tag" = "v$ver" ] || { echo "::error::tag $tag does not match VERSION $ver (expected v$ver)"; err=1; }
    tv=$(sed -n 's/^VERSION=//p' tools/ngx-fsr4)
    [ "$tv" = "$ver" ] || { echo "::error::tools/ngx-fsr4 says VERSION=$tv, VERSION file says $ver"; err=1; }
    [ -n "$(changelog_section "$ver")" ] || { echo "::error::CHANGELOG.md has no '## $ver' section"; err=1; }
    [ $err = 0 ] && echo "tag $tag, VERSION, helper and CHANGELOG agree on $ver"
    exit $err
    ;;
title) echo "ngx-fsr4 ${2#v} (experimental)" ;;
notes)
    v=${2#v}
    cat <<MD
**DLSS Super Resolution → AMD FSR 4 for Proton games on AMD Linux. Experimental.**
Tested on one PC (RX 9070 XT) with a synthetic GPU test and two games. For everyday use,
[OptiScaler](https://github.com/optiscaler/OptiScaler) is the mature project that does this and
much more. Full guide: [USER-GUIDE.md]($repo/blob/v$v/docs/USER-GUIDE.md).

## Downloads

| file | for |
|---|---|
| \`ngx-fsr4-git-$v.r0.g*-1-any.pkg.tar.zst\` | Arch Linux: \`sudo pacman -U\` it |
| \`ngx-fsr4-$v-proton.tar.gz\` | any distribution: the DLL, two self-tests, the \`ngx-fsr4\` helper and the docs |
| \`SHA256SUMS\` | checksums: \`sha256sum -c SHA256SUMS --ignore-missing\` |

**Not included:** AMD's FSR 4 runtime, \`amd_fidelityfx_upscaler_dx12.dll\` (FidelityFX SDK 2.x).
GOverlay's FSR 4 download is found automatically; otherwise pass \`--ffx PATH\`.
Needs an AMD RDNA 4 GPU (RX 9000 series), Steam with Proton, and a DirectX 12 game with DLSS.

## About $v

$(changelog_section "$v")

## Install

- **Arch Linux:** \`sudo pacman -U ngx-fsr4-git-*-any.pkg.tar.zst\`
- **Other distributions:** \`tar xf ngx-fsr4-$v-proton.tar.gz && cd ngx-fsr4-$v-proton\`, then
  use \`NGX_FSR4_LIB=\$PWD ./tools/ngx-fsr4 ...\` in place of \`ngx-fsr4\` below
  (\`INSTALL.txt\` shows a system-wide install).
- **Updating from an earlier build:** install the new package, then \`ngx-fsr4 disable APPID\` and
  \`ngx-fsr4 enable APPID\` again for each game: \`enable\` copies the DLL into the game.

## Turn it on, use it, turn it off

APPID is the game's Steam App ID (the number in its store page URL). Close the game first.

\`\`\`sh
ngx-fsr4 enable APPID      # put it into the game's Proton prefix (add --optiscaler-spoof if the game hides DLSS)
ngx-fsr4 run APPID         # start the game with an NVIDIA GPU identity, so it offers DLSS
                           # in the game: pick DLSS Super Resolution and a quality mode
ngx-fsr4 log APPID         # in another terminal: watch what the game asks for
ngx-fsr4 disable APPID     # put everything back exactly as it was
ngx-fsr4 status            # which games it is on
\`\`\`

\`ngx-fsr4 run APPID --dry-run\` shows which Proton and game program it would start, without
starting anything.

## Where things are

- **Logs:** \`ngx-fsr4.log\` beside the copy of the DLL that loaded, usually in the game folder
  next to \`nvngx_dlss.dll\`, sometimes the prefix's \`drive_c/windows/system32/\`.
  \`ngx-fsr4 log APPID\` finds the newest. \`disable\` moves them into the backup folder.
- **Backups:** \`~/.local/share/ngx-fsr4/backups/APPID-DATE-TIME/\` (a manifest of every change,
  the replaced files, a copy of \`system.reg\`).
- **Installed files:** \`/usr/bin/ngx-fsr4\`, \`/usr/lib/ngx-fsr4/\`, \`/usr/share/doc/ngx-fsr4/\`.

## Troubleshooting

| you see | do this |
|---|---|
| DLSS greyed out or missing in the game | start it with \`ngx-fsr4 run\`; if that isn't enough and OptiScaler is installed for the game, \`disable\`, then \`enable APPID --optiscaler-spoof\` |
| \`FSR 4 runtime ... not found\` | get \`amd_fidelityfx_upscaler_dx12.dll\` (GOverlay or AMD's SDK) and pass \`--ffx PATH\` |
| \`several Proton builds found\` / \`several .exe files\` | pick one from the list with \`--proton DIR\` / \`--exe PATH\` |
| no \`ngx-fsr4.log\` anywhere | the DLL never loaded: not a DX12 DLSS game, or OptiScaler still answers DLSS (re-run \`enable\` without \`--optiscaler-spoof\`) |
| log says \`using 3.1.5\` | the AMD DLL has no FSR 4, or the GPU isn't RDNA 4 |
| shimmer or smearing | try \`NGX_FSR4_JITTER_SIGN=-1 ngx-fsr4 run APPID\` |
| settings undone after a Steam launch | GOverlay's launcher rewrote them: start with \`ngx-fsr4 run\` |

When reporting a problem, include the game and App ID, the Proton version, the FSR DLL version,
and the first ~200 lines of \`ngx-fsr4.log\`.

Clean-room: written from NVIDIA's public NGX SDK headers and AMD's public FidelityFX SDK; no
NVIDIA files are included. MIT licence. Sources and licences: [CREDITS.md]($repo/blob/v$v/CREDITS.md).
DLSS and NGX are NVIDIA trademarks, FSR and FidelityFX are AMD trademarks; not affiliated with either.
Don't use it in online games with anti-cheat.
MD
    ;;
*) sed -n '2,7s/^# \{0,1\}//p' "$0"; exit 1 ;;
esac
