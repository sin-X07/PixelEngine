"""Build editable menu environments from the three in-game map art directions.

Run with Blender --background --python-exit-code 1 --python this_file.
Coordinates in this authoring script use the engine's Y-up convention.
All solid scenery stays behind Z=-3, outside the existing character arena.
"""
import math
import random
import shutil
import uuid
import json
import struct
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
AUTHORING = Path(__file__).resolve().parent
BUILD = ROOT / 'build-menu-scenes' / uuid.uuid4().hex
OUTPUT = BUILD / 'assets'
SOURCE = BUILD / 'sources'
OUTPUT.mkdir(parents=True, exist_ok=True)
SOURCE.mkdir(parents=True, exist_ok=True)
MATERIALS = {}
RNG = random.Random(28)


def point(x, y, z):
    return (x, -z, y)


def material(name, color):
    if name not in MATERIALS:
        mat = bpy.data.materials.new(name)
        mat.diffuse_color = (*color, 1)
        mat.use_nodes = True
        shader = mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = (*color, 1)
        shader.inputs['Roughness'].default_value = .83
        MATERIALS[name] = mat
    return MATERIALS[name]


def finish(obj, name, mat):
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def box(name, pos, size, mat, bevel=.025):
    bpy.ops.mesh.primitive_cube_add(size=1, location=point(*pos))
    obj = bpy.context.object
    obj.dimensions = (size[0], size[2], size[1])
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new('Soft worn edges', 'BEVEL')
        modifier.width = bevel
        modifier.segments = 2
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        modifier = obj.modifiers.new('Face weighted normals', 'WEIGHTED_NORMAL')
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return finish(obj, name, mat)


def beam(name, start, end, radius, mat, vertices=10):
    a, b = Vector(point(*start)), Vector(point(*end))
    bpy.ops.mesh.primitive_cone_add(vertices=vertices, radius1=radius,
                                  radius2=radius * .86, depth=(b-a).length,
                                  location=(a+b)/2)
    obj = bpy.context.object
    obj.rotation_euler = (b-a).to_track_quat('Z', 'Y').to_euler()
    return finish(obj, name, mat)


def stone(name, pos, scale, mat):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1, location=point(*pos))
    obj = bpy.context.object
    obj.scale = (scale[0], scale[2], scale[1])
    return finish(obj, name, mat)


def arch(x, z, height, stone_mat, dark):
    radius = 1.10
    spring = height-radius
    for side in (-1, 1):
        box('Arch pier', (x+side*1.34, spring/2, z), (.48, spring, .8), stone_mat)
        box('Pier capital', (x+side*1.34, spring-.05, z), (.64,.17, .94), stone_mat)
    # Each voussoir is a closed wedge, including its rear face for overhead views.
    for i in range(13):
        a, b = math.pi*i/13+.008, math.pi*(i+1)/13-.008
        verts = [point(x+r*math.cos(t), spring+r*math.sin(t), z+d)
                 for d in (-.4,.4) for r,t in
                 [(radius,a),(radius,b),(radius+.48,b),(radius+.48,a)]]
        mesh = bpy.data.meshes.new('Voussoir')
        mesh.from_pydata(verts, [], [(0,3,2,1),(4,5,6,7),(0,1,5,4),
                                     (1,2,6,5),(2,3,7,6),(3,0,4,7)])
        obj = bpy.data.objects.new('Arch stone', mesh)
        bpy.context.collection.objects.link(obj)
        finish(obj, 'Arch stone', stone_mat)
    box('Recessed passage', (x,spring/2,z-.8), (2.2,spring,.1), dark)


def crate(x, z, wood, iron, height=.9):
    box('Shipping crate', (x,height/2,z), (1,height,.85), wood)
    for offset in (-.39,.39):
        box('Crate steel strap', (x+offset,height/2,z+.44), (.07,height,.035), iron,.008)
    brace = box('Crate diagonal brace', (x,height/2,z+.46), (.09,height*1.2,.06), wood,.008)
    brace.rotation_euler.y = -.65


def plant(x, z, height, green, bark, palm=False):
    beam('Tree trunk', (x,0,z),(x+.12,height,z),.13,bark)
    for i in range(9):
        angle = 2*math.pi*i/9
        length = height*(.38 if palm else .27)
        end = (x+math.cos(angle)*length,height-.25,z+math.sin(angle)*length)
        beam('Leaf stem',(x+.12,height,z),end,.028,green,6)
        if palm:
            vertices = []
            for j in range(9):
                t = j/8
                distance = length*1.7*t
                width = .28*math.sin(math.pi*t)+.012
                for sign in (-1,1):
                    vertices.append(point(math.cos(angle)*distance-sign*math.sin(angle)*width,
                                          .3*math.sin(math.pi*t)-.55*t,
                                          math.sin(angle)*distance+sign*math.cos(angle)*width))
            mesh = bpy.data.meshes.new('Curved palm leaf')
            mesh.from_pydata(vertices,[],[(j*2,j*2+1,j*2+3,j*2+2) for j in range(8)])
            leaf = bpy.data.objects.new('Wind palm frond',mesh)
            bpy.context.collection.objects.link(leaf)
            leaf.location = point(x+.12,height,z)
            finish(leaf,leaf.name,green)
            bpy.context.view_layer.objects.active = leaf
            solid = leaf.modifiers.new('Leaf thickness','SOLIDIFY')
            solid.thickness = .012
            bpy.ops.object.modifier_apply(modifier=solid.name)
        else:
            leaf = stone('Canopy',end,(.80,.54,.85),green)
        leaf['menu_wind'] = True
        for frame in range(0,121,10):
            leaf.rotation_euler.y = .028*math.sin(2*math.pi*frame/120+angle)
            leaf.keyframe_insert(data_path='rotation_euler',frame=frame)
        action = leaf.animation_data.action
        track = leaf.animation_data.nla_tracks.new()
        track.name = 'Menu_Environment'
        strip = track.strips.new('Menu_Environment',0,action)
        strip.action_slot = action.slots[0]
        strip.action_frame_start,strip.action_frame_end = 0,120
        strip.frame_start,strip.frame_end = 0,120
        leaf.animation_data.action = None


def car(x, z, paint, metal, dark):
    box('Abandoned car chassis',(x,.38,z),(2.8,.48,1.3),paint,.12)
    box('Car cabin',(x-.12,.89,z),(1.34,.66,1.16),paint,.13)
    for side in (-1,1):
        for offset in (-.43,.23):
            box('Dust covered glass',(x+offset,.96,z+side*.591),(.53,.34,.025),dark,.025)
        box('Door trim',(x-.12,.60,z+side*.66),(1.3,.055,.035),metal,.008)
        box('Door handle',(x-.30,.75,z+side*.67),(.16,.035,.04),metal,.008)
        for axle in (-.94,.94):
            beam('Flat tire',(x+axle,.28,z+side*.52),(x+axle,.28,z+side*.78),.30,dark,14)
            beam('Wheel hub',(x+axle,.28,z+side*.78),(x+axle,.28,z+side*.80),.13,metal,12)
    for offset in (-1.43,1.43):
        box('Bumper',(x+offset,.35,z),(.09,.15,1.30),metal)
    box('Bonnet',(x+.94,.67,z),(.73,.08,1.15),paint,.04)
    for side in (-1,1):
        box('Broken headlamp',(x+1.48,.53,z+side*.44),(.035,.12,.22),metal,.01)
    rust = material('Exposed rust',(.24,.095,.035))
    for i in range(12):
        stone('Chipped paint',(x+RNG.uniform(-1.3,1.3),.46+RNG.random()*.17,z+.66),
              (.12,.045,.012),rust)
    broken = box('Bent bonnet panel',(x+1,.73,z),(.68,.05,.9),paint,.015)
    broken.rotation_euler.y = -.22
    for dx in (-.9,.85):
        beam('Cracked side glass',(x+dx-.1,.83,z+.61),(x+dx+.08,1.1,z+.61),.008,metal,5)


def barrel(x, z, metal, rust):
    beam('Weathered barrel',(x,.05,z),(x,.95,z),.33,metal,16)
    for y in (.1,.32,.70,.91):
        beam('Barrel rolled rim',(x,y-.025,z),(x,y+.025,z),.35,rust,16)
    beam('Barrel lid',(x,.95,z),(x,.97,z),.30,metal,16)
    beam('Barrel bung',(x+.13,.97,z),(x+.13,1,z),.04,rust,10)


def make_scene(theme):
    global MATERIALS, RNG
    bpy.ops.wm.read_factory_settings(use_empty=True)
    MATERIALS = {}
    RNG = random.Random(28)
    ancient = theme == 'ancient_ruins'
    mirage = theme == 'mirage'
    base = (.32,.34,.29) if ancient else ((.62,.46,.30) if mirage else (.58,.47,.25))
    wall = material('Weathered limestone',base)
    trim = material('Carved pale edges',tuple(c*1.22 for c in base))
    shadow = material('Deep passage',(.065,.075,.072))
    wood = material('Old timber',(.24,.15,.08))
    metal = material('Oxidised fittings',(.19,.22,.20))
    green = material('Sage foliage',(.13,.24,.10) if ancient else (.22,.30,.12))
    ground = material('Courtyard surface',(.30,.30,.25) if ancient else (.52,.41,.26))
    box('Continuous level courtyard',(0,-.11,0),(40,.18,48),ground,.0)
    for row in range(18):
        for column in range(18):
            # Flush inlaid paving retains a completely level movement surface.
            x,z = (column-8.5)*1.25,(row-8)*1.25
            tint = .90+RNG.random()*.15
            paving_color = tuple(c*tint for c in ground.diffuse_color[:3])
            tile_name = 'Paving '+str(int(tint*20))
            if ancient and (abs(x)>5 or z < -3) and RNG.random()<.18:
                tile_name,paving_color = 'Moss stained paving',(.23,.28,.18)
            if mirage and abs(x)>6 and z < -2:
                tile_name,paving_color = ('Courtyard dark mosaic',(.24,.26,.24)) if (row+column)%2 else ('Courtyard pale mosaic',(.66,.60,.47))
            tile = material(tile_name,paving_color)
            box('Courtyard paving',(x,-.008,z),(1.23,.016,1.23),tile,.0)
    portals = (-7,0,7) if ancient else ((0,) if not mirage else (-8.5,-4.5,0,4.5,8.5))
    for x in portals:
        height = 3.8 if ancient else 4.5
        arch(x,-7,height,trim,shadow)
        box('Arcade parapet',(x,height+.15,-7),(4.45,.42,1.05),wall)
        box('Cornice',(x,height+.42,-7),(4.55,.13,1.17),trim)
        if not ancient:
            box('Upper town facade',(x,height+1.5,-8),(4.4,2.3,1.1),wall)
            for dx in (-1,1):
                box('Recessed upper window',(x+dx,height+1.55,-7.42),(.62,1.05,.08),shadow)
                box('Window sill',(x+dx,height+.99,-7.35),(.80,.12,.28),trim)
                for bar in (-.18,0,.18):
                    box('Window grille',(x+dx+bar,height+1.55,-7.34),(.028,.96,.03),metal,.0)
    if ancient:
        for side in (-1,1):
            for row in range(8):
                for column in range(4):
                    x = side*(3.0+column*.85)
                    if row > 5 and column == 2:
                        continue
                    box('Broken masonry',(x,.20+row*.43,-7.25),(.81,.40,.78),wall,.035)
            for i in range(18):
                x = side*(3.5+math.sin(i*.7)*.45)
                stone('Climbing ivy',(x,3.6-i*.17,-6.74),(.22,.14,.08),green)
    if not ancient and not mirage:
        for x,height in ((-7.8,6.8),(-4.6,5.3),(4.4,5.8),(7.8,7.4)):
            box('Desert house',(x,height/2,-8.2),(3.1,height,2.0),wall,.05)
            box('Flat roof coping',(x,height,-8.2),(3.3,.18,2.15),trim)
            for y in (1.2,3.3):
                for dx in (-.7,.7):
                    box('House inset',(x+dx,y,-7.17),(.55,.85,.07),shadow)
                    box('Shutter',(x+dx,y,-7.12),(.48,.77,.035),wood)
            beam('Roof vent',(x+.7,height,-8),(x+.7,height+.7,-8),.12,metal)
    for side in (-1,1):
        box('Enclosing side wall',(side*12,2,-1),(1,4,13),wall)
        for z in (-6,-2,2):
            box('Side buttress',(side*11.4,1.7,z),(.45,3.4,.65),trim)
        crate(side*6.8,-5.8,wood,metal)
        crate(side*9,-5.8,wood,metal,1.25)
        barrel(side*5.6,-5,metal,wood)
        plant(side*10,-5,4.8,green,wood,palm=not ancient)
    if ancient:
        for x in (-6.8,6.8):
            for y in (.2,1.1,2,2.9):
                box('Ancient column block',(x,y,-6),(.85,.75,.85),wall,.08)
                box('Carved column ring',(x,y+.33,-6),(1,.09,1),trim)
            for i in range(9):
                stone('Moss foliage',(x+RNG.uniform(-.5,.5),RNG.uniform(.3,3.5),-5.5),(.18,.12,.10),green)
        for i in range(26):
            stone('Rubble',(RNG.uniform(-10,10),.12,RNG.uniform(-6,-3.5)),(.23,.16,.19),wall)
    else:
        car(-7.5,-3.9,material('Faded car paint',(.22,.31,.28)),metal,shadow)
        for i in range(12):
            stone('Wind swept sand',(RNG.uniform(-10,10),.04,RNG.uniform(-6,-3.4)),(.5,.08,.3),ground)
    if mirage:
        terracotta = material('Terracotta roof',(.37,.15,.085))
        teal = material('Teal shutters',(.12,.29,.27))
        cloth = material('Sage striped awning',(.32,.41,.22))
        for side in (-1,1):
            canopy_z = -5.8 if side < 0 else -4.8
            for i in range(15):
                beam('Clay roof tile',(side*6+i*.16-1.1,6.8,-9),(side*6+i*.16-1.1,6.8,-7),.095,terracotta)
            for dx in (-.5,.5):
                box('Courtyard shutter',(side*4.5+dx,2.3,-6.4),(.42,1,.09),teal)
            for i in range(10):
                box('Striped market canopy',(side*8+i*.19-.9,2.6,canopy_z),(.19,.06,1.5),cloth if i%2 else trim)
            for x in (side*8-.95,side*8+.95):
                beam('Awning support',(x,0,canopy_z+.75),(x,2.7,canopy_z+.75),.04,wood)
            for x in (side*5.5,side*9.5):
                beam('Terracotta planter',(x,0,-3.8),(x,.5,-3.8),.25,terracotta,12)
                for i in range(5):
                    stone('Potted shrub',(x+RNG.uniform(-.2,.2),.6,-3.8+RNG.uniform(-.2,.2)),(.25,.3,.25),green)
    scene = bpy.context.scene
    scene['menu_environment'] = theme
    scene['solid_prop_limit_engine_z'] = -3.0
    scene.render.fps = 30
    scene.frame_start,scene.frame_end = 0,120
    scene.frame_set(0)
    bpy.ops.object.camera_add(location=point(0,3,12))
    camera = bpy.context.object
    camera.name = 'Preview camera'
    camera.rotation_euler = (Vector(point(0,2,-3))-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.lens = 28
    scene.camera = camera
    bpy.ops.object.light_add(type='AREA', location=point(-4,10,4))
    bpy.context.object.name = 'Preview softbox'
    bpy.context.object.data.energy = 1700
    bpy.context.object.data.shape = 'DISK'
    bpy.context.object.data.size = 12
    scene.world = bpy.data.worlds.new('Preview world')
    scene.world.color = (.23,.25,.28)
    scene.render.resolution_x,scene.render.resolution_y = 1280,720
    guide = bpy.data.texts.new('START_HERE')
    guide.write('主菜单环境编辑源\n\n'
                '当前主题 '+theme+'。网格与风动动画均可编辑。\n'
                'Blender Z 为高度，Blender -Y 对应游戏 Z。\n'
                '保持角色活动区 X=-6.3..6.3、游戏 Z=-2.7..6.1 内无实体道具。\n'
                '地砖顶面位于高度0，基础地面顶面-0.02，避免共面闪烁。\n'
                '风动为0..120帧、30fps循环。相机和灯光仅用于源文件预览。\n'
                'create_scenes.py 为可重复制作脚本，执行会重新生成三份源及GLB。\n')
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.shading.color_type = 'MATERIAL'
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE/(theme+'.blend')))
    # Merge draw calls by material while retaining the editable objects in .blend.
    bpy.ops.object.select_all(action='SELECT')
    for obj in scene.objects:
        if obj.get('menu_wind'):
            obj.select_set(False)
    bpy.context.view_layer.objects.active = next(o for o in scene.objects if o.type == 'MESH' and not o.get('menu_wind'))
    bpy.ops.object.join()
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT/(theme+'.glb')),export_format='GLB',
                             export_animations=True,export_animation_mode='NLA_TRACKS',
                             export_merge_animation='NLA_TRACK',
                             export_force_sampling=True,export_cameras=False,export_lights=False)
    print('MENU ENVIRONMENT EXPORTED',theme,flush=True)


for name in ('ancient_ruins','dust_2','mirage'):
    make_scene(name)

# Publish the complete six-file bundle only after all three scenes export.
targets = [(SOURCE/(name+'.blend'), AUTHORING/(name+'.blend'))
           for name in ('ancient_ruins','dust_2','mirage')]
targets += [(OUTPUT/(name+'.glb'), ROOT/'assets/models/menu/scenes'/(name+'.glb'))
            for name in ('ancient_ruins','dust_2','mirage')]
previous = BUILD/'previous'
previous.mkdir()
existing = {}
for index,(source,target) in enumerate(targets):
    assert source.is_file() and source.stat().st_size > 1000
    if source.suffix == '.glb':
        data = source.read_bytes()
        document = json.loads(data[20:20+struct.unpack_from('<I',data,12)[0]])
        animations = document.get('animations',[])
        assert len(animations) == 1 and len(animations[0]['channels']) == 18, 'All foliage must share one wind clip'
    if target.exists():
        backup = previous/str(index)
        shutil.copyfile(target,backup)
        existing[target] = backup
try:
    for source,target in targets:
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
except Exception:
    for source,target in targets:
        if target in existing:
            shutil.copyfile(existing[target],target)
        elif target.exists():
            target.unlink()
    raise
print('MENU ENVIRONMENT BUNDLE PUBLISHED',flush=True)
