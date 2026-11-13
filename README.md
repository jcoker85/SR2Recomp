# SR2Recomp

Smuggler's Run 2: Hostile Territory (PS2, 2001) as a native Windows program, built with
[ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp) from your own disc, with a launcher and a set of
optional improvements: widescreen, 60 FPS, higher resolutions, upscaling, and more. A sister project of
[SR1Recomp](https://github.com/jcoker85/SR1Recomp) (Smuggler's Run),
[MC1Recomp](https://github.com/jcoker85/MC1Recomp) (Midnight Club: Street Racing) and
[MC2Recomp](https://github.com/jcoker85/MC2Recomp) (Midnight Club II), the same studio's engine before and after, whose
launcher and toolkit changes it reuses.

| Game | Disc | Serial |
|---|---|---|
| Smuggler's Run 2: Hostile Territory | USA | SLUS-20204 (program CRC 0EF2C216) |

This is an unofficial fan project. It is not affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Sony
Interactive Entertainment.

## Play

You need your own disc image of Smuggler's Run 2: Hostile Territory (USA) as an `.iso`. The game is on a CD, so a `.chd`
is unpacked with MAME's chdman:
`chdman extractcd -i "Smuggler's Run 2.chd" -o "Smuggler's Run 2.cue" -ob "Smuggler's Run 2.iso"`. If the `.cue` it writes
says `MODE1/2048`, the `.iso` is ready. If it says `MODE1/2352` or `MODE2/2352` (a raw dump), convert it with the toolkit:
`python ps2-native-recomp/scripts/disc.py iso "Smuggler's Run 2.cue" "Smuggler's Run 2.iso"`.

Windows 10 or 11 (64-bit), a CPU with AVX2 and a Direct3D 12 graphics card.

1. Run `SR2Recomp.exe` (build it first, below).
2. Press **Play**. The first time, it asks for your disc image; the launcher remembers it
   (**Settings > Disc Image** changes it).
3. **Settings** has three tabs: **Graphics**, with the options below, **P1 Controls** and **P2 Controls**. Every option
   starts off, so a fresh install plays the game as it is on a PS2.

### Options

- **Fullscreen**, **Resolution** (1x to 4x the PS2's), **Vsync**, **Frame Pacing Fix** (frames on an even 59.94 Hz clock)
- **Picture Mode**: how the picture is enlarged to the window: Crisp, Soft or Sharp, each with optional edge smoothing,
  or **FSR** (AMD FidelityFX Super Resolution 1)
- **Texture Filtering**: the game's own (Bilinear PS2), Nearest, or Bilinear (Forced)
- **60 FPS**: the game runs at 30 frames a second on a PS2, in the menus and while driving, but it times itself by the
  clock and its developers left a 60 mode in the code. The option sets the game's frame interval to one vsync, and keeps
  the rear-view mirror and the radar's terrain, which the game's own 60 mode leaves out to save time
- **Widescreen** 16:9 (the PCSX2 community patch by No.47), **Fixed Widescreen HUD** (the driving HUD keeps its 4:3 shapes:
  the radar, speed and gear at the right edge, the rear-view mirror in the middle), **Menu/Movie Aspect Ratio** (menus,
  loading screens and videos in 4:3 while driving is 16:9)
- **Color Settings**: the PS2's 16-bit picture, or the game's full 32-bit picture; **Dithering** (the PS2's dither on the
  16-bit picture)
- **Smooth Distant Textures** (less shimmer on far terrain; not something a PS2 does)
- **Fix Distant Z-Fighting** (not something a PS2 does): the depth test also keeps the fraction of each 16-bit depth, so
  of two far surfaces drawn on top of each other the nearer one wins every frame instead of the two taking turns (see
  SR1Recomp's README for the long version)
- **Skip Intro**: straight to the title screen, without the Rockstar and Angel Studios logos and the intro movie. The
  menus keep their music and their moving background (the game's own `-nofmv` switch would take the background away too)

**P1 Controls** and **P2 Controls** work as in the sister projects: choose Controller or Keyboard (with the mouse) for
player 1 and rebind every PS2 button and stick direction; the bindings are kept in
`ps2-native-recomp/games/sr2/keys.ini`. Saves go to the memory card file in `ps2-native-recomp/games/sr2` (Smuggler's
Missions offers to format it the first time).

### Known limits

- The pause menu's box is not part of the driving HUD, so with Widescreen on it is drawn a third wider than on a PS2.
- Tried so far: the boot, the menus, Joyridin' (Russia), the pause menu and quitting back to the menus. Smuggler's
  Missions, Turf Wars, Checkpoint Races and two players have not been played through yet; code the build has not seen is
  translated while you play.

## Build

On Windows, with Git for Windows (Git Bash), CMake, Ninja, Python 3 and llvm-mingw (UCRT). The toolkit's
`scripts\bootstrap.ps1` installs them with winget; no Visual Studio is needed.

```bash
git clone --recursive https://github.com/jcoker85/SR2Recomp.git
cd SR2Recomp
bash tools/build.sh "/c/Games/Smuggler's Run 2 - Hostile Territory (USA).iso"
```

`tools/build.sh`:

1. fetches ps2-native-recomp v0.0.4 (the submodule) and applies `patches/ps2-native-recomp.patch`;
2. sets up the toolkit's build tools;
3. translates the game's code from your disc into C++ and builds the game program (`ps2run.exe`), with the 60 FPS
   option's switchable instructions (`sr2-options/toggles.txt`), the Fixed Widescreen HUD's hooks
   (`sr2-options/hud43.pnach`) and Skip Intro's switch (`sr2-options/skipintro.pnach`) built in, all off until the
   launcher turns them on;
4. builds the launcher, `SR2Recomp.exe`. It has no icon built in: on its first start it downloads its icon (credited
   below) into `covers/icon.ico`, beside the box art, and its window and the game window show it from there (a square PNG
   put at `covers/icon.png` is used instead). It also writes the icon into its own `SR2Recomp.exe` (it renames itself to
   `SR2Recomp.exe.old` and puts a copy with the icon in its place), so Explorer and shortcuts show it from the next start.

The first build takes a while. Code the first build did not see is translated while you play and kept for next time.

## What is and is not in this repository

The repository holds the launcher, patches, build files and documentation. It does **not** contain disc images, the game's
program, game assets, cover art or recompiled game code. The recompiled C++ and the game program are generated on your
machine from your own disc and stay in `ps2-native-recomp/generated` and `ps2-native-recomp/games`, which git ignores. When
the game runs, it reads its data from your disc image.

- `hub/`: the launcher (`hub.c`, SR1Recomp's launcher with this game's options) and its build files.
- `sr2-options/`:
  - `widescreen.txt`: Widescreen as `ps2run --patches` lines. The PCSX2 patch sets one float, the game's own aspect
    factor at `0x36EDB8` (1.0 on an NTSC TV; the game multiplies the view's width by it at `0x2EDDA0`), to 4/3. The launcher
    copies the lines of the options that are on into `boot.txt` at each start.
  - `60fps.txt` and `toggles.txt`: 60 FPS. The game's frame interval is the word at `0x36EDBC`: 2 vblanks a frame in the
    menus and while driving, 1 on the loading screens. The game measures the time each frame took, so at 1 it runs at the
    same speed with twice the frames; a debug function at `0x103890` flips the word between 1 and 2. While it is below 2
    the game leaves out the rear-view mirror, the radar's terrain and a third drawing pass (the comparisons at `0x1CA990`,
    `0x1CCCC8` and `0x1CF704`); `toggles.txt` (eerecomp `--toggles` lines) builds both versions of those three instructions
    into the game program, switched by the option word at `0xFFF08`, so 60 FPS keeps them.
  - `menus43.txt` / `widescreen43.txt`: Menu/Movie Aspect Ratio. The picture is 16:9 only while two words of the game's
    say it is driving (`0x36FADC`, a 3D scene is drawn, and `0x36EE6C`, a level is loaded: from the moment the loading
    screen fades into the level until the player quits, the pause menu included), and the runtime mirrors that into the
    word at `0xFFF00`, which `widescreen43.txt` uses to write the 16:9 factor or the game's own 1.0.
  - `hud43.py` / `hud43.pnach` / `hud43.txt`: Fixed Widescreen HUD, the same method as SR1Recomp's with this game's
    addresses. The game has the same HUD part loop as Smuggler's Run (here `0x20CFA8`, called from the HUD's draw function
    `0x1CA958`), so the one call there (`0x20CFEC`) is hooked: while the HUD option word (`0xFFF04`) and the 16:9 word
    (`0xFFF00`) are on, each part's drawing is marked in the game's VIF1 packets (pointer `0x70000654`, end `0x70000650`,
    full buffer `0x2EB0C0`), and the runtime squeezes what a marked part draws toward the edge it belongs to. The
    rear-view mirror (part 0) draws the view behind the car first, across the whole frame, so its side is given: the
    centre. `hud43.txt` tells the launcher the hooks are built in.
  - `skipintro.pnach`: Skip Intro. The game asks for its developer switch `nofmv` in three places: the object that runs
    the game's start (`0x11CB70`) leaves out the logos, the intro movie and the step after it that starts the menus' music;
    the boot (`0x10862C`) starts that music itself when the switch is given; and the menus leave out their moving
    background. This patch makes the first two ask for another name, `skipintro` (kept at `0xFFF20`), which the launcher
    passes with `--arg -skipintro`, so the intro is skipped and the menus keep both their music and their background.
- `patches/ps2-native-recomp.patch`: this project's changes to the toolkit: SR1Recomp's (MC2Recomp's and MC1Recomp's
  fixes for this engine, dithering, texture filtering, smooth distant textures, FSR, keyboard and controller bindings, two
  players, conditional patch lines, the sound chip's ENDX, faster drawing at higher resolutions, fair interrupt turns, Fix
  Distant Z-Fighting and more; see their READMEs for the list), plus these for Smuggler's Run 2 (the first two stopped the
  game):
  - GS SIGNAL events one at a time (`gs.cpp`): this engine pairs every stop of its VIF1 DMA chain with a GS SIGNAL of the
    same number and starts the chain again from the SIGNAL's interrupt. The real GS stops at a SIGNAL that arrives while
    the one before has not been acknowledged; the runtime raised them all at once, so two in one burst became one event
    with the second number, the chain waiting for the first never started again, and the level never finished loading.
    Now a SIGNAL that arrives while CSR.SIGNAL is still set waits, and is raised when the game clears the bit.
  - `sceCdSeek` finishes with its callback (`iop/cdvd.cpp`): the game's sound driver (`AUDIO.IRX`) starts every music
    stream with a seek and goes on from the drive's callback. The runtime's seek returned at once and never called it, so
    no music stream ever started, and pausing the game, which waits for the streams, hung for ever.
  - debugging aids in the sound chip (`spu2.cpp`): `PS2_SPU2_SOLO=<hex mask>` lets only those voices of core 1 be heard,
    and the voice list `PS2_TRACE_SPU2` prints at the end of a run has each voice's volumes;
  - a HUD part's mark survives the game's own VIF MARKs (`gif.cpp`): this engine sends a MARK with every texture it waits
    for, inside the HUD's parts too, which ended each part's mark before it had drawn anything.
- `ps2-native-recomp/`: the toolkit, as a git submodule.
- `tools/`: the build script, the release packaging script and `mips.py` (a small R5900 disassembler and
  address-reference finder used to trace the game's code).

## Credits

- [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp) by Samir Nuri and exoteric33 (MIT): the
  recompiler and runtime this project is built on.
- The PCSX2 community patch (from [PCSX2/pcsx2_patches](https://github.com/PCSX2/pcsx2_patches)): Widescreen 16:9 by
  No.47.
- AMD FidelityFX Super Resolution 1.0 (MIT, Copyright (c) 2021 Advanced Micro Devices, Inc.): the FSR picture mode is
  written after AMD's published shaders.
- Box art in the launcher: [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails).
- The launcher and game window icon:
  [Smuggler's Run 2: Hostile Territory icon by Haxy](https://www.steamgriddb.com/icon/31611) on SteamGridDB, downloaded
  by the launcher on its first start.

## License

MIT; see `LICENSE`. ps2-native-recomp is MIT as well.

Smuggler's Run is a trademark of Take-Two Interactive Software, Inc., and the game is copyright its owners. This
repository contains none of its code, data or artwork, nor the icon and box art the launcher downloads.

## AI disclosure

This project was built with Claude (Anthropic) working as a coding assistant under the direction of the author.

- **Claude did:** ported SR1Recomp's launcher, build and packaging scripts to this game, traced the game's code and its
  sound driver for the addresses and the three runtime faults described above, wrote the option files and patches, and
  drafted this README.
- **Claude did not:** write ps2-native-recomp, which is another project used here as a submodule, the PCSX2 community
  patch, or the game itself, whose code the recompiler translates from your own disc.
