#!/usr/bin/env python3
"""Windows icon = the Mac app icon (Mud Slogel idle sprite, frame 0, nearest-neighbour; app/icon/make_icon.py).
Output: loader/start/win/icon/dsmodded.ico (16..256 px)."""
import os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else
                         '~/dungeonsettlersdb/site/anim_scenes/MudSlogel_MS_Idle_sheet.png')
OUT = os.path.join(HERE, 'dsmodded.ico')

sheet = Image.open(SRC).convert('RGBA')
assert sheet.size == (160, 36), sheet.size
body = sheet.crop((0, 0, 32, 36)).crop((2, 4, 30, 34))   # same crop as app/icon/make_icon.py
sq = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
sq.paste(body, ((32 - body.width) // 2, (32 - body.height) // 2))
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [sq.resize((s, s), Image.Resampling.NEAREST) for s in sizes]     # every size nearest-neighbour (no blur)
imgs[-1].save(OUT, format='ICO', sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
print(OUT)
