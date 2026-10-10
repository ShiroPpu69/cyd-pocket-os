"""Build small flash-resident hero portraits from the Deadlock API assets."""
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from pathlib import Path
import json
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SIZE = 24

def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "CYD-Deadlock-assets/1.0"})
    with urllib.request.urlopen(request, timeout=25) as response:
        return response.read()

def convert(hero):
    url = hero.get("images", {}).get("icon_image_small")
    if not url:
        return hero, None, None
    try:
        source = Image.open(BytesIO(fetch(url))).convert("RGBA")
        source.thumbnail((SIZE, SIZE), Image.Resampling.LANCZOS)
        image = Image.new("RGBA", (SIZE, SIZE))
        image.alpha_composite(source, ((SIZE - source.width)//2, (SIZE - source.height)//2))
        pixels, mask = [], []
        for y in range(SIZE):
            bits = 0
            for x in range(SIZE):
                r, g, b, a = image.getpixel((x, y))
                pixels.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                bits = (bits << 1) | int(a >= 128)
                if x % 8 == 7:
                    mask.append(bits)
                    bits = 0
        return hero, pixels, mask
    except Exception as error:
        print(f"Hero {hero['id']} portrait unavailable: {type(error).__name__}")
        return hero, None, None

def main():
    heroes = json.loads(fetch("https://api.deadlock-api.com/v1/assets/heroes"))
    lines = ['// Generated from Deadlock API assets; run tools/generate_deadlock_assets.py.',
             '#pragma once', '#include <Arduino.h>',
             'struct DeadlockHeroAsset { uint16_t id; const char* name; const uint16_t* pixels; const uint8_t* mask; };']
    entries = []
    with ThreadPoolExecutor(max_workers=6) as pool:
        for hero, pixels, mask in pool.map(convert, heroes):
            hero_id = int(hero['id'])
            if pixels:
                lines.append(f'const uint16_t deadlock_icon_{hero_id}[576] PROGMEM = {{')
                lines.extend('  ' + ','.join(f'0x{v:04x}' for v in pixels[i:i+16]) + ','
                             for i in range(0, len(pixels), 16))
                lines.append('};')
                lines.append(f'const uint8_t deadlock_mask_{hero_id}[72] PROGMEM = {{' +
                             ','.join(f'0x{v:02x}' for v in mask) + '};')
            name = json.dumps(hero['name'], ensure_ascii=True)
            entries.append(f'  {{{hero_id},{name},' +
                           (f'deadlock_icon_{hero_id},deadlock_mask_{hero_id}' if pixels else 'nullptr,nullptr') + '},')
    lines += ['const DeadlockHeroAsset DEADLOCK_HERO_ASSETS[] = {', *entries, '};',
              'inline const DeadlockHeroAsset* deadlockHeroAsset(uint16_t id) {',
              '  for (const auto& hero : DEADLOCK_HERO_ASSETS) if (hero.id == id) return &hero;',
              '  return nullptr;', '}']
    (ROOT / 'esp32_system/deadlock_hero_assets.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Generated {len(entries)} hero mappings and {sum("nullptr" not in e for e in entries)} portraits.')

if __name__ == '__main__':
    main()
