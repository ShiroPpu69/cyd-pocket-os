"""Host layout QA, not a photograph of the physical LCD. Uses its real pixel font/assets."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import re
import json
import urllib.request
import argparse
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
font_source = (Path.home() / 'Documents/Arduino/libraries/LovyanGFX/src/lgfx/Fonts/glcdfont.h').read_text()
font_body = re.search(r'font\[\].*?\{(.*?)\};', font_source, re.S).group(1)
font_body = re.sub(r'//[^\n]*|/\*.*?\*/', '', font_body, flags=re.S)
font = [int(n, 16) for n in re.findall(r'0x([0-9a-fA-F]+)', font_body)]
assets = (ROOT / 'esp32_system/deadlock_hero_assets.h').read_text()
parser = argparse.ArgumentParser(description='Preview your own history locally. Do not publish personal previews.')
parser.add_argument('--account-id', required=True, type=int, help='Your Steam account ID (32-bit)')
args = parser.parse_args()
request = urllib.request.Request(f'https://api.deadlock-api.com/v1/players/{args.account_id}/match-history', headers={'User-Agent': 'Mozilla/5.0'})
with urllib.request.urlopen(request, timeout=20) as response:
    matches = json.load(response)[:10]

def rgb(value):
    return (((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31)

for page in range(3):
    image = Image.new('RGB', (240, 320), rgb(0x0841))
    draw = ImageDraw.Draw(image)
    def text(value, x, y, color, size=1):
        assert x + len(value) * 6 * size <= 240 and y + 8 * size <= 320, (value, x, y)
        for i, char in enumerate(value):
            for col in range(5):
                bits = font[ord(char) * 5 + col]
                for row in range(8):
                    if bits & (1 << row):
                        left, top = x + (i * 6 + col) * size, y + row * size
                        draw.rectangle((left, top, left + size - 1, top + size - 1), fill=rgb(color))
    def roundrect(x, y, w, h, radius, fill=None, outline=None):
        draw.rounded_rectangle((x, y, x+w-1, y+h-1), radius, fill=rgb(fill) if fill is not None else None,
                               outline=rgb(outline) if outline is not None else None)
    draw.rectangle((8, 9, 10, 26), fill=rgb(0xfdc8))
    text('DEADLOCK', 18, 8, 0xf7be, 2)
    roundrect(175, 6, 57, 24, 5, outline=0x39c7)
    text('UPDATE', 184, 14, 0xfdc8)
    text('MATCH JOURNAL', 8, 33, 0x7bef)
    text('UTC+8', 196, 33, 0x7bef)
    roundrect(8, 45, 224, 13, 3, fill=0x18c3)
    text(f'{len(matches):02d} RECENT  /  {sum(m["player_match_outcome"] == 1 for m in matches):02d} WINS', 15, 48, 0xfdc8)
    for row, match in enumerate(matches[page*4:page*4+4]):
        y = 64 + row*55
        outcome = match['player_match_outcome']
        result = 0x6dd1 if outcome == 1 else 0xf36c if outcome == 2 else 0xfdc8
        roundrect(7, y, 226, 51, 5, fill=0x10a2, outline=0x2945)
        roundrect(7, y+8, 3, 35, 1, fill=result)
        hero = match['hero_id']
        pixels = re.search(rf'deadlock_icon_{hero}\[576\].*?\{{(.*?)\}};', assets, re.S)
        mask = re.search(rf'deadlock_mask_{hero}\[72\].*?\{{(.*?)\}};', assets, re.S)
        if pixels and mask:
            pixels = [int(n, 16) for n in re.findall(r'0x([0-9a-f]+)', pixels.group(1))]
            mask = [int(n, 16) for n in re.findall(r'0x([0-9a-f]+)', mask.group(1))]
            for dy in range(40):
                for dx in range(40):
                    sx, sy = dx*24//40, dy*24//40
                    if mask[sy*3+sx//8] & (0x80 >> (sx & 7)):
                        image.putpixel((13+dx, y+5+dy), rgb(pixels[sy*24+sx]))
        name = re.search(rf'\{{{hero},"([^"]+)"', assets).group(1)[:20]
        text(name, 61, y+6, 0xf7be)
        roundrect(193, y+4, 33, 12, 3, fill=0x1944 if outcome == 1 else 0x38c3)
        text('WIN' if outcome == 1 else 'LOSS' if outcome == 2 else 'N/A', 197, y+6, result)
        text('K / D / A', 61, y+19, 0x7bef)
        text(f'{match["player_kills"]} / {match["player_deaths"]} / {match["player_assists"]}', 61, y+29, 0xf7be)
        text('SOULS', 172, y+19, 0x6d5c)
        text(f'{match["net_worth"]:,}', 166, y+29, 0xa71f)
        date = datetime.fromtimestamp(match['start_time'], timezone(timedelta(hours=8))).strftime('%y/%m/%d %H:%M')
        text(date, 61, y+41, 0x7bef)
        text(f'{(match["match_duration_s"]+30)//60}m', 203, y+41, 0x7bef)
    roundrect(8, 289, 43, 23, 5, outline=0xfdc8 if page else 0x2945)
    text('<', 25, 294, 0xfdc8 if page else 0x7bef, 2)
    text(f'PAGE {page+1} / 3', 88, 298, 0xf7be)
    roundrect(189, 289, 43, 23, 5, outline=0xfdc8 if page < 2 else 0x2945)
    text('>', 205, 294, 0xfdc8 if page < 2 else 0x7bef, 2)
    roundrect(100, 316, 40, 2, 1, fill=0x39c7)
    image.resize((720, 960), Image.Resampling.NEAREST).save(ROOT / f'build/deadlock-preview-{page+1}.png')
print('All three page layouts passed text bounds checks; previews saved in build/.')
