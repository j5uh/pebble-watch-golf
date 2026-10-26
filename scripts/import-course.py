"""Rebuild every course in data/courses/ from its committed OSM snapshot. No network required.

Writes courses/index.json and courses/<id>.json for hosting, and the built-in course
to src/pkjs/course.json so the app still works before any download."""
import hashlib
import json
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
# Watch receive buffers in src/c/main.c; keep in step.
MAX_POINTS, MAX_SHAPES = 800, 32

def build(folder):
    meta=json.loads((folder/'tees.json').read_text())
    # A multi-course site shares one snapshot; osmCourse picks holes by golf:course:name.
    r = ET.parse(ROOT/'data/courses'/meta.get('snapshot',folder.name)/'course.osm').getroot()
    nodes = {e.get('id'): [float(e.get('lat')), float(e.get('lon'))] for e in r.findall('node')}
    lat0 = round(sum(p[0] for p in nodes.values())/len(nodes), 2)
    def tags(e): return {t.get('k'): t.get('v') for t in e.findall('tag')}
    def points(e): return [nodes[n.get('ref')] for n in e.findall('nd')]
    def inside(p, ring):
        y, x = p
        result = False
        for a, b in zip(ring, ring[1:]):
            if (a[0] > y) != (b[0] > y) and x < (b[1]-a[1])*(y-a[0])/(b[0]-a[0])+a[1]:
                result = not result
        return result
    # Shoelace centroid in lon/lat, translated to avoid cancellation.
    def centroid(ring):
        oy, ox = ring[0]
        xy = [(lon-ox, lat-oy) for lat, lon in ring]
        area = cx = cy = 0
        for (x,y),(u,v) in zip(xy,xy[1:]):
            cross=x*v-u*y; area+=cross; cx+=(x+u)*cross; cy+=(y+v)*cross
        return [round(oy+cy/(3*area),7),round(ox+cx/(3*area),7)]
    greens=[e for e in r.findall('way') if tags(e).get('golf')=='green']
    holes=[]; others=[]
    for e in r.findall('way'):
        t=tags(e)
        if t.get('golf') != 'hole': continue
        path=points(e)
        if meta.get('osmCourse') and t.get('golf:course:name')!=meta['osmCourse']:
            others.append(dict(number=None,path=path,tee=path[0])); continue
        matches=[g for g in greens if inside(path[-1],points(g))]
        assert len(matches)==1, f"{folder.name} hole {t['ref']}: expected one containing green, got {len(matches)}"
        g=matches[0]; ring=points(g)
        holes.append(dict(number=int(t['ref']),par=int(t['par']),osmHoleId=e.get('id'),osmGreenId=g.get('id'),center=centroid(ring),green=ring,tee=path[0],path=path))
    holes.sort(key=lambda h:h['number'])
    assert [h['number'] for h in holes] in (list(range(1,10)),list(range(1,19))), f"{folder.name}: expected holes 1-9 or 1-18"
    assert len({h['osmGreenId'] for h in holes})==len(holes)
    # Associate each bunker to its nearest hole path. Water may border several holes.
    # Distances use a local metric projection; keep associations reproducible and inspectable.
    def xy(p): return (p[1]*111195*math.cos(math.radians(lat0)), p[0]*111195)
    def segment_distance(p,a,b):
        px,py=xy(p); ax,ay=xy(a); bx,by=xy(b)
        dx,dy=bx-ax,by-ay
        t=max(0,min(1,((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy))) if dx or dy else 0
        return math.hypot(px-ax-t*dx,py-ay-t*dy)
    def path_distance(ring,h):
        return min(segment_distance(p,a,b) for p in ring for a,b in zip(h['path'],h['path'][1:]))
    for h in holes: h['hazards']=[]
    features=[]
    for e in r.findall('way'):
        t=tags(e)
        if t.get('golf') not in ['bunker','water_hazard','lateral_water_hazard']: continue
        ring=points(e)
        assert ring[0]==ring[-1], f"Hazard {e.get('id')} is not a closed polygon"
        kind='BUNKER' if t['golf']=='bunker' else 'WATER'
        ranked=sorted(((path_distance(ring,h),h['number']) for h in holes+others),key=lambda x:x[0])
        assigned=[number for d,number in ranked if d<=50 and number] if kind=='WATER' else [ranked[0][1]] if ranked[0][0]<=65 and ranked[0][1] else []
        feature=dict(osmId=e.get('id'),kind=kind,center=centroid(ring),polygon=ring)
        features.append(dict(feature,holes=assigned))
        for n in assigned: holes[n-1]['hazards'].append(feature)
    if meta.get('osmCourse'): features=[f for f in features if f['holes']]
    (folder/'hazard-associations.json').write_text(json.dumps(features,indent=2)+'\n')
    assert 1<=len(meta['tees'])<=6 and all(t['name'].isupper() and len(t['name'])<=8 for t in meta['tees'])
    assert all(isinstance(t['totalYards'],int) and t['totalYards']>0 for t in meta['tees'])
    # The short name heads every view; "SHARPSTOWN / HAZARDS" is the longest checked heading.
    assert meta['short'].isupper() and len(meta['short'])<=10, f"{folder.name}: short name must be 10 capitals or fewer"
    # Preserve outer rings and cutouts in OSM multipolygon landcover.
    ways={e.get('id'): e for e in r.findall('way')}
    def landcover(key,value):
        result=[]
        for e in r.findall('way'):
            if tags(e).get(key)==value:
                ring=points(e)
                if ring and ring[0]==ring[-1]: result.append((ring,[]))
        for relation in r.findall('relation'):
            if tags(relation).get(key)!=value: continue
            rings={'outer':[],'inner':[]}
            for member in relation.findall('member'):
                if member.get('type')=='way' and member.get('role','outer') in rings:
                    rings[member.get('role','outer')].append(points(ways[member.get('ref')]))
            # Stitch split ways by shared endpoints, retaining every vertex.
            for role,parts in rings.items():
                joined=[]
                while parts:
                    ring=parts.pop(0)
                    while ring[0]!=ring[-1]:
                        for i,part in enumerate(parts):
                            if ring[-1]==part[0] or ring[-1]==part[-1]:
                                if ring[-1]==part[-1]: part=list(reversed(part))
                                ring+=part[1:]; parts.pop(i); break
                        else: raise ValueError('Unclosed landcover relation '+relation.get('id'))
                    joined.append(ring)
                rings[role]=joined
            for outer in rings['outer']:
                result.append((outer,[ring for ring in rings['inner'] if inside(ring[0],outer)]))
        return result
    for h in holes: h['fairways']=[]; h['fairwayCutouts']=[]; h['tees']=[]
    for ring,cutouts in landcover('golf','fairway'):
        c=centroid(ring)
        ranked=sorted(((min(segment_distance(c,a,b) for a,b in zip(h['path'],h['path'][1:])),h['number']) for h in holes+others),key=lambda x:x[0])
        if ranked[0][0]<=65 and ranked[0][1]:
            h=holes[ranked[0][1]-1]
            h['fairways'].append(ring); h['fairwayCutouts'].extend(cutouts)
    # OSM tee boxes carry no colour tags; assign each to the hole whose playing line,
    # extended 150 m behind its start, passes nearest. Selected tee colour is not mapped.
    def back_line(h):
        (tx,ty),(nx,ny)=xy(h['tee']),xy(h['path'][1]); length=math.hypot(tx-nx,ty-ny)
        bx,by=tx+(tx-nx)/length*150,ty+(ty-ny)/length*150
        return [by/111195,bx/(111195*math.cos(math.radians(lat0)))],h['path'][1]
    for e in r.findall('way'):
        if tags(e).get('golf')!='tee': continue
        ring=points(e)
        if ring[0]!=ring[-1]: continue
        distance,number=min(((segment_distance(centroid(ring),*back_line(h)),h['number']) for h in holes+others),key=lambda x:x[0])
        if distance<=25 and number: holes[number-1]['tees'].append(ring)
    # Hole-up vector maps share their projection with the phone's position marker.
    # Each hole's watch map is little-endian bytes (hex): tee and green int16 x,y; shape
    # count u8; per shape kind u8, point count u16, OSM id u32; then int16 x,y points.
    for h in holes:
        origin=h['tee']; cos=math.cos(math.radians(origin[0])); scale=111195
        def local(p): return ((p[1]-origin[1])*scale*cos,(p[0]-origin[0])*scale)
        ge,gn=local(h['center']); length=math.hypot(ge,gn); fe,fn=ge/length,gn/length
        def rotate(p):
            e,n=local(p); return (e*fn-n*fe,-(e*fe+n*fn))
        shapes=[(0,h['path'],0)]+[(4,ring,0) for ring in h['fairways']]+[(6,ring,0) for ring in h['fairwayCutouts']]+[(5,ring,0) for ring in h['tees']]+[(1,h['green'],0)]+[(2 if f['kind']=='BUNKER' else 3,f['polygon'],int(f['osmId'])) for f in h['hazards']]
        all_points=[rotate(p) for _,ring,_ in shapes for p in ring]
        minx,miny=min(p[0] for p in all_points)-10,min(p[1] for p in all_points)-10
        maxx,maxy=max(p[0] for p in all_points)+10,max(p[1] for p in all_points)+10
        zoom=min(170/(maxx-minx),112/(maxy-miny))
        offsetx=130-(minx+maxx)*zoom/2; offsety=153-(miny+maxy)*zoom/2
        h['map']=dict(origin=origin,eastScale=scale*cos,northScale=scale,forward=[fe,fn],zoom=zoom,offset=[offsetx,offsety])
        def pixel(p):
            x,y=rotate(p); return [round((x*zoom+offsetx)*16),round((y*zoom+offsety)*16)]
        assert len(shapes)<=MAX_SHAPES and len(all_points)<=MAX_POINTS, f"{folder.name} hole {h['number']}: map too detailed for the watch"
        data=struct.pack('<4hB',*pixel(h['tee']),*pixel(h['center']),len(shapes))
        data+=b''.join(struct.pack('<BHI',kind,len(ring),osm_id) for kind,ring,osm_id in shapes)
        data+=b''.join(struct.pack('<2h',*pixel(p)) for _,ring,_ in shapes for p in ring)
        h['mapHex']=data.hex()
    # Standard GeoJSON ([lon, lat]) for reuse outside the app; same ODbL terms.
    def geo(kind,coords,polygon=True,**props):
        coords=[[p[1],p[0]] for p in coords]
        return dict(type='Feature',properties=dict(golf=kind,**props),
            geometry=dict(type='Polygon',coordinates=[coords]) if polygon else dict(type='LineString',coordinates=coords))
    geojson=[]
    for h in holes:
        n=h['number']
        geojson.append(geo('hole',h['path'],False,ref=n,par=h['par'],osmId=h['osmHoleId']))
        geojson.append(geo('green',h['green'],hole=n,osmId=h['osmGreenId']))
        # ponytail: fairway cutouts aren't paired to their outer ring, so they're omitted here.
        geojson+=[geo('fairway',ring,hole=n) for ring in h['fairways']]+[geo('tee',ring,hole=n) for ring in h['tees']]
    geojson+=[geo('bunker' if f['kind']=='BUNKER' else 'water_hazard',f['polygon'],holes=f['holes'],osmId=f['osmId']) for f in features]
    (ROOT/'courses'/f'{folder.name}.geojson').write_text(json.dumps(dict(type='FeatureCollection',
        name=meta['course'],attribution='© OpenStreetMap contributors',license='ODbL-1.0',source=meta['osmUrl'],
        features=geojson))+'\n')
    # Fairways are drawn from mapHex only; keep them out of the phone bundle.
    for h in holes: del h['fairways'], h['fairwayCutouts'], h['tees']
    centers=[h['center'] for h in holes]
    course=dict(id=folder.name,name=meta['course'],short=meta['short'],city=meta['city'],
        center=[round(sum(c[0] for c in centers)/len(centers),6),round(sum(c[1] for c in centers)/len(centers),6)],
        tees=meta['tees'],source='© OpenStreetMap contributors',license='ODbL-1.0',sourceUrl=meta['osmUrl'],
        snapshotTimestamp=r.find('meta').get('osm_base') if r.find('meta') is not None else meta['verifiedDate'],
        target='Derived green centroid; not daily cup position',holes=holes)
    print(f"{folder.name}: {len(holes)} holes / {len(greens)} greens, par {sum(h['par'] for h in holes)}, "
          f"{sum(f['kind']=='BUNKER' for f in features)} bunkers, {sum(f['kind']=='WATER' for f in features)} water polygons")
    return course, meta.get('builtIn', False)

out=ROOT/'courses'; out.mkdir(exist_ok=True)
(out/'LICENSE.md').write_text((ROOT/'data/LICENSE.md').read_text())
index=[]
for folder in sorted((ROOT/'data/courses').iterdir()):
    if not (folder/'tees.json').exists(): continue
    course,built_in=build(folder)
    course['version']=hashlib.sha1(json.dumps(course,separators=(',',':')).encode()).hexdigest()[:12]
    text=json.dumps(course,separators=(',',':'))+'\n'
    (out/f"{course['id']}.json").write_text(text)
    if built_in: (ROOT/'src/pkjs/course.json').write_text(text)
    index.append({k:course[k] for k in ('id','name','short','city','center','version')})
(out/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(f"Wrote {len(index)} courses to {out.relative_to(ROOT)}/")
