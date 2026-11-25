"""Editable Observatory of Rain and Indigo Caravanserai menu scenery.
Run through create_scenes.py or call the same construction stages in Blender MCP.
"""
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import bpy
import surface_finish
import numpy as np
from mathutils import Vector

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from salt_market import arch, box, camera, export, light_rig, mat, mesh, prism, rod, scene, viewport, wind

TAU = math.tau
TITLES = {'ancient_ruins': 'Observatory of Rain', 'mirage': 'Indigo Caravanserai'}
# Authored sRGB swatches for paint, vertex-free fixed-pipeline shading and texture synthesis.
COLORS = {
    'ancient_ruins': {'rock':(124,137,119),'edge':(173,165,129),'lichen':(110,133,67),
        'basalt':(66,85,83),'jade':(40,148,126),'gold':(210,164,83),'deep':(34,51,52),
        'bark':(94,78,56),'fern':(53,111,71),'leaf':(116,151,65),'canopy':(68,109,98),
        'earth':(112,106,82),'water':(72,127,117),'clay':(156,85,53),'floor':(122,123,101)},
    'mirage': {'stucco':(225,175,116),'rose':(198,124,93),'ivory':(244,220,166),
        'indigo':(47,86,139),'tile':(64,129,151),'gold':(221,162,66),'deep':(49,54,68),
        'bark':(104,67,44),'leaf':(91,127,83),'brass':(160,113,63),'cloth':(159,53,54),
        'water':(83,152,153),'clay':(178,94,62),'haze':(165,148,135),'floor':(200,171,135)}
}


def begin(theme):
    title = TITLES[theme]
    current = bpy.data.scenes.get(title)
    if current:
        if current.get('menu_environment') != theme:
            raise ValueError('An unrelated open scene uses the requested name')
        for obj in list(current.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
    else:
        current = bpy.data.scenes.new(title)
    bpy.context.window.scene = current
    prefix = title + ' | '
    current['menu_environment'] = theme
    current['object_prefix'] = current['material_prefix'] = prefix
    current['static_object'] = title + ' architecture'
    current['static_geometry'] = title + ' static mesh'
    for block in (bpy.data.meshes, bpy.data.actions, bpy.data.images):
        for item in list(block):
            if item.name.startswith(prefix) and item.users == 0:
                block.remove(item)
    for key, rgb in COLORS[theme].items():
        material = bpy.data.materials.get(prefix+key) or bpy.data.materials.new(prefix+key)
        material.use_nodes = True
        for node in list(material.node_tree.nodes):
            if node.type == 'TEX_IMAGE':
                material.node_tree.nodes.remove(node)
        color = tuple((c/255)**2.2 for c in rgb) + (1,)
        material.diffuse_color = color
        surface = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        surface.inputs['Base Color'].default_value = color
        surface.inputs['Roughness'].default_value = .36 if key in ('water','jade','tile','gold','brass') else .86
    current.frame_start, current.frame_end, current.render.fps = 0, 120, 30
    current.render.resolution_x, current.render.resolution_y = 1280, 720
    current.render.resolution_percentage = 100
    current.world = bpy.data.worlds.get(prefix+'sky') or bpy.data.worlds.new(prefix+'sky')
    current.world.color = (.23,.25,.28)
    current.camera = camera(prefix+'main camera',(0,-8,1.05),(0,0,.95))
    box('Continuous level courtyard',0,0,-.12,40,48,.20,'floor',0)
    viewport()


def vessel(name,x,y,z,profile,key,n=20):
    # The octagonal fountain is cut stone; keep its authored eight-sided footprint.
    if not name.startswith(("Fountain octagonal","Fountain blue coping","Fountain water")):
        n=max(n,{"Indigo ribbed cupola": 80, "Distant cupola": 32, "Turquoise pavilion cap": 64, "Archaeological pot": 48}.get(name, 24))
    if any(word in name.lower() for word in ('pot','cupola','amphora','merchant jug')):
        # Monotone cubic interpolation preserves the authored heights, radii and
        # extremal silhouette while rounding the belly between profile samples.
        slopes=[(r2-r1)/(z2-z1) for (z1,r1),(z2,r2) in zip(profile,profile[1:])]
        tangents=[slopes[0]]+[2*a*b/(a+b) if a*b>0 else 0 for a,b in zip(slopes,slopes[1:])]+[slopes[-1]]
        rings=[]
        for j,((z1,r1),(z2,r2)) in enumerate(zip(profile,profile[1:])):
            for t in ((0, .5) if name in ('Distant cupola','Cupola finial','Teapot lid knob') else (0,1/3,2/3)):
                r=(1+2*t)*(1-t)**2*r1 + t*(1-t)**2*(z2-z1)*tangents[j]
                r+=t*t*(3-2*t)*r2 + t*t*(t-1)*(z2-z1)*tangents[j+1]
                rings.append((z1+t*(z2-z1),r))
        profile=rings+[profile[-1]]
    verts = [(x+r*math.cos(TAU*i/n),y+r*math.sin(TAU*i/n),z+h)
             for h,r in profile for i in range(n)]
    faces = [tuple(reversed(range(n)))]
    for j in range(len(profile)-1):
        for i in range(n):
            a=j*n+i
            b=j*n+(i+1)%n
            faces.append((a,b,b+n,a+n))
    faces.append(tuple(range(len(verts)-n,len(verts))))
    obj=mesh(name,verts,faces,key,0,True)
    if n>=24:
        obj['lathe_origin']=(x,y,z)
        obj['lathe_axis']=(0.,0.,1.)
    return obj


def ring(name,x,y,z,r,w,key,n=40):
    n=max(96,n)
    verts=[(x+rad*math.cos(TAU*i/n),depth,z+rad*math.sin(TAU*i/n))
           for depth,rad in ((y,r),(y,r-w),(y+.055,r),(y+.055,r-w)) for i in range(n)]
    faces=[]
    for i in range(n):
        j=(i+1)%n
        for a,b in ((0,1),(1,3),(3,2),(2,0)):
            faces.append((a*n+i,a*n+j,b*n+j,b*n+i))
    obj=mesh(name,verts,faces,key,0,True)
    obj['lathe_origin']=(x,y,z)
    obj['lathe_axis']=(0.,1.,0.)
    return obj


def diamond(name,x,y,z,w,h,key):
    return prism(name,[(x-w/2,z),(x,z+h/2),(x+w/2,z),(x,z-h/2)],y,y+.035,key,.006)


def leaf_spray(name,origin,angle,length,width,drop,key,animated=False):
    start=Vector(origin)
    forward=Vector((math.cos(angle),math.sin(angle),0))
    side=Vector((-forward.y,forward.x,0))
    verts=[]
    faces=[]
    for j in range(11):
        t=j/10
        ridge=start+forward*length*t+Vector((0,0,length*(.32*math.sin(math.pi*t)-drop*t*t)))
        half=width*(.035+math.sin(math.pi*t)**.72)
        for sign in (-1,0,1):
            verts.append(tuple(ridge+side*sign*half+forward*abs(sign)*(.05 if j%2 else 0)
                               +Vector((0,0,-abs(sign)*half*.38))))
        if j:
            i=3*j
            faces.extend(((i-3,i-2,i+1,i),(i-2,i-1,i+2,i+1)))
    obj=mesh(name,verts,faces,key)
    if animated:
        wind(obj,start,angle)
    return obj


def ground_shadow(n):
    from mathutils.bvhtree import BVHTree
    bpy.context.view_layer.update()
    vertices,faces=[],[]
    for obj in sorted(scene().objects,key=lambda obj:obj.name):
        if obj.type!='MESH' or 'Continuous level courtyard' in obj.name:
            continue
        first=len(vertices)
        vertices.extend(obj.matrix_world @ v.co for v in obj.data.vertices)
        faces.extend([first+i for i in f.vertices] for f in obj.data.polygons)
    tree=BVHTree.FromPolygons(vertices,faces)
    sun=Vector((-1.12,.83,.71)).normalized()
    return np.array([[tree.ray_cast((24*(i+.5)/n-12,24*(j+.5)/n-9,.015),sun,32)[0] is not None
                      for i in range(n)] for j in range(n)],dtype=np.float32)


def paint(theme, work):
    rng=np.random.default_rng(371 if theme=='ancient_ruins' else 719)
    keys=('floor','rock','basalt','edge','bark','jade','lichen') if theme=='ancient_ruins' else (
          'floor','stucco','rose','bark','cloth','tile','ivory')
    for key in keys:
        n=768 if key=='floor' else 256
        v,u=np.mgrid[:n,:n]/n
        def noise(cells):
            values=rng.random((cells,cells))
            x,y=u*cells,v*cells
            ix,iy=x.astype(int),y.astype(int)
            fx,fy=x%1,y%1
            fx,fy=fx*fx*(3-2*fx),fy*fy*(3-2*fy)
            return ((1-fx)*values[iy,ix]+fx*values[iy,(ix+1)%cells])*(1-fy)+(
                (1-fx)*values[(iy+1)%cells,ix]+fx*values[(iy+1)%cells,(ix+1)%cells])*fy
        cloud, fleck, grain = noise(8),noise(38),rng.random((n,n))
        color=np.array(COLORS[theme][key])/255
        rgb=color*(.79+.23*cloud+.12*fleck+.10*grain)[:,:,None]
        if key=='floor':
            x,y=u*24-12,v*24-9
            row=np.floor(y/.83)
            ix=(x/.99+row%2*.5+.028*(fleck-.5))%1
            iy=(y/.83+.022*(fleck-.5))%1
            gap=np.minimum(np.minimum(ix,1-ix),np.minimum(iy,1-iy))
            rgb*= (1-.47*np.exp(-gap*65)+.065*np.sin(row*76+np.floor(x/.99+row%2*.5)*8))[:,:,None]
            if theme=='ancient_ruins':
                moss=np.clip((cloud*.5+fleck*.35+grain*.15-.57)*3+.25*np.exp(-gap*25),0,.70)
                rgb=rgb*(1-moss[:,:,None])+np.array([.28,.39,.19])*moss[:,:,None]
                radius=np.hypot(x-2.4,y-2.4)
                band=(np.abs(radius-3.65)<.040)|(np.abs(radius-3.86)<.046)
                rgb[band]*=[.62,.84,.80]
            else:
                # Two narrow, glazed diagonal processional bands cross the pale paving.
                axis=x+.38*y
                band=(abs(axis-2.8)<.23)|(abs(axis+4.8)<.23)
                motif=(np.floor(y*4+axis*2.4).astype(int)%2)==0
                rgb[band & motif]*=[.44,.70,.81]
                rgb[band & ~motif]*=[.86,.66,.43]
            # Abrasion exposes pale stone beside the recessed mortar.
            wear=np.exp(-abs(gap-.047)*52)*np.clip(1.5*fleck+.6*grain-.46,0,1)*.26
            rgb=rgb*(1-wear[:,:,None])+np.array([.84,.78,.65])*wear[:,:,None]
            rgb=surface_finish.paving(theme,rgb,x,y,grain,cloud)
            shadows=ground_shadow(n//3).repeat(3,0).repeat(3,1)
            shadows=(4*shadows+sum(np.roll(shadows,d,a) for d,a in ((1,0),(-1,0),(1,1),(-1,1))))/8
            rgb*=1-shadows[:,:,None]*np.array([.46,.39,.25])
        elif key in ('rock','edge','basalt','lichen','stucco','rose','ivory'):
            veins=np.abs(np.sin(13*u+18*v+9*cloud+2*fleck))
            rgb*= (1-.19*np.exp(-veins*23)-.11*np.clip((fleck-.68)*4,0,1))[:,:,None]
            if theme=='ancient_ruins':
                moss=np.clip((cloud*.65+fleck*.35-.52)*2.9,0,.62)[:,:,None]
                rgb=rgb*(1-moss)+np.array([.32,.42,.21])*moss
            else:
                peel=np.clip((cloud*.56+fleck*.34+grain*.10-.65)*6,0,.75)[:,:,None]
                rgb=rgb*(1-peel)+np.array([.69,.56,.45])*peel
        elif key=='bark':
            rgb*= (.83+.16*np.sin(125*u+9*cloud+2*fleck)+.22*fleck)[:,:,None]
        elif key=='cloth':
            rgb*= (.90+.06*np.cos(TAU*u*52)+.05*np.cos(TAU*v*58))[:,:,None]
        else:
            rgb*= (.91+.12*np.cos(18*u+8*v+5*cloud)+.08*fleck)[:,:,None]
        pixels=np.ones((n,n,4),np.float32)
        pixels[:,:,:3]=np.clip(rgb,0,1)
        name=scene()['material_prefix']+key
        cached=bpy.data.images.get(name)
        if cached and cached.users==0:
            bpy.data.images.remove(cached)
        image=bpy.data.images.new(name,n,n,alpha=False)
        image.pixels.foreach_set(pixels.ravel())
        image.file_format='JPEG'
        image.filepath_raw=str(work/(theme+'-'+key+'.jpg'))
        image.save()
        image.pack()
        material=mat(key)
        tex=material.node_tree.nodes.new('ShaderNodeTexImage')
        tex.image=image
        bsdf=next(n for n in material.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        material.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    bpy.context.view_layer.update()
    for obj in scene().objects:
        if obj.type!='MESH': continue
        data=obj.data
        if not any(m.name.split(' | ')[-1] in keys for m in data.materials): continue
        uv=data.uv_layers.new(name='Surface')
        for face in data.polygons:
            key=data.materials[face.material_index].name.split(' | ')[-1]
            normal=face.normal
            pottery='Archaeological pot' in obj.name
            if pottery and abs(normal.z)<.999:
                origin=Vector(obj['lathe_origin'])
                loops=list(face.loop_indices)
                points=[data.vertices[data.loops[i].vertex_index].co-origin for i in loops]
                angles=[math.atan2(v.y,v.x)/TAU for v in points]
                wrap=max(angles)-min(angles)>.5
                for i,u,v in zip(loops,angles,points):
                    uv.data[i].uv=(u+(1 if wrap and u<0 else 0),v.z)
                continue
            for i in face.loop_indices:
                x,y,z=obj.matrix_world @ data.vertices[data.loops[i].vertex_index].co
                if key=='floor': coords=((x+12)/24,(y+9)/24)
                elif abs(normal.z)>.7: coords=(x/2,y/2)
                else: coords=((x if abs(normal.y)>abs(normal.x) else y)/2,z/2)
                uv.data[i].uv=coords


def save_source(path):
    with tempfile.TemporaryDirectory() as tmp:
        library=Path(tmp)/'source.blend'
        bpy.data.libraries.write(str(library),{scene()},compress=True)
        setup=("import bpy; s=bpy.context.scene; assert s.name=="+repr(scene().name)+"; "
               "areas=[a for a in bpy.context.screen.areas if a.type=='VIEW_3D']; "
               "[(setattr(a.spaces.active.region_3d,'view_perspective','CAMERA'),"
               "setattr(a.spaces.active.shading,'color_type','TEXTURE'),"
               "setattr(a.spaces.active.overlay,'show_overlays',False)) for a in areas]; "
               "bpy.context.preferences.filepaths.save_version=0; "
               f"bpy.ops.wm.save_as_mainfile(filepath={str(path.resolve())!r},compress=True)")
        subprocess.run([bpy.app.binary_path,'--background',str(library),'--python-exit-code','1',
                        '--python-expr',setup],check=True,stdout=subprocess.DEVNULL)


def finish(theme, blend, glb):
    with tempfile.TemporaryDirectory() as tmp:
        scene().frame_set(0)
        import midpoly
        midpoly.finish(scene())
        paint(theme,Path(tmp))
        light_rig()
        sun=next(o for o in scene().objects if o.type=='LIGHT' and o.data.type=='SUN')
        direction=Vector((-1.12,.83,.71))
        sun.location=Vector((0,5,0))+direction*10
        sun.rotation_euler=(-direction).to_track_quat('-Z','Y').to_euler()
        surface_finish.repair_uvs(scene())
        with tempfile.TemporaryDirectory() as maps:
            surface_finish.materials(scene(),Path(maps))
        doc=export(glb)
        assert len(doc['animations'])==1 and doc['animations'][0]['name']=='Menu_Environment'
        assert len(doc['animations'][0]['channels'])==18
        assert len(doc['images'])==14 and all('bufferView' in im for im in doc['images'])
        for part in doc['meshes']:
            for primitive in part['primitives']:
                pbr=doc['materials'][primitive['material']].get('pbrMetallicRoughness', {})
                if 'baseColorTexture' in pbr:
                    channel=pbr['baseColorTexture'].get('texCoord', 0)
                    assert f'TEXCOORD_{channel}' in primitive['attributes'], part['name']
        save_source(blend)


def stages(theme):
    import rain_observatory, indigo_caravanserai
    return rain_observatory if theme=='ancient_ruins' else indigo_caravanserai


def build(theme, blend, glb):
    begin(theme)
    art=stages(theme)
    art.layout()
    art.details()
    finish(theme,blend,glb)

