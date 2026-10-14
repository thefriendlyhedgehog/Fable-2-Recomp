#!/usr/bin/env python3
# Compares every screenshot of a sweep directory with the first one and prints
# the share of pixels that differ (any channel by more than 24), to spot
# flickering frames: snowflakes alone keep shots within about 2%.
# Usage: python3 -I tools/shot_diff.py [out/capture/sweep]
import glob
import os
import sys

from PIL import Image, ImageChops

directory = sys.argv[1] if len(sys.argv) > 1 else "out/capture/sweep"
shots = sorted(p for p in glob.glob(os.path.join(directory, "*.png"))
               if os.path.basename(p) != "sheet.png")
base = Image.open(shots[0]).convert("RGB")
for shot in shots[1:]:
    image = Image.open(shot).convert("RGB")
    diff = ImageChops.difference(base, image).convert("L").point(lambda v: 255 if v > 24 else 0)
    changed = diff.histogram()[255]
    print(f"{os.path.basename(shot)} {100.0 * changed / (image.width * image.height):.1f}%")
