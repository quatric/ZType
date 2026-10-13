# ZType (Wii)

A port of Dominic Szablewski's [ZType](https://zty.pe/) typing shooter for Nintendo Wii with USB keyboard support. Built with hardware-accelerated GX rendering at 640x480 @ 60 FPS.

## Features

- **Authentic ZType Visuals**:
  - Parallax scrolling starfield and deep atmospheric space gradient
  - Glowing cyber grid that flashes upon enemy destruction
  - Authentic ship designs: Player fighter, Missiles, rotating Mines, armored Destroyers, and massive Oppressor dreadnoughts
  - Smooth vector typography using ZType's authentic Avenir fonts (untyped letters in crisp white, typed letters in vibrant orange)
  - Zooming circular targeting lock-on reticle with laser targeting beam
  - Radiant cyan plasma bolts, animated multi-frame explosions, and neon spark particles
  - Authentic top HUD: streak multiplier bar with x2/x3 indicators, EMP stock counters, shield integrity, and wave clear banners
  - Authentic title screen with the iconic ZType logo, hovering ship, and pulsing thruster flames
  - Detailed Game Over statistics screen (Score, Wave, Words, Accuracy %, WPM, Longest Streak, High Score)
- **Authentic Audio**:
  - Sound effects from ZType (plasma shots, hits, misses, reticle target lock, small/medium/large explosions, EMP blast, wave spawn, streak multipliers)
  - Looping electronic soundtrack with mute toggle
- **Hardware-Accelerated GX Renderer**:
  - Native 640x480 resolution at a locked 60 FPS
  - Hardware bilinear texture filtering, alpha blending, and additive blending for brilliant neon glows
- **Controls**:
  - **USB Keyboard**:
    - `A` - `Z`: Type letters to lock onto targets and shoot
    - `Space` / `Enter`: Activate EMP shockwave (in game) / Start & retry (menus)
    - `Escape`: Pause / Resume
    - `Tab`: Toggle sound mute
  - **Wii Remote**:
    - `A`: Start game / confirm
    - `B`: Activate EMP
    - `+`: Pause / Resume
    - `-`: Toggle sound mute
    - `HOME`: Exit to Homebrew Channel
- **Persistence**: High score automatically saved to SD card (`sd:/apps/typeshot/highscore.dat`)
- **Homebrew Channel Ready**: App bundle in `apps/typeshot/` with custom `icon.png` and `meta.xml`

## Building

Requires devkitPPC and libogc:

```bash
make
```

Output binary: `ztype-wii.dol` (also copied to `apps/typeshot/boot.dol`).

## Installation

1. Copy the `apps/typeshot/` folder to the `apps/` directory on your SD card or USB drive (`SD:/apps/typeshot/`).
2. Launch from the Homebrew Channel.
3. Plug a standard USB keyboard into one of the Wii's rear USB ports.
