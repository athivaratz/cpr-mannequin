#!/usr/bin/env python3
"""Serve web/ on localhost and bridge USB Serial <-> WebSocket for PC users.

Standard library only (no pyserial or third-party packages). Supports Windows
via Win32 ctypes and macOS/Linux via termios.

    python3 tools/usb_server.py
    python3 tools/usb_server.py --port COM3
    python3 tools/usb_server.py --webserial
"""

import argparse
import base64
import glob
import hashlib
import json
import mimetypes
import os
import select
import socket
import struct
import sys
import threading
import time
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

if os.name == "nt":
    import ctypes
    from ctypes import wintypes
    import winreg
else:
    import termios

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")
REC_DIR = os.path.join(ROOT, "recordings")
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# Engineering values, tunable: heartbeat period to keep ESP32 in USB mode.
USB_HEARTBEAT_S = 3.0
USB_RECONNECT_S = 1.0


def list_serial_ports():
    """Return a list of (port_name, label) tuples, USB adapters first."""
    if os.name == "nt":
        return _list_windows_ports()
    return _list_posix_ports()


def _list_windows_ports():
    ports = []
    try:
        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"HARDWARE\DEVICEMAP\SERIALCOMM",
        )
    except OSError:
        return ports

    try:
        index = 0
        while True:
            try:
                device, com_name, _ = winreg.EnumValue(key, index)
            except OSError:
                break
            index += 1
            com_str = str(com_name).strip()
            dev_str = str(device).strip()
            if com_str:
                ports.append((com_str, dev_str))
    finally:
        winreg.CloseKey(key)

    def sort_key(item):
        com_name, dev_name = item
        low = dev_name.lower()
        usb_hint = any(
            token in low
            for token in ("silab", "vcp", "usbser", "ch34", "ftdi", "prolific")
        )
        num = 9999
        if com_name.upper().startswith("COM"):
            try:
                num = int(com_name[3:])
            except ValueError:
                pass
        return (0 if usb_hint else 1, num, com_name)

    ports.sort(key=sort_key)
    return ports


def _list_posix_ports():
    patterns = (
        "/dev/cu.SLAB*",
        "/dev/cu.wch*",
        "/dev/cu.usbserial*",
        "/dev/cu.usbmodem*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
    )
    found = []
    seen = set()
    for pattern in patterns:
        for path in sorted(glob.glob(pattern)):
            if path not in seen:
                seen.add(path)
                found.append((path, os.path.basename(path)))
    return found


if os.name == "nt":
    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
    PURGE_TXCLEAR = 0x0004
    PURGE_RXCLEAR = 0x0008

    class _DCB(ctypes.Structure):
        _fields_ = [
            ("DCBlength", wintypes.DWORD),
            ("BaudRate", wintypes.DWORD),
            ("fBinary", wintypes.DWORD, 1),
            ("fParity", wintypes.DWORD, 1),
            ("fOutxCtsFlow", wintypes.DWORD, 1),
            ("fOutxDsrFlow", wintypes.DWORD, 1),
            ("fDtrControl", wintypes.DWORD, 2),
            ("fDsrSensitivity", wintypes.DWORD, 1),
            ("fTXContinueOnXoff", wintypes.DWORD, 1),
            ("fOutX", wintypes.DWORD, 1),
            ("fInX", wintypes.DWORD, 1),
            ("fErrorChar", wintypes.DWORD, 1),
            ("fNull", wintypes.DWORD, 1),
            ("fRtsControl", wintypes.DWORD, 2),
            ("fAbortOnError", wintypes.DWORD, 1),
            ("fDummy2", wintypes.DWORD, 17),
            ("wReserved", wintypes.WORD),
            ("XonLim", wintypes.WORD),
            ("XoffLim", wintypes.WORD),
            ("ByteSize", wintypes.BYTE),
            ("Parity", wintypes.BYTE),
            ("StopBits", wintypes.BYTE),
            ("XonChar", ctypes.c_char),
            ("XoffChar", ctypes.c_char),
            ("ErrorChar", ctypes.c_char),
            ("EofChar", ctypes.c_char),
            ("EvtChar", ctypes.c_char),
            ("wReserved1", wintypes.WORD),
        ]

    class _COMMTIMEOUTS(ctypes.Structure):
        _fields_ = [
            ("ReadIntervalTimeout", wintypes.DWORD),
            ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
            ("ReadTotalTimeoutConstant", wintypes.DWORD),
            ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
            ("WriteTotalTimeoutConstant", wintypes.DWORD),
        ]

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    ]
    _kernel32.CreateFileW.restype = wintypes.HANDLE
    _kernel32.GetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(_DCB)]
    _kernel32.GetCommState.restype = wintypes.BOOL
    _kernel32.SetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(_DCB)]
    _kernel32.SetCommState.restype = wintypes.BOOL
    _kernel32.SetCommTimeouts.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(_COMMTIMEOUTS),
    ]
    _kernel32.SetCommTimeouts.restype = wintypes.BOOL
    _kernel32.PurgeComm.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    _kernel32.PurgeComm.restype = wintypes.BOOL
    _kernel32.ReadFile.argtypes = [
        wintypes.HANDLE,
        wintypes.LPVOID,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID,
    ]
    _kernel32.ReadFile.restype = wintypes.BOOL
    _kernel32.WriteFile.argtypes = [
        wintypes.HANDLE,
        wintypes.LPCVOID,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID,
    ]
    _kernel32.WriteFile.restype = wintypes.BOOL
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    _kernel32.CloseHandle.restype = wintypes.BOOL


class SerialPort:
    """Minimal blocking/timeout serial wrapper using stdlib only."""

    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.handle = None
        self.fd = None
        self._open()

    def _open(self):
        if os.name == "nt":
            path = self.port if self.port.startswith("\\\\.\\") else "\\\\.\\" + self.port
            handle = _kernel32.CreateFileW(
                path,
                GENERIC_READ | GENERIC_WRITE,
                0,
                None,
                OPEN_EXISTING,
                0,
                None,
            )
            if handle == INVALID_HANDLE_VALUE:
                err = ctypes.get_last_error()
                raise OSError(err, "CreateFileW failed for %s" % self.port)

            try:
                dcb = _DCB()
                dcb.DCBlength = ctypes.sizeof(_DCB)
                if not _kernel32.GetCommState(handle, ctypes.byref(dcb)):
                    raise OSError(ctypes.get_last_error(), "GetCommState failed")
                dcb.BaudRate = self.baudrate
                dcb.fBinary = 1
                dcb.fParity = 0
                dcb.fOutxCtsFlow = 0
                dcb.fOutxDsrFlow = 0
                dcb.fDtrControl = 0
                dcb.fRtsControl = 0
                dcb.fOutX = 0
                dcb.fInX = 0
                dcb.ByteSize = 8
                dcb.Parity = 0
                dcb.StopBits = 0
                if not _kernel32.SetCommState(handle, ctypes.byref(dcb)):
                    raise OSError(ctypes.get_last_error(), "SetCommState failed")

                timeouts = _COMMTIMEOUTS()
                timeouts.ReadIntervalTimeout = 20
                timeouts.ReadTotalTimeoutMultiplier = 0
                timeouts.ReadTotalTimeoutConstant = 50
                timeouts.WriteTotalTimeoutMultiplier = 0
                timeouts.WriteTotalTimeoutConstant = 500
                if not _kernel32.SetCommTimeouts(handle, ctypes.byref(timeouts)):
                    raise OSError(ctypes.get_last_error(), "SetCommTimeouts failed")

                _kernel32.PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR)
            except Exception:
                _kernel32.CloseHandle(handle)
                raise

            self.handle = handle
        else:
            fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                attrs = termios.tcgetattr(fd)
                baud_const = getattr(termios, "B%d" % self.baudrate, termios.B115200)
                attrs[0] = 0
                attrs[1] = 0
                attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
                attrs[3] = 0
                attrs[4] = baud_const
                attrs[5] = baud_const
                attrs[6][termios.VMIN] = 0
                attrs[6][termios.VTIME] = 0
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
                termios.tcflush(fd, termios.TCIOFLUSH)
            except Exception:
                os.close(fd)
                raise
            self.fd = fd

    def read(self, size=512):
        if os.name == "nt":
            if self.handle is None:
                raise OSError("serial port closed")
            buf = ctypes.create_string_buffer(size)
            read_count = wintypes.DWORD(0)
            ok = _kernel32.ReadFile(
                self.handle,
                buf,
                size,
                ctypes.byref(read_count),
                None,
            )
            if not ok:
                raise OSError(ctypes.get_last_error(), "ReadFile failed")
            return buf.raw[: read_count.value]
        if self.fd is None:
            raise OSError("serial port closed")
        ready, _, _ = select.select([self.fd], [], [], 0.05)
        if not ready:
            return b""
        data = os.read(self.fd, size)
        if not data:
            raise OSError("serial EOF")
        return data

    def write(self, data):
        if not data:
            return
        if os.name == "nt":
            if self.handle is None:
                raise OSError("serial port closed")
            view = memoryview(data)
            written_total = 0
            while written_total < len(data):
                chunk = bytes(view[written_total:])
                written = wintypes.DWORD(0)
                ok = _kernel32.WriteFile(
                    self.handle,
                    chunk,
                    len(chunk),
                    ctypes.byref(written),
                    None,
                )
                if not ok:
                    raise OSError(ctypes.get_last_error(), "WriteFile failed")
                if written.value == 0:
                    raise OSError("WriteFile wrote 0 bytes")
                written_total += written.value
        else:
            if self.fd is None:
                raise OSError("serial port closed")
            view = memoryview(data)
            written_total = 0
            while written_total < len(data):
                count = os.write(self.fd, view[written_total:])
                if count <= 0:
                    raise OSError("serial write failed")
                written_total += count

    def close(self):
        if os.name == "nt":
            if self.handle is not None:
                try:
                    _kernel32.CloseHandle(self.handle)
                except OSError:
                    pass
                self.handle = None
        else:
            if self.fd is not None:
                try:
                    os.close(self.fd)
                except OSError:
                    pass
                self.fd = None


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


class SerialBridge:
    """Bridges one USB serial port to all connected WebSocket clients."""

    def __init__(self, port_name=None, baudrate=115200):
        self.requested_port = port_name
        self.baudrate = baudrate
        self.serial = None
        self.active_port = None
        self.tx_lock = threading.Lock()
        self.clients_lock = threading.Lock()
        self.clients = set()
        self.last_cfg = None
        self.stop_event = threading.Event()
        self._reader_thread = None
        self._hb_thread = None
        self._had_clients = False

    def start(self):
        self._reader_thread = threading.Thread(
            target=self._serial_loop, daemon=True
        )
        self._hb_thread = threading.Thread(
            target=self._heartbeat_loop, daemon=True
        )
        self._reader_thread.start()
        self._hb_thread.start()

    def stop(self):
        self.stop_event.set()
        self.send_line("usb idle")
        with self.clients_lock:
            clients = list(self.clients)
            self.clients.clear()
        for client in clients:
            client.stop()
        with self.tx_lock:
            if self.serial is not None:
                self.serial.close()
                self.serial = None

    def add_client(self, client):
        became_active = False
        cached_cfg = None
        with self.clients_lock:
            was_empty = len(self.clients) == 0
            self.clients.add(client)
            became_active = was_empty
            cached_cfg = self.last_cfg

        if cached_cfg:
            client.send_text(cached_cfg)
        if became_active:
            self._had_clients = True
            self.send_line("usb active")
            self.send_line("config")

    def remove_client(self, client):
        became_empty = False
        with self.clients_lock:
            self.clients.discard(client)
            if len(self.clients) == 0 and self._had_clients:
                self._had_clients = False
                became_empty = True
        if became_empty:
            self.send_line("usb idle")

    def has_clients(self):
        with self.clients_lock:
            return len(self.clients) > 0

    def broadcast(self, line):
        with self.clients_lock:
            clients = list(self.clients)
        dead = []
        for client in clients:
            if not client.send_text(line):
                dead.append(client)
        if dead:
            for client in dead:
                self.remove_client(client)

    def send_line(self, line):
        clean = line.strip()
        if not clean:
            return False
        payload = (clean + "\n").encode("utf-8", errors="replace")
        with self.tx_lock:
            if self.serial is None:
                return False
            try:
                self.serial.write(payload)
                return True
            except OSError:
                return False

    def _pick_port(self):
        if self.requested_port:
            return self.requested_port
        ports = list_serial_ports()
        if not ports:
            return None
        return ports[0][0]

    def _serial_loop(self):
        warned_missing = False
        while not self.stop_event.is_set():
            port_name = self._pick_port()
            if not port_name:
                if not warned_missing:
                    print("usb: waiting for serial port...", file=sys.stderr, flush=True)
                    warned_missing = True
                self.stop_event.wait(USB_RECONNECT_S)
                continue

            try:
                sp = SerialPort(port_name, self.baudrate)
            except OSError as err:
                if not warned_missing:
                    print(
                        "usb: cannot open %s (%s), retrying..." % (port_name, err),
                        file=sys.stderr,
                        flush=True,
                    )
                    warned_missing = True
                self.stop_event.wait(USB_RECONNECT_S)
                continue

            warned_missing = False
            with self.tx_lock:
                self.serial = sp
                self.active_port = port_name
            print(
                "usb: connected to %s at %d baud" % (port_name, self.baudrate),
                file=sys.stderr,
                flush=True,
            )

            got_cfg = False
            opened_at = time.monotonic()
            retried_cfg = False
            if self.has_clients():
                self.send_line("usb active")
            self.send_line("config")

            buf = ""
            try:
                while not self.stop_event.is_set():
                    if not got_cfg and not retried_cfg and (time.monotonic() - opened_at) >= 2.0:
                        retried_cfg = True
                        if self.has_clients():
                            self.send_line("usb active")
                        self.send_line("config")

                    chunk = sp.read(512)
                    if not chunk:
                        continue
                    buf += chunk.decode("utf-8", errors="replace")
                    while "\n" in buf:
                        raw_line, buf = buf.split("\n", 1)
                        line = raw_line.rstrip("\r")
                        if not line:
                            continue
                        if line.startswith("{"):
                            try:
                                obj = json.loads(line)
                            except ValueError:
                                continue
                            if isinstance(obj, dict) and obj.get("ty") == "cfg":
                                got_cfg = True
                                with self.clients_lock:
                                    self.last_cfg = line
                            self.broadcast(line)
                        elif line.startswith("#"):
                            print("device: %s" % line, file=sys.stderr, flush=True)
                    if len(buf) > 8192:
                        buf = ""
            except OSError as err:
                if not self.stop_event.is_set():
                    print(
                        "usb: disconnected from %s (%s)" % (port_name, err),
                        file=sys.stderr,
                        flush=True,
                    )
            finally:
                with self.tx_lock:
                    if self.serial is sp:
                        self.serial = None
                    self.active_port = None
                sp.close()

            if not self.stop_event.is_set():
                self.stop_event.wait(USB_RECONNECT_S)

    def _heartbeat_loop(self):
        last_sent = 0.0
        while not self.stop_event.is_set():
            if self.has_clients():
                now = time.monotonic()
                if (now - last_sent) >= USB_HEARTBEAT_S:
                    if self.send_line("usb active"):
                        last_sent = now
            else:
                last_sent = 0.0
            self.stop_event.wait(0.5)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "CPRUsbServer/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def do_HEAD(self):
        self.handle_get(head_only=True)

    def do_GET(self):
        self.handle_get(head_only=False)

    def handle_get(self, head_only):
        path = urllib.parse.urlparse(self.path).path
        if path == "/ws":
            if self.server.bridge is None:
                self.send_error(404, "websocket bridge disabled in webserial mode")
                return
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
        if os.path.splitext(full)[1].lower() in (".html", ".js", ".css"):
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
            rel = path[len("/recordings/") :]
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

        bridge = self.server.bridge
        client = WsClient(self.connection)
        bridge.add_client(client)
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
                    try:
                        text = data.decode("utf-8")
                    except UnicodeDecodeError:
                        text = data.decode("utf-8", "replace")
                    for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
                        line = line.strip()
                        if line:
                            bridge.send_line(line)
        finally:
            bridge.remove_client(client)
            client.stop()
            self.log_message("websocket disconnected")


class UsbHttpServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, bridge):
        self.bridge = bridge
        super().__init__(address, Handler)


def bind_http_server(host, preferred_port, bridge):
    last_err = None
    for port in range(preferred_port, preferred_port + 20):
        try:
            return UsbHttpServer((host, port), bridge)
        except OSError as err:
            last_err = err
    raise last_err


def choose_port_interactive(ports):
    if len(ports) == 1:
        return ports[0][0]
    if not ports or not sys.stdin.isatty():
        return ports[0][0] if ports else None

    print("พบพอร์ต USB หลายช่อง กรุณาเลือก:")
    for idx, (name, label) in enumerate(ports, start=1):
        print("  %d) %s (%s)" % (idx, name, label))
    try:
        choice = input("เลือกหมายเลข [1]: ").strip()
    except EOFError:
        return ports[0][0]
    if not choice:
        return ports[0][0]
    try:
        num = int(choice)
        if 1 <= num <= len(ports):
            return ports[num - 1][0]
    except ValueError:
        pass
    return ports[0][0]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Serve CPR dashboard locally and bridge USB Serial <-> WebSocket."
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g. COM3 or /dev/ttyUSB0). Auto-detected if omitted.",
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    parser.add_argument(
        "--http-port",
        type=int,
        default=8080,
        help="Local HTTP port (default 8080).",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Bind host.")
    parser.add_argument(
        "--webserial",
        action="store_true",
        help="Serve web/ only and let Chrome/Edge connect directly via Web Serial.",
    )
    parser.add_argument(
        "--no-open",
        action="store_true",
        help="Do not open a browser window automatically.",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List detected serial ports and exit.",
    )
    args = parser.parse_args(argv)

    if args.list_ports:
        ports = list_serial_ports()
        if not ports:
            print("No serial ports found.")
        else:
            for name, label in ports:
                print("%s\t%s" % (name, label))
        return 0

    bridge = None
    if not args.webserial:
        selected_port = args.port
        if not selected_port:
            ports = list_serial_ports()
            if ports:
                selected_port = choose_port_interactive(ports)
        bridge = SerialBridge(port_name=selected_port, baudrate=args.baud)
        bridge.start()

    server = bind_http_server(args.host, args.http_port, bridge)
    host, port = server.server_address[:2]
    query = "?usb=1" if args.webserial else "?ws=1"
    url = "http://%s:%d/%s" % (host, port, query)
    print("CPR Dashboard (USB): %s" % url, flush=True)
    if args.webserial:
        print("Mode: Web Serial (Chrome/Edge). Press Ctrl+C to stop.", flush=True)
    else:
        print(
            "Mode: USB Bridge (%s, %d baud). Press Ctrl+C to stop."
            % (bridge.requested_port or "auto", args.baud),
            flush=True,
        )

    if not args.no_open:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if bridge is not None:
            bridge.stop()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
