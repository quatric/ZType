# ZType (Wii)

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

## Music

The soundtrack of the website is not part of this repository. Put the two tracks into `music/` next to the saved data on the SD card:

```
sd:/apps/typeshot/music/endure.ogg
sd:/apps/typeshot/music/orientation.ogg
```

From the root of the SD card:

```bash
mkdir -p apps/typeshot/music && cd apps/typeshot/music
curl -O https://zty.pe/media/music/endure.ogg -O https://zty.pe/media/music/orientation.ogg
```

(`apps/typeshot/music/` is in `.gitignore`, so a copy inside the repository stays out of git.) The tracks are Ogg Vorbis files that are decoded on the fly (libvorbisidec) and streamed from the SD card, so they cost almost no memory. They behave like on the website:

- a new game starts one of the two tracks at random, and when it ends the next one is picked at random, looping forever
- the music keeps playing through the pause / settings menu and follows the "music" volume of the settings
- when the game is over it fades out over one second; the title screen is silent

Without the files (or without an SD card) a small synthesized loop is played during the game instead.

## Building

Requires devkitPPC and libogc (devkitPro), plus the Ogg Vorbis libraries for the music (`dkp-pacman -S ppc-libogg ppc-libvorbisidec`):

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make
```

The output is `zType-wii.dol`. For the Homebrew Channel, copy it as `boot.dol` into a folder together with `apps/typeshot/icon.png` and `apps/typeshot/meta.xml` (the GitHub workflow packages it like that).

## Installation

1. Put `boot.dol`, `icon.png` and `meta.xml` into `apps/typeshot/` on your SD card or USB drive (`SD:/apps/typeshot/`).
2. Launch it from the Homebrew Channel.
3. Optional: add the music, see [Music](#music).
4. Plug a standard USB keyboard into one of the Wii's rear USB ports.

## Assets

`tools/pack_assets.py` bakes the original media into GX textures (`data/*.bin`) and the headers that describe them (`source/assets.h`, `source/words.h`). It downloads the media it needs from `https://zty.pe/media/` into `zty_assets/` (not committed), so it needs Pillow and an internet connection:

```bash
python3 tools/pack_assets.py          # textures, headers, word lists
python3 tools/pack_assets.py --sfx    # also re-encode the sound effects (needs ffmpeg)
```

The big static backgrounds (stars, gradient, grid) are baked at the size they are shown on a 640x480 frame, with full RGBA8 precision so the dark gradients do not band. The stats card uses Arimo (Apache License 2.0, `software/fonts/Arimo_Regular.ttf`), the open twin of the Arial that browsers use for `sans-serif`.

ZType, its artwork, sounds, music ("The PhobosLab Works" by Nofate) and word lists belong to Dominic Szablewski ([phoboslab.org](https://phoboslab.org/)); this repository is a fan port.

## Source layout

| File | |
|---|---|
| `source/main.c` | setup, page background, input, frame loop |
| `source/game.c` | the game: waves, enemies, targeting, plasma, explosions, EMP, HUD |
| `source/menus.c` | title, settings, credits, game over, "my stats" |
| `source/gx_render.c` | the canvas-2D-like layer on top of GX: transforms, alpha, additive blending, bitmap fonts |
| `source/stats.c` | SD card storage |
| `source/sound.c` | sound effects through ASND, the music streamed from the SD card (Ogg Vorbis, synth loop as fallback) |

## Testing in Dolphin

`make EXTRA_CFLAGS="-DZT_TEST_SCRIPT -DZT_FORCE_43"` builds a scripted run (new game, an autopilot that types the words, pause, game over, credits, stats, settings; the saved stats and settings are neither read nor written, the music still streams from the SD card) with a fixed 4:3 frame, for taking screenshots to compare against zty.pe.

## Software-renderer engine (`software/`)

A second, clean-room engine that draws everything in code (no extracted assets) into a 640x480 framebuffer:
portrait 2:3 playfield, Avenir/Roboto text via stb_truetype, generated art and sound effects.

```bash
make ENGINE=soft        # builds zType-wii-soft.dol (objects in build-soft/)
tools/install_avenir.sh # optional, macOS: use Avenir like the original (licensed, kept out of git; Roboto otherwise)
```

- Music: same `sd:/apps/typeshot/music/` folder as the main engine, see [Music](#music) (synth loop if absent)
- Own text: put `text.txt` in `sd:/apps/typeshot/` and pick "load your own text"
- Keyboard: Enter = EMP/select, Backspace = cancel target, Esc = pause, arrows = menus, `I` on the title = credits
