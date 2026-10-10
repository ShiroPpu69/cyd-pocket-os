"""Offline launcher design preview, using firmware RGB565 tokens and pixel font.

This is a layout preview, not a photograph or a capture of the physical LCD.
Pillow's circle/rounded-rectangle rasterization can differ from LovyanGFX.
"""
from pathlib import Path
import math
import re
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
STYLE = (ROOT / 'esp32_system/home_style.h').read_text(encoding='utf-8')
SKETCH = (ROOT / 'esp32_system/esp32_system.ino').read_text(encoding='utf-8')
TOKENS = {key: int(value, 0) for key, value in re.findall(
    r'constexpr (?:int|uint16_t) (\w+) = (0x[0-9A-Fa-f]+|\d+);', STYLE + SKETCH)}
FONT_PATH = Path.home() / 'Documents/Arduino/libraries/LovyanGFX/src/lgfx/Fonts/glcdfont.h'
FONT = [int(value, 16) for value in re.findall(
    r'0x([0-9A-Fa-f]+)', FONT_PATH.read_text().split('font[] PROGMEM = {')[1].split('};')[0])]
HERO_SOURCE = (ROOT / 'esp32_system/hero_icons_rgb565.h').read_text()


def rgb(c):
    return ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def mix(a, b, amount):
    inverse = 256 - amount
    r = ((a >> 11) * inverse + (b >> 11) * amount) >> 8
    g = (((a >> 5) & 63) * inverse + ((b >> 5) & 63) * amount) >> 8
    bl = ((a & 31) * inverse + (b & 31) * amount) >> 8
    return (r << 11) | (g << 5) | bl


def array(name):
    values = re.search(rf'{name}\[[^\]]+\] PROGMEM = \{{(.*?)\}};', HERO_SOURCE, re.S)[1]
    return [int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]+)', values)]


def preview(page):
    im = Image.new('RGB', (240, 320), rgb(TOKENS['HOME_BG']))
    d = ImageDraw.Draw(im)

    def rect(x, y, w, h, c, outline=False, radius=0):
        if outline:
            kwargs = {'outline': rgb(c)}
        else:
            kwargs = {'fill': rgb(c)}
        if radius:
            d.rounded_rectangle((x, y, x+w-1, y+h-1), radius, **kwargs)
        else:
            d.rectangle((x, y, x+w-1, y+h-1), **kwargs)

    def circle(x, y, r, c, outline=False):
        d.ellipse((x-r, y-r, x+r, y+r), **{'outline' if outline else 'fill': rgb(c)})

    def line(x1, y1, x2, y2, c):
        d.line((x1, y1, x2, y2), rgb(c))

    def triangle(points, c):
        d.polygon(points, rgb(c))

    def text(s, x, y, c, scale=1, center=False):
        if center:
            x -= len(s)*6*scale//2
        for ch in s:
            for col in range(5):
                value = FONT[ord(ch)*5+col]
                for row in range(8):
                    if value & (1 << row):
                        rect(x+col*scale, y+row*scale, scale, scale, c)
            x += 6*scale

    bg = TOKENS['HOME_BG']
    surface = TOKENS['HOME_SURFACE']
    edge = TOKENS['HOME_EDGE']
    primary = TOKENS['HOME_TEXT']
    secondary = TOKENS['HOME_SECONDARY']
    accent = TOKENS['HOME_ACCENT']
    triangle([(120,23), (239,23), (239,230)], 0x10A2)
    triangle([(175,23), (239,23), (239,155)], 0x18E3)
    line(120,23,239,230,0x2104)
    text('HOME',13,10,secondary)
    for i in range(3):
        rect(179+i*5,17-i*3,3,3+i*3,accent)
    circle(208,13,3,secondary,True)
    line(203,13,204,13,secondary)
    line(212,13,213,13,secondary)
    line(208,8,208,9,secondary)
    line(208,17,208,18,secondary)
    text('78',219,10,secondary)
    text('CYD OS',12,36,primary,3)
    text('Your apps, one touch away.',13,67,secondary)

    entries = re.findall(r'\{Screen::\w+, "([^"]+)", "[^"]+", (\w+), (\d+)\}',
                         (ROOT / 'esp32_system/home_apps.h').read_text(encoding='utf-8'))
    for slot, (name, color_name, kind_string) in enumerate(entries[page*6:page*6+6]):
        x = TOKENS['HOME_GRID_X'] + slot % 3 * TOKENS['HOME_GRID_STEP_X']
        y = TOKENS['HOME_GRID_Y'] + slot // 3 * TOKENS['HOME_GRID_STEP_Y']
        cx, cy = x+32, y+32
        assert x+64 <= 240 and y+TOKENS['HOME_CELL_HEIGHT'] < TOKENS['HOME_NAV_Y']
        color = TOKENS[color_name]
        tile = mix(surface, color, 35)
        ink = mix(color, primary, 88)
        rect(x+1,y+3,62,63,0,radius=15)
        rect(x,y,64,64,surface,radius=15)
        rect(x+2,y+2,60,60,tile,radius=13)
        rect(x,y,64,64,edge,True,15)
        line(x+17,y+2,x+46,y+2,mix(tile,primary,32))
        kind = int(kind_string)
        if kind == 0:
            for i in range(8):
                tx, ty = cx+int(math.cos(i*.785398)*14), cy+int(math.sin(i*.785398)*14)
                rect(tx-3,ty-3,7,7,ink,radius=1)
            circle(cx,cy,14,ink)
            circle(cx,cy,7,tile)
            circle(cx,cy,4,ink,True)
        elif kind == 1:
            rect(cx-17,cy-17,34,34,ink)
            triangle([(cx-15,cy-17),(cx+17,cy+12),(cx+17,cy+17)],tile)
            triangle([(cx-15,cy-17),(cx+17,cy+17),(cx-17,cy-10)],tile)
            triangle([(cx+4,cy-14),(cx+14,cy-14),(cx+14,cy-4)],tile)
            triangle([(cx-14,cy+4),(cx-14,cy+14),(cx-4,cy+14)],tile)
        elif kind == 2:
            circle(cx,cy,20,ink)
            for curve in range(3):
                span = 13-curve*2
                for px in range(-span,span+1):
                    py = cy-7+curve*7+px*px//80+int(px/8)
                    circle(cx+px,py,1,tile)
        elif kind == 3:
            pixels, mask = array('hero_icon_2'), array('hero_icon_mask_2')
            for iy in range(24):
                for ix in range(24):
                    if mask[iy*3+ix//8] & (0x80 >> (ix%8)):
                        rect(cx-24+ix*2,cy-24+iy*2,2,2,pixels[iy*24+ix])
        elif kind == 4:
            rect(cx-20,cy-15,40,30,ink,radius=3)
            for key in range(1,5):
                line(cx-20+key*8,cy-12,cx-20+key*8,cy+12,tile)
            for key in [0,1,3]:
                rect(cx-15+key*8,cy-15,5,17,tile)
        else:
            circle(cx,cy,18,ink,True)
            circle(cx,cy,17,ink,True)
            circle(cx,cy-9,2,ink)
            rect(cx-2,cy-3,5,16,ink,radius=1)
        text(name,x+32,y+68,primary,center=True)
    if page*6 >= len(entries):
        rect(74,127,92,78,edge,True,18)
        rect(84,117,72,78,secondary,True,15)
        line(108,155,131,155,accent)
        line(120,143,120,166,accent)
        text('Room for more',120,223,primary,center=True)
    rect(12,278,216,27,surface,radius=13)
    left = primary if page > 0 else edge
    right = primary if page == 0 else edge
    line(30,286,25,291,left)
    line(25,291,30,296,left)
    line(210,286,215,291,right)
    line(215,291,210,296,right)
    text(f'{page+1:02d} / 02',120,287,secondary,center=True)
    rect(104,313,32,3,edge,radius=1)
    rect(104+page*16,313,16,3,accent,radius=1)
    return im


if __name__ == '__main__':
    out = ROOT / 'assets/home_ui'
    out.mkdir(parents=True, exist_ok=True)
    for page in range(2):
        preview(page).save(out / f'page_{page+1}.png')
    combined = Image.new('RGB',(504,344),'#24282a')
    combined.paste(preview(0),(8,12))
    combined.paste(preview(1),(256,12))
    combined.resize((1008,688),Image.Resampling.NEAREST).save(out / 'launcher_preview.png')
    print(out / 'launcher_preview.png')
