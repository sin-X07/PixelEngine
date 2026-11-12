"""Editable reconstruction scene and direct, repeatable export of its existing rigs."""
from pathlib import Path

import bpy
from mathutils import Vector


def tag_actor(objects, actor):
    for obj in objects:
        obj['menu_actor'] = actor
        obj['menu_runtime_name'] = obj.name


def export_prepared(scene, output, actors):
    output.mkdir(parents=True, exist_ok=True)
    for actor in actors:
        objects = [obj for obj in scene.objects if obj.get('menu_actor') == actor]
        if not objects:
            raise RuntimeError(f'Prepared scene is missing actor {actor}')
        placement = next(obj for obj in objects if obj.get('menu_runtime_name') == actor + '/review_placement')
        placement.location = (0, 0, 0)
        placement.scale = (1, 1, 1)
        placement.rotation_euler = (0, 0, 0)
        rig = next(obj for obj in objects if obj.type == 'ARMATURE')
        rig.animation_data.action = None
        for track in rig.animation_data.nla_tracks:
            track.mute = False
            track.is_solo = False
            if actor != 'CHICKEN':
                print('Baking', actor, track.name, '(prepared scene)', flush=True)
        bpy.ops.object.select_all(action='DESELECT')
        for obj in objects:
            obj.name = obj['menu_runtime_name']
            obj.hide_set(False)
            obj.hide_viewport = False
            obj.hide_render = False
            obj.select_set(True)
        bpy.context.view_layer.objects.active = rig
        scene.frame_start, scene.frame_end = 0, 120
        scene.render.fps = 30
        scene.frame_set(0)
        bpy.ops.export_scene.gltf(filepath=str(output / (actor.lower() + '.glb')),
            export_format='GLB', use_selection=True, use_renderable=True,
            export_animation_mode='NLA_TRACKS', export_merge_animation='NLA_TRACK',
            export_force_sampling=True, export_sampling_interpolation_fallback='LINEAR',
            export_skins=True, export_extras=False, export_cameras=False,
            export_lights=False, export_optimize_animation_size=True)
        for obj in objects:
            obj.name = actor + '/' + obj['menu_runtime_name']


def save_prepared(scene, path):
    scene['menu_prepared_version'] = 2
    scene['menu_export_script'] = 'tools/menu-models/export_models.py'
    actors = {actor: [obj for obj in scene.objects if obj.get('menu_actor') == actor]
              for actor in ('T', 'CT', 'CHICKEN')}
    if any(not objects for objects in actors.values()):
        raise RuntimeError('An editable delivery scene requires all three actors')
    previous_collections = list(scene.collection.children)
    for collection in previous_collections:
        collection.name = 'Archived / ' + collection.name
    references = bpy.data.collections.new('Reference | hidden source library')
    scene.collection.children.link(references)
    references.hide_render = True
    for obj in list(scene.objects):
        if not obj.get('menu_actor'):
            obj.hide_set(True)
            obj.hide_render = True
            for collection in list(obj.users_collection):
                collection.objects.unlink(obj)
            references.objects.link(obj)
    for actor, objects in actors.items():
        collection = bpy.data.collections.new(actor + ' | editable reconstruction')
        scene.collection.children.link(collection)
        for obj in objects:
            for previous in list(obj.users_collection):
                previous.objects.unlink(obj)
            collection.objects.link(obj)
            runtime = obj['menu_runtime_name']
            visible = not runtime.startswith(('weapon_', 'accessory_')) or runtime == 'weapon_Idle_AK'
            obj.hide_set(not visible)
            obj.hide_render = not visible
        placement = next(obj for obj in objects if obj['menu_runtime_name'] == actor + '/review_placement')
        placement.location = {'T': (-1.35, 0, 0), 'CT': (1.35, 0, 0),
                              'CHICKEN': (0, -1.15, 0)}[actor]
        rig = next(obj for obj in objects if obj.type == 'ARMATURE')
        rig.show_in_front = True
        rig.animation_data.action = None
        for track in rig.animation_data.nla_tracks:
            track.mute = track.name != ('Menu_Idle' if actor == 'CHICKEN' else 'Menu_Idle_AK')
            track.is_solo = False
    for collection in previous_collections:
        scene.collection.children.unlink(collection)
    studio = bpy.data.collections.new('Studio | preview only')
    scene.collection.children.link(studio)

    def studio_object(obj):
        for collection in list(obj.users_collection):
            collection.objects.unlink(obj)
        studio.objects.link(obj)

    target = Vector((0, 0, .9))
    for location, energy, size in (((-4, -5, 7), 1100, 5), ((5, -2, 4), 800, 4), ((0, 4, 6), 1000, 3)):
        bpy.ops.object.light_add(type='AREA', location=location)
        light = bpy.context.object
        light.data.energy, light.data.size = energy, size
        light.rotation_euler = (target - light.location).to_track_quat('-Z', 'Y').to_euler()
        studio_object(light)
    bpy.ops.object.camera_add(location=(4, -9, 4))
    camera = bpy.context.object
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 6.3
    studio_object(camera)
    scene.camera = camera
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.03))
    floor = bpy.context.object
    floor.name = 'Studio floor | preview only'
    material = bpy.data.materials.new('Studio charcoal')
    material.diffuse_color = (.065, .075, .09, 1)
    material.use_nodes = True
    shader = next(node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = material.diffuse_color
    shader.inputs['Roughness'].default_value = .85
    floor.data.materials.append(material)
    studio_object(floor)
    scene.world.color = (.12, .12, .12)
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
    scene.render.fps = 30
    scene.frame_start, scene.frame_end = 0, 120
    scene.frame_set(0)
    guide = bpy.data.texts.get('START_HERE') or bpy.data.texts.new('START_HERE')
    guide.clear()
    guide.write('主菜单角色编辑源\n\n'
                '三个角色集合分别包含网格、骨架、配饰挂点和武器接口。Reference 集合保存隐藏的源资源。\n'
                '选中角色骨架，在 NLA 编辑器中启用一条 Menu_ 轨道，预览对应的持久姿势。\n'
                '在大纲视图中显示同名姿势与武器的 weapon_ 网格，按需显示一件 accessory_ 配饰。\n'
                '动画范围为 0 到 120 帧，30 fps，四秒循环。\n'
                '场景显示基础材质。游戏通过 Menu3D 的调色板生成 CT 蓝色、各款小鸡羽色与服装配色。\n'
                '重命名时保留 menu_actor 与 menu_runtime_name 自定义属性。\n'
                '保存修改后运行 tools/update-menu-models.ps1，可用 -Source 指定当前 blend 文件。\n'
                '准备好的场景直接导出现有网格和骨架动画。场景摆位在导出时归一化到模型原点。\n')
    bpy.ops.object.select_all(action='DESELECT')
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
                area.spaces.active.shading.type = 'MATERIAL'
    path = Path(path).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
