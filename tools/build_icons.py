#!/usr/bin/env python3
"""Build the inline icon sprite in web/index.html from vendor/lucide.

Standard library only. Run from the repository root:

    python3 tools/build_icons.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "vendor", "lucide")
INDEX = os.path.join(ROOT, "web", "index.html")

ICONS = [
    "heart-pulse",
    "ruler",
    "activity",
    "arrow-up-from-line",
    "hand",
    "repeat",
    "timer",
    "wind",
    "trophy",
    "settings",
    "wifi",
    "usb",
    "monitor-play",
    "play",
    "square",
    "music",
    "info",
    "circle-help",
    "x",
    "circle-check",
    "check",
    "triangle-alert",
    "zap",
    "download",
    "rotate-ccw",
    "shield-check",
    "sun",
    "moon",
    "baby",
    "user",
    "sliders-horizontal",
    "scale",
    "target",
    "chevron-right",
    "chevron-left",
    "chevron-down",
    "clock",
    "bar-chart-3",
]

BEGIN = "<!-- icons:begin -->"
END = "<!-- icons:end -->"
ATTRIBUTE_RE = re.compile(r'([a-zA-Z_:][-a-zA-Z0-9_:.]*)\s*=\s*"([^"]*)"')


def read_svg(name):
    path = os.path.join(ICON_DIR, name + ".svg")
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as exc:
        raise SystemExit("cannot read %s: %s" % (path, exc))

    text = re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()
    match = re.search(r"<svg\b([^>]*)>(.*)</svg>", text, flags=re.S)
    if not match:
        raise SystemExit("no <svg> in %s" % path)

    attrs = dict(ATTRIBUTE_RE.findall(match.group(1)))
    body = match.group(2).strip()
    view_box = attrs.get("viewBox", "0 0 24 24")

    keep = []
    for key in ("fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin"):
        if key in attrs:
            keep.append('%s="%s"' % (key, attrs[key]))

    lines = ['    <symbol id="i-%s" viewBox="%s"%s>' % (
        name, view_box, (" " + " ".join(keep)) if keep else "")]
    for line in body.splitlines():
        line = line.strip()
        if line:
            lines.append("      " + line)
    lines.append("    </symbol>")
    return "\n".join(lines)


def build_sprite():
    symbols = [read_svg(name) for name in ICONS]
    return "\n".join([
        '  <svg id="icon-sprite" hidden aria-hidden="true" focusable="false">',
        "    " + BEGIN,
        "\n".join(symbols),
        "    " + END,
        "  </svg>",
    ])


def main():
    with open(INDEX, "r", encoding="utf-8") as handle:
        html = handle.read()

    begin = html.find(BEGIN)
    end = html.find(END)
    if begin < 0 or end < 0 or end < begin:
        raise SystemExit("icon markers not found in %s" % INDEX)
    end += len(END)

    start = html.find('<svg id="icon-sprite"', 0, begin)
    if start < 0:
        start = begin
    start = html.rfind("\n", 0, start) + 1

    cursor = end
    while True:
        match = re.match(r"\s*</svg>", html[cursor:])
        if not match:
            break
        cursor += match.end()

    updated = html[:start] + build_sprite() + html[cursor:]
    if updated == html:
        print("build_icons: no changes")
        return 0
    with open(INDEX, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(updated)
    print("build_icons: wrote %d icons to %s" % (len(ICONS), os.path.relpath(INDEX, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
