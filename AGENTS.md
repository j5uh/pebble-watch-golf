# Pebble Golf

Work in this directory. This is a native C watchapp for Pebble Round 2 (`gabbro`), with PebbleKit JS on the paired phone. It does not use Alloy.

## Project layout

- `src/c/main.c`: watch UI, course list, button handling, settings, GPS status, and phone messages. The watch holds only the current hole's map, sent by the phone.
- `src/c/map-camera.h` and `src/c/hazard-leaders.h`: map framing and label geometry.
- `src/pkjs/index.js`: phone GPS, nearby course list, course download and offline cache, request revisions, and message delivery.
- `src/pkjs/distance.js`: green, hazard, and layup calculations.
- `scripts/import-course.py`: generates `courses/` (hosted course files, gitignored) and the built-in `src/pkjs/course.json` from `data/courses/`. Update the importer or source data rather than hand-editing generated geometry.
- `data/courses/<id>/`: per-course OSM snapshot (`course.osm`), tees and names (`tees.json`), and generated hazard associations.
- `tests/`: Node.js and host C checks; `screenshots/`: UI reference images.

## Build and validation

Use SDK 4.33.1 and target `gabbro`:

```sh
pebble build --sdk 4.33.1
```

After changing message keys, run `pebble clean` before building. Keep PKJS compatible with the SDK's legacy JavaScript bundler; do not use exponentiation or newer syntax without transpilation.

Run checks relevant to the change. The Node tests read `courses/`, so run the importer first:

```sh
python3 scripts/import-course.py
node tests/distance.test.js
node tests/phone.test.js
cc -Wall -Wextra -Werror tests/map-camera-test.c -lm -o /tmp/pebble-map-camera-test
/tmp/pebble-map-camera-test
cc -Wall -Wextra -Werror tests/hazard-leaders-test.c -lm -o /tmp/pebble-hazard-leaders-test
/tmp/pebble-hazard-leaders-test
```

Distance tests cover green/hazard geometry and layup math. Phone tests cover GPS recovery, fix age, permissions, revisions, and delivery retry. Host C tests cover map zoom/framing and non-crossing hazard labels. Build after watch changes, and visually inspect UI changes on the round emulator.

SDK documentation: https://developer.repebble.com/llms.txt.

## Product and display rules

- Up/Down always select holes during play. Select cycles Green + Layup → Hazards → Map. Hold Select opens settings.
- Hole selection remains manual. Automatic hole detection and pin placement are explicitly excluded.
- Phone GPS is mandatory for live distances; never imply independent watch GPS.
- Keep stale or disconnected measurements clearly unavailable, and simulated GPS explicitly labeled.
- Use the circular display area. Course name and view label follow the upper curvature; center the complete measured string together, including spaces and separators, for every view.
- Keep labels and values inside the circular boundary and check the longest heading when changing layout.
- Map geometry uses 1/16 overview-pixel coordinates in both the phone-sent hole map (`mapHex`) and the phone marker protocol. Keep the importer, `parse_map` in `main.c`, and `MAX_POINTS`/`MAX_SHAPES` consistent.
- Course selection is manual from a nearest-first list; never pick a course automatically.
- Preserve OSM provenance and attribution. Static map targets and green centroids are not today's pin position.

## Course data

Run `python3 scripts/import-course.py` to regenerate course assets from the local OSM snapshots. Preserve the source snapshots and OSM IDs, and review generated geometry and hazard associations when changing the importer.

Each course is a folder in `data/courses/<id>/` with `course.osm` (an Overpass area extract of `golf=*` features, or an OSM API bbox map) and `tees.json` (`course`, `short` of 10 capitals or fewer for the curved header, `city`, `osmUrl`, tee names/yards, and their source). Sharpstown sets `builtIn` and ships inside the phone bundle so the app works without downloads. Memorial Park tee yardages come from GolfPass and are not yet checked against the course tee sheet.

The phone downloads `index.json` and `<id>.json` from `COURSES_URL` in `src/pkjs/index.js`. It points at GitHub Pages (https://j5uh.github.io/pebble-watch-golf/), which `.github/workflows/courses.yml` rebuilds from `data/` on every push to `main`. The importer also writes `<id>.geojson` for reuse outside the app; `data/README.md` documents the open dataset and how to add a course. Code is MIT (`LICENSE`); course data is ODbL (`data/LICENSE.md`). To test unpublished course changes, temporarily point `COURSES_URL` at `http://127.0.0.1:8879/` and run:

```sh
cd courses && python3 -m http.server 8879 --bind 127.0.0.1
```

## Emulator and visible demo

The local browser viewer is configured at http://127.0.0.1:8878/. Its temporary server lives in `/tmp/pebble-golf-viewer` and connects to the SDK VNC/WebSocket display. These temporary files may need to be recreated in a new session.

While using the viewer, pass `--vnc` on every `pebble --emulator gabbro` operation; switching to SDL stops the browser display.

The requested demo preset is stationary GPS at hole 14's mapped starting tee:

```sh
python3 scripts/make-preview.py --fixed-hole 14
pebble install --emulator gabbro --vnc /tmp/pebble-golf-preview.pbw
```

Choose Sharpstown on the course list (nearest first), a tee, then select hole 14 manually for matching demo yardages. This preset supersedes the earlier fixed first-White-tee demo.

The preview script creates a separate simulated-GPS package in `/tmp`; keep `build/pebble-golf.pbw` as the production package requiring real phone GPS. The labeled preview may remain installed for the requested interactive demo. Install the production package when returning to real-GPS testing.

## Repository

Remote: https://github.com/j5uh/pebble-watch-golf.git. Default branch: `main`.

Keep generated build output, SDK lock files, dependencies, and temporary preview packages out of version control; respect `.gitignore`.
