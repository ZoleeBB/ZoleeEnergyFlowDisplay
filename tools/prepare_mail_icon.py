#!/usr/bin/env python3
"""Saját, kézzel rajzolt postaláda-jelvény (52x52 RGBA PNG) – külső grafika nélkül.

Nyitott, piros ajtós postaláda kilógó levelekkel, felhajtott piros zászlóval
és faoszloppal, kerek, borostyán keretes háttéren. A rajz 8x felbontáson
készül, majd élsimítva kicsinyítjük. A kimenet a firmware-be ágyazott
`main/assets/png/mail_box.png`; futásidőben nincs skálázás.
"""
import sys
from PIL import Image, ImageDraw

SIZE = 52
SCALE = 8
S = SIZE * SCALE


def p(*values):
    return [round(v * SCALE) for v in values]


def draw(path):
    image = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    # Round badge: light sky with grass, amber ring so it reads on day and night themes.
    d.ellipse(p(1, 1, 51, 51), fill=(232, 244, 250, 255))
    grass = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grass)
    gd.rectangle(p(0, 34, 52, 52), fill=(118, 182, 74, 255))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).ellipse(p(1, 1, 51, 51), fill=255)
    image.paste(grass, (0, 0), Image.composite(grass, Image.new('RGBA', (S, S)), mask).split()[3])
    d = ImageDraw.Draw(image)
    d.ellipse(p(1, 1, 51, 51), outline=(232, 146, 15, 255), width=round(2.2 * SCALE))
    outline = (58, 66, 74, 255)
    w = round(1.1 * SCALE)
    # Wooden post.
    d.rectangle(p(24, 28, 30, 46), fill=(190, 124, 62, 255), outline=(122, 76, 34, 255), width=w)
    # Mailbox body (side), rounded top towards the back.
    d.rounded_rectangle(p(15, 15, 41, 30), radius=round(6 * SCALE), fill=(244, 246, 248, 255),
                        outline=outline, width=w)
    d.rectangle(p(15, 22, 22, 30), fill=(244, 246, 248, 255))
    d.line(p(15, 22, 15, 30), fill=outline, width=w)
    d.line(p(15, 30, 22, 30), fill=outline, width=w)
    # Open front: dark arched opening.
    d.rounded_rectangle(p(11, 13, 21, 30), radius=round(5 * SCALE), fill=(52, 60, 68, 255),
                        outline=outline, width=w)
    # Letters sticking out: airmail envelope behind, kraft envelope in front.
    d.polygon(p(11, 17, 16, 15.5, 18, 27, 13, 28), fill=(250, 250, 250, 255), outline=(40, 90, 170, 255))
    d.line(p(11.3, 17.5, 13.2, 27.6), fill=(214, 47, 47, 255), width=round(0.9 * SCALE))
    d.polygon(p(13.5, 18, 20, 18.5, 19.5, 29, 13, 28.5), fill=(233, 194, 122, 255), outline=(150, 108, 40, 255))
    d.line(p(13.5, 18, 16.8, 22.5, 20, 18.5), fill=(150, 108, 40, 255), width=round(0.7 * SCALE))
    # Open red door hanging down from the front hinge.
    d.polygon(p(11, 30, 21, 30, 15, 39, 9, 40, 5, 38), fill=(214, 47, 47, 255), outline=(140, 24, 24, 255))
    d.polygon(p(12.5, 31.5, 18.5, 31.5, 14, 37.5, 8.5, 37), fill=(236, 110, 110, 255))
    # Raised red flag on the side.
    d.rectangle(p(33, 7, 35, 24), fill=(214, 47, 47, 255), outline=(140, 24, 24, 255), width=round(0.6 * SCALE))
    d.rectangle(p(33, 7, 40, 12), fill=(214, 47, 47, 255), outline=(140, 24, 24, 255), width=round(0.6 * SCALE))
    d.ellipse(p(33.2, 21.5, 34.8, 23.1), fill=(150, 20, 20, 255))
    image = image.resize((SIZE, SIZE), Image.LANCZOS)
    image.save(path, optimize=True)


if __name__ == '__main__':
    from pathlib import Path
    root = Path(__file__).resolve().parents[1]
    draw(sys.argv[1] if len(sys.argv) > 1 else root / 'main/assets/png/mail_box.png')
