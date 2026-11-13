#!/usr/bin/env bash
# Stage a ready-to-run release from a finished build (tools/build.sh) and zip it.
#
# The result is what a player downloads: the launcher, the game program and the files it reads beside itself. It holds no
# disc images, saves, settings or controls from this machine, cover art, sources or generated C++. The player runs the
# launcher, which downloads the cover art, and the first Play asks for their own disc image.
#
# Usage (from the repository root, after tools/build.sh):
#   bash tools/package_release.sh [--version 0.2]
# Output: dist/SR2Recomp-<version>/ and the same name + .zip
#
# The layout keeps the launcher's paths (hub/hub.c): games.txt points at <toolkit>/build/native-sr2/ps2run.exe, the
# toolkit folder holds games/sr2/ (patches.txt; the memory card and keys.ini are made there on first use), and
# sr2-options/ beside the toolkit folder switches on the widescreen / 60 FPS / HUD / 4:3 menus options.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:?}"; shift 2 ;;
    *) echo "error: unknown argument $1" >&2; exit 1 ;;
  esac
done
[[ -n "$VERSION" ]] || VERSION="$(git describe --tags --always 2>/dev/null || echo dev)"
VERSION="${VERSION#v}"
NAME="SR2Recomp-$VERSION"
STAGE="dist/$NAME"
TK=ps2-native-recomp
BUILD="$TK/build/native-sr2"
GAME="$TK/games/sr2"

for need in SR2Recomp.exe "$BUILD/ps2run.exe" "$BUILD/SDL2.dll" "$BUILD/dxcompiler.dll" "$BUILD/dxil.dll" \
            "$GAME/patches.txt" sr2-options/widescreen.txt sr2-options/widescreen43.txt sr2-options/menus43.txt sr2-options/60fps.txt sr2-options/hud43.txt; do
  [[ -f "$need" ]] || { echo "error: $need is missing; run tools/build.sh first" >&2; exit 1; }
done

rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE/game/build/native-sr2" "$STAGE/game/games/sr2" "$STAGE/sr2-options" "$STAGE/licenses"
cp SR2Recomp.exe "$STAGE/"
# games.txt with the game program at game/build/native-sr2 (the toolkit folder is "game" in the release)
sed 's#|ps2-native-recomp/build/native-sr2/ps2run.exe|#|game/build/native-sr2/ps2run.exe|#' hub/games.txt > "$STAGE/games.txt"
grep -q '|game/build/native-sr2/ps2run.exe|' "$STAGE/games.txt" || { echo "error: games.txt was not rewritten" >&2; exit 1; }
cp "$BUILD/ps2run.exe" "$BUILD/SDL2.dll" "$BUILD/dxcompiler.dll" "$BUILD/dxil.dll" "$STAGE/game/build/native-sr2/"
cp "$GAME/patches.txt" "$STAGE/game/games/sr2/"
cp sr2-options/widescreen.txt sr2-options/widescreen43.txt sr2-options/menus43.txt sr2-options/60fps.txt sr2-options/hud43.txt "$STAGE/sr2-options/"

cp LICENSE "$STAGE/licenses/SR2Recomp-LICENSE.txt"
cp "$TK/LICENSE" "$STAGE/licenses/ps2-native-recomp-LICENSE.txt"
for f in "$TK"/third_party/dxc/LICEN*; do cp "$f" "$STAGE/licenses/dxc-$(basename "$f")"; done
cat > "$STAGE/licenses/OTHER-NOTICES.txt" <<'EOF'
SDL2 (SDL2.dll, https://libsdl.org): zlib license, Copyright (C) 1997-2024 Sam Lantinga.
wgpu-native (built into ps2run.exe, https://github.com/gfx-rs/wgpu-native): MIT or Apache-2.0, Copyright the wgpu developers.
DirectX Shader Compiler (dxcompiler.dll, dxil.dll, https://github.com/microsoft/DirectXShaderCompiler): see the dxc-*
  files in this folder.
AMD FidelityFX Super Resolution 1.0: MIT, Copyright (c) 2021 Advanced Micro Devices, Inc. (the FSR picture mode is
  written after AMD's published shaders).
Widescreen 16:9 is the PCSX2 community patch by No.47, https://github.com/PCSX2/pcsx2_patches.
EOF

cat > "$STAGE/START_HERE.txt" <<EOF
SR2Recomp $VERSION

Smuggler's Run 2: Hostile Territory (PS2, 2001) as a native Windows program. Unofficial fan project; not affiliated with or endorsed by
Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment.

You need your own disc image of Smuggler's Run 2: Hostile Territory (USA, SLUS-20204) as an .iso. Nothing from the game's disc is included
here. The game is on a CD: a .chd can be unpacked with MAME's chdman,
  chdman extractcd -i "Smuggler's Run 2.chd" -o "Smuggler's Run 2.cue" -ob "Smuggler's Run 2.iso"
which writes the data track as 2048-byte sectors (MODE1/2048), an .iso the launcher takes.

Needs 64-bit Windows 10 or 11, a CPU with AVX2, and a graphics card with Direct3D 12.

1. Run SR2Recomp.exe. The cover art downloads in the background the first time.
2. Press Play. The first time, it asks for your disc image; the launcher remembers it
   (Settings > Disc Image changes it).
3. Settings has three tabs: Graphics (widescreen, 60 FPS, resolution, FSR and more), P1 Controls and P2 Controls.
   Every option starts off, so a fresh install plays the game as it is on a PS2.

Two players: in Settings > P1 Controls > Device choose Keyboard to play player 1 on the keyboard with one controller for
player 2, or Controller to use two controllers.

Saves and controls are kept in game\\games\\sr2. The game program contains a machine-translated build of the game's own
code. Licenses are in the licenses folder.
EOF

# Nothing personal or taken from the disc may ride along.
bad="$(find "$STAGE" -type f \( -iname '*.iso' -o -iname '*.chd' -o -iname '*.cue' -o -iname '*.bin' -o -iname '*.elf' \
      -o -iname 'SLUS_*' -o -iname '*.ps2' -o -iname 'keys.ini' -o -iname 'settings.ini' -o -iname 'boot.txt' \
      -o -iname '*.png' -o -iname '*.cpp' -o -iname '*.a' -o -iname '*.state' \) 2>/dev/null || true)"
[[ -z "$bad" ]] || { echo "error: the package would include files it must not:" >&2; echo "$bad" >&2; exit 1; }

PY="$(command -v python3 || command -v python)"
"$PY" - "$STAGE" "dist/$NAME.zip" <<'PY'
import os, sys, zipfile
stage, out = sys.argv[1], sys.argv[2]
base = os.path.dirname(stage)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for d, _, files in os.walk(stage):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, base))
print(f"{out}: {os.path.getsize(out) / 1048576:.1f} MB")
PY
