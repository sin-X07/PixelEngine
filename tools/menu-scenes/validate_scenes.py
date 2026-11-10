"""Check editable scenery against the full main-menu movement envelope."""
import hashlib
import json
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
rows = []
for theme in ('ancient_ruins', 'dust_2', 'mirage'):
    source = Path(__file__).parent/(theme+'.blend')
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    assert scene.render.fps == 30
    assert scene.frame_start == 0 and scene.frame_end == 120
    props = [o for o in scene.objects if o.type == 'MESH' and
             not o.name.startswith(('Continuous level courtyard','Courtyard paving'))]
    failures = []
    # Keep independently placed props clear of the whole abandoned-car assembly.
    car_names = ('Abandoned car','Car cabin','Dust covered glass','Door trim','Door handle',
                 'Flat tire','Wheel hub','Bumper','Bonnet','Broken headlamp','Chipped paint',
                 'Bent bonnet','Cracked side glass')
    car_parts = [o for o in props if o.name.startswith(car_names)]
    if car_parts:
        car_corners = [o.matrix_world @ Vector(v) for o in car_parts for v in o.bound_box]
        car_low = [min(v[i] for v in car_corners) for i in range(3)]
        car_high = [max(v[i] for v in car_corners) for i in range(3)]
        for obj in props:
            if not obj.name.startswith(('Shipping crate','Awning support','Tree trunk',
                                        'Weathered barrel','Terracotta planter')):
                continue
            corners = [obj.matrix_world @ Vector(v) for v in obj.bound_box]
            low = [min(v[i] for v in corners) for i in range(3)]
            high = [max(v[i] for v in corners) for i in range(3)]
            if all(low[i] < car_high[i] and high[i] > car_low[i] for i in range(3)):
                failures.append(dict(object=obj.name,reason='Independent prop overlaps car bounds'))
    for frame in range(121):
        scene.frame_set(frame)
        for obj in props:
            corners = [obj.matrix_world @ Vector(v) for v in obj.bound_box]
            # Blender axes map to engine X, -Z, Y. The arena includes margin
            # beyond all swept chicken radii and both armed character positions.
            low = [min(v[i] for v in corners) for i in range(3)]
            high = [max(v[i] for v in corners) for i in range(3)]
            overlap = low[0] < 6.3 and high[0] > -6.3 and -low[1] > -2.7 and -high[1] < 6.1
            if overlap and high[2] > 0 and low[2] < 6.5:
                failures.append(dict(object=obj.name,frame=frame,minimum=low,maximum=high))
    asset = ROOT/'assets/models/menu/scenes'/(theme+'.glb')
    rows.append(dict(theme=theme,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                     asset_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),
                     props=len(props),frames=121,findings=failures))
    print('SCENE CLEARANCE',theme,'props',len(props),'findings',len(failures),flush=True)
output = ROOT/'build-evidence/menu-scenes/clearance.json'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(rows,indent=2),encoding='utf-8')
assert not any(row['findings'] for row in rows), 'Scenery enters the protected arena'
