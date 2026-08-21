# cyberdeck-cli

Minimal command-line client for the CyberDeck Tab5 remote flashing API.
Python 3 standard library only - no `pip install` needed.

See `docs/remote_access.md` and `docs/remote_protocol.md` in the repository
root for the full protocol/security model this client talks to.

## Usage

```bash
# One-time pairing: read the 6-digit code from the CyberDeck's
# Settings > Remote Access > "Show Pair Code" screen.
python3 cyberdeck.py --host 192.168.1.50 pair --code 482917

# Status
python3 cyberdeck.py --host 192.168.1.50 status

# Connected USB target
python3 cyberdeck.py --host 192.168.1.50 devices

# Flash an ESP-IDF build directory (reads flasher_args.json - no manual
# addresses needed)
python3 cyberdeck.py --host 192.168.1.50 flash ./build

# Reset / bootloader control
python3 cyberdeck.py --host 192.168.1.50 reset
python3 cyberdeck.py --host 192.168.1.50 bootloader

# Interactive serial terminal (Unix only - see note below)
python3 cyberdeck.py --host 192.168.1.50 serial --baud 115200
```

The paired token is stored in `~/.cyberdeck/config.json` (0600 permissions)
after `pair` - it is not re-requested on later commands.

## Known limitations (honest status, see repository final report)

- **Not tested against real hardware or a running device in the session
  that wrote this** - there was no network path to a device and no
  ESP-IDF toolchain available to build firmware to test against. Treat
  this as a solid first draft, not a verified client.
- The WebSocket client (`serial` command) is a small hand-rolled RFC 6455
  implementation (text/binary frames, client masking, no fragmentation/
  compression) instead of a pip dependency, to keep this tool
  dependency-free. Verify it against a real device before relying on it.
- `serial`'s interactive TX (typing while connected) uses `select()` on
  stdin, which does not work on Windows. Use `tools/cyberdeck-web/` for a
  serial terminal on Windows.
- Chunk CRC32 (`X-Chunk-CRC32` header) is deliberately **not** sent by
  this client - see `docs/remote_protocol.md` for why the exact CRC32
  variant needs verifying against the device before enabling it.
