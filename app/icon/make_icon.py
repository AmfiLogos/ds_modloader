#!/usr/bin/env python3
"""App icon = Mud Slogel idle sprite, frame 0, nearest-neighbour scaled (no blur).
Source: the wiki's extracted sheet (160 x 36, 5 frames of 32 px). Output: app/icon/AppIcon.icns."""
import os, subprocess, sys, tempfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else
                         '~/dungeonsettlersdb/site/anim_scenes/MudSlogel_MS_Idle_sheet.png')
OUT = os.path.join(HERE, 'AppIcon.icns')

sheet = Image.open(SRC).convert('RGBA')
assert sheet.size == (160, 36), sheet.size
frame = sheet.crop((0, 0, 32, 36))
x0, y0, x1, y1 = (2, 4, 30, 34)              # the slogel itself inside frame 0
body = frame.crop((x0, y0, x1, y1))           # 28 x 30
side = 32                                      # square canvas, 1 px margin around the 30 px tall body
sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
sq.paste(body, ((side - body.width) // 2, (side - body.height) // 2))

tmp = tempfile.mkdtemp(prefix='dsicon_')
iconset = os.path.join(tmp, 'AppIcon.iconset')
os.mkdir(iconset)
for s in (16, 32, 128, 256, 512):
    for scale, suffix in ((1, ''), (2, '@2x')):
        px = s * scale
        sq.resize((px, px), Image.Resampling.NEAREST).save(os.path.join(iconset, f'icon_{s}x{s}{suffix}.png'))
subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', OUT], check=True)
print(OUT)
