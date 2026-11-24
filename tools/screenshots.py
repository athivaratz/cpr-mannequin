#!/usr/bin/env python3
"""Capture dashboard screenshots with Playwright.

Run with the tools virtualenv:

    /home/box/.tools-venv/bin/python tools/screenshots.py

Serves web/ with ``python3 -m http.server`` on a free port, then captures the
desktop (1280x800), phone (390x844) and landscape (844x390) views.
"""

import os
import socket
import subprocess
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")
OUT_DIR = os.path.join(ROOT, "docs", "screenshots")


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


def settle(page, wait_ms):
    page.evaluate(
        "() => document.fonts.ready.then(() => new Promise((resolve) => {"
        "requestAnimationFrame(() => requestAnimationFrame(resolve)); }))"
    )
    page.wait_for_timeout(wait_ms)


def capture(context, base, name, query, wait_selector=None, wait_ms=650,
            scroll_selector=None):
    page = context.new_page()
    errors = []
    page.on("console", lambda message: errors.append(
        "console %s: %s" % (message.type, message.text))
        if message.type == "error" else None)
    page.on("pageerror", lambda error: errors.append("pageerror: %s" % error))

    page.goto(base + "index.html" + query, wait_until="load")
    if wait_selector:
        page.wait_for_selector(wait_selector, state="visible", timeout=8000)
    settle(page, wait_ms)
    if scroll_selector:
        page.eval_on_selector(
            scroll_selector,
            "el => el.scrollIntoView({ block: 'center', inline: 'nearest' })",
        )
        page.evaluate("window.scrollBy(0, -18)")
        page.wait_for_timeout(150)

    path = os.path.join(OUT_DIR, name)
    page.screenshot(path=path)
    page.close()
    if errors:
        raise RuntimeError("%s console errors:\n%s" % (name, "\n".join(errors)))
    print(path)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    process = None
    try:
        process, base = start_server()
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=True)
            try:
                desktop = browser.new_context(
                    viewport={"width": 1280, "height": 800},
                    device_scale_factor=1,
                )
                phone = browser.new_context(
                    viewport={"width": 390, "height": 844},
                    device_scale_factor=2,
                )
                landscape = browser.new_context(
                    viewport={"width": 844, "height": 390},
                    device_scale_factor=1,
                )
                for label, context in (("1280", desktop), ("390", phone)):
                    scroll = "#countTile" if label == "390" else None
                    capture(context, base, "01-practice-%s.png" % label,
                            "?demo=good&seek=14&motion=freeze", wait_ms=700)
                    capture(context, base, "02-cycle-%s.png" % label,
                            "?demo=cycle&seek=31.8&motion=freeze", wait_ms=450,
                            scroll_selector=scroll)
                    capture(context, base, "02b-breaths-%s.png" % label,
                            "?demo=cycle&seek=38.2&motion=freeze", wait_ms=650,
                            scroll_selector=scroll)
                    capture(context, base, "03-summary-%s.png" % label,
                            "?demo=test2min&seek=200&sheet=summary&motion=freeze",
                            wait_selector="#sheetSummary:not([hidden])",
                            wait_ms=800)
                    capture(context, base, "04-calib-%s.png" % label,
                            "?demo=good&seek=5&sheet=calib&motion=freeze",
                            wait_selector="#sheetCalib:not([hidden])",
                            wait_ms=650)
                    capture(context, base, "05-shallow-%s.png" % label,
                            "?demo=shallow&seek=5.0&motion=freeze", wait_ms=300)
                    capture(context, base, "07-connect-%s.png" % label,
                            "?sheet=connect&motion=freeze",
                            wait_selector="#sheetConnect:not([hidden])",
                            wait_ms=450)
                    capture(context, base, "08-settings-%s.png" % label,
                            "?demo=good&seek=5&sheet=settings&motion=freeze",
                            wait_selector="#sheetSettings:not([hidden])",
                            wait_ms=650)

                capture(phone, base, "09-info-390.png",
                        "?demo=good&seek=14&sheet=info&info=score&motion=freeze",
                        wait_selector="#sheetInfo:not([hidden])",
                        wait_ms=650)

                capture(landscape, base, "10-landscape-844.png",
                        "?demo=good&seek=14&motion=freeze", wait_ms=700)

                capture(desktop, base, "06-light-1280.png",
                        "?demo=good&seek=14&theme=light&motion=freeze", wait_ms=700)
            finally:
                browser.close()
    finally:
        stop_server(process)
    return 0


if __name__ == "__main__":
    sys.exit(main())
