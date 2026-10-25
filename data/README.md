# Open golf course data

Golf course maps built from OpenStreetMap, checked for completeness and published as
plain files. Pebble Golf uses them, but they're meant for any app. License: ODbL 1.0,
© OpenStreetMap contributors ([details](LICENSE.md)).

## Download

Generated files are published at https://j5uh.github.io/pebble-watch-golf/:

| File | What it is |
|---|---|
| `index.json` | Every course: `id`, `name`, `short`, `city`, `center` [lat, lon], `version` |
| `<id>.geojson` | Standard GeoJSON (`[lon, lat]`). Open it in geojson.io, QGIS, Mapbox, Leaflet. |
| `<id>.json` | Pebble Golf's own format (`[lat, lon]`), including per-hole watch maps (`mapHex`) |

GeoJSON features carry a `golf` property:

| `golf` | Geometry | Properties |
|---|---|---|
| `hole` | LineString, tee to green | `ref` (hole number), `par`, `osmId` |
| `green` | Polygon | `hole`, `osmId` |
| `fairway`, `tee` | Polygon | `hole` (fairway cutouts are omitted) |
| `bunker`, `water_hazard` | Polygon | `holes` (list), `osmId` |

## What's checked, and what isn't

Checked on every build: 18 numbered holes with par, each hole ending in exactly one green,
no green shared, tee metadata present.

Not checked: bunker, water, fairway, and tee boxes are matched to holes by distance to the
playing line, so review `hazard-associations.json` and `hazard-map-audit.png`. Green
centers are polygon centroids, not today's pin. OSM tee boxes have no colour.

## Layout

```
data/courses/<id>/
  course.osm                 OSM snapshot (source of truth, never hand-edited)
  tees.json                  name, short header name, city, OSM link, tee yardages + source
  hazard-associations.json   generated: which hazards belong to which holes
```

## Add a course

1. Make sure the course is mapped in OSM: `golf=hole` ways with `ref` and `par` that end
   inside a `golf=green`, plus bunkers and water. Fix gaps in OSM itself so everyone benefits.
2. Download the course area from [Overpass Turbo](https://overpass-turbo.eu/) (export → raw OSM data)
   into `data/courses/<id>/course.osm`. `<id>` is lowercase with dashes, e.g. `memorial-park`.
   ```
   [out:xml];
   way(<OSM way id of the course>);map_to_area->.a;
   (nwr["golf"](area.a););
   (._;>;);
   out meta;
   ```
   For a course mapped as a relation, use `rel(<id>);map_to_area->.a;` instead.
3. Write `data/courses/<id>/tees.json`, copying an existing one. `short` is 10 capitals or
   fewer; tee names are capitals, up to 8 letters. Record where the yardages came from in
   `source`, `sourceUrl`, and `verifiedDate`.
4. Run `python3 scripts/import-course.py`. Fix any assertion it raises (usually in OSM).
5. Open `courses/<id>.geojson` in geojson.io and check the holes and hazards look right,
   then open a pull request.
