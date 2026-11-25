"""Author the original Salt Exchange courtyard. Coordinates are Blender X/right, Y/depth, Z/up."""
import math
import json
import struct
import subprocess
import tempfile
from pathlib import Path

import bpy
import surface_finish
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'build-evidence/pix39/salt-exchange'
SCENE = 'Salt Exchange'
TAU = math.tau

PALETTE = {
    'chalk': (.74,.64,.49), 'plaster': (.61,.44,.28), 'clay': (.47,.26,.15),
    'sand': (.58,.45,.30), 'stone': (.50,.42,.32), 'deep': (.066,.081,.084),
    'gate': (.047,.235,.19), 'brass': (.65,.43,.19), 'iron': (.14,.16,.15),
    'timber': (.40,.25,.13), 'rust': (.51,.19,.09), 'linen': (.77,.64,.42),
    'stripe': (.39,.17,.115), 'leaves': (.20,.33,.17), 'sage': (.34,.42,.25),
    'glass': (.16,.27,.28), 'haze': (.41,.40,.37), 'ochre': (.70,.48,.26),
    'shadow': (.31,.27,.23), 'paper': (.81,.74,.56), 'slate': (.24,.31,.32)
}


def scene():
    return bpy.context.scene


def mat(key):
    return bpy.data.materials[scene().get('material_prefix', 'Salt | ')+key]


def mesh(name, verts, faces, key, bevel=None, curved=False):
    name = scene().get('object_prefix', '') + name
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(data)
    bm.free()
    data.materials.append(mat(key))
    obj = bpy.data.objects.new(name, data)
    scene().collection.objects.link(obj)
    if bevel is not None:
        obj["edge_width"]=bevel
        obj["round_surface"]=curved
    return obj


def prism(name, outline, front, back, key, bevel=0):
    n = len(outline)
    verts = [(x,y,z) for y in (front,back) for x,z in outline]
    faces = [tuple(range(n)),tuple(range(n,2*n))]
    faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
    return mesh(name,verts,faces,key,bevel)


def box(name,x,y,z,w,d,h,key,bevel=.015):
    return prism(name,[(x-w/2,z-h/2),(x+w/2,z-h/2),(x+w/2,z+h/2),(x-w/2,z+h/2)],
                 y-d/2,y+d/2,key,bevel)


def rod(name,a,b,r,key,n=8,r2=None):
    n=max(96 if r>=.75 else 48 if r>=.25 else 24,n) if n!=4 else n
    a,b=Vector(a),Vector(b)
    axis=(b-a).normalized()
    u=axis.orthogonal().normalized()
    v=axis.cross(u)
    verts=[tuple(p+(u*math.cos(TAU*i/n)+v*math.sin(TAU*i/n))*radius)
           for p,radius in ((a,r),(b,r if r2 is None else r2)) for i in range(n)]
    obj=mesh(name,verts,[tuple(range(n)),tuple(range(n,2*n))]+
                [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],key,0,n>4)
    if n>4:
        obj["lathe_origin"]=tuple(a)
        obj["lathe_axis"]=tuple(axis)
    return obj


def arch(name,x,y,r,s,key,count=15):
    for i in range(count):
        a,b=math.pi*i/count+.009, math.pi*(i+1)/count-.009
        outer=r+(.34 if i==count//2 else .26)
        ts=[a+(b-a)*j/3 for j in range(4)]
        poly=[(x+k*math.cos(t),s+k*math.sin(t)) for k,angles in ((outer,ts),(r,reversed(ts))) for t in angles]
        prism(name,poly,y-.13,y+.22,key,.012)


def camera(name,eye,look):
    d=bpy.data.cameras.new(name)
    d.sensor_fit='VERTICAL'
    d.lens=d.sensor_height/(2*math.tan(math.radians(28)))
    o=bpy.data.objects.new(name,d)
    scene().collection.objects.link(o)
    o.location=eye
    o.rotation_euler=(Vector(look)-Vector(eye)).to_track_quat('-Z','Y').to_euler()
    return o


def init():
    # Use a separate scene so an artist's other open scene is never cleared.
    old=bpy.data.scenes.get(SCENE)
    if old:
        assert old.get('pix39_authoring')
        for obj in list(old.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
        for data in list(bpy.data.meshes):
            if data.users == 0 and any(m and m.name.startswith('Salt | ') for m in data.materials):
                bpy.data.meshes.remove(data)
        bpy.context.window.scene=old
    else:
        bpy.context.window.scene=bpy.data.scenes.new(SCENE)
    s=scene()
    s['pix39_authoring']=True
    s.render.fps=30
    s.frame_start,s.frame_end=0,120
    s.render.resolution_x,s.render.resolution_y=1280,720
    s.render.resolution_percentage=100
    s.world=bpy.data.worlds.new('Salt | sky')
    s.world.color=(.23,.25,.28)
    for key,color in PALETTE.items():
        m=bpy.data.materials.get('Salt | '+key) or bpy.data.materials.new('Salt | '+key)
        m.diffuse_color=(*color,1)
        m.use_nodes=True
        for node in list(m.node_tree.nodes):
            if node.type=='TEX_IMAGE': m.node_tree.nodes.remove(node)
        shader=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        shader.inputs['Base Color'].default_value=(*color,1)
        shader.inputs['Roughness'].default_value=.48 if key in ('gate','brass','glass') else .87
    s.camera=camera('Street camera',(0,-8,1.05),(0,0,.95))
    EVIDENCE.mkdir(parents=True,exist_ok=True)


def layout():
    box('Continuous level courtyard',0,0,-.12,40,48,.20,'sand',0)
    # Twin-leaf portal under a stepped crown, flanked by a cistern and an open stair.
    for side in (-1,1):
        x=2.6+side*1.85
        box('Portal cheek',x,6.35,1.97,.81,1.04,3.94,'plaster',.045)
        box('Plinth',x,6.31,.301,.94,1.15,.638,'stone',.035)
        box('Spring course',x,6.29,2.83,1.04,1.18,.21,'chalk')
    for i in range(15):
        x=1.19+i*.201
        top=2.82+math.sqrt(max(0,1.48**2-(x-2.6)**2))
        gap=.084 if i==7 else .012
        box('Hammered gate leaf',x,6.49,top/2,.201-gap,.16,top,'gate',.012)
    arch('Portal archivolt',2.6,6.12,1.49,2.8,'chalk',19)
    for i in range(17):
        dx=(i-8)*.27
        low=2.81+math.sqrt(max(0,1.75**2-dx**2)) if abs(dx)<1.75 else 3.89
        high=4.84+(.23 if abs(i-8)<6 else 0)+(.18 if abs(i-8)<3 else 0)
        box('Stepped parapet',2.6+dx,6.55,(low+high)/2,.271,.71,high-low,'plaster',.004)
    buildings=[(-9.15,5.45,3.65,3.2,5.55,'clay'),(-5.96,7.95,2.74,2.5,4.48,'plaster'),
               (6.91,8.0,3.58,3.15,4.72,'plaster'),(10.1,6.0,2.7,3.0,5.92,'chalk')]
    for x,y,w,d,h,k in buildings:
        box('Townhouse',x,y,h/2,w,d,h,k,.06)
        box('Coping',x,y,h,w+.2,d+.2,.17,'chalk')
    for i in range(11):
        h=(i+1)*.154
        box('Limestone stair',-2.48-i*.31,4.93,h/2-.004,.314,1.55,h,'stone',.018)
    box('Raised dye market',-6.41,5.10,.85,1.79,2.0,1.7,'plaster')
    arch('Market vault',-6.2,6.55,.81,2.48,'chalk',11)
    for side in (-1,1):
        box('Market pier',-6.2+side*.94,6.59,1.27,.30,.49,2.58,'chalk')
    box('Vault interior',-6.2,6.85,2.28,1.6,.1,2.25,'deep')
    for i in range(12):
        x=-18+i*3.05
        h=4.5+1.16*math.sin(2.13*i)+.19*i
        y=13.6+1.5*math.cos(1.82*i)
        box('Hill district',x,y,h/2,2.9,2.6,h,'haze')
        box('Hill district coping',x,y,h,3.0,2.77,.13,'stone')
    rod('Cistern drum',(5.7,11,4.7),(5.7,11,6.48),1.18,'clay',20)
    rod('Cistern crown',(5.7,11,6.48),(5.7,11,7.52),1.24,'sage',20,.18)
    box('Cargo proxy',-.80,4.68,.66,1.55,1.22,1.32,'timber')
    box('Salt sacks proxy',6.02,3.02,.43,2.48,1,.86,'linen')


def viewport():
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.color_type='MATERIAL'
            area.spaces.active.overlay.show_overlays=False


def whitebox():
    init()
    layout()
    s=scene()
    viewport()
    s.render.engine='BLENDER_WORKBENCH'
    s.display.shading.color_type='SINGLE'
    s.display.shading.show_shadows=True
    s.display.shading.show_cavity=True
    s.render.filepath=str(EVIDENCE/'whitebox.png')
    bpy.ops.render.render(write_still=True)
    print('Whitebox:',len(s.objects),'objects')


def label(text,x,y,z,size,key):
    curve=bpy.data.curves.new(text,'FONT')
    curve.body=text
    curve.size=size
    curve.align_x='CENTER'
    curve.align_y='CENTER'
    curve.space_character=1.12
    curve.resolution_u=3
    obj=bpy.data.objects.new(text,curve)
    scene().collection.objects.link(obj)
    obj.rotation_euler.x=math.pi/2
    obj.location=(x,y,z)
    bpy.context.view_layer.update()
    data=bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    sign=bpy.data.objects.new(text,data)
    scene().collection.objects.link(sign)
    sign.matrix_world=obj.matrix_world.copy()
    data.materials.append(mat(key))
    bpy.data.objects.remove(obj,do_unlink=True)


def export(path, *, legacy_limits=True):
    s=scene()
    bpy.context.view_layer.update()
    static=sorted((o for o in s.objects if o.type=='MESH' and not o.animation_data),key=lambda o:o.name)
    bpy.ops.object.select_all(action='DESELECT')
    # Join disposable copies, leaving every editable piece in the source scene.
    depsgraph=bpy.context.evaluated_depsgraph_get()
    geometry=[bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),
              depsgraph=depsgraph,preserve_all_data_layers=True) if obj.modifiers else obj.data.copy()
              for obj in static]
    copies=[]
    for obj,data in zip(static,geometry):
        cp=obj.copy()
        cp.data=data
        cp.modifiers.clear()
        s.collection.objects.link(cp)
        cp.select_set(True)
        copies.append(cp)
    # The active UV drives all current texture nodes. Give it a common name so
    # joining procedural objects does not allocate empty UV channels for each naming convention.
    for cp in copies:
        if not any(n.type=='TEX_IMAGE' for m in cp.data.materials if m and m.node_tree for n in m.node_tree.nodes):
            for uv in list(cp.data.uv_layers):
                cp.data.uv_layers.remove(uv)
        if cp.data.uv_layers.active and len(cp.data.uv_layers)==1:
            cp.data.uv_layers.active.name='Menu surface'
    merged=copies[0]
    merged.name=s.get('static_object', 'Exchange masonry and furnishings')
    bpy.context.view_layer.objects.active=merged
    disposable_meshes = [cp.data for cp in copies]
    bpy.ops.object.join()
    merged.data.name=s.get('static_geometry', 'Salt Exchange static geometry')
    for obj in s.objects:
        if obj.animation_data:
            obj.select_set(True)
    try:
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,use_active_scene=True,
            export_cameras=False,export_lights=False,export_animations=True,
            export_animation_mode='NLA_TRACKS',export_merge_animation='NLA_TRACK',
            export_force_sampling=True,export_extras=False)
    finally:
        bpy.data.objects.remove(merged,do_unlink=True)
        for cp_mesh in disposable_meshes:
            if cp_mesh.users == 0:
                bpy.data.meshes.remove(cp_mesh)
    data=path.read_bytes()
    doc=json.loads(data[20:20+struct.unpack_from('<I',data,12)[0]])
    # Blender's sampled quaternion components can differ in the last float bits
    # between a live viewport and a headless depsgraph. Quantize that noise only.
    binary=bytearray(data)
    start=28+struct.unpack_from('<I',data,12)[0]
    for clip in doc.get('animations',[]):
        for sampler in clip['samplers']:
            accessor=doc['accessors'][sampler['output']]
            if accessor['type']!='VEC4': continue
            view=doc['bufferViews'][accessor['bufferView']]
            at=start+view.get('byteOffset',0)+accessor.get('byteOffset',0)
            for j in range(accessor['count']*4):
                value=struct.unpack_from('<f',binary,at+4*j)[0]
                struct.pack_into('<f',binary,at+4*j,round(value,6) or 0.0)
    path.write_bytes(binary)
    vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count']
                 for m in doc['meshes'] for p in m['primitives'])
    if legacy_limits:
        assert vertices<=150000 and len(data)<=8_000_000 and len(doc['materials'])<=24,(vertices,len(data),len(doc['materials']))
    print(path.name,vertices,'vertices,',len(data),'bytes,',len(doc['materials']),'materials')
    return doc


def gate_detail():
    for side in (-1,1):
        x=2.6+side*.775
        for h in (.26,1.13,2.42,2.80):
            box('Riveted strap',x,6.382,h,1.37,.074,.093,'iron')
            for j in range(5):
                px=x+(j-2)*.288
                rod('Forged clinch',(px,6.348,h),(px,6.301,h),.028,'brass',8,.019)
        box('Latch escutcheon',2.6+side*.225,6.355,1.57,.19,.12,.33,'brass',.025)
        cx=2.6+side*.225
        for j in range(10):
            a,b=TAU*j/10,TAU*(j+1)/10
            # Keep each forged segment and its endpoints, with four curved spans per piece.
            points=[]
            for step in range(5):
                t=a+(b-a)*step/4
                for k in range(24):
                    u=TAU*k/24
                    points.append((cx+(.083+.019*math.cos(u))*math.cos(t),
                                   6.255+.019*math.sin(u),1.51+(.107+.019*math.cos(u))*math.sin(t)))
            faces=[tuple(range(24)),tuple(range(96,120))]
            faces.extend((24*h+k,24*h+(k+1)%24,24*(h+1)+(k+1)%24,24*(h+1)+k)
                         for h in range(4) for k in range(24))
            mesh('Ring pull',points,faces,'iron',0,True)
        for h in (.85,2.17):
            rod('Hinge barrel',(2.6+side*1.42,6.29,h),(2.6+side*1.42,6.29,h+.27),.047,'brass')
    box('Enamel exchange cartouche',2.6,6.019,4.83,2.72,.084,.40,'gate',.024)
    label('SALT EXCHANGE',2.6,5.9755,4.84,.245,'paper')
    for i in range(23):
        x=.48+i*.191
        box('Dentil',x,6.107,4.63,.11,.19,.088,'chalk')
    for i in range(9):
        dx=(i-4)*.155
        box('Fanlight grille',2.6+dx,6.385,3.56,.027,.065,.48-.52*abs(dx),'brass',.006)
    for x in (.75,4.45):
        box('Tide gauge tablet',x,5.79,1.58,.37,.045,.91,'deep')
        for i in range(8):
            box('Gauge gradation',x-.05,5.763,1.24+i*.095,.10+.07*(i%2),.015,.018,'chalk',0)


def cargo(x,y,w,d,h,base=0):
    n=6
    for i in range(n):
        px=x+(i+.5-n/2)*w/n
        foot=.022 if base==0 else 0
        box('Staved cedar case',px,y,base+(h-foot)/2,w/n-.012,d,h+foot,'timber',.014)
    for px in (x-w*.39,x+w*.39):
        for yy in (y-d/2-.021,y+d/2+.021):
            box('Case batten',px,yy,base+h/2,.105,.06,h-.016,'ochre')
            for hh in (.085,h-.085):
                rod('Case nail',(px,yy-.024,base+hh),(px,yy-.06,base+hh),.017,'iron')
        box('Case top batten',px,y,base+h+.019,.105,d+.08,.053,'ochre')
    rod('Shipping brace',(x-w*.43,y-d*.5-.04,base+.11),(x+w*.43,y-d*.5-.04,base+h-.11),.054,'timber',4)
    label('S  /  07',x,y-d/2-.0015,base+h*.64,min(.15,w*.12),'paper')


def dock_detail():
    for obj in list(scene().objects):
        if obj.name.startswith(('Cargo proxy','Salt sacks proxy')):
            bpy.data.objects.remove(obj,do_unlink=True)
    cargo(-1.10,4.97,1.64,1.18,.95)
    cargo(-.82,5.04,.92,.83,.69,.99)
    cargo(-2.0,5.95,.84,.9,.68)
    for i in (0,3,6,10):
        x=-2.48-i*.31
        h=(i+1)*.154
        rod('Oxide stair standard',(x,4.15,h),(x,4.15,h+.67),.032,'rust')
    for h in (.35,.81):
        rod('Market handrail',(-2.44,4.15,h),(-5.63,4.15,h+1.57),.037,'iron')
    for i in range(11):
        box('Worn tread nose',-2.34-i*.31,4.93,(i+1)*.154+.004,.078,1.55,.025,'chalk',.006)
    for layer,n in enumerate((5,4,3)):
        for i in range(n):
            x=6.42+(i-(n-1)/2)*.56
            y=3.23+.18*(x-6.42)**2
            z=.155+layer*.252
            verts=[]
            sides=24
            for j,scale in enumerate((.20,.81,1,.85,.21)):
                for k in range(sides):
                    a=TAU*k/sides
                    verts.append((x+(j-2)*.155,y+math.cos(a)*.32*scale,
                                  z+math.sin(a)*.163*scale+.023*math.cos(2*a)))
            faces=[tuple(range(sides)),tuple(range(4*sides,5*sides))]+[(j*sides+k,j*sides+(k+1)%sides,(j+1)*sides+(k+1)%sides,(j+1)*sides+k) for j in range(4) for k in range(sides)]
            mesh('Tied salt sack',verts,faces,'linen',0,True)
            rod('Jute knot',(x+.296,y,z),(x+.334,y+.025,z+.02),.044,'timber',6)
    for x,y in ((6.96,3.98),(7.62,4.27)):
        rod('Oil drum',(x,y,.015),(x,y,.92),.32,'rust',18,.305)
        for z in (.07,.26,.65,.91):
            rod('Barrel hoop',(x,y,z),(x,y,z+.035),.333,'iron',18)


def facade():
    for x,y,w,h in ((-9.15,3.85,3.65,5.55),(-5.96,6.70,2.74,4.48),(6.91,6.42,3.58,4.72),(10.1,4.50,2.7,5.92)):
        for k in range(3):
            px=x+(k-1)*w*.27
            z=h-1.13
            box('Window reveal',px,y-.025,z,.68,.14,1.20,'deep',.035)
            box('Drip sill',px,y-.10,z-.63,.83,.30,.12,'chalk')
            if (k+int(abs(x)))%3==0:
                for j in range(8):
                    box('Cedar louvre',px,y-.12,z-.46+j*.126,.58,.083,.066,'timber')
            elif (k+int(abs(x)))%3==1:
                for dx in (-.18,.12):
                    rod('Shutter plank',(px+dx-.09,y-.12,z-.51),(px+dx+.14,y-.12,z+.5),.087,'timber',4)
            else:
                prism('Broken blue glass',[(px-.28,z+.49),(px+.28,z+.49),(px+.28,z+.01),(px+.11,z-.15),(px-.04,z+.11),(px-.28,z-.27)],y-.115,y-.093,'glass')
                box('Casement mullion',px,y-.130,z,.032,.04,1.07,'iron')
        box('Balcony slab',x,y-.24,h-1.93,w*.77,.65,.15,'stone')
        for i in range(9):
            px=x+(i-4)*w*.086
            rod('Balustrade',(px,y-.53,h-1.89),(px,y-.53,h-1.30),.022,'iron',6)
        rod('Balcony rail',(x-w*.35,y-.53,h-1.3),(x+w*.35,y-.53,h-1.3),.038,'iron')
        rod('Flue',(x+.69,y+.72,h),(x+.69,y+.72,h+.64),.12,'clay',10)
        rod('Flue cowl',(x+.69,y+.72,h+.64),(x+.69,y+.72,h+.72),.17,'stone',10)
    # A striped sail over the street dyer's stall with a scalloped hem.
    for i in range(21):
        x=-10.68+i*.153
        k='linen' if i%5<3 else 'stripe'
        profile=[(3.77,3.32),(3.23,3.15),(2.72,2.98),(2.22,2.94),(2.22,2.80)]
        verts=[(x+dx,y,z) for y,z in profile for dx in (0,.154)]
        mesh('Woven awning stripe',verts,[(j*2,j*2+1,j*2+3,j*2+2) for j in range(4)],k)
    for x in (-10.65,-7.51):
        rod('Awning stay',(x,3.78,2.33),(x,2.23,2.929),.030,'rust')
    box('Dyer fascia',-9.12,3.70,2.13,2.86,.13,.46,'stripe')
    label('INDIGO   /   DYES',-9.12,3.6335,2.14,.217,'paper')
    for x in (-9.91,-9.09,-8.26):
        box('Stall recess',x,3.73,1.04,.73,.11,1.57,'deep')
        for i in range(4):
            box('Shutter slat',x,3.63,.42+i*.34,.655,.13,.27,'timber')
    for x,y,z in ((.96,5.77,1.92),(6.32,6.36,1.4),(-5.07,6.63,1.89)):
        box('Pasted market notice',x,y,z,.34,.018,.49,'paper',0)
        for j in range(4):
            box('Letterpress lines',x,y-.015,z+.13-j*.061,.21-.03*(j%3),.009,.015,'rust',0)


def wind(obj, origin, phase):
    # Pivot at the attachment point and keep the authored rest shape in local space.
    for v in obj.data.vertices:
        v.co-=Vector(origin)
    obj.location=origin
    for frame in range(0,121,10):
        obj.rotation_euler.y=.038*math.sin(TAU*frame/120+phase)
        obj.keyframe_insert(data_path='rotation_euler',frame=frame)
    action=obj.animation_data.action
    track=obj.animation_data.nla_tracks.new()
    track.name='Menu_Environment'
    strip=track.strips.new('Menu_Environment',0,action)
    strip.action_slot=action.slots[0]
    obj.animation_data.action=None


def palm(x,y,h):
    for j in range(12):
        t=j/12
        rod('Palm bark course',(x+t*t*.36,y,t*h),(x+((j+1)/12)**2*.36,y,(j+1)*h/12),
            .19-.055*t,'timber',10,.148-.055*t)
    start=Vector((x+.36,y,h))
    for j in range(8):
        a=j*TAU/8+x
        along=Vector((math.cos(a),math.sin(a),0))
        side=Vector((-along.y,along.x,0))
        verts=[]
        faces=[]
        for i in range(12):
            t=i/12
            center=start+along*(2.04*t)+Vector((0,0,.56*math.sin(math.pi*t)-.70*t*t))
            end=center+.21*along+Vector((0,0,.12-.28*t))
            ridge=len(verts)
            verts.extend([tuple(center-side*.026),tuple(center+side*.026),
                          tuple(end+side*.014),tuple(end-side*.014)])
            faces.append(tuple(range(ridge,ridge+4)))
            for sign in (-1,1):
                tip=center+sign*side*(.03+.36*math.sin(math.pi*(t+.035)))+.24*along-Vector((0,0,.075))
                n=len(verts)
                verts.extend([tuple(center-.055*along),tuple(end+.035*along),tuple(tip)])
                faces.append((n,n+1,n+2))
        obj=mesh('Wind | date frond',verts,faces,'sage' if j%3==0 else 'leaves')
        wind(obj,start,a)


def truck():
    x,y=-7.88,2.89
    sections=[(-1.54,.42,.60,.47),(-1.38,.29,.72,.58),(-.92,.27,.76,.62),
              (-.60,.26,1.21,.61),(-.34,.26,1.31,.61),(.32,.26,1.29,.61),
              (.72,.26,.79,.60),(1.32,.29,.72,.57),(1.48,.39,.58,.48)]
    verts=[]
    for dx,bottom,top,w in sections:
        verts.extend([(x+dx,y+v,z) for v,z in [(-w+.09,bottom),(-w,bottom+(top-bottom)*.28),
            (-w,.68 if top>.8 else top-.08),(-w*.76,top),(w*.76,top),(w,min(.68,top-.08)),
            (w,bottom+(top-bottom)*.28),(w-.09,bottom)]])
    faces=[tuple(range(8)),tuple(range(len(verts)-8,len(verts)))]+[(j*8+k,j*8+(k+1)%8,
           (j+1)*8+(k+1)%8,(j+1)*8+k) for j in range(len(sections)-1) for k in range(8)]
    body=mesh('Derelict courier estate',verts,faces,'slate',.007)
    for side in (-1,1):
        for lo,hi in ((-.78,-.16),(-.09,.55)):
            glass=mesh('Dusty side glazing',[(x+lo,y+side*.615,.81),(x+hi,y+side*.615,.81),
                (x+hi-.13,y+side*.50,1.18),(x+lo+.14,y+side*.50,1.20)],[(0,1,2,3)],'glass')
            for vertex in glass.data.vertices:
                hit,pos,normal,_=body.ray_cast((vertex.co.x,y+side*2,vertex.co.z),(0,-side,0))
                if hit:
                    vertex.co=pos+normal*.0018
        for axle in (-.99,.95):
            rod('Road tyre',(x+axle,y+side*.48,.31),(x+axle,y+side*.68,.31),.32,'deep',18)
            rod('Pressed steel rim',(x+axle,y+side*.67,.31),(x+axle,y+side*.70,.31),.184,'iron',14)
        box('Door rub strip',x,y+side*.619,.66,1.42,.035,.043,'rust')
    for end in (-1,1):
        box('Chrome bumper',x+end*1.46,y,.39,.12,1.09,.13,'iron')
    box('Plate',x+1.518,y,.44,.016,.43,.13,'paper',.004)
    for j in range(7):
        dx=j*.191
        flake=prism('Flaked sill',[(x-.78+dx,.44),(x-.70+dx,.52+.04*math.sin(j)),(x-.58+dx,.43)],
              y-.620,y-.597,'rust')
        for vertex in flake.data.vertices:
            hit,pos,normal,_=body.ray_cast((vertex.co.x,y-2,vertex.co.z),(0,1,0))
            if hit:
                depth=.006 if vertex.co.y<y-.61 else .0017
                vertex.co=pos+normal*depth


def street():
    truck()
    palm(-11.17,3.35,6.27)
    palm(9.27,3.99,6.81)
    for x,y,h in ((-7.10,3.77,5.48),(8.92,4.20,6.29)):
        rod('Utility pole',(x,y,0),(x+.08,y,h),.105,'timber',9,.075)
        box('Crossarm',x,y,h-.13,.12,.82,.10,'iron')
    for j in range(3):
        def along(t):
            return (-7.02+16.02*t,3.53+j*.24+.43*t,5.35+.81*t-(1.02+j*.12)*4*t*(1-t))
        for i in range(16):
            rod('Sagging power line',along(i/16),along((i+1)/16),.014,'deep',5)
    for i in range(2):
        x=-4.72+i*.60
        verts=[(x+u*.49,6.20+.08*math.sin(u*3),3.87-v*(.75+.09*math.sin(u*5)))
               for v in (0,1) for u in (0,.25,.5,.75,1)]
        obj=mesh('Wind | dyer swatch',verts,[(j,j+1,j+6,j+5) for j in range(4)],'stripe' if i else 'linen')
        wind(obj,(x,6.2,3.87),i*1.57)
    rod('Clothesline',(-5.2,6.2,3.88),(-3.40,6.2,3.88),.015,'iron',5)
    for x in (-5.20,-3.4):
        rod('Drying rack',(x,6.2,0),(x,6.2,3.90),.037,'timber')
    rod('Street lamp',(4.92,5.78,0),(4.92,5.78,3.00),.066,'iron')
    rod('Lamp bracket',(4.92,5.78,2.98),(4.61,5.57,3.12),.038,'iron')
    rod('Storm lantern',(4.61,5.57,2.75),(4.61,5.57,3.07),.123,'linen',6,.16)
    rod('Lantern hood',(4.61,5.57,3.08),(4.61,5.57,3.19),.197,'iron',6,.045)
    rod('Refuse bin',(7.77,5.56,.02),(7.77,5.56,.86),.26,'iron',12,.31)
    rod('Refuse lid',(7.77,5.56,.86),(7.77,5.56,.94),.33,'sage',12,.24)
    for i in range(42):
        x=math.sin(i*73.47)*10
        y=3.22+abs(math.sin(i*11.3))*5
        rod('Loose limestone',(x,y,-.019),(x+.024,y,.065+.035*math.sin(i)),.095,'stone',5,.046)
    # Flush paving follows the old salt-cart route into the square.
    for row in range(17):
        for col in range(5):
            x=-.68+(col-2)*.85+.39*(row%2)+.13*row
            y=-7+row*.82
            box('Courtyard paving',x,y,-.013,.82,.797,.026,'sand',.006)
    scene().frame_set(0)
    viewport()


def light_rig():
    s=scene()
    for name,kind,loc,power,color in [('Late sun','SUN',(-11.2,-.5,7.1),3.35,(1,.79,.55)),
                                      ('Courtyard skylight','AREA',(-4,-4,10),1520,(.72,.83,1))]:
        data=bpy.data.lights.new(name,kind)
        data.energy=power
        data.color=color
        if kind=='AREA':
            data.shape='DISK'
            data.size=12
        else:
            data.angle=.031
        o=bpy.data.objects.new(name,data)
        s.collection.objects.link(o)
        o.location=loc
        o.rotation_euler=(Vector((0,5,0))-Vector(loc)).to_track_quat('-Z','Y').to_euler()
    s.render.engine='CYCLES'
    s.cycles.samples=12
    s.cycles.use_denoising=True


def occluders():
    from mathutils.bvhtree import BVHTree
    verts,faces=[],[]
    bpy.context.view_layer.update()
    for obj in scene().objects:
        if obj.type!='MESH' or obj.name.startswith(('Continuous level','Courtyard paving')):
            continue
        offset=len(verts)
        verts.extend(obj.matrix_world @ v.co for v in obj.data.vertices)
        faces.extend([offset+i for i in f.vertices] for f in obj.data.polygons)
    return BVHTree.FromPolygons(verts,faces)


def ground_shadows(size):
    import numpy as np
    tree=occluders()
    sun=Vector((-1.12,-.55,.71)).normalized()
    mask=np.array([[tree.ray_cast((24*(i+.5)/size-12,24*(j+.5)/size-9,.012),sun,32)[0] is not None
                    for i in range(size)] for j in range(size)],dtype=np.float32)
    return (mask*4+np.roll(mask,1,0)+np.roll(mask,-1,0)+np.roll(mask,1,1)+np.roll(mask,-1,1))/8


def paint():
    import numpy as np
    # Tileable value noise with integer lattice hashing, independent of gameplay RNG.
    def noise(u,v,n,m,seed):
        px,py=u*n,v*m
        ix,iy=np.floor(px).astype(np.int64),np.floor(py).astype(np.int64)
        fx,fy=px-ix,py-iy
        fx,fy=fx*fx*(3-2*fx),fy*fy*(3-2*fy)
        def h(a,b):
            k=(a%n)*374761393+(b%m)*668265263+seed*1779033703
            k=((k^(k>>13))*1274126177)&0xffffffff
            return (k^(k>>16))/4294967295
        return ((1-fx)*h(ix,iy)+fx*h(ix+1,iy))*(1-fy)+((1-fx)*h(ix,iy+1)+fx*h(ix+1,iy+1))*fy
    keys=('sand','timber','gate','plaster','clay','chalk','linen','rust')
    for key in keys:
        n=1024 if key=='sand' else 384
        v,u=np.mgrid[0:n,0:n].astype(np.float32)/n
        broad=noise(u,v,7,7,1)
        grain=noise(u,v,n//2,n//2,17)
        medium=noise(u,v,43,36,3)
        patina=.9+.12*(broad-.5)+.12*(medium-.5)+.14*(grain-.5)
        # Encoded sRGB albedo for the fixed-pipeline modulator.
        palette={'sand':(.79,.67,.51),'timber':(.61,.42,.263),'gate':(.19,.46,.36),
                 'plaster':(.80,.67,.49),'clay':(.71,.495,.35),'chalk':(.87,.78,.61),
                 'linen':(.75,.65,.46),'rust':(.63,.32,.18)}
        rgb=np.array(palette[key])[None,None,:]*patina[:,:,None]
        if key=='sand':
            x,y=u*24-12,v*24-9
            rows=np.floor((y+7.41)/.82)
            edge=np.abs((x-(-.68+.39*(rows%2)+.13*rows))/.85)
            paving=(rows>=0)&(rows<17)&(edge<2.49)
            joints=np.minimum(np.abs(((y+7.41)/.82+.5)%1-.5),
                              np.abs((edge%1)-.5))
            slabs=noise((x+12)/24,(y+9)/24,31,29,55)
            slab=(.61+.10*slabs+.08*medium+.038*grain)[:,:,None]*np.array([1,.94,.83])
            dust=np.clip(.24+.48*broad+.26*medium-.26*np.exp(-joints*36),0,.8)
            slab=slab*(1-dust[:,:,None])+rgb*dust[:,:,None]
            rgb=np.where(paving[:,:,None],slab*(1-.23*np.exp(-joints*80))[:,:,None],rgb)
            ripples=np.sin(180*v+19*broad+7*medium)*.018
            rgb+=ripples[:,:,None]
            rgb=surface_finish.paving('dust_2',rgb,x,y,grain,broad)
            mask=ground_shadows(n//2).repeat(2,0).repeat(2,1)
            rgb*=1-mask[:,:,None]*np.array([.43,.36,.24])
        elif key=='timber':
            knots=np.sin(2*math.pi*(u*26+1.8*noise(u,v,5,3,42)+.38*medium))
            rgb*= (.94+.12*knots+.21*(noise(u,v,124,5,56)-.5))[:,:,None]
        elif key in ('gate','rust'):
            worn=np.clip((.53*broad+.31*medium+.16*grain-.58)*8,0,1)[:,:,None]
            rgb=rgb*(1-worn)+worn*np.array([.51,.289,.146])
            rgb*= (.95+.09*noise(u,v,157,7,87))[:,:,None]
        elif key=='linen':
            rgb*= (.91+.055*np.cos(TAU*u*85)+.049*np.cos(TAU*v*77))[:,:,None]
        else:
            chipped=np.clip((medium*.31+broad*.51+grain*.18-.63)*13,0,1)
            brickrow=np.floor(v*14)
            mortar=(np.abs((u*7+(brickrow%2)*.5)%1-.5)>.47)|(v*14%1<.055)
            under=np.array([.61,.46,.34])[None,None,:]*(.82+.26*medium+.17*mortar)[:,:,None]
            rgb=rgb*(1-chipped[:,:,None])+under*chipped[:,:,None]
            rgb*= (.91+.09*np.clip(v*5,0,1)-.07*noise(u,v,113,6,48))[:,:,None]
        pixels=np.ones((n,n,4),np.float32)
        pixels[:,:,:3]=np.clip(rgb,0,1)
        image=bpy.data.images.new('Salt | '+key,width=n,height=n,alpha=False)
        image.pixels.foreach_set(pixels.ravel())
        image.file_format='JPEG'
        image.filepath_raw=str(EVIDENCE/(key+'.jpg'))
        image.save()
        image.pack()
        material=mat(key)
        nodes=material.node_tree.nodes
        shader=next(node for node in nodes if node.type=='BSDF_PRINCIPLED')
        tex=nodes.new('ShaderNodeTexImage')
        tex.image=image
        material.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])
    for obj in scene().objects:
        if obj.type!='MESH': continue
        key=obj.data.materials[0].name.removeprefix('Salt | ')
        if key not in keys: continue
        data=obj.data
        uv=data.uv_layers.new(name='Surface coordinates')
        for face in data.polygons:
            normal=face.normal
            for i in face.loop_indices:
                x,y,z=obj.matrix_world @ data.vertices[data.loops[i].vertex_index].co
                if key=='sand':
                    coord=((x+12)/24,(y+9)/24)
                elif abs(normal.z)>.7:
                    coord=(x/2.7,y/2.7)
                else:
                    coord=((x if abs(normal.y)>abs(normal.x) else y)/2.7,z/3.8)
                uv.data[i].uv=coord
    viewport()


def save(blend,glb):
    scene().frame_set(0)
    viewport()
    surface_finish.repair_uvs(scene())
    with tempfile.TemporaryDirectory() as maps:
        surface_finish.materials(scene(),Path(maps))
    doc=export(glb)
    animations=doc.get('animations',[])
    assert len(animations)==1 and animations[0]['name']=='Menu_Environment'
    assert len(animations[0]['channels'])==18
    assert len(doc['images'])==18 and all('bufferView' in im for im in doc['images'])
    assert not doc.get('cameras') and not doc.get('lights')
    # Package just this scene as a regular, camera-ready .blend. Saving the
    # entire live MCP session would also publish the artist's other open scenes.
    with tempfile.TemporaryDirectory() as tmp:
        library=Path(tmp)/'scene.blend'
        bpy.data.libraries.write(str(library),{scene()},compress=True)
        setup=("import bpy; s=bpy.context.scene; assert s.name=='Salt Exchange'; "
               "areas=[a for a in bpy.context.screen.areas if a.type=='VIEW_3D']; "
               "[(setattr(a.spaces.active.region_3d,'view_perspective','CAMERA'),"
               "setattr(a.spaces.active.shading,'color_type','TEXTURE'),"
               "setattr(a.spaces.active.overlay,'show_overlays',False)) for a in areas]; "
               "bpy.context.preferences.filepaths.save_version=0; "
               f"bpy.ops.wm.save_as_mainfile(filepath={str(blend.resolve())!r},compress=True)")
        subprocess.run([bpy.app.binary_path,'--background',str(library),'--python-exit-code','1',
                        '--python-expr',setup],check=True,stdout=subprocess.DEVNULL)



def build(blend,glb):
    init()
    layout()
    gate_detail()
    dock_detail()
    facade()
    street()
    ornament()
    from courtyard_edges import salt_street
    salt_street()
    import midpoly
    midpoly.finish(scene())
    paint()
    facade_light()
    light_rig()
    save(blend,glb)


def ornament():
    # Indigo and limestone lozenges mark the municipal weigh-house.
    for x in (.75,4.45):
        for h in (.83,2.50):
            box('Enamel frieze',x,5.79,h,.77,.048,.21,'gate',.015)
            for i in range(5):
                cx=x+(i-2)*.14
                prism('Ceramic lozenge',[(cx-.062,h),(cx,h+.077),(cx+.062,h),(cx,h-.077)],
                      5.757,5.771,'chalk',.003)
        for h in (.15,.58,2.67,3.55):
            box('Pier moulding',x,6.31,h,.965,1.17,.072,'stone',.016)
    for z,r,k in ((4.84,1.24,'chalk'),(5.12,1.22,'stone'),(6.35,1.26,'chalk'),(6.471,1.33,'clay')):
        rod('Cistern ring',(5.7,11,z),(5.7,11,z+.085),r,k,20)
    for i in range(14):
        a=TAU*i/14
        x,y=5.7+1.171*math.cos(a),11+1.171*math.sin(a)
        rod('Drum pilaster',(x,y,5.17),(x,y,6.36),.066,'chalk',6)
    rod('Copper finial',(5.7,11,7.42),(5.7,11,7.84),.082,'brass',8,.01)
    for i in range(6):
        x=-3.7+i*.49
        box('Distant shade recess',x,11.51,3.2,.30,.08,.76,'slate')
    # Coiled hose and enamel delivery stencil at the far end of the dye shop.
    for i in range(28):
        a,b=TAU*i/28,TAU*(i+1)/28
        rod('Hose reel',(-8.01+.24*math.cos(a),3.68,1.64+.24*math.sin(a)),
            (-8.01+.24*math.cos(b),3.68,1.64+.24*math.sin(b)),.034,'deep',5)
    # Dark recessed shopfronts have a wooden screen under the balcony.
    for x in (6.02,7.14):
        box('Latticed doorway',x,6.38,1.29,.92,.13,2.37,'deep',.027)
        for i in range(6):
            px=x+(i-2.5)*.134
            box('Mashrabiya upright',px,6.28,1.40,.028,.055,1.91,'timber',.006)
        for i in range(7):
            box('Mashrabiya crossbar',x,6.25,.54+i*.27,.83,.051,.029,'timber',.005)
        box('Whitewashed threshold',x,6.2,.077,1.08,.41,.153,'chalk')
    for i in range(7):
        x=8.55+i*.34
        box('Roof merlon',x,4.73,6.16,.23,.29,.37,'chalk')
    for i in range(6):
        z=3.01+i*.32
        rod('Drainpipe',(9.00,4.38,z),(9.00,4.38,z+.324),.063,'rust')
    for x,y in ((-6.47,4.86),(-5.91,4.99)):
        rod('Terrace amphora foot',(x,y,1.70),(x,y,1.85),.115,'clay',12,.19)
        rod('Terrace amphora belly',(x,y,1.85),(x,y,2.10),.19,'clay',12,.21)
        rod('Terrace amphora neck',(x,y,2.10),(x,y,2.22),.21,'clay',12,.094)
        rod('Terrace amphora rim',(x,y,2.22),(x,y,2.27),.112,'rust',12)


def facade_light():
    """Bake overhang and balcony shadows into a single nine-panel facade atlas."""
    import numpy as np
    tree=occluders()
    panels=[(-9.15,3.85,3.65,5.55,'clay','Townhouse'),
            (-5.96,6.70,2.74,4.48,'plaster','Townhouse'),
            (6.91,6.425,3.58,4.72,'plaster','Townhouse'),
            (10.1,4.5,2.7,5.92,'chalk','Townhouse'),
            (.75,5.83,.81,3.94,'plaster','Portal cheek'),
            (4.45,5.83,.81,3.94,'plaster','Portal cheek'),
            (2.6,6.41,3.03,4.34,'gate','Hammered gate leaf'),
            (-6.41,4.10,1.79,1.7,'plaster','Raised dye market'),
            (2.6,6.195,4.62,5.29,'plaster','Stepped parapet')]
    tile,pad=256,3
    atlas=np.ones((3*tile,3*tile,4),dtype=np.float32)
    plate=bpy.data.materials.get('Salt | Facade light') or bpy.data.materials.new('Salt | Facade light')
    plate.use_nodes=True
    sun=Vector((-1.12,-.55,.71)).normalized()
    for i,(cx,y,w,h,key,prefix) in enumerate(panels):
        row,col=divmod(i,3)
        uv=np.clip((np.arange(tile)-pad+.5)/(tile-2*pad),0,1)
        xs,zs=cx+(uv-.5)*w,uv*h
        nodes=mat(key).node_tree.nodes
        image=next(n.image for n in nodes if n.type=='TEX_IMAGE')
        pixels=np.empty(image.size[0]*image.size[1]*4,np.float32)
        image.pixels.foreach_get(pixels)
        source=pixels.reshape(image.size[1],image.size[0],4)
        diffuse=source[((zs/3.8%1)*image.size[1]).astype(int)[:,None],
                       ((xs/2.7%1)*image.size[0]).astype(int)[None,:],:3]
        def shade(x,z):
            pos=(x,y-.012,z)
            direct=tree.ray_cast(pos,sun,25)[0] is not None
            hit=tree.ray_cast(pos,(0,-.28,.96),1.10)[3]
            return .37*direct+(.20*(1-hit/1.10) if hit is not None else 0)
        low=np.array([[shade(x,z) for x in xs[::2]] for z in zs[::2]],dtype=np.float32)
        shadow=low.repeat(2,0).repeat(2,1)
        rgb=diffuse*(1-shadow[:,:,None]*np.array([1,.89,.72]))
        atlas[row*tile:(row+1)*tile,col*tile:(col+1)*tile,:3]=rgb
        for obj in scene().objects:
            if not obj.name.startswith(prefix) or obj.type!='MESH': continue
            data=obj.data
            for f in data.polygons:
                mid=obj.matrix_world @ f.center
                if f.normal.y>-.999 or abs(mid.y-y)>.02 or abs(mid.x-cx)>w*.51: continue
                if plate.name not in data.materials:
                    data.materials.append(plate)
                f.material_index=data.materials.find(plate.name)
                for li in f.loop_indices:
                    x,_,z=obj.matrix_world @ data.vertices[data.loops[li].vertex_index].co
                    data.uv_layers.active.data[li].uv=((col+(pad+(x-cx+w/2)/w*(tile-2*pad))/tile)/3,
                                                      (row+(pad+z/h*(tile-2*pad))/tile)/3)
    image=bpy.data.images.new('Exchange shadow atlas',width=tile*3,height=tile*3,alpha=False)
    image.pixels.foreach_set(atlas.ravel())
    image.file_format='JPEG'
    image.filepath_raw=str(EVIDENCE/'facades.jpg')
    image.save()
    image.pack()
    nodes=plate.node_tree.nodes
    for node in list(nodes):
        if node.type=='TEX_IMAGE': nodes.remove(node)
    tex=nodes.new('ShaderNodeTexImage')
    tex.image=image
    shader=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    plate.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])

