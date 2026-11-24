#!/usr/bin/env python3
"""Subset the Anuphan variable font for the dashboard.

Outputs web/fonts/anuphan-thai.woff2 and copies the OFL licence next to it.
Run it with a Python that has fontTools and brotli:

    /home/box/.tools-venv/bin/python tools/subset_font.py
"""

import os
import shutil
import sys
import tempfile

from fontTools.subset import main as subset_main

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "vendor", "fonts", "Anuphan[wght].ttf")
LICENCE = os.path.join(ROOT, "vendor", "fonts", "OFL-Anuphan.txt")
OUT_DIR = os.path.join(ROOT, "web", "fonts")
OUT = os.path.join(OUT_DIR, "anuphan-thai.woff2")

UNICODES = ",".join([
    "U+0020-007E",  # Basic Latin
    "U+0E00-0E7F",  # Thai
    "U+2013", "U+2014",  # en/em dash
    "U+2019",  # right single quote
    "U+201C", "U+201D",  # curly quotes
    "U+2022",  # bullet
    "U+00B0",  # degree
    "U+00D7",  # multiplication sign
])

# Thai needs ccmp/locl for mark placement, mark/mkmk for vowel and tone
# stacking, and rlig/kern/liga/calt for normal run shaping.
FEATURES = "ccmp,locl,mark,mkmk,rlig,kern,liga,calt"
LIMIT_BYTES = 45 * 1000


def subset(source, output, extra=()):
    args = [
        source,
        "--output-file=" + output,
        "--flavor=woff2",
        "--unicodes=" + UNICODES,
        "--layout-features=" + FEATURES,
        "--drop-tables+=DSIG",
        "--name-IDs=1,2,3,4,5,6",
        "--name-languages=0x409",
    ]
    args.extend(extra)
    subset_main(args)
    return os.path.getsize(output)


def static_weights():
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    made = []
    for weight in (500, 700):
        with tempfile.TemporaryDirectory() as tmp:
            static_ttf = os.path.join(tmp, "anuphan-%d.ttf" % weight)
            font = TTFont(SRC)
            instantiateVariableFont(font, {"wght": weight}, inplace=True)
            font.save(static_ttf)
            out = os.path.join(OUT_DIR, "anuphan-thai-%d.woff2" % weight)
            size = subset(static_ttf, out)
            made.append((out, size))
    return made


def main():
    if not os.path.isfile(SRC):
        raise SystemExit("missing source font: %s" % SRC)

    os.makedirs(OUT_DIR, exist_ok=True)
    size = subset(SRC, OUT)

    if size > LIMIT_BYTES:
        print("variable subset is %d bytes (limit %d); making static 500/700"
              % (size, LIMIT_BYTES))
        os.remove(OUT)
        for path, made_size in static_weights():
            print("  %s: %d bytes" % (os.path.relpath(path, ROOT), made_size))
        print("use font-weight 500 and 700 @font-face rules in app.css")
    else:
        print("wrote %s: %d bytes" % (os.path.relpath(OUT, ROOT), size))

    shutil.copyfile(LICENCE, os.path.join(OUT_DIR, "OFL.txt"))
    print("copied %s" % os.path.relpath(os.path.join(OUT_DIR, "OFL.txt"), ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
