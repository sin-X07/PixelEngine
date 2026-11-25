"""Texture and detail the existing menu skyline without moving its buildings."""
from collections import Counter
import math

import bmesh
import bpy
from mathutils import Vector
import numpy as np

from salt_market import box, mat, prism, rod


TARGETS = {
    'dust_2': ('Hill district', 'Distant shade recess', 'Cistern drum', 'Cistern crown'),
    'mirage': ('Far roofline', 'Distant cupola', 'Hostel roof block', 'Hostel parapet', 'Terracotta vent'),
    'ancient_ruins': ('Distant terrace', 'Distant roof comb'),
}
SOURCE_TILES = {
    'dust_2': {'wall':'plaster','trim':'chalk','recess':'timber','wood':'timber','roof':'clay','awning':'linen'},
    'mirage': {'wall':'stucco','trim':'ivory','recess':'bark','wood':'bark','roof':'tile','awning':'cloth'},
    'ancient_ruins': {'wall':'rock','trim':'edge','recess':'basalt','wood':'bark','roof':'lichen','awning':'lichen'},
}
BODY_NAMES = ('Hill district', 'Far roofline', 'Hostel roof block', 'Distant terrace')


def bounds(obj):
    corners = [obj.matrix_world @ Vector(v) for v in obj.bound_box]
    return (
        Vector([min(v[i] for v in corners) for i in range(3)]),
        Vector([max(v[i] for v in corners) for i in range(3)]),
    )


def inspect_background(theme):
    scene = bpy.context.scene
    prefix = scene.get('object_prefix', '')
    report = []
    for obj in sorted(scene.objects,key=lambda o:o.name):
        name = obj.name.removeprefix(prefix)
        if obj.type!='MESH':
            continue
        if not any(name.startswith(label) for label in TARGETS[theme]):
            # The middle-distance townhouses are part of the salt skyline.
            if theme!='dust_2' or not name.startswith(('Townhouse','Coping')) or bounds(obj)[0].y < 9:
                continue
        low, high = bounds(obj)
        distance = ((low+high)*.5 - Vector((0,-8,1.05))).length
        materials = [m for m in obj.data.materials if m]
        textured = any(n.type=='TEX_IMAGE' for m in materials if m.node_tree for n in m.node_tree.nodes)
        report.append((obj, {
            'name':obj.name, 'center_m':list((low+high)*.5), 'camera_distance_m':round(distance,2),
            'bounds_m':[list(low),list(high)], 'materials':[m.name for m in materials],
            'base_color_texture':textured, 'source_vertices':len(obj.data.vertices),
            'existing_bevel_m':obj.get('edge_width',0.0) or 0.0,
            'detail_gaps':['windows and frames','wall joints','eaves and parapet articulation']
                if any(name.split('.')[0] == n for n in BODY_NAMES) else ['surface variation'],
        }))
    return report


def baked_tile(name, source, role, theme):
    """Reuse courtyard paint and bake local heightfield occlusion into its trim tile."""
    node = next(n for n in source.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    texture = node.inputs['Base Color'].links[0].from_node
    image = texture.image
    w,h = image.size
    pixels = np.empty(w*h*4,dtype=np.float32)
    image.pixels.foreach_get(pixels)
    n = 512
    ix = np.arange(n)*w//n
    iy = np.arange(n)*h//n
    paint = pixels.reshape(h,w,4)[iy[:,None],ix[None,:],:3].copy()
    luma = paint @ np.array((.2126,.7152,.0722),dtype=np.float32)
    y, x = np.mgrid[0:n,0:n].astype(np.float32)/n
    stone = role in ('wall','trim','roof')
    grout = np.zeros_like(luma)
    if stone:
        rows = (4 if role=='wall' else 3) if theme=='ancient_ruins' else (5 if role=='roof' else 3)
        cols = 4 if role=='roof' else 2
        course = y*rows
        span = x*cols + .5*(np.floor(course)%2)
        dist_x = np.abs(np.mod(span+.5,1)-.5)/cols
        dist_y = np.abs(np.mod(course+.5,1)-.5)/rows
        grout = 1-np.clip(np.minimum(dist_x,dist_y)*130,0,1)
    elevation = (.035*luma - .033*grout) if stone else .018*luma
    horizon = np.zeros_like(luma)
    for dy,dx in ((0,1),(1,1),(1,0),(1,-1),(0,-1),(-1,-1),(-1,0),(-1,1)):
        slope = np.zeros_like(luma)
        for width in (2,4,8,16):
            adjacent = np.roll(elevation, (dy*width,dx*width), axis=(0,1))
            slope = np.maximum(slope, (adjacent-elevation)*n/(width*math.hypot(dx,dy)))
        horizon += np.arctan(slope)
    ao = np.clip(1-horizon*(.39/(math.pi*4)),.63,1)
    paint = paint*.76 + luma[:,:,None]*.24
    if role=='recess':
        paint *= .64
    elif role=='wall':
        paint = paint*.79 + np.array((.09,.085,.075),dtype=np.float32)
    elif role=='awning':
        paint *= (.89 + .11*np.cos(x*math.tau*6)**2)[:,:,None]
    paint *= (ao*(1-.18*grout))[:,:,None]
    target = bpy.data.images.new(name,width=n,height=n,alpha=False)
    rgba = np.ones((n,n,4),dtype=np.float32)
    rgba[:,:,:3] = np.clip(paint,0,1)
    target.pixels.foreach_set(rgba.ravel())
    target.pack()
    return target


def backdrop_materials(theme):
    prefix = bpy.context.scene.get('material_prefix','Salt | ')
    result = {}
    for role, source_key in SOURCE_TILES[theme].items():
        name = 'Backdrop '+role
        material = bpy.data.materials.new(prefix+name)
        material.use_nodes = True
        nodes = material.node_tree.nodes
        shader = next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
        shader.inputs['Metallic'].default_value = 0
        shader.inputs['Roughness'].default_value = .78 if role=='roof' else .91
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = baked_tile(prefix+name+' · baked surface',mat(source_key),role,theme)
        material.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])
        result[role] = name
    return result


def portal_outline(x,z,width,height,arched):
    if not arched:
        return [(x-width*.5,z),(x+width*.5,z),(x+width*.5,z+height),(x-width*.5,z+height)]
    radius=width*.5
    rise=height-radius
    return [(x-radius,z),(x+radius,z)]+[(x+radius*math.cos(math.pi*i/16),
            z+rise+radius*math.sin(math.pi*i/16)) for i in range(17)]


def cut_recesses(body, front, openings):
    vertices, faces = [], []
    for outline in openings:
        n = len(outline)
        start = len(vertices)
        vertices.extend((x,y,z) for y in (front-.13,front+.14) for x,z in outline)
        faces.extend([tuple(start+i for i in range(n)),tuple(start+n+i for i in range(n))])
        faces.extend((start+i,start+(i+1)%n,start+(i+1)%n+n,start+i+n) for i in range(n))
    data = bpy.data.meshes.new('Temporary facade recess cutter')
    data.from_pydata(vertices,[],faces)
    data.materials.append(body.data.materials[0])
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(data)
    bm.free()
    cutter = bpy.data.objects.new(data.name,data)
    bpy.context.scene.collection.objects.link(cutter)
    mod = body.modifiers.new('Inset window and doorway recesses','BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    mod.object = cutter
    with bpy.context.temp_override(object=body,active_object=body):
        assert bpy.ops.object.modifier_apply(modifier=mod.name)=={'FINISHED'}
    bpy.data.objects.remove(cutter,do_unlink=True)
    bpy.data.meshes.remove(data)
    material = body.data.materials[0]
    body.data.materials.clear()
    body.data.materials.append(material)
    for poly in body.data.polygons:
        poly.material_index = 0


def facade(theme, body, keys, index, add):
    low, high = bounds(body)
    x, center_y = (low.x+high.x)*.5, (low.y+high.y)*.5
    width, depth, height = high.x-low.x, high.y-low.y, high.z-low.z
    front = low.y
    if theme=='ancient_ruins':
        if height < .65 or width < 2.1:
            return
        add(box('Backdrop terrace stringcourse',x,front-.055,high.z-.115,width-.04,.19,.12,keys['trim'],.022))
        bays = 3 if width>4.1 else 2
        z = low.z+.18
        places = [(x+(j-(bays-1)*.5)*width/(bays+1),z,min(.67,width/(bays+1)*.61),
                   min(height-.37,.57),j%2==0) for j in range(bays)]
    else:
        add(box('Backdrop stone base',x,front-.048,low.z+.16,width-.02,.15,.31,keys['trim'],.026))
        add(box('Backdrop projecting eave',x,front-.084,high.z-.16,width+.12,.30,.16,keys['trim'],.036))
        add(box('Backdrop roof sill',x,center_y,high.z-.048,width+.13,depth+.12,.13,keys['roof'],.03))
        if index%3 != 1:
            for sign in (-1,1):
                add(box('Backdrop stepped parapet',x+sign*width*.355,front+.01,high.z+.095,
                        width*.30,.24,.24,keys['wall'],.026))
        if index%4 == 0 and height>3.3:
            cx=x+width*.29
            add(rod('Backdrop chimney',(cx,center_y+.28,high.z+.01),
                    (cx,center_y+.28,high.z+.47),.14,keys['roof'],24))
            add(rod('Backdrop chimney cap',(cx,center_y+.28,high.z+.44),
                    (cx,center_y+.28,high.z+.55),.18,keys['trim'],24))
        places = [(x,low.z+.34,min(.72,width*.34),min(1.38,height*.34),theme=='mirage')]
        for j in (-1,1):
            places.append((x+j*width*.236, low.z+height*.53, min(.62,width*.23),
                           min(.98,height*.26),theme=='mirage' or index%3==1))
        add(box('Backdrop upper floor course',x,front-.044,low.z+height*.44+.085,
                width-.13,.105,.10,keys['trim'],.018))
        if index%3==0:
            pts=[(x-width*.32,places[0][1]+places[0][3]+.39),
                 (x+width*.32,places[0][1]+places[0][3]+.39),
                 (x+width*.32,places[0][1]+places[0][3]+.28),
                 (x-width*.32,places[0][1]+places[0][3]+.28)]
            add(prism('Backdrop shade valance',pts,front-.32,front-.27,keys['awning'],.008))
            add(box('Backdrop awning roof',x,front-.15,pts[0][1]-.026,width*.62,.35,.055,keys['awning'],.008))
    outlines = [portal_outline(*place) for place in places]
    cut_recesses(body,front,outlines)
    for outline,(px,z,w,h,arched) in zip(outlines,places):
        add(prism('Backdrop shaded inset',outline,front+.118,front+.15,keys['recess'],.006))
        for side in (-1,1):
            abacus = h-w*.5 if arched else h
            add(box('Backdrop jamb',px+side*(w/2+.044),front-.021,z+abacus*.5,
                    .094,.11,abacus+.015,keys['trim'],.009))
        add(box('Backdrop deep sill',px,front-.073,z-.027,w+.19,.24,.095,keys['trim'],.01))
        if arched:
            inner = w*.5+.018
            outer = inner+.093
            spring = z+h-w*.5
            for j in range(16):
                a,b = math.pi*j/16+.012, math.pi*(j+1)/16-.012
                polygon = [(px+r*math.cos(t),spring+r*math.sin(t))
                           for r,ts in ((outer,(a,b)),(inner,(b,a))) for t in ts]
                add(prism('Backdrop arch voussoir',polygon,front-.084,front+.031,keys['trim'],.007))
        else:
            add(box('Backdrop header',px,front-.041,z+h+.036,w+.20,.155,.115,keys['trim'],.012))
        for j in (-1,0,1):
            add(box('Backdrop shutter spindle',px+j*w*.215,front+.075,z+h*.43,.038,.116,h*.73,keys['wood'],.006))
        add(box('Backdrop shutter transom',px,front+.060,z+.36*h,w*.87,.108,.058,keys['wood'],.006))


def cupola_details(body, keys):
    low, high = bounds(body)
    center = (low+high)*.5
    radius = (high.x-low.x)*.5
    return [
        rod('Backdrop cupola stone collar',(center.x,center.y,low.z+.008),
            (center.x,center.y,low.z+.10),radius+.035,keys['trim'],32),
        rod('Backdrop cupola finial',(center.x,center.y,high.z-.033),
            (center.x,center.y,high.z+.26),.074,keys['trim'],24,.029),
    ]


def surface(obj, role):
    data = obj.data
    angle = math.radians(30)
    if obj.get('edge_width') and not obj.name.startswith('Sunlit dust'):
        data.shade_smooth()
        data.set_sharp_from_angle(angle=angle)
        # Heritage geometry already has bevels. Finish newly authored parts here.
        if obj.get('backdrop_new'):
            bev = obj.modifiers.new('Backdrop rounded edges','BEVEL')
            bev.width = obj['edge_width']
            bev.segments = 3 if role=='wall' else 2
            bev.limit_method = 'ANGLE'
            bev.angle_limit = angle
            bev.miter_outer = 'MITER_ARC'
            bev.harden_normals = True
            bev.use_clamp_overlap = True
        wn = obj.modifiers.new('Backdrop area normals','WEIGHTED_NORMAL')
        wn.mode = 'FACE_AREA'
        wn.keep_sharp = True
        wn.weight = 50
    uv = data.uv_layers.active
    if uv is None:
        uv = data.uv_layers.new(name='Menu surface')
    uv.active_render = True
    for poly in data.polygons:
        normal = poly.normal
        axis = max(range(3),key=lambda a:abs(normal[a]))
        u, v = ((1,2),(0,2),(0,1))[axis]
        for loop in poly.loop_indices:
            co = obj.matrix_world @ data.vertices[data.loops[loop].vertex_index].co
            scale = 2.15 if role=='wall' else 1.32
            uv.data[loop].uv = (co[u]/scale, co[v]/scale)


def detail_background(theme):
    scene = bpy.context.scene
    if any(obj.get('backdrop_new') or obj.get('backdrop_role') for obj in scene.objects):
        raise ValueError("This scene already contains the backdrop detail pass")
    prefix = scene.get('object_prefix','')
    bpy.context.view_layer.update()
    audit = inspect_background(theme)
    assert audit
    keys = backdrop_materials(theme)
    changed = []
    def add(obj):
        obj['backdrop_new'] = True
        changed.append(obj)
    for index,(obj, row) in enumerate(audit):
        base = obj.name.removeprefix(prefix).split('.')[0]
        role = ('recess' if 'recess' in base.lower() else
                'roof' if any(term in base.lower() for term in ('cupola','cap','coping','parapet','crown','vent','roof comb')) else
                'wall')
        obj.data.materials[0] = mat(keys[role])
        obj['backdrop_role'] = role
        changed.append(obj)
        if base in BODY_NAMES or base=='Townhouse':
            facade(theme,obj,keys,index,add)
        elif base=='Distant cupola':
            for detail in cupola_details(obj,keys):
                add(detail)
    bpy.context.view_layer.update()
    for obj in changed:
        key = obj.data.materials[0].name.rsplit('Backdrop ',1)[-1]
        surface(obj,key)
    return {
        'source_audit':[row for _,row in audit], 'original_background_objects':len(audit),
        'detail_objects':sum(bool(o.get('backdrop_new')) for o in changed),
        'textured_roles':sorted(keys),
        'geometry_by_role':dict(Counter(o.data.materials[0].name.rsplit('Backdrop ',1)[-1] for o in changed)),
    }
