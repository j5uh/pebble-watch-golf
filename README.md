# Pebble Golf

Native C rangefinder for **Pebble Round 2 (`gabbro`)**, with **Sharpstown Park Golf Course, Houston** built in and more courses downloaded on demand. Version 0.2.0.

## Playing a round

1. Choose a course. The phone lists up to five courses, nearest first once it has a GPS position (otherwise the last course played leads). Selecting one downloads it to the phone if needed; it is kept there for offline play.
2. Choose a tee using Up/Down; names and total lengths come from the course. Select starts a new round on hole 1. Back returns to the course list.
3. **Up / Down** always select the previous / next hole during play. Selection is manual and wraps through all of the course's holes (9 or 18).
4. **Select** cycles through Green + Layup → all-hazards overview → Map → Green.
5. **Hold Select** for settings: refresh GPS, change tee, preferred layup approach distance, yards/meters, new round, credits, and return to play.
6. In settings use Up/Down to browse, Select to open/confirm, Back to cancel/return. Changing tees during a round preserves the current hole.

The green screen shows **front, middle, and back together**, with a large middle distance, hole/par, selected tee, units, GPS accuracy, and reading age. Tee selection records round context; live distances are always measured from your current phone location.

## Hole map

The map view draws bundled OpenStreetMap geometry directly on the watch: available fairways, green, bunkers, water, and the mapped playing line. The current OSM snapshot includes fairway polygons for only three holes; other holes show the playing line and available green/hazard geometry. It is oriented tee-to-green upward. Inside roughly 240 yards (220 meters), the map automatically zooms and pans toward your position and the green, up to 6×. The full green and player stay in frame; nearby geometry is clipped to the map viewport. Missing or stale GPS returns to the whole-hole overview. **G** marks the green, **T** the mapped starting tee (not a surveyed daily tee-marker position), and a **magenta ring** marks the current phone GPS position. Stale positions are hidden; positions beyond the map bounds show an outside-map message. The map itself works without phone GPS or an internet connection.

The map uses dark feature outlines, light edge shadows, striped greens, dotted sand, wave-patterned blue water, a dashed playing line, and a high-contrast magenta player ring with a white halo. These are visual styling cues, not elevation or terrain shading.

This is a vector course overview, not aerial or satellite imagery. Each hole's map (`mapHex`) is generated with the phone's matching projection metadata by `scripts/import-course.py`; the phone sends the watch only the current hole's map. The simulator currently uses our labeled fake-GPS feed.

## Hazard distances

The bundled OSM snapshot contains **20 bunker polygons and four water polygons**. The importer associates bunkers with the nearest mapped hole path (within 65 m); water can be associated with multiple bordering holes within 50 m. Associations are inspectable in `data/courses/sharpstown-park/hazard-associations.json` and the audit image `data/courses/sharpstown-park/hazard-map-audit.png`.

For the selected hole, one overview shows the green and all mapped bunker/water polygons together, with compact **R** (reach) and **C** (carry) distance callouts on the sides. Thin leader lines terminate at the corresponding near/far boundary crossings. Reach/carry label order adjusts to prevent each pair from crossing, and numbers sit two pixels from the line ends. A compact Front / Middle / Back row above the map keeps green yardages visible on the same screen. Dark-green F/M/B callouts and non-crossing leader lines mark the exact front edge, green centroid, and back edge used by those measurements. Missing front/back intersections show -- without edge leaders. Labels are distributed vertically to avoid collisions; the legend shows the current units. Select opens Layup directly instead of stepping through individual hazard cards.

The phone includes measurements for mapped hazards behind the player as well, so their outlines do not disappear from the overview after passing them. Stale/missing readings appear as `--`, while the map remains available. Up to eight hazard measurements are supported; the Sharpstown dataset fits within this limit.

- **Reach:** first boundary crossing along the bearing toward that hazard's mapped centroid.
- **Carry:** last forward boundary crossing on that same bearing.
- Both measurements are calculated from the phone's current location, and the mapped edge anchors travel with the distances to keep leader lines tied to the same geometry.

These are horizontal map distances on the indicated bearing. They do not predict ball flight or account for elevation, wind, or a safety margin. An empty list says **None mapped ahead**; it does not establish that the hole is free of hazards. Public mapping and per-hole associations still need on-course verification.

## Layup targets

Choose a preferred remaining approach distance from **50–200 yards or meters**, in increments of 25. Default is 100 in the selected unit. Switching units keeps the selected number (100 yards becomes a 100-meter preference).

The app finds a point on the hole's mapped playing line at that direct distance from the green middle, then calculates your straight-line shot distance to that point. This accounts for the geometry of doglegs instead of simply subtracting the preferred approach from the green distance. The screen shows **No forward layup target** when already inside that approach distance or when no suitable forward point exists.

A mapped playing-line point is a planning aid: it is not a surveyed safe landing area or a guarantee that the shot avoids hazards.

## GPS and connectivity

Carry the paired phone with you. The watch has no independent GPS; leaving the phone in a cart measures from the cart. Location permission belongs to the Pebble mobile app.

- Continuous high-accuracy location subscription, plus a five-second status heartbeat.
- Fresh-fix requests and automatic recovery when readings stop; a watchdog handles providers that never call back.
- Periodic subscription restart during a prolonged outage, with bounded message-delivery backoff.
- Clear permission, timeout, weak-GPS, stale-reading, and disconnected-phone states.
- Original fix age and timestamp travel with yardages, so delayed delivery does not make an old reading new.
- Measurements are hidden after 30 seconds. Hole/settings revisions reject out-of-date responses.
- Permission denial stops automatic location retries; use **Settings → Refresh GPS** after changing permission.

Sharpstown is bundled with the phone JavaScript; other courses are downloaded once when chosen and kept on the phone, so no cellular data is needed during play. A working watch–phone connection and phone location service are still required. Real mobile background behavior, accuracy, and battery usage need testing on a paired phone during a round.

## Build and checks

Requires Pebble SDK 4.33.1 and its dependencies. Only `gabbro` is targeted.

```sh
python3 scripts/import-course.py
node tests/distance.test.js
node tests/phone.test.js
cc -Wall -Wextra -Werror tests/map-camera-test.c -o /tmp/pebble-map-camera-test
/tmp/pebble-map-camera-test
cc -Wall -Wextra -Werror tests/hazard-leaders-test.c -o /tmp/pebble-hazard-leaders-test
/tmp/pebble-hazard-leaders-test
pebble build --sdk 4.33.1
pebble install --emulator gabbro
```

Run `pebble clean` if message keys change; this SDK can retain stale generated key headers during incremental builds.

For a real watch, enable Developer Connection in the Pebble mobile app, then use `pebble login` and `pebble install --cloudpebble`, or open `build/pebble-golf.pbw` on the phone.

Tests cover all 18 green geometries, polygon edge distances, hazards behind/containing the player, dogleg layups, unit conversion, GPS timeouts, permission recovery, stale/invalid readings, delayed callbacks, settings revisions, and message retry.

### Emulator previews

`python3 scripts/make-preview.py` creates **`/tmp/pebble-golf-preview.pbw`**, a separate diagnostic package with simulated phone GPS at hole 1's mapped tee, refreshed every five seconds and labeled **SIMULATED GPS** on the watch. The release `build/pebble-golf.pbw` is untouched and always requires real GPS. Use `--distance-yards 100` for a fixed approach or `--walk` to cycle simulated positions from the tee to 10 yards, with a new step every 12 seconds. Fake GPS follows the manually selected hole; Up/Down restarts the simulated approach on that hole. Use `--step-seconds` to adjust the pace. `--fixed-hole 8` holds GPS at hole 8's mapped starting tee, regardless of the displayed hole. `--white-first-tee` holds a fixed approximate hole 1 white-tee position (559 yards directly from the mapped green middle); it does not follow hole selection and is not a surveyed tee marker. The fake-GPS demo can stay installed for interactive testing; install the release when testing real GPS.

Screenshots in `screenshots/round2-green.png`, `round2-hazards.png`, and `round2-layup.png` use simulated GPS, not an on-course measurement.

## Data provenance

Course data **© OpenStreetMap contributors**, available under the [Open Database License](https://opendatacommons.org/licenses/odbl/1-0/). See [OSM copyright](https://www.openstreetmap.org/copyright). Derived course data and hazard associations are distributed under ODbL 1.0.

- Course: https://www.openstreetmap.org/relation/1705949 (mapped as Sharpstown Country Club).
- OSM snapshot downloaded 2026-10-23: `https://api.openstreetmap.org/api/0.6/map?bbox=-95.536,29.705,-95.523,29.716`.
- `data/courses/sharpstown-park/course.osm`: original map extract, including surrounding features.
- `src/pkjs/course.json` and `courses/<id>.json`: derived greens, hole paths, hazards, watch maps, and original OSM IDs.
- `scripts/import-course.py`: reproducible conversion; requires all 18 hole endpoints to match unique green polygons.
- Tee names follow the [official Sharpstown scorecard](https://www.memorialparkgolf.com/copy-of-sharpstown-park-golf-course); total course lengths come from the [USGA course rating database](https://ncrdb.usga.org/courseTeeInfo?CourseID=20651), recorded in `data/courses/sharpstown-park/tees.json`.
- Memorial Park (https://www.openstreetmap.org/relation/13729803): Overpass extract of golf features within the course area, 2026-10-24. Tee yardages from the [GolfPass scorecard](https://www.golfpass.com/courses/3037-memorial-park-golf-course/scorecard-and-layout), not yet verified with the course.

Green middles are derived polygon centroids. Front/back are crossings on the line from the player through the middle. These are **green distances, not today's cup location**. Neither automatic hole detection nor pin placement is implemented.

## Open course data and license

The course files are an open dataset for any app. GeoJSON and JSON downloads, field docs, and how to add a course are in [data/README.md](data/README.md). Code is [MIT](LICENSE); course data is [ODbL 1.0](data/LICENSE.md), © OpenStreetMap contributors.

## Developer references

- https://developer.repebble.com/sdk/
- https://developer.repebble.com/docs/c/
- https://developer.repebble.com/guides/communication/using-pebblekit-js/
- https://developer.repebble.com/guides/tools-and-resources/testing-with-the-mobile-app/
