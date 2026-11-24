#!/usr/bin/env python3
"""Serve web/ and replay a recording over WebSocket for dashboard testing.

Standard library only. The RFC 6455 handshake and frame format are written out
by hand so the dev server has no dependencies.

    python3 tools/devserver.py --port 8080 \
        --recording recordings/good.jsonl --loop
"""

import argparse
import base64
import hashlib
import json
import mimetypes
import os
import struct
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")
REC_DIR = os.path.join(ROOT, "recordings")
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def load_recording(path):
    messages = []
    last_t = 0.0
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line[0] != "{":
                continue
            try:
                obj = json.loads(line)
            except ValueError:
                continue
            if not isinstance(obj, dict):
                continue
            if isinstance(obj.get("t"), (int, float)):
                at = float(obj["t"])
                last_t = at
            elif obj.get("ty") == "sum":
                at = last_t + 1.0
            else:
                at = last_t
            messages.append((at, obj))
    messages.sort(key=lambda item: item[0])
    return messages


def canonical_command(line):
    words = line.strip().lower().split()
    if not words:
        return "unknown"
    if words[0] == "cal":
        return "cal " + words[1] if len(words) > 1 else "cal"
    if words[0] == "wifi":
        return "wifi pass"
    return words[0]


def recv_exact(conn, size):
    chunks = []
    remaining = size
    while remaining > 0:
        try:
            chunk = conn.recv(remaining)
        except OSError:
            return None
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def recv_frame(conn):
    header = recv_exact(conn, 2)
    if header is None:
        return None
    first, second = header[0], header[1]
    opcode = first & 0x0F
    masked = (second & 0x80) != 0
    length = second & 0x7F
    if length == 126:
        extra = recv_exact(conn, 2)
        if extra is None:
            return None
        length = struct.unpack("!H", extra)[0]
    elif length == 127:
        extra = recv_exact(conn, 8)
        if extra is None:
            return None
        length = struct.unpack("!Q", extra)[0]

    mask = None
    if masked:
        mask = recv_exact(conn, 4)
        if mask is None:
            return None
    payload = b""
    if length:
        payload = recv_exact(conn, length)
        if payload is None:
            return None
        if mask is not None:
            payload = bytes(payload[i] ^ mask[i % 4] for i in range(length))
    return (first & 0x80) != 0, opcode, payload


def make_frame(opcode, payload):
    length = len(payload)
    header = bytearray()
    header.append(0x80 | (opcode & 0x0F))
    if length < 126:
        header.append(length)
    elif length <= 0xFFFF:
        header.append(126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", length))
    return bytes(header) + payload


class WsClient:
    def __init__(self, conn):
        self.conn = conn
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.closed = False

    def send_frame(self, opcode, payload):
        with self.lock:
            if self.closed:
                return False
            try:
                self.conn.sendall(make_frame(opcode, payload))
            except OSError:
                self.closed = True
                self.stop_event.set()
                return False
            return True

    def send_text(self, text):
        return self.send_frame(0x1, text.encode("utf-8"))

    def send_pong(self, payload):
        return self.send_frame(0xA, payload)

    def send_close(self, payload=b""):
        self.send_frame(0x8, payload)
        self.closed = True
        self.stop_event.set()

    def stop(self):
        self.stop_event.set()


def replay(client, messages, loop, speed):
    if not messages:
        return
    base_at = messages[0][0]
    while not client.stop_event.is_set():
        started = time.monotonic()
        for at, obj in messages:
            if client.stop_event.is_set():
                return
            target = (at - base_at) / 1000.0 / speed
            delay = target - (time.monotonic() - started)
            if delay > 0:
                if client.stop_event.wait(delay):
                    return
            text = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
            if not client.send_text(text):
                return
        if not loop:
            return
        if client.stop_event.wait(0.25):
            return


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "CPRDevServer/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def do_HEAD(self):
        self.handle_get(head_only=True)

    def do_GET(self):
        self.handle_get(head_only=False)

    def handle_get(self, head_only):
        path = urllib.parse.urlparse(self.path).path
        if path == "/ws":
            self.handle_websocket()
            return
        full = self.resolve_path(path)
        if full is None or not os.path.isfile(full):
            self.send_error(404, "not found")
            return
        try:
            with open(full, "rb") as handle:
                data = handle.read()
        except OSError:
            self.send_error(500, "read failed")
            return
        mime = self.mime_for(full)
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        if os.path.basename(full) == "index.html":
            self.send_header("Cache-Control", "no-cache")
        else:
            self.send_header("Cache-Control", "max-age=86400")
        self.end_headers()
        if not head_only:
            try:
                self.wfile.write(data)
            except OSError:
                pass

    @staticmethod
    def resolve_path(path):
        if path in ("", "/"):
            rel = "index.html"
            base = WEB_DIR
        elif path.startswith("/recordings/"):
            rel = path[len("/recordings/"):]
            base = REC_DIR
        else:
            rel = path.lstrip("/")
            base = WEB_DIR
        full = os.path.realpath(os.path.join(base, rel))
        base_real = os.path.realpath(base)
        if full != base_real and not full.startswith(base_real + os.sep):
            return None
        if os.path.isdir(full):
            full = os.path.join(full, "index.html")
        return full

    @staticmethod
    def mime_for(path):
        ext = os.path.splitext(path)[1].lower()
        overrides = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".jsonl": "application/x-ndjson; charset=utf-8",
            ".woff2": "font/woff2",
            ".svg": "image/svg+xml",
            ".txt": "text/plain; charset=utf-8",
        }
        if ext in overrides:
            return overrides[ext]
        guessed = mimetypes.guess_type(path)[0]
        return guessed or "application/octet-stream"

    def handle_websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        upgrade = (self.headers.get("Upgrade") or "").lower()
        if not key or upgrade != "websocket":
            self.send_error(400, "websocket upgrade required")
            return

        accept = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
        ).decode("ascii")
        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n"
            "\r\n" % accept
        )
        try:
            self.wfile.write(response.encode("ascii"))
            self.wfile.flush()
        except OSError:
            return

        self.close_connection = True
        try:
            self.connection.settimeout(None)
        except OSError:
            pass

        client = WsClient(self.connection)
        replay_thread = threading.Thread(
            target=replay,
            args=(client, self.server.messages, self.server.loop,
                  self.server.speed),
            daemon=True,
        )
        replay_thread.start()

        self.log_message("websocket connected")
        try:
            while not client.stop_event.is_set():
                frame = recv_frame(self.connection)
                if frame is None:
                    break
                fin, opcode, payload = frame
                if opcode == 0x8:
                    client.send_close(payload)
                    break
                if opcode == 0x9:
                    client.send_pong(payload)
                    continue
                if opcode == 0xA:
                    continue
                if opcode in (0x1, 0x2):
                    data = payload
                    while not fin:
                        more = recv_frame(self.connection)
                        if more is None:
                            break
                        fin, more_opcode, more_payload = more
                        if more_opcode == 0x8:
                            client.send_close(more_payload)
                            return
                        if more_opcode == 0x9:
                            client.send_pong(more_payload)
                            continue
                        if more_opcode in (0x0, 0x1, 0x2):
                            data += more_payload
                    self.handle_client_text(client, data)
        finally:
            client.stop()
            self.log_message("websocket disconnected")

    def handle_client_text(self, client, data):
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("utf-8", "replace")
        for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
            line = line.strip()
            if not line:
                continue
            print("command: %s" % line, flush=True)
            ack = {
                "ty": "ack",
                "cmd": canonical_command(line),
                "ok": 1,
                "msg": "ok",
            }
            client.send_text(json.dumps(ack, ensure_ascii=False,
                                        separators=(",", ":")))


class DevServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, messages, loop, speed):
        self.messages = messages
        self.loop = loop
        self.speed = speed
        super().__init__(address, Handler)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Serve web/ and replay a recording over WebSocket.")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--recording", default="recordings/good.jsonl")
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--speed", type=float, default=1.0)
    args = parser.parse_args(argv)

    if args.speed <= 0:
        parser.error("--speed must be greater than zero")
    path = args.recording
    if not os.path.isabs(path):
        path = os.path.join(ROOT, path)
    if not os.path.isfile(path):
        parser.error("recording not found: %s" % args.recording)
    messages = load_recording(path)
    if not messages:
        parser.error("recording is empty: %s" % args.recording)

    server = DevServer((args.host, args.port), messages, args.loop, args.speed)
    host, port = server.server_address[:2]
    print("serving %s at http://%s:%d/ (ws at /ws, %d messages)"
          % (os.path.relpath(path, ROOT), host, port, len(messages)),
          flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
