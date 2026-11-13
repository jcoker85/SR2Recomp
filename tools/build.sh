#!/usr/bin/env bash
# Builds SR2Recomp from your own Smuggler's Run 2 - Hostile Territory (USA) disc image, in Git Bash on Windows.
#
#   bash tools/build.sh "/c/Games/Smuggler's Run 2 - Hostile Territory (USA).iso"
#
# (a .cue/.bin CD dump works too: the toolkit cuts it down to an .iso in ps2-native-recomp/games/sr2/disc.iso)
#
# 1. fetches ps2-native-recomp (the submodule, v0.0.4) and applies this project's changes to it (patches/);
# 2. sets up the toolkit's build tools (its scripts/setup.sh);
# 3. translates the game's code from your disc into C++ and builds the game program (the toolkit's scripts/port.sh), with
#    the Widescreen and 60 FPS options built in as switchable instructions (sr2-options/toggles.txt) and the Fixed
#    Widescreen HUD's hooks (sr2-options/hud43.pnach, made by sr2-options/hud43.py), all off until the launcher turns them on;
# 4. builds the launcher, SR2Recomp.exe, in this folder.
# Everything taken from the disc stays in ps2-native-recomp/games and ps2-native-recomp/generated, which git ignores.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
iso=${1:-}
if [ -z "$iso" ] || [ ! -f "$iso" ]; then
  echo "usage: bash tools/build.sh <your Smuggler's Run 2 - Hostile Territory (USA) disc image: .iso, or .cue/.bin>" >&2
  exit 2
fi
iso=$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")

cd "$here"
git submodule update --init ps2-native-recomp
cd ps2-native-recomp
patch=../patches/ps2-native-recomp.patch
if git apply --check "$patch" 2> /dev/null; then
  git apply "$patch"
  echo "applied $patch"
elif git apply --reverse --check "$patch" 2> /dev/null; then
  echo "$patch is already applied"
else
  echo "ps2-native-recomp has changes that are not $patch: set it back (git -C ps2-native-recomp checkout .) and run again" >&2
  exit 1
fi

bash scripts/setup.sh --native-only
# The exploring run drives: through the title, the memory card warning, the main menu to Joyridin' (Russia, the Super Buggy),
# then gas for 10 seconds, pause and resume. The vector unit programs it meets are compiled into the game program (the
# driving ones, the smoke and dust among them, would otherwise start in the slow interpreter until compiled while playing).
explore=(--frames 5400 --press 300:start)
for f in 600 900 1200 1500 1800; do explore+=(--press "$f:cross"); done
explore+=(--press 2200:down --press 2230:down --press 2260:down)
for f in 2300 2500 2700 2900 3100 3300; do explore+=(--press "$f:cross"); done
explore+=(--press 4000:cross:600 --press 4700:start --press 4800:start --press 4900:cross:300)
bash scripts/port.sh sr2 "$iso" --native-only "${explore[@]}" --toggles ../sr2-options/toggles.txt   --pnach ../sr2-options/hud43.pnach --pnach ../sr2-options/skipintro.pnach --pnach-section "HUD 4:3"

cd "$here"
cmd //c "hub\build.cmd"
echo
echo "Done: run SR2Recomp.exe, press Play and choose your disc image (an .iso)."
