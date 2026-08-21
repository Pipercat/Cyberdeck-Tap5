# cyberdeck-web

Small static web UI for the CyberDeck Tab5 remote flashing API. Vanilla
HTML/CSS/JS, no build step, no framework, no dependencies.

Served from the device was considered (see `docs/remote_flashing.md` for
why it was deliberately deferred - this session couldn't verify on
hardware whether embedding it via `EMBED_TXTFILES` is safe given the
project's history of boot crashes near this area, see
`docs/hardware_reference.md`). For now, open it directly from disk or any
static file server on your PC/Mac.

## Usage

1. Open `index.html` in a browser (double-click it, or serve the folder
   with e.g. `python3 -m http.server` from within this directory).
2. Enter the CyberDeck's IP address (Settings > Remote Access on the
   device, or `cyberdeck.py status` once paired) and click Connect.
3. First time: read the pairing code from Settings > Remote Access >
   "Show Pair Code" on the device and enter it. The resulting token is
   stored in the browser's `localStorage`, scoped to this page's origin.
4. Select an ESP-IDF `build/` directory (folder picker) to flash - the
   page reads `flasher_args.json` client-side to resolve addresses/binaries,
   same as `tools/cyberdeck-cli`.

## Known limitations (honest status)

- **Not tested against a real device or in a real browser in the session
  that wrote this** - no network path to hardware was available. Review
  before relying on it, especially the WebSocket reconnect logic and the
  `flasher_args.json` parsing for your exact ESP-IDF version's format.
- Requires the browser's `webkitdirectory` folder-picker attribute
  (supported in Chrome/Edge/Safari; Firefox support has historically been
  partial - if the folder picker doesn't work, use `tools/cyberdeck-cli`
  instead).
- CORS: the device's REST API replies with `Access-Control-Allow-Origin: *`
  so this page can be opened from any origin (including `file://`) without
  a matching server-side allowlist - reasonable for a LAN engineering tool,
  not for a public deployment.
