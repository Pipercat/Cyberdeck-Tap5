#!/usr/bin/env python3
"""
cyberdeck.py - Minimal command-line client for the CyberDeck Tab5 remote
flashing API (see docs/remote_access.md, docs/remote_protocol.md).

Deliberately dependency-free (Python standard library only) so it runs on
a plain "python3 cyberdeck.py ..." without a virtualenv/pip step - this is
meant to be a small, inspectable client, not a framework. That includes a
hand-rolled minimal WebSocket client (RFC 6455 basics only: text/binary
frames, client-to-server masking, no extensions/fragmentation) for the
`serial` command, since the standard library has no WebSocket client.

NOT hardware-tested against a real CyberDeck in the session that wrote it
(no network access to a device, no ESP-IDF toolchain to build firmware to
test against - see the repository's final implementation report). Treat
this as a solid first draft to validate against real hardware, especially
the WebSocket framing and the flasher_args.json parsing for your exact
ESP-IDF version.

Usage:
    cyberdeck.py status   --host 192.168.1.50
    cyberdeck.py pair     --host 192.168.1.50 --code 482917
    cyberdeck.py devices  --host 192.168.1.50
    cyberdeck.py flash    --host 192.168.1.50 ./build
    cyberdeck.py reset    --host 192.168.1.50
    cyberdeck.py bootloader --host 192.168.1.50
    cyberdeck.py serial   --host 192.168.1.50 --baud 115200

The paired client token is stored per-host in ~/.cyberdeck/config.json
after a successful `pair` - no need to pass --token on later commands.
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

CONFIG_PATH = Path.home() / ".cyberdeck" / "config.json"
CHUNK_SIZE = 4096  # muss <= FLASH_CHUNK_MAX_LEN auf dem Geraet sein (flash_manager.h)


def load_config():
    if CONFIG_PATH.exists():
        try:
            return json.loads(CONFIG_PATH.read_text())
        except (OSError, json.JSONDecodeError):
            return {}
    return {}


def save_config(cfg):
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2))
    try:
        os.chmod(CONFIG_PATH, 0o600)  # enthaelt Bearer-Tokens
    except OSError:
        pass


def get_token(host):
    return load_config().get(host, {}).get("token")


class ApiError(Exception):
    pass


def api_request(host, method, path, token=None, json_body=None, raw_body=None,
                 extra_headers=None, timeout=10):
    url = f"http://{host}{path}"
    headers = dict(extra_headers or {})
    data = None
    if json_body is not None:
        data = json.dumps(json_body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    elif raw_body is not None:
        data = raw_body
        headers["Content-Type"] = "application/octet-stream"
    if token:
        headers["Authorization"] = f"Bearer {token}"

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read()
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as e:
        body = e.read()
        try:
            parsed = json.loads(body)
            message = parsed.get("message", parsed.get("error", body.decode(errors="replace")))
        except (json.JSONDecodeError, UnicodeDecodeError):
            message = body.decode(errors="replace")
        raise ApiError(f"HTTP {e.code}: {message}") from e
    except urllib.error.URLError as e:
        raise ApiError(f"Connection failed: {e.reason}") from e


def cmd_pair(args):
    resp = api_request(args.host, "POST", "/api/v1/pair/confirm",
                        json_body={"code": args.code, "client_name": args.name})
    cfg = load_config()
    cfg[args.host] = {"token": resp["client_token"], "client_id": resp["client_id"]}
    save_config(cfg)
    print(f"Paired with {resp.get('device', args.host)}. Token stored in {CONFIG_PATH}.")


def cmd_status(args):
    system = api_request(args.host, "GET", "/api/v1/system")
    network = api_request(args.host, "GET", "/api/v1/network")
    print(f"Device:       {system.get('device')}  (firmware {system.get('firmware')})")
    print(f"Uptime:       {system.get('uptime_s', 0)} s")
    print(f"Heap:         {system.get('heap_free', 0) // 1024} / {system.get('heap_total', 0) // 1024} kB free")
    print(f"PSRAM:        {system.get('psram_free', 0) // 1024} / {system.get('psram_total', 0) // 1024} kB free")
    wifi = network.get("wifi", {})
    print(f"Wi-Fi:        {'connected' if wifi.get('connected') else 'disconnected'} "
          f"({wifi.get('ssid', '?')}, {wifi.get('rssi_dbm', '?')} dBm, {wifi.get('ip', '?')})")
    server = network.get("remote_server", {})
    print(f"Remote server: {'running' if server.get('running') else 'stopped'}, "
          f"{server.get('clients', 0)} client(s)")


def cmd_devices(args):
    token = get_token(args.host)
    resp = api_request(args.host, "GET", "/api/v1/devices", token=token)
    devices = resp.get("devices", [])
    if not devices:
        print("No USB target connected.")
        return
    for d in devices:
        print(f"{d.get('product', d.get('manufacturer', '?'))}")
        print(f"  VID:PID       {d.get('vid')}:{d.get('pid')}")
        print(f"  Bridge        {d.get('bridge')}")
        print(f"  Serial-capable {d.get('serial_supported')}")
        print(f"  Flash-capable  {d.get('flash_supported')}")


def cmd_reset(args):
    token = get_token(args.host)
    api_request(args.host, "POST", "/api/v1/device/reset", token=token, json_body={})
    print("Reset triggered.")


def cmd_bootloader(args):
    token = get_token(args.host)
    resp = api_request(args.host, "POST", "/api/v1/device/bootloader", token=token, json_body={})
    if resp.get("ok"):
        print("Bootloader entry sequence sent.")
    else:
        print(resp.get("message", "Bootloader entry failed."))


def parse_flasher_args(build_dir):
    """Liest build/flasher_args.json (Standard-ESP-IDF-Build-Artefakt) und
    baut daraus die Manifest-Dateiliste - der Nutzer muss keine Adressen von
    Hand eingeben (Nutzeranforderung 22)."""
    path = Path(build_dir) / "flasher_args.json"
    if not path.exists():
        raise ApiError(f"{path} not found - run 'idf.py build' first.")
    data = json.loads(path.read_text())
    flash_files = data.get("flash_files", {})
    chip = data.get("extra_esptool_args", {}).get("chip", "")

    files = []
    for addr_str, rel_path in sorted(flash_files.items(), key=lambda kv: int(kv[0], 0)):
        full_path = Path(build_dir) / rel_path
        if not full_path.exists():
            raise ApiError(f"Referenced binary not found: {full_path}")
        files.append({
            "name": full_path.name,
            "address": addr_str,
            "size": full_path.stat().st_size,
            "_local_path": str(full_path),
        })
    return chip, files


def cmd_flash(args):
    token = get_token(args.host)
    chip, files = parse_flasher_args(args.build_dir)
    if not files:
        raise ApiError("No flashable files found in flasher_args.json")

    total = sum(f["size"] for f in files)
    print(f"Chip hint: {chip or '?'}  Files: {len(files)}  Total: {total / 1024 / 1024:.2f} MB")

    manifest = {"chip": chip, "files": [{"name": f["name"], "address": f["address"], "size": f["size"]} for f in files]}
    api_request(args.host, "POST", "/api/v1/flash/start", token=token, json_body=manifest, timeout=30)
    print("Flash started - entering bootloader / syncing...")

    written_total = 0
    start_time = time.time()
    for idx, f in enumerate(files):
        offset = 0
        with open(f["_local_path"], "rb") as fh:
            while True:
                chunk = fh.read(CHUNK_SIZE)
                if not chunk:
                    break
                api_request(args.host, "POST", f"/api/v1/flash/chunk?file={idx}&offset={offset}",
                            token=token, raw_body=chunk, timeout=15)
                offset += len(chunk)
                written_total += len(chunk)
                pct = 100 * written_total / total
                elapsed = max(time.time() - start_time, 0.001)
                speed_kb = (written_total / 1024) / elapsed
                sys.stdout.write(f"\r{f['name']:<24} {pct:5.1f}%  {speed_kb:6.0f} kB/s")
                sys.stdout.flush()
    print()

    api_request(args.host, "POST", "/api/v1/flash/finish", token=token, json_body={}, timeout=30)
    print("Flash finished successfully. Target reset.")


def build_ws_key():
    return base64.b64encode(os.urandom(16)).decode()


def ws_connect(host, path):
    """Minimaler RFC-6455-Handshake+Frame-Client (nur Text/Binary, kein
    Fragmentieren/Kompression) - siehe Moduldocstring fuer den Grund, warum
    kein externes Paket verwendet wird."""
    if ":" in host:
        hostname, port = host.split(":", 1)
        port = int(port)
    else:
        hostname, port = host, 80

    sock = socket.create_connection((hostname, port), timeout=10)
    key = build_ws_key()
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode())

    response = b""
    while b"\r\n\r\n" not in response:
        chunk = sock.recv(4096)
        if not chunk:
            raise ApiError("WebSocket handshake failed (connection closed - check pairing token)")
        response += chunk
    header_block, _, _ = response.partition(b"\r\n\r\n")
    lines = header_block.split(b"\r\n")
    status_line = lines[0].decode(errors="replace")
    if " 101 " not in status_line:
        raise ApiError(f"WebSocket handshake rejected: {status_line}")

    headers = {}
    for line in lines[1:]:
        if b":" in line:
            k, v = line.split(b":", 1)
            headers[k.strip().lower()] = v.strip()
    expected_accept = base64.b64encode(
        hashlib.sha1(key.encode() + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest()
    )
    if headers.get(b"sec-websocket-accept") != expected_accept:
        sock.close()
        raise ApiError("WebSocket handshake failed Sec-WebSocket-Accept verification")
    return sock


def ws_send(sock, payload: bytes, opcode=0x2):
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    length = len(payload)
    header = bytes([0x80 | opcode])
    if length < 126:
        header += bytes([0x80 | length])
    elif length < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", length)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", length)
    sock.sendall(header + mask + masked)


def ws_recv(sock):
    """Liest genau einen (unmaskierten Server->Client) Frame. Gibt (opcode,
    payload) zurueck oder (None, b"") bei Verbindungsende."""
    header = sock.recv(2)
    if len(header) < 2:
        return None, b""
    opcode = header[0] & 0x0F
    length = header[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", sock.recv(8))[0]
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            break
        payload += chunk
    return opcode, payload


def cmd_serial(args):
    """Interaktives Terminal: liest RX vom Geraet und TX von stdin parallel
    ueber select() (Unix). Unter Windows fehlt select() auf stdin - dort
    bitte die Web-UI (tools/cyberdeck-web/) fuer den seriellen Terminal
    verwenden, siehe README dort."""
    import select

    token = get_token(args.host)
    if not token:
        raise ApiError("No paired token for this host - run 'pair' first.")
    sock = ws_connect(args.host, f"/api/v1/serial?token={token}&baud={args.baud}")
    print(f"Connected. Baud={args.baud}. Ctrl+C to exit.")
    try:
        while True:
            readable, _, _ = select.select([sock, sys.stdin], [], [], 0.2)
            for src in readable:
                if src is sock:
                    opcode, payload = ws_recv(sock)
                    if opcode is None:
                        print("\nConnection closed by device.")
                        return
                    if opcode in (0x1, 0x2):  # text or binary
                        sys.stdout.buffer.write(payload)
                        sys.stdout.flush()
                elif src is sys.stdin:
                    line = sys.stdin.readline()
                    if not line:
                        return
                    ws_send(sock, line.encode())
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description="CyberDeck Tab5 remote client")
    parser.add_argument("--host", required=True, help="Device IP or hostname (e.g. 192.168.1.50)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status")
    sub.add_parser("devices")

    p_pair = sub.add_parser("pair")
    p_pair.add_argument("--code", required=True, help="6-digit pairing code shown on the device")
    p_pair.add_argument("--name", default=os.uname().nodename if hasattr(os, "uname") else "cli-client")

    p_flash = sub.add_parser("flash")
    p_flash.add_argument("build_dir", help="ESP-IDF build/ directory containing flasher_args.json")

    sub.add_parser("reset")
    sub.add_parser("bootloader")

    p_serial = sub.add_parser("serial")
    p_serial.add_argument("--baud", type=int, default=115200)

    args = parser.parse_args()
    handlers = {
        "status": cmd_status,
        "devices": cmd_devices,
        "pair": cmd_pair,
        "flash": cmd_flash,
        "reset": cmd_reset,
        "bootloader": cmd_bootloader,
        "serial": cmd_serial,
    }
    try:
        handlers[args.command](args)
    except ApiError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
