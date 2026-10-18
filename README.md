# ZType (Wii)

**Version:** 1.2 · **Coder:** quatric

A port of Dominic Szablewski's [ZType](https://zty.pe/) typing shooter for the Nintendo Wii, played with a USB keyboard. It is built to look like the website: the same title screen, menus, HUD, game over screen, credits, settings and stats card, drawn with the original artwork on the Wii's GX hardware at 640x480 (or 16:9 widescreen) and 60 FPS.

## What it looks like

The 480x720 game canvas of zty.pe sits in the middle of the page and the whole page is scaled to the TV, exactly like a 960x720 browser window shown on a 640x480 screen:

- the tiled page background and the soft `box-shadow` around the canvas
- the title screen with the PHOBOSLAB mark, the ZTYPE logo, the hovering ship with its exhaust, the menu entries, the info icon and the glitchy transition into a game
- the game: scrolling stars, teal gradient, flashing grid, additive-blended sprites, the Avenir word labels, the targeting reticle, the EMP, the multiplier bar and the EMP counter, wave clear banners and screen shake
- the settings / "game paused" menu with sound and music volume
- the game over screen with your score, wave, accuracy, longest streak, the personal best badge and the graph of your last games
- the credits and the "my stats" card

Wide screen is detected from the Wii settings; on a 16:9 TV the page simply gets wider, the canvas keeps its shape.

## Controls

| | USB keyboard | Wii Remote |
|---|---|---|
| Shoot | type the letters of a word | - |
| Cancel the current target | `Backspace` | `-` |
| EMP | `Enter` | `B` |
| Pause / settings | `Esc` | `+` |
| Menus: move | arrow keys | D-pad |
| Menus: select | `Enter` / `Space` | `A` |
| Volume | left / right arrows | D-pad left / right |
| Credits (on the title) | `I` | `1` |
| Exit | - | `HOME` |

The Wii Remote cannot type, so a keyboard is needed to actually play.

## Own text, stats and settings

Everything ZType keeps in the browser's `localStorage` is saved on the SD card in `sd:/apps/typeshot/`:

- `stats.dat`: personal best, volumes and the history of your games (the "my stats" card and the graph on the game over screen)
- `text.txt` (optional): put any text in this file and pick "load your own text"; the words of the text become the enemies, like the "load your own text" button of the website

Without an SD card everything still works, nothing is saved.

## Building

Requires devkitPPC and libogc (devkitPro):

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make
```

The output is `zType-wii.dol` and `zType-wii-xz.dol`, the same program packed into a self-extracting `.dol` a fraction of the size. For the Homebrew Channel, copy the `-xz` one as `boot.dol` into a folder together with `apps/typeshot/icon.png` and `apps/typeshot/meta.xml` (the GitHub workflow packages it like that). The first build downloads and builds [dolxz](https://github.com/FIX94/dolxz), which needs a host C compiler and liblzma (Debian/Ubuntu: `apt-get install liblzma-dev pkg-config`; macOS: `brew install xz`). `make NO_DOLXZ=1` skips the compressed `.dol`.

## Installation

1. Put `boot.dol`, `icon.png` and `meta.xml` into `apps/typeshot/` on your SD card or USB drive (`SD:/apps/typeshot/`).
2. Launch it from the Homebrew Channel.
3. Plug a standard USB keyboard into one of the Wii's rear USB ports.

## Assets

`tools/pack_assets.py` bakes the original media into GX textures (`data/*.bin`) and the headers that describe them (`source/assets.h`, `source/words.h`). It downloads the media it needs from `https://zty.pe/media/` into `zty_assets/` (not committed), so it needs Pillow and an internet connection:

```bash
python3 tools/pack_assets.py          # textures, headers, word lists
python3 tools/pack_assets.py --sfx    # also re-encode the sound effects (needs ffmpeg)
```

The big static backgrounds (stars, gradient, grid) are baked at the size they are shown on a 640x480 frame, with full RGBA8 precision so the dark gradients do not band. The stats card uses Arimo (Apache License 2.0, `software/fonts/Arimo_Regular.ttf`), the open twin of the Arial that browsers use for `sans-serif`.

ZType, its artwork, sounds and word lists belong to Dominic Szablewski ([phoboslab.org](https://phoboslab.org/)); this repository is a fan port.

## Source layout

| File | |
|---|---|
| `source/main.c` | setup, page background, input, frame loop |
| `source/game.c` | the game: waves, enemies, targeting, plasma, explosions, EMP, HUD |
| `source/menus.c` | title, settings, credits, game over, "my stats" |
| `source/gx_render.c` | the canvas-2D-like layer on top of GX: transforms, alpha, additive blending, bitmap fonts |
| `source/stats.c` | SD card storage |
| `source/sound.c` | sound effects and music through ASND |

## Testing in Dolphin

`make EXTRA_CFLAGS="-DZT_TEST_SCRIPT -DZT_FORCE_43"` builds a scripted run (new game, an autopilot that types the words, pause, game over, credits, stats, settings; nothing is read from or written to the SD card) with a fixed 4:3 frame, for taking screenshots to compare against zty.pe.

## Software-renderer engine (`software/`)

A second, clean-room engine that draws everything in code (no extracted assets) into a 640x480 framebuffer:
portrait 2:3 playfield, Avenir/Roboto text via stb_truetype, generated art and sound effects.

```bash
make ENGINE=soft        # builds zType-wii-soft.dol (objects in build-soft/)
tools/install_avenir.sh # optional, macOS: use Avenir like the original (licensed, kept out of git; Roboto otherwise)
```

- Music: put `endure.ogg` / `orientation.ogg` in `sd:/apps/typeshot/music/`; they stream during a game (synth loop if absent)
- Own text: put `text.txt` in `sd:/apps/typeshot/` and pick "load your own text"
- Keyboard: Enter = EMP/select, Backspace = cancel target, Esc = pause, arrows = menus, `I` on the title = credits
