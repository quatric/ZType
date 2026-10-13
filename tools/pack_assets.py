import os, sys, subprocess, urllib.request
from PIL import Image

def ensure_assets():
    os.makedirs('zty_assets', exist_ok=True)
    if not os.path.exists('ztype_web.js'):
        print('Downloading ztype_web.js...')
        urllib.request.urlretrieve('https://zty.pe/ztype.min.js', 'ztype_web.js')
    
    assets = [
        'background/gradient.png', 'background/grid.png', 'background/stars.jpg',
        'fonts/avenir-18-white.png', 'fonts/avenir-22-orange-v2.png', 'fonts/avenir-22-white-v2.png',
        'fonts/avenir-36-blue.png', 'fonts/avenir-36-orange.png', 'fonts/avenir-36-white.png',
        'sprites/bullet.png', 'sprites/destroyer.png', 'sprites/emp.png', 'sprites/explosion.png',
        'sprites/mine.png', 'sprites/missle.png', 'sprites/oppressor.png', 'sprites/plasma.png',
        'sprites/ship.png', 'sprites/spark.png',
        'title/credits.png', 'title/exhaust.png', 'title/ship.png', 'title/ztype.png', 'title/phoboslab.png',
        'ui/bar-blue.png', 'ui/emp-buttons.png', 'ui/multi-indicator.png', 'ui/reticle.png',
        'sounds/cancel.ogg', 'sounds/click.ogg', 'sounds/emp.ogg', 'sounds/explosion-large.ogg',
        'sounds/explosion-player.ogg', 'sounds/explosion-small.ogg', 'sounds/explosion.ogg',
        'sounds/hit.ogg', 'sounds/multi-2.ogg', 'sounds/multi-3.ogg', 'sounds/plasma.ogg',
        'sounds/spawn.ogg', 'sounds/target.ogg'
    ]
    for a in assets:
        dest = os.path.join('zty_assets', a)
        if not os.path.exists(dest):
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            print(f'Downloading {a}...')
            urllib.request.urlretrieve('https://zty.pe/media/' + a, dest)

ensure_assets()
def encode_rgba8(img):
    w, h = img.size
    bw = (w + 3) // 4
    bh = (h + 3) // 4
    data = bytearray(bw * bh * 64)
    idx = 0
    for by in range(bh):
        for bx in range(bw):
            for y in range(4):
                py = by * 4 + y
                for x in range(4):
                    px = bx * 4 + x
                    r, g, b, a = img.getpixel((px, py)) if px < w and py < h else (0,0,0,0)
                    data[idx] = a
                    data[idx + 1] = r
                    idx += 2
            for y in range(4):
                py = by * 4 + y
                for x in range(4):
                    px = bx * 4 + x
                    r, g, b, a = img.getpixel((px, py)) if px < w and py < h else (0,0,0,0)
                    data[idx] = g
                    data[idx + 1] = b
                    idx += 2
    return data

def encode_rgb565(img):
    w, h = img.size
    bw = (w + 3) // 4
    bh = (h + 3) // 4
    data = bytearray(bw * bh * 32)
    idx = 0
    for by in range(bh):
        for bx in range(bw):
            for y in range(4):
                py = by * 4 + y
                for x in range(4):
                    px = bx * 4 + x
                    r, g, b = img.getpixel((px, py)) if px < w and py < h else (0,0,0)
                    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    data[idx] = (val >> 8) & 0xff
                    data[idx + 1] = val & 0xff
                    idx += 2
    return data

os.makedirs('data', exist_ok=True)

# ----------------- 1. Sprites Atlas -----------------
print("Packing tex_sprites...")
atlas_sprites = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
reticle = Image.open('zty_assets/ui/reticle.png').convert('RGBA')
atlas_sprites.paste(reticle, (0, 0))
emp = Image.open('zty_assets/sprites/emp.png').convert('RGBA')
atlas_sprites.paste(emp, (256, 0))

sprite_files = [
    ('ship', 'zty_assets/sprites/ship.png'),
    ('plasma', 'zty_assets/sprites/plasma.png'),
    ('oppressor', 'zty_assets/sprites/oppressor.png'),
    ('destroyer', 'zty_assets/sprites/destroyer.png'),
    ('mine', 'zty_assets/sprites/mine.png'),
    ('missle', 'zty_assets/sprites/missle.png'),
    ('bullet', 'zty_assets/sprites/bullet.png'),
    ('explosion', 'zty_assets/sprites/explosion.png'),
    ('spark', 'zty_assets/sprites/spark.png'),
    ('emp_buttons', 'zty_assets/ui/emp-buttons.png'),
    ('multi_indicator', 'zty_assets/ui/multi-indicator.png'),
    ('exhaust', 'zty_assets/title/exhaust.png'),
    ('phoboslab', 'zty_assets/title/phoboslab.png'),
    ('bar_blue', 'zty_assets/ui/bar-blue.png'),
]

sprite_rects = {
    'reticle': (0, 0, 256, 256),
    'emp': (256, 0, 256, 256)
}

cur_x, cur_y, row_h = 0, 256, 0
for name, p in sprite_files:
    im = Image.open(p).convert('RGBA')
    w, h = im.size
    if name == 'bar_blue':
        im = im.crop((0, 0, 64, 2))
        w, h = 64, 2
    if cur_x + w > 512:
        cur_x = 0
        cur_y += row_h + 2
        row_h = 0
    atlas_sprites.paste(im, (cur_x, cur_y))
    sprite_rects[name] = (cur_x, cur_y, w, h)
    cur_x += w + 2
    row_h = max(row_h, h)

with open('data/tex_sprites.bin', 'wb') as f:
    f.write(encode_rgba8(atlas_sprites))

# ----------------- 2. Enemy Font -----------------
print("Packing tex_font_enemy...")
font_enemy = Image.new('RGBA', (512, 224), (0, 0, 0, 0))
f_w = Image.open('zty_assets/fonts/avenir-22-white-v2.png').convert('RGBA')
f_o = Image.open('zty_assets/fonts/avenir-22-orange-v2.png').convert('RGBA')
font_enemy.paste(f_w, (0, 0))
font_enemy.paste(f_o, (0, 112))

with open('data/tex_font_enemy.bin', 'wb') as f:
    f.write(encode_rgba8(font_enemy))

# ----------------- 3. UI Font -----------------
print("Packing tex_font_ui...")
def parse_impact_font(path):
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    char_h = h - 1
    bottom = [im.getpixel((x, h - 1))[3] for x in range(w)]
    glyphs = []
    in_char = False
    start_x = 0
    for x in range(w):
        if bottom[x] > 127:
            if not in_char:
                in_char = True
                start_x = x
        else:
            if in_char:
                in_char = False
                glyphs.append((start_x, 0, x - start_x, char_h))
    if in_char:
        glyphs.append((start_x, 0, w - start_x, char_h))
    return im.crop((0, 0, w, char_h)), glyphs

font_ui = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
ui_font_maps = {}
cur_x, cur_y, row_h = 0, 0, 0
for fname in ['avenir-36-blue', 'avenir-36-white', 'avenir-18-white']:
    im, glyphs = parse_impact_font(f'zty_assets/fonts/{fname}.png')
    g_map = []
    for gx, gy, gw, gh in glyphs:
        if cur_x + gw > 512:
            cur_x = 0
            cur_y += row_h + 1
            row_h = 0
        font_ui.paste(im.crop((gx, gy, gx + gw, gy + gh)), (cur_x, cur_y))
        g_map.append((cur_x, cur_y, gw, gh))
        cur_x += gw + 1
        row_h = max(row_h, gh)
    cur_x = 0
    cur_y += row_h + 2
    row_h = 0
    ui_font_maps[fname] = g_map

with open('data/tex_font_ui.bin', 'wb') as f:
    f.write(encode_rgba8(font_ui))

# ----------------- 4. Title Texture -----------------
print("Packing tex_title...")
title_tex = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
ztype_logo = Image.open('zty_assets/title/ztype.png').convert('RGBA')
target_h = int(ztype_logo.height * 512 / ztype_logo.width)
ztype_logo = ztype_logo.resize((512, target_h), Image.Resampling.LANCZOS)
title_tex.paste(ztype_logo, (0, 0))
title_ship = Image.open('zty_assets/title/ship.png').convert('RGBA')
title_tex.paste(title_ship, (0, target_h + 4))

with open('data/tex_title.bin', 'wb') as f:
    f.write(encode_rgba8(title_tex))

title_rects = {
    'logo': (0, 0, 512, target_h),
    'ship': (0, target_h + 4, title_ship.width, title_ship.height)
}

# ----------------- 5. Backgrounds -----------------
print("Packing tex_stars & tex_grid...")
stars = Image.open('zty_assets/background/stars.jpg').convert('RGB').resize((512, 512), Image.Resampling.LANCZOS)
with open('data/tex_stars.bin', 'wb') as f:
    f.write(encode_rgb565(stars))

grid = Image.open('zty_assets/background/grid.png').convert('RGBA').resize((512, 512), Image.Resampling.LANCZOS)
with open('data/tex_grid.bin', 'wb') as f:
    f.write(encode_rgba8(grid))

# ----------------- 6. SFX PCM -----------------
print("Exporting SFX PCM...")
sfx_info = [
    ('plasma', 'plasma.ogg', 0.5),
    ('hit', 'hit.ogg', 0.5),
    ('click', 'click.ogg', 0.3),
    ('target', 'target.ogg', 0.5),
    ('explode_s', 'explosion-small.ogg', 0.8),
    ('explode_m', 'explosion.ogg', 1.0),
    ('explode_l', 'explosion-large.ogg', 1.5),
    ('explode_p', 'explosion-player.ogg', 2.0),
    ('emp', 'emp.ogg', 1.5),
    ('spawn', 'spawn.ogg', 2.0),
    ('multi_2', 'multi-2.ogg', 0.8),
    ('multi_3', 'multi-3.ogg', 0.8),
    ('cancel', 'cancel.ogg', 0.2),
]

for name, fname, dur in sfx_info:
    p = os.path.join('zty_assets/sounds', fname)
    res = subprocess.run(['ffmpeg', '-y', '-i', p, '-t', str(dur), '-ar', '32000', '-ac', '1', '-f', 's16le', 'pipe:1'], capture_output=True)
    with open(f'data/sfx_{name}.bin', 'wb') as f:
        f.write(res.stdout)

# ----------------- 7. Generate source/assets.h -----------------
print("Generating source/assets.h...")
with open('ztype_web.js') as f:
    web_js = f.read()

pos = web_js.find('METRICS=[') + len('METRICS=[')
pos_end = web_js.find('];', pos)
enemy_metrics = [int(x) for x in web_js[pos:pos_end].split(',')]

with open('source/assets.h', 'w') as f:
    f.write('/* Generated by tools/pack_assets.py - Do not edit manually */\n#ifndef ASSETS_H\n#define ASSETS_H\n\n')
    f.write('#include <gccore.h>\n#include <stdint.h>\n\n')

    # Sprite rects
    f.write('typedef struct { float u0, v0, u1, v1; int w, h; } SpriteRect;\n\n')
    for name, (rx, ry, rw, rh) in sprite_rects.items():
        f.write(f'#define SPRITE_{name.upper()}_X {rx}\n')
        f.write(f'#define SPRITE_{name.upper()}_Y {ry}\n')
        f.write(f'#define SPRITE_{name.upper()}_W {rw}\n')
        f.write(f'#define SPRITE_{name.upper()}_H {rh}\n')
        f.write(f'#define SPRITE_{name.upper()}_U0 ({rx}.0f / 512.0f)\n')
        f.write(f'#define SPRITE_{name.upper()}_V0 ({ry}.0f / 512.0f)\n')
        f.write(f'#define SPRITE_{name.upper()}_U1 (({rx} + {rw}) / 512.0f)\n')
        f.write(f'#define SPRITE_{name.upper()}_V1 (({ry} + {rh}) / 512.0f)\n\n')

    # Title rects
    for name, (rx, ry, rw, rh) in title_rects.items():
        f.write(f'#define TITLE_{name.upper()}_U0 ({rx}.0f / 512.0f)\n')
        f.write(f'#define TITLE_{name.upper()}_V0 ({ry}.0f / 512.0f)\n')
        f.write(f'#define TITLE_{name.upper()}_U1 (({rx} + {rw}) / 512.0f)\n')
        f.write(f'#define TITLE_{name.upper()}_V1 (({ry} + {rh}) / 512.0f)\n')
        f.write(f'#define TITLE_{name.upper()}_W {rw}\n')
        f.write(f'#define TITLE_{name.upper()}_H {rh}\n\n')

    # Enemy font metrics
    f.write('typedef struct {\n    int16_t sx, sy, sw, sh, ox, oy, adv;\n} Glyph22;\n\n')
    f.write('static const Glyph22 ENEMY_FONT_METRICS[224] = {\n')
    for i in range(len(enemy_metrics) // 7):
        sx, sy, sw, sh, ox, oy, adv = enemy_metrics[i*7:(i+1)*7]
        f.write(f'    {{{sx}, {sy}, {sw}, {sh}, {ox}, {oy}, {adv}}},\n')
    f.write('};\n\n')

    # UI font glyphs
    f.write('typedef struct {\n    float u0, v0, u1, v1;\n    uint8_t w, h, adv;\n} GlyphUI;\n\n')
    for fname, arr_name in [('avenir-36-blue', 'FONT36_BLUE'), ('avenir-36-white', 'FONT36_WHITE'), ('avenir-18-white', 'FONT18_WHITE')]:
        f.write(f'static const GlyphUI {arr_name}_GLYPHS[95] = {{\n')
        for gx, gy, gw, gh in ui_font_maps[fname]:
            u0 = gx / 512.0
            v0 = gy / 512.0
            u1 = (gx + gw) / 512.0
            v1 = (gy + gh) / 512.0
            f.write(f'    {{{u0:.6f}f, {v0:.6f}f, {u1:.6f}f, {v1:.6f}f, {gw}, {gh}, {gw}}},\n')
        f.write('};\n\n')

    # SFX IDs
    f.write('enum {\n')
    for i, (name, _, _) in enumerate(sfx_info):
        f.write(f'    SFX_{name.upper()},\n')
    f.write('    SFX_MAX\n};\n\n')

    f.write('#endif\n')

# ----------------- 8. Generate source/words.h -----------------
print("Generating source/words.h...")
pos_w = web_js.find('ig.WORDS.EN={')
pos_wend = web_js.find('};', pos_w)
words_dict = eval(web_js[pos_w + len('ig.WORDS.EN='):pos_wend + 1])

with open('source/words.h', 'w') as f:
    f.write('/* Generated authentic ZType word lists */\n#ifndef WORDS_H\n#define WORDS_H\n\n')
    for length in sorted(words_dict.keys()):
        words = words_dict[length]
        f.write(f'static const char *const WORDS_{length}[{len(words)}] = {{\n')
        for i in range(0, len(words), 8):
            chunk = words[i:i+8]
            f.write('    ' + ', '.join(f'"{w}"' for w in chunk) + ',\n')
        f.write('};\n')
        f.write(f'#define WORDS_{length}_COUNT {len(words)}\n\n')
    
    f.write('static const char *const *const WORDS_BY_LEN[13] = {\n')
    f.write('    0, 0,\n')
    for l in range(2, 13):
        f.write(f'    WORDS_{l},\n')
    f.write('};\n\n')
    f.write('static const int WORDS_COUNT_BY_LEN[13] = {\n')
    f.write('    0, 0,\n')
    for l in range(2, 13):
        f.write(f'    WORDS_{l}_COUNT,\n')
    f.write('};\n\n')
    f.write('#endif\n')

print("All assets packed successfully!")
