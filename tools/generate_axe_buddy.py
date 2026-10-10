"""Generate the ESP32 Axe Buddy pixel-art animation set.

The artwork is drawn at 72x78 and enlarged 2x with nearest-neighbour
sampling so every edge stays crisp on the RGB565 display.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "buddy_axe" / "gifs"
PREVIEW = ROOT / "assets" / "buddy_axe" / "axe_buddy_preview.png"
SIZE = (72, 78)
SCALE = 2

BLACK = "#000000"
OUTLINE = "#100c0d"
SKIN_DARK = "#742923"
SKIN = "#b94335"
SKIN_LIGHT = "#e07858"
ARMOR_DARK = "#20252a"
ARMOR = "#4d555b"
ARMOR_LIGHT = "#9aa2a5"
STEEL = "#c8ced0"
LEATHER = "#49291f"
GOLD = "#d39b3e"
EYE = "#ffe29a"
HEART = "#ef3340"


def poly(draw: ImageDraw.ImageDraw, points, fill, outline=OUTLINE, width=1):
    draw.polygon(points, fill=fill)
    draw.line(points + [points[0]], fill=outline, width=width, joint="curve")


def ellipse(draw: ImageDraw.ImageDraw, box, fill, outline=OUTLINE, width=1):
    draw.ellipse(box, fill=fill, outline=outline, width=width)


def make_weapon(angle: float) -> Image.Image:
    layer = Image.new("RGBA", SIZE, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.line((36, 7, 36, 69), fill=OUTLINE, width=5)
    d.line((36, 8, 36, 68), fill="#784b2d", width=3)
    d.line((35, 13, 37, 13), fill=GOLD, width=1)
    d.line((35, 58, 37, 58), fill=GOLD, width=1)
    poly(d, [(35, 5), (24, 1), (13, 5), (18, 14), (35, 11)], ARMOR)
    poly(d, [(37, 5), (48, 1), (59, 5), (54, 14), (37, 11)], ARMOR)
    d.line((21, 5, 32, 7), fill=ARMOR_LIGHT, width=1)
    d.line((40, 7, 51, 5), fill=STEEL, width=1)
    return layer.rotate(angle, resample=Image.Resampling.NEAREST, center=(36, 38))


def draw_heart(d: ImageDraw.ImageDraw, cx: int, cy: int, scale: int):
    s = scale
    d.rectangle((cx - 2 * s, cy - s, cx + 2 * s, cy + s), fill=HEART)
    d.rectangle((cx - s, cy + s, cx + s, cy + 2 * s), fill=HEART)
    d.rectangle((cx - 2 * s, cy - 2 * s, cx - s, cy - s), fill=HEART)
    d.rectangle((cx + s, cy - 2 * s, cx + 2 * s, cy - s), fill=HEART)


def draw_character(
    pose: str,
    phase: float,
    *,
    axe_angle: float = -28,
    body_y: int = 0,
    wobble: float = 0,
) -> Image.Image:
    canvas = Image.new("RGB", SIZE, BLACK)

    # The axe is deliberately rendered first so the silhouette reads cleanly.
    weapon = make_weapon(axe_angle)
    canvas.paste(weapon, (0, body_y), weapon)
    body = Image.new("RGBA", SIZE, (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    y = body_y

    crouch = 8 if pose == "sleep" else 0
    victory = pose in {"completed", "celebrate"}
    active = pose == "busy"

    # Legs and boots.
    leg_top = 54 + y - crouch // 2
    if pose == "sleep":
        poly(d, [(26, 51 + y), (37, 52 + y), (31, 65 + y), (19, 65 + y)], SKIN_DARK)
        poly(d, [(37, 52 + y), (48, 54 + y), (53, 65 + y), (39, 65 + y)], SKIN_DARK)
        poly(d, [(17, 62 + y), (31, 62 + y), (32, 70 + y), (15, 70 + y)], ARMOR_DARK)
        poly(d, [(40, 62 + y), (54, 62 + y), (57, 70 + y), (40, 70 + y)], ARMOR_DARK)
    else:
        poly(d, [(27, leg_top), (36, leg_top), (34, 69 + y), (24, 69 + y)], SKIN_DARK)
        poly(d, [(37, leg_top), (46, leg_top), (50, 69 + y), (39, 69 + y)], SKIN_DARK)
        poly(d, [(22, 65 + y), (35, 65 + y), (35, 74 + y), (19, 74 + y)], ARMOR_DARK)
        poly(d, [(39, 65 + y), (51, 65 + y), (55, 74 + y), (38, 74 + y)], ARMOR_DARK)
        d.line((23, 67 + y, 33, 67 + y), fill=ARMOR_LIGHT)
        d.line((41, 67 + y, 50, 67 + y), fill=ARMOR_LIGHT)

    # Broad torso, harness and belt.
    poly(d, [(22, 29 + y), (50, 29 + y), (54, 47 + y), (46, 58 + y),
             (27, 58 + y), (18, 47 + y)], SKIN)
    poly(d, [(24, 30 + y), (30, 28 + y), (46, 55 + y), (41, 58 + y)], LEATHER)
    poly(d, [(45, 29 + y), (49, 31 + y), (30, 57 + y), (25, 55 + y)], LEATHER)
    d.rectangle((22, 52 + y, 50, 58 + y), fill=ARMOR_DARK, outline=OUTLINE)
    d.rectangle((33, 52 + y, 40, 59 + y), fill=GOLD, outline=OUTLINE)
    d.rectangle((35, 54 + y, 38, 57 + y), fill=OUTLINE)
    d.line((28, 39 + y, 33, 40 + y), fill=SKIN_LIGHT)
    d.line((40, 40 + y, 46, 39 + y), fill=SKIN_LIGHT)

    # Arms change silhouette for the action states.
    if victory:
        poly(d, [(22, 32 + y), (15, 27 + y), (18, 14 + y), (24, 16 + y),
                 (29, 35 + y)], SKIN)
        poly(d, [(47, 33 + y), (54, 26 + y), (53, 14 + y), (59, 13 + y),
                 (61, 29 + y), (52, 40 + y)], SKIN)
        ellipse(d, (14, 11 + y, 23, 20 + y), ARMOR)
        ellipse(d, (52, 10 + y, 61, 19 + y), ARMOR)
    elif active:
        swing = int(round(math.sin(phase) * 4))
        poly(d, [(21, 33 + y), (11, 39 + y + swing), (7, 48 + y + swing),
                 (13, 51 + y + swing), (29, 41 + y)], SKIN)
        poly(d, [(48, 32 + y), (58, 36 + y - swing), (65, 30 + y - swing),
                 (68, 36 + y - swing), (59, 45 + y), (47, 42 + y)], SKIN)
        ellipse(d, (6, 43 + y + swing, 16, 53 + y + swing), ARMOR)
        ellipse(d, (60, 28 + y - swing, 70, 39 + y - swing), ARMOR)
    elif pose == "sleep":
        poly(d, [(20, 36 + y), (13, 48 + y), (25, 56 + y), (33, 49 + y)], SKIN)
        poly(d, [(49, 36 + y), (58, 48 + y), (47, 56 + y), (39, 49 + y)], SKIN)
        ellipse(d, (10, 45 + y, 21, 56 + y), ARMOR)
        ellipse(d, (52, 45 + y, 63, 56 + y), ARMOR)
    else:
        arm = int(round(math.sin(phase) * 2))
        poly(d, [(21, 32 + y), (13, 39 + y + arm), (12, 52 + y + arm),
                 (19, 54 + y + arm), (29, 39 + y)], SKIN)
        poly(d, [(49, 32 + y), (57, 39 + y - arm), (58, 52 + y - arm),
                 (51, 54 + y - arm), (43, 39 + y)], SKIN)
        ellipse(d, (9, 47 + y + arm, 21, 58 + y + arm), ARMOR)
        ellipse(d, (50, 47 + y - arm, 62, 58 + y - arm), ARMOR)

    # Huge armored shoulders.
    poly(d, [(17, 27 + y), (24, 23 + y), (31, 28 + y), (27, 38 + y),
             (17, 36 + y), (12, 31 + y)], ARMOR)
    poly(d, [(42, 28 + y), (49, 23 + y), (57, 27 + y), (61, 31 + y),
             (56, 36 + y), (46, 38 + y)], ARMOR)
    d.line((16, 29 + y, 25, 26 + y), fill=ARMOR_LIGHT)
    d.line((48, 26 + y, 57, 29 + y), fill=ARMOR_LIGHT)

    # Head, ears, mohawk, face planes and beard.
    head_y = 11 + y + (3 if pose == "sleep" else 0)
    ellipse(d, (22, head_y, 51, head_y + 27), SKIN)
    ellipse(d, (18, head_y + 9, 27, head_y + 18), SKIN_DARK)
    ellipse(d, (47, head_y + 9, 56, head_y + 18), SKIN_DARK)
    poly(d, [(24, head_y + 3), (25, head_y - 3), (29, head_y),
             (32, head_y - 6), (36, head_y - 1), (41, head_y - 7),
             (43, head_y + 2)], OUTLINE, outline=OUTLINE)
    poly(d, [(22, head_y + 17), (28, head_y + 14), (36, head_y + 18),
             (45, head_y + 14), (51, head_y + 18), (47, head_y + 30),
             (39, head_y + 35), (34, head_y + 29), (27, head_y + 34)],
         OUTLINE, outline=OUTLINE)
    poly(d, [(32, head_y + 8), (38, head_y + 6), (42, head_y + 16),
             (35, head_y + 18), (31, head_y + 14)], SKIN_LIGHT)
    d.line((25, head_y + 8, 32, head_y + 10), fill=OUTLINE, width=2)
    d.line((40, head_y + 10, 48, head_y + 8), fill=OUTLINE, width=2)
    if pose == "sleep":
        d.line((26, head_y + 11, 31, head_y + 11), fill=OUTLINE, width=1)
        d.line((41, head_y + 11, 46, head_y + 11), fill=OUTLINE, width=1)
    elif pose == "dizzy":
        for ex in (29, 44):
            d.line((ex - 2, head_y + 9, ex + 2, head_y + 13), fill=EYE)
            d.line((ex + 2, head_y + 9, ex - 2, head_y + 13), fill=EYE)
    else:
        d.rectangle((27, head_y + 10, 31, head_y + 12), fill=EYE)
        d.rectangle((42, head_y + 10, 46, head_y + 12), fill=EYE)
    d.line((31, head_y + 22, 42, head_y + 22), fill=SKIN_LIGHT, width=1)

    # State punctuation lives in the safe upper corners.
    if pose == "attention":
        d.rectangle((62, 5, 65, 17), fill=EYE)
        d.rectangle((62, 20, 65, 23), fill=EYE)
    elif pose == "dizzy":
        for sx, sy in ((11, 8), (61, 9), (8, 24)):
            d.line((sx - 2, sy, sx + 2, sy), fill=GOLD)
            d.line((sx, sy - 2, sx, sy + 2), fill=GOLD)
    elif pose == "heart":
        draw_heart(d, 61, 10, 1 + (1 if math.sin(phase) > 0.65 else 0))
    elif pose == "celebrate":
        for px, py, color in ((8, 10, GOLD), (64, 17, HEART), (10, 31, EYE)):
            d.rectangle((px, py, px + 2, py + 2), fill=color)

    if wobble:
        body = body.rotate(wobble, resample=Image.Resampling.NEAREST,
                           center=(36, 46))
    canvas.paste(body, (0, 0), body)
    return canvas


def frames_for(state: str) -> list[Image.Image]:
    frames = []
    count = 8 if state in {"busy", "celebrate"} else 6
    for index in range(count):
        phase = index / count * math.tau
        bob = int(round(math.sin(phase) * (2 if state in {"attention", "celebrate"} else 1)))
        angle = -28
        wobble = 0.0
        if state == "sleep":
            angle, bob = 82, int(index >= count // 2)
        elif state == "busy":
            angle = -75 + index * (150 / (count - 1))
        elif state == "attention":
            angle, bob = -18, -abs(int(round(math.sin(phase) * 4)))
        elif state in {"completed", "celebrate"}:
            angle = 0
            bob = -abs(int(round(math.sin(phase) * (2 if state == "completed" else 5))))
        elif state == "dizzy":
            angle = -20
            wobble = math.sin(phase) * 5
        elif state == "heart":
            angle = -35
        elif state == "idle_1":
            angle = -34 + math.sin(phase) * 4
        frame = draw_character(state, phase, axe_angle=angle, body_y=bob,
                               wobble=wobble)
        frame = frame.resize((SIZE[0] * SCALE, SIZE[1] * SCALE),
                             Image.Resampling.NEAREST)
        frames.append(frame)
    return frames


def save_gif(path: Path, frames: list[Image.Image], duration: int):
    # A shared 64-colour palette minimizes flash use and prevents per-frame
    # colour changes when AnimatedGIF converts to RGB565.
    palette_seed = Image.new("RGB", (frames[0].width, frames[0].height * len(frames)))
    for index, frame in enumerate(frames):
        palette_seed.paste(frame, (0, index * frame.height))
    palette = palette_seed.quantize(colors=64, method=Image.Quantize.MEDIANCUT)
    paletted = [frame.quantize(palette=palette, dither=Image.Dither.NONE)
                for frame in frames]
    paletted[0].save(
        path,
        save_all=True,
        append_images=paletted[1:],
        loop=0,
        duration=duration,
        disposal=2,
        optimize=False,
    )


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    specs = {
        "sleep": ("sleep", 220),
        "idle_0": ("idle", 150),
        "idle_1": ("idle_1", 150),
        "busy": ("busy", 90),
        "attention": ("attention", 120),
        "completed": ("completed", 130),
        "celebrate": ("celebrate", 100),
        "dizzy": ("dizzy", 130),
        "heart": ("heart", 150),
    }
    previews = []
    for filename, (state, duration) in specs.items():
        frames = frames_for(state)
        save_gif(OUT / f"{filename}.gif", frames, duration)
        previews.append((filename, frames[0]))

    preview = Image.new("RGB", (3 * 160, 3 * 184), "#101214")
    pd = ImageDraw.Draw(preview)
    for index, (label, frame) in enumerate(previews):
        x = (index % 3) * 160 + 8
        y = (index // 3) * 184 + 8
        preview.paste(frame, (x, y))
        pd.text((x + 4, y + 160), label.upper(), fill="#d6dadb")
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    preview.save(PREVIEW)

    total = sum(path.stat().st_size for path in OUT.glob("*.gif"))
    print(f"Generated {len(specs)} animations, {total} bytes total")
    print(PREVIEW)


if __name__ == "__main__":
    main()
