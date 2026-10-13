# TypeShot (Wii)

A ZType-style typing shooter for Wii with USB keyboard support. Original code and
procedural graphics (not a copy of zty.pe's assets).

- Build: `make` (devkitPPC + libogc + libwiikeyboard)
- Install: copy `apps/typeshot/` to `SD:/apps/` and launch from the Homebrew Channel, or `wiiload zType-wii.dol`
- Controls: type letters to lock on and shoot; Enter = start; Esc = pause; Wiimote A/+ = start/pause; HOME = exit; Tab or Wiimote - = mute
- Sound is synthesized at startup (SFX + looping music), no audio assets
- CI: GitHub Actions builds the .dol on every push (see `.github/workflows/build.yml`)
