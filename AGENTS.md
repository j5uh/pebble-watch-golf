# Pebble Golf

Work in this directory. Native C watchapp for Pebble Round 2 (`gabbro`); phone code is PebbleKit JS, not Alloy.

- SDK 4.33.1; `pebble build --sdk 4.33.1`.
- `node tests/distance.test.js` checks green/hazard geometry and layup math.
- Compile/run `tests/map-camera-test.c` with the host C compiler to verify zoom and framing.
- Map geometry uses 1/16 overview-pixel coordinates in both the watch header and phone marker protocol.
- `node tests/phone.test.js` checks GPS recovery, fix age, permissions, revisions, and delivery retry.
- After changing message keys, `pebble clean` before building.
- Up/Down always select holes during play; Select cycles green + layup/hazards/map; hold Select opens settings.
- No automatic hole detection or pin placement (explicitly excluded by the user).
- `scripts/make-preview.py` makes a separate simulated-GPS package in /tmp; restore the production package after UI checks.
- `python3 scripts/import-course.py` rebuilds course.json from the local OSM snapshot.
- Keep PKJS compatible with the SDK's legacy JavaScript bundler (no exponentiation operator or newer syntax without transpilation).
- Preserve OSM provenance and attribution. Never label static map targets as today's pin position.
- Phone GPS is mandatory; do not imply independent watch GPS.
- SDK docs: https://developer.repebble.com/llms.txt.

## Visible demo viewer

The interactive local demo is open in Chrome at http://127.0.0.1:8878/. Its temporary viewer/server lives in `/tmp/pebble-golf-viewer` and connects to the SDK VNC/WebSocket display. While using it, pass `--vnc` on all `pebble --emulator gabbro` operations; switching back to SDL stops the browser display. Current requested demo GPS preset is `--fixed-hole 14`, stationary at hole 14's mapped starting tee. This supersedes the earlier fixed first-White-tee demo.
