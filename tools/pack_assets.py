#!/usr/bin/env python3
"""Bake the original ZType (https://zty.pe/) media into GX texture blobs (data/*.bin)
and the C headers the Wii port reads (source/assets.h, source/words.h).

Run from the repository root:

    python3 tools/pack_assets.py            # textures, headers, word lists
    python3 tools/pack_assets.py --sfx      # also re-encode the sound effects (needs ffmpeg)

Needs Pillow. Missing media is downloaded from https://zty.pe/media/ into zty_assets/.
"""
import os, shutil, sys, subprocess, urllib.request
from PIL import Image, ImageDraw, ImageFont

MEDIA = 'https://zty.pe/media/'
ASSETS = [
    'background/gradient.png', 'background/grid.png', 'background/stars.jpg', 'background/page.png',
    'fonts/avenir-18-white.png', 'fonts/avenir-22-orange-v2.png', 'fonts/avenir-22-white-v2.png',
    'fonts/avenir-36-blue.png', 'fonts/avenir-36-orange.png', 'fonts/avenir-36-white.png',
    'sprites/bullet.png', 'sprites/destroyer.png', 'sprites/emp.png', 'sprites/explosion.png',
    'sprites/explosion-huge.jpg', 'sprites/explosion-huge-glitch.jpg',
    'sprites/mine.png', 'sprites/missle.png', 'sprites/oppressor.png', 'sprites/plasma.png',
    'sprites/ship.png', 'sprites/spark.png',
    'title/credits.png', 'title/exhaust.png', 'title/ship.png', 'title/ztype.png', 'title/phoboslab.png',
    'title/glitch-stripe.png', 'title/glitch-code.png', 'title/glitch-jpeg.png',
    'title/glitch-graph.png', 'title/glitch-log.png',
    'ui/bar-blue.png', 'ui/emp-buttons.png', 'ui/multi-indicator.png', 'ui/reticle.png',
    'ui/pause.png', 'ui/personal-best-badge.png', 'ui/information.png',
    'ui/fb-button.png', 'ui/twitter-button.png',
    'sounds/cancel.ogg', 'sounds/click.ogg', 'sounds/emp.ogg', 'sounds/explosion-large.ogg',
    'sounds/explosion-player.ogg', 'sounds/explosion-small.ogg', 'sounds/explosion.ogg',
    'sounds/hit.ogg', 'sounds/multi-2.ogg', 'sounds/multi-3.ogg', 'sounds/plasma.ogg',
    'sounds/spawn.ogg', 'sounds/target.ogg',
    'music/endure.ogg', 'music/orientation.ogg',
]
SANS_FONT = 'software/fonts/Arimo_Regular.ttf'   # Arimo (Apache 2.0), metric twin of the Arial that the browser's sans-serif maps to

# The Wii draws the 480x720 canvas into a 320x480 area of the 640x480 frame (scale 2/3), so the big
# static backgrounds are baked at that final size instead of being squeezed by GX at run time.
SCREEN = 2.0 / 3.0


def ensure_assets():
    os.makedirs('zty_assets', exist_ok=True)
    if not os.path.exists('ztype_web.js'):
        print('Downloading ztype_web.js...')
        urllib.request.urlretrieve('https://zty.pe/ztype.min.js', 'ztype_web.js')
    for a in ASSETS:
        dest = os.path.join('zty_assets', a)
        if not os.path.exists(dest):
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            print(f'Downloading {a}...')
            urllib.request.urlretrieve(MEDIA + a, dest)


def load(path, mode='RGBA'):
    return Image.open(os.path.join('zty_assets', path)).convert(mode)


# ---------------------------------------------------------------- GX texture encoders

def encode_rgba8(img):
    img = img.convert('RGBA')
    w, h = img.size
    bw, bh = (w + 3) // 4, (h + 3) // 4
    px = img.tobytes()
    out = bytearray(bw * bh * 64)
    o = 0
    for by in range(bh):
        for bx in range(bw):
            ar = bytearray(32)
            gb = bytearray(32)
            i = 0
            for y in range(4):
                py = by * 4 + y
                for x in range(4):
                    pxx = bx * 4 + x
                    if pxx < w and py < h:
                        k = (py * w + pxx) * 4
                        r, g, b, a = px[k], px[k + 1], px[k + 2], px[k + 3]
                    else:
                        r = g = b = a = 0
                    ar[i] = a
                    ar[i + 1] = r
                    gb[i] = g
                    gb[i + 1] = b
                    i += 2
            out[o:o + 32] = ar
            out[o + 32:o + 64] = gb
            o += 64
    return out


def encode_rgb565(img):
    img = img.convert('RGB')
    w, h = img.size
    bw, bh = (w + 3) // 4, (h + 3) // 4
    px = img.tobytes()
    out = bytearray(bw * bh * 32)
    o = 0
    for by in range(bh):
        for bx in range(bw):
            for y in range(4):
                py = by * 4 + y
                for x in range(4):
                    pxx = bx * 4 + x
                    if pxx < w and py < h:
                        k = (py * w + pxx) * 3
                        r, g, b = px[k], px[k + 1], px[k + 2]
                    else:
                        r = g = b = 0
                    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    out[o] = v >> 8
                    out[o + 1] = v & 0xff
                    o += 2
    return out


def write_tex(name, img, fmt='rgba8'):
    assert img.width <= 1024 and img.height <= 1024, (name, img.size)
    data = encode_rgba8(img) if fmt == 'rgba8' else encode_rgb565(img)
    with open(f'data/tex_{name}.bin', 'wb') as f:
        f.write(data)
    print(f'  tex_{name}: {img.width}x{img.height} {fmt} ({len(data) // 1024} KB)')
    return img.width, img.height


def shelf_pack(items, width, margin=2):
    """items: [(name, Image)] -> ({name: (x, y)}, total_height). Tallest first, left to right."""
    pos = {}
    x = y = shelf_h = 0
    for name, im in sorted(items, key=lambda it: -it[1].height):
        w, h = im.size
        if x + w > width:
            x, y, shelf_h = 0, y + shelf_h + margin, 0
        pos[name] = (x, y)
        x += w + margin
        shelf_h = max(shelf_h, h)
    return pos, y + shelf_h


def round4(n):
    return (n + 3) & ~3


# ---------------------------------------------------------------- impact.js font strips

def parse_impact_font(path):
    """Mirror of ig.Font._loadMetrics: glyph widths come from the opaque pixels of the last row."""
    im = load(path)
    w, h = im.size
    px = im.tobytes()
    widths, indices = [], []
    cur = 0
    for x in range(w):
        if px[((h - 1) * w + x) * 4 + 3] > 127:
            cur += 1
        elif cur:
            widths.append(cur)
            indices.append(x - cur)
            cur = 0
    widths.append(cur)
    indices.append(w - cur)
    # drawn height is (font.height - 2) with font.height = image height - 1
    return im, h - 1, h - 3, list(zip(indices, widths))


def main():
    sfx = '--sfx' in sys.argv
    ensure_assets()
    os.makedirs('data', exist_ok=True)
    hdr = []   # lines of source/assets.h

    hdr.append('/* Generated by tools/pack_assets.py - Do not edit manually */')
    hdr.append('#ifndef ASSETS_H\n#define ASSETS_H\n')
    hdr.append('#include <gccore.h>\n#include <stdint.h>\n')

    # ----------------- sprite atlas -----------------
    print('Packing sprite atlas...')
    sheet = [
        ('reticle', 'ui/reticle.png'), ('emp', 'sprites/emp.png'), ('ship', 'sprites/ship.png'),
        ('plasma', 'sprites/plasma.png'), ('oppressor', 'sprites/oppressor.png'),
        ('destroyer', 'sprites/destroyer.png'), ('mine', 'sprites/mine.png'),
        ('missle', 'sprites/missle.png'), ('bullet', 'sprites/bullet.png'),
        ('explosion', 'sprites/explosion.png'), ('spark', 'sprites/spark.png'),
        ('emp_buttons', 'ui/emp-buttons.png'), ('multi_indicator', 'ui/multi-indicator.png'),
        ('exhaust', 'title/exhaust.png'), ('phoboslab', 'title/phoboslab.png'),
        ('pause', 'ui/pause.png'), ('personal_best', 'ui/personal-best-badge.png'),
        ('information', 'ui/information.png'),
        ('fb_button', 'ui/fb-button.png'), ('twitter_button', 'ui/twitter-button.png'),
    ]
    imgs = [(n, load(p)) for n, p in sheet]
    pos, ah = shelf_pack(imgs, 1024)
    ah = round4(ah)
    atlas = Image.new("RGBA", (1024, ah), (0, 0, 0, 0))
    for n, im in imgs:
        atlas.paste(im, pos[n])
    sprites_w, sprites_h = write_tex('sprites', atlas)
    hdr.append('/* tex_sprites atlas rects (pixels) */')
    hdr.append('typedef struct { uint16_t x, y, w, h; } Spr;')
    for n, im in imgs:
        x, y = pos[n]
        hdr.append(f'static const Spr SPR_{n.upper()} = {{{x}, {y}, {im.width}, {im.height}}};')
    hdr.append('')

    # ----------------- avenir UI fonts -----------------
    print('Packing UI fonts...')
    ui_names = [('FONT_36_WHITE', 'avenir-36-white'), ('FONT_36_ORANGE', 'avenir-36-orange'),
                ('FONT_36_BLUE', 'avenir-36-blue'), ('FONT_18_WHITE', 'avenir-18-white')]
    parsed = []
    for enum, fname in ui_names:
        im, height, ch, glyphs = parse_impact_font(f'fonts/{fname}.png')
        parsed.append((enum, fname, im, height, ch, glyphs))
    cells = []
    for enum, fname, im, height, ch, glyphs in parsed:
        for gi, (gx, gw) in enumerate(glyphs):
            cells.append(((fname, gi), im.crop((gx, 0, gx + max(gw, 1), ch))))
    cpos, fh = shelf_pack(cells, 1024, margin=1)
    fh = round4(fh)
    fatlas = Image.new('RGBA', (1024, fh), (0, 0, 0, 0))
    for key, cim in cells:
        fatlas.paste(cim, cpos[key])
    ui_w, ui_h = write_tex('font_ui', fatlas)
    hdr.append('/* impact.js bitmap fonts packed into tex_font_ui */')
    hdr.append('typedef struct { uint16_t x, y, w; } GlyphUI;')
    hdr.append('typedef struct { const GlyphUI *g; int n; int height; int ch; } FontUI;')
    for enum, fname, im, height, ch, glyphs in parsed:
        arr = f'{enum}_GLYPHS'
        hdr.append(f'static const GlyphUI {arr}[{len(glyphs)}] = {{')
        for gi, (gx, gw) in enumerate(glyphs):
            cx, cy = cpos[(fname, gi)]
            hdr.append(f'    {{{cx}, {cy}, {gw}}},')
        hdr.append('};')
        hdr.append(f'static const FontUI {enum}_DEF = {{{arr}, {len(glyphs)}, {height}, {ch}}};')
    hdr.append('enum { ' + ', '.join(e for e, *_ in parsed) + ', FONT_UI_COUNT };')
    hdr.append('static const FontUI *const FONT_UI_DEFS[FONT_UI_COUNT] = { ' +
               ', '.join(f'&{e}_DEF' for e, *_ in parsed) + ' };')
    hdr.append('')

    # ----------------- avenir-22 enemy fonts -----------------
    print('Packing enemy font...')
    fe = Image.new('RGBA', (512, 224), (0, 0, 0, 0))
    fe.paste(load('fonts/avenir-22-white-v2.png'), (0, 0))
    fe.paste(load('fonts/avenir-22-orange-v2.png'), (0, 112))
    enemy_w, enemy_h = write_tex('font_enemy', fe)
    web_js = open('ztype_web.js').read()
    pos0 = web_js.find('METRICS=[') + len('METRICS=[')
    metrics = [int(v) for v in web_js[pos0:web_js.find('];', pos0)].split(',')]
    hdr.append('/* avenir-22 glyph metrics (game.avenir-next), white strip at y=0, orange at y=112 */')
    hdr.append('typedef struct { int16_t sx, sy, sw, sh, ox, oy, adv; } Glyph22;')
    hdr.append(f'static const Glyph22 ENEMY_FONT_METRICS[{len(metrics) // 7}] = {{')
    for i in range(len(metrics) // 7):
        hdr.append('    {' + ', '.join(str(v) for v in metrics[i * 7:(i + 1) * 7]) + '},')
    hdr.append('};\n')

    # ----------------- sans-serif (stats screen) -----------------
    print('Packing sans font...')
    sans_sizes = (10, 12, 14, 28)
    gl = []
    for size in sans_sizes:
        f = ImageFont.truetype(SANS_FONT, size)
        for c in range(32, 127):
            ch = chr(c)
            l, t, r, b = f.getbbox(ch, anchor='ls')
            w, h = max(r - l, 0), max(b - t, 0)
            im = Image.new('RGBA', (max(w, 1), max(h, 1)), (255, 255, 255, 0))
            if w and h:
                mask = Image.new('L', (w, h), 0)
                ImageDraw.Draw(mask).text((-l, -t), ch, font=f, fill=255, anchor='ls')
                im.putalpha(mask)
            gl.append(((size, c), im, l, t, f.getlength(ch)))
    spos, sh = shelf_pack([(k, im) for k, im, *_ in gl], 512, margin=1)
    sh = round4(sh)
    satlas = Image.new('RGBA', (512, sh), (255, 255, 255, 0))
    for k, im, *_ in gl:
        satlas.paste(im, spos[k])
    sans_w, sans_h = write_tex('font_sans', satlas)
    hdr.append('/* Roboto glyphs for the stats screen: offsets are from the text baseline origin */')
    hdr.append('typedef struct { uint16_t x, y, w, h; int8_t xo, yo; float adv; } GlyphSans;')
    for size in sans_sizes:
        hdr.append(f'static const GlyphSans SANS_{size}[95] = {{')
        for k, im, l, t, adv in gl:
            if k[0] != size:
                continue
            x, y = spos[k]
            has = im.getchannel('A').getbbox() is not None
            hdr.append(f'    {{{x}, {y}, {im.width if has else 0}, {im.height if has else 0}, {l}, {t}, {adv:.3f}f}},')
        hdr.append('};')
    hdr.append('')

    # ----------------- backgrounds, title, overlays -----------------
    print('Packing backgrounds...')
    # page.png tiles on the body over a black background
    page = load('background/page.png')
    base = Image.new('RGBA', page.size, (0, 0, 0, 255))
    base.alpha_composite(page)
    write_tex('page', base)
    hdr.append('#define PAGE_TILE 512')

    stars = load('background/stars.jpg', 'RGB')       # drawn at 0.75 inside the canvas
    sw, sh2 = round(stars.width * 0.75 * SCREEN), round(stars.height * 0.75 * SCREEN)
    write_tex('stars', stars.resize((sw, sh2), Image.Resampling.LANCZOS))
    hdr.append(f'#define STARS_W {sw}\n#define STARS_H {sh2}\n#define STARS_SRC_H {stars.height}')

    grad = load('background/gradient.png', 'RGB')      # stretched over the whole 480x720 canvas
    write_tex('gradient', grad.resize((round(480 * SCREEN), round(720 * SCREEN)), Image.Resampling.LANCZOS))

    grid = load('background/grid.png')                 # 620px tiles
    g = round(grid.width * SCREEN)
    write_tex('grid', grid.resize((g, g), Image.Resampling.LANCZOS))
    hdr.append(f'#define GRID_TILE {grid.width}')

    for name, path, fmt in [
        ('logo', 'title/ztype.png', 'rgba8'), ('tship', 'title/ship.png', 'rgba8'),
        ('credits', 'title/credits.png', 'rgba8'),
        ('huge', 'sprites/explosion-huge.jpg', 'rgba8'),
        ('huge_glitch', 'sprites/explosion-huge-glitch.jpg', 'rgba8'),
        ('gl_stripe', 'title/glitch-stripe.png', 'rgb565'), ('gl_code', 'title/glitch-code.png', 'rgb565'),
        ('gl_jpeg', 'title/glitch-jpeg.png', 'rgb565'), ('gl_graph', 'title/glitch-graph.png', 'rgb565'),
        ('gl_log', 'title/glitch-log.png', 'rgb565'),
    ]:
        im = load(path)
        write_tex(name, im, fmt)
        hdr.append(f'#define TEX_{name.upper()}_W {im.width}\n#define TEX_{name.upper()}_H {im.height}')
    hdr.append(f'#define TEX_SPRITES_W {sprites_w}\n#define TEX_SPRITES_H {sprites_h}')
    hdr.append(f'#define TEX_FONT_UI_W {ui_w}\n#define TEX_FONT_UI_H {ui_h}')
    hdr.append(f'#define TEX_FONT_ENEMY_W {enemy_w}\n#define TEX_FONT_ENEMY_H {enemy_h}')
    hdr.append(f'#define TEX_FONT_SANS_W {sans_w}\n#define TEX_FONT_SANS_H {sans_h}')
    hdr.append(f'#define TEX_STARS_W {sw}\n#define TEX_STARS_H {sh2}')
    hdr.append(f'#define TEX_GRID_W {g}\n#define TEX_GRID_H {g}')
    hdr.append(f'#define TEX_GRADIENT_W {round(480 * SCREEN)}\n#define TEX_GRADIENT_H {round(720 * SCREEN)}')
    hdr.append('#define TEX_PAGE_W 512\n#define TEX_PAGE_H 512\n')

    # ----------------- sound effects -----------------
    sfx_info = [
        ('plasma', 'plasma.ogg', 0.5), ('hit', 'hit.ogg', 0.5), ('click', 'click.ogg', 0.3),
        ('target', 'target.ogg', 0.5), ('explode_s', 'explosion-small.ogg', 0.8),
        ('explode_m', 'explosion.ogg', 1.0), ('explode_l', 'explosion-large.ogg', 1.5),
        ('explode_p', 'explosion-player.ogg', 2.0), ('emp', 'emp.ogg', 1.5),
        ('spawn', 'spawn.ogg', 2.0), ('multi_2', 'multi-2.ogg', 0.8),
        ('multi_3', 'multi-3.ogg', 0.8), ('cancel', 'cancel.ogg', 0.2),
    ]
    if sfx:
        print('Exporting SFX PCM...')
        for name, fname, dur in sfx_info:
            p = os.path.join('zty_assets/sounds', fname)
            # ASND plays big-endian samples, like everything else on the Wii's PowerPC
            res = subprocess.run(['ffmpeg', '-y', '-i', p, '-t', str(dur), '-ar', '32000', '-ac', '1',
                                  '-f', 's16be', 'pipe:1'], capture_output=True)
            with open(f'data/sfx_{name}.bin', 'wb') as f:
                f.write(res.stdout)
    # The two soundtracks are linked in as they are and decoded while they play (source/music.c)
    for name in ('endure', 'orientation'):
        shutil.copyfile(f'zty_assets/music/{name}.ogg', f'data/music_{name}.ogg')
    hdr.append('enum {')
    for name, _, _ in sfx_info:
        hdr.append(f'    SFX_{name.upper()},')
    hdr.append('    SFX_MAX\n};\n')
    hdr.append('#endif')

    with open('source/assets.h', 'w') as f:
        f.write('\n'.join(hdr) + '\n')
    print('Wrote source/assets.h')

    # ----------------- word lists (straight from the game's source) -----------------
    pos_w = web_js.find('ig.WORDS.EN={')
    pos_wend = web_js.find('};', pos_w)
    words = eval(web_js[pos_w + len('ig.WORDS.EN='):pos_wend + 1])
    with open('source/words.h', 'w') as f:
        f.write('/* Generated authentic ZType word lists */\n#ifndef WORDS_H\n#define WORDS_H\n\n')
        for length in sorted(words):
            f.write(f'static const char *const WORDS_{length}[{len(words[length])}] = {{\n')
            for i in range(0, len(words[length]), 8):
                f.write('    ' + ', '.join(f'"{w}"' for w in words[length][i:i + 8]) + ',\n')
            f.write('};\n')
            f.write(f'#define WORDS_{length}_COUNT {len(words[length])}\n\n')
        f.write('static const char *const *const WORDS_BY_LEN[13] = {\n    0, 0,\n')
        for l in range(2, 13):
            f.write(f'    WORDS_{l},\n')
        f.write('};\n\nstatic const int WORDS_COUNT_BY_LEN[13] = {\n    0, 0,\n')
        for l in range(2, 13):
            f.write(f'    WORDS_{l}_COUNT,\n')
        f.write('};\n\n#endif\n')
    print('Wrote source/words.h')

    # tex_title.bin / tex_grid etc. from older layouts are no longer used
    for stale in ('data/tex_title.bin',):
        if os.path.exists(stale):
            os.remove(stale)
    print('All assets packed successfully!')


main()
