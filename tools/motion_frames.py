#!/usr/bin/env python3
"""Capture motion strips and a short summary video with Playwright.

Run with the tools virtualenv:

    /home/box/.tools-venv/bin/python tools/motion_frames.py [outdir]

Serves web/ on a free port. Output defaults to /workspace/cpr-preview/u3.
The frames are taken without ?motion=freeze so the animations are visible.
"""

import io
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

from PIL import Image, ImageDraw, ImageFont
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")
DEFAULT_OUT = "/workspace/cpr-preview/u3"
VIEWPORT = {"width": 390, "height": 844}


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_server(url, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                if response.status == 200:
                    return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("dev server did not start")


def start_server():
    port = free_port()
    command = [
        sys.executable, "-m", "http.server", str(port),
        "--bind", "127.0.0.1", "--directory", WEB_DIR,
    ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    base = "http://127.0.0.1:%d/" % port
    wait_for_server(base)
    return process, base


def stop_server(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def capture_frames(page, targets):
    frames = []
    actual = []
    start = time.perf_counter()
    for target in targets:
        while True:
            elapsed = (time.perf_counter() - start) * 1000.0
            remaining = target - elapsed
            if remaining <= 0:
                break
            time.sleep(min(remaining / 1000.0, 0.02))
        before = time.perf_counter()
        data = page.screenshot()
        after = time.perf_counter()
        frames.append(data)
        actual.append((((before + after) / 2.0) - start) * 1000.0)
    return frames, actual


def load_font(size):
    paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ]
    for path in paths:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def compose_strip(frames, targets, path):
    images = [Image.open(io.BytesIO(data)).convert("RGB") for data in frames]
    scale = 0.5
    width = max(1, int(round(images[0].width * scale)))
    height = max(1, int(round(images[0].height * scale)))
    gap = 8
    pad = 16
    label_h = 30
    canvas_w = pad * 2 + len(images) * width + (len(images) - 1) * gap
    canvas_h = pad * 2 + height + label_h
    canvas = Image.new("RGB", (canvas_w, canvas_h), (14, 15, 19))
    draw = ImageDraw.Draw(canvas)
    font = load_font(14)
    for index, image in enumerate(images):
        thumb = image.resize((width, height), Image.LANCZOS)
        x = pad + index * (width + gap)
        y = pad
        canvas.paste(thumb, (x, y))
        label = "%d ms" % targets[index]
        box = draw.textbbox((0, 0), label, font=font)
        text_w = box[2] - box[0]
        draw.text(
            (x + (width - text_w) / 2, y + height + 8),
            label,
            fill=(200, 204, 214),
            font=font,
        )
    canvas.save(path)


def close_open_sheet(page):
    if page.locator("#sheetLayer:not([hidden])").count():
        page.keyboard.press("Escape")
        page.wait_for_timeout(450)


def capture_first_load(context, base, outdir):
    page = context.new_page()
    page.goto(base + "index.html?demo=good&seek=14", wait_until="domcontentloaded")
    targets = [0, 40, 80, 140, 200, 280, 380, 600]
    frames, actual = capture_frames(page, targets)
    page.close()
    path = os.path.join(outdir, "strip-first-load.png")
    compose_strip(frames, targets, path)
    print(path)
    print("first-load actual ms:", " ".join("%.1f" % value for value in actual))
    return path


def capture_summary(context, base, outdir):
    page = context.new_page()
    page.goto(base + "index.html?demo=test2min&seek=200", wait_until="load")
    page.wait_for_timeout(1500)
    close_open_sheet(page)
    targets = [0, 60, 150, 260, 400, 560, 750, 1000]
    page.click('[data-nav="summary"]')
    frames, actual = capture_frames(page, targets)
    page.close()
    path = os.path.join(outdir, "strip-summary.png")
    compose_strip(frames, targets, path)
    print(path)
    print("summary actual ms:", " ".join("%.1f" % value for value in actual))
    return path


def capture_sheet(context, base, outdir):
    page = context.new_page()
    page.goto(base + "index.html?demo=good&seek=14", wait_until="load")
    page.wait_for_timeout(1500)
    targets = [0, 40, 90, 150, 220, 300, 380, 500]
    page.click('[data-nav="settings"]')
    frames, actual = capture_frames(page, targets)
    page.close()
    path = os.path.join(outdir, "strip-sheet.png")
    compose_strip(frames, targets, path)
    print(path)
    print("sheet actual ms:", " ".join("%.1f" % value for value in actual))
    return path


def capture_video(browser, base, outdir):
    video_dir = tempfile.mkdtemp(prefix="cpr-u3-video-")
    context = browser.new_context(
        viewport=VIEWPORT,
        device_scale_factor=1,
        record_video_dir=video_dir,
        record_video_size=VIEWPORT,
    )
    try:
        page = context.new_page()
        page.goto(base + "index.html?demo=test2min&seek=200", wait_until="load")
        page.wait_for_timeout(400)
        close_open_sheet(page)
        page.click('[data-nav="summary"]')
        page.wait_for_timeout(900)
        video = page.video
        page.close()
        context.close()
        source = video.path()
        target = os.path.join(outdir, "summary-open.webm")
        shutil.move(source, target)
        print(target)
        return target
    finally:
        shutil.rmtree(video_dir, ignore_errors=True)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    os.makedirs(outdir, exist_ok=True)
    process = None
    try:
        process, base = start_server()
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=True)
            try:
                context = browser.new_context(
                    viewport=VIEWPORT,
                    device_scale_factor=1,
                )
                try:
                    capture_first_load(context, base, outdir)
                    capture_summary(context, base, outdir)
                    capture_sheet(context, base, outdir)
                finally:
                    context.close()
                capture_video(browser, base, outdir)
            finally:
                browser.close()
    finally:
        stop_server(process)
    return 0


if __name__ == "__main__":
    sys.exit(main())
