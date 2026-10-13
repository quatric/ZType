# ZType for Wii

A ZType-style typing shooter for Wii with USB keyboard support, laid out and styled after
[zty.pe](https://zty.pe/) (ZType by PhobosLab): portrait 2:3 playfield on the dark diamond page,
starfield title screen with the big ZTYPE logo, grid-lit space background, word labels next to
each ship, wave-clear banner, EMP, game-over stats and the settings/pause menu.
Gameplay rules (waves, enemy types, scoring, streak multiplier, 3 EMPs) follow the original.
All graphics and audio are generated in code (no copied assets); text uses Roboto (Apache 2.0).

- Build: `make` (devkitPPC + libogc + libwiikeyboard)
- Install: copy `apps/typeshot/` to `SD:/apps/` and launch from the Homebrew Channel, or `wiiload zType-wii.dol`
- Keyboard: type letters to lock on and shoot; Enter = EMP (or select); Backspace = cancel target; Esc = pause/settings; arrows = menus; `I` on the title = credits
- Wiimote: D-pad = menus, A = select / EMP, + = pause, 1 = cancel target, 2 = credits, HOME = exit
- Title menu: new game, settings (sound/music volume), my stats, load your own text
- Own text: put `text.txt` in `sd:/apps/typeshot/` and pick "load your own text" to use its words
- Scores and stats are saved to `sd:/apps/typeshot/highscore.dat`
- CI: GitHub Actions builds the .dol on every push (see `.github/workflows/build.yml`)
