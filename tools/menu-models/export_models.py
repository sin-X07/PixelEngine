"""Bake the reviewed rigs into menu GLBs. Run with Blender --background --python.

CharacterLab.blend retains the approved editable rigs, accessories and held poses.
Weapons use Quaternius geometry. All coordinates below are Blender Z-up meters.
"""
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / 'assets/models/menu'
WEAPONS = ('AK', 'SMG', 'Sniper', 'Shotgun', 'Pistol', 'Knife_1')
POSES = ('Idle', 'Crouch', 'HandsOnHips', 'FistPalm')
FRAMES = range(1, 122, 3)
bpy.ops.wm.open_mainfile(filepath=str(HERE / 'CharacterLab.blend'))
scene = bpy.context.scene
scene.render.fps = 30


def capture(rig):
    return {b.name: (b.location.copy(), b.rotation_quaternion.copy(), b.scale.copy())
            for b in rig.pose.bones}


def restore(rig, pose):
    for bone in rig.pose.bones:
        bone.location, bone.rotation_quaternion, bone.scale = pose[bone.name]
    bpy.context.view_layer.update()


def aim(bone, direction):
    matrix = bone.matrix.copy()
    head = bone.head.copy()
    rotation = (bone.tail - head).rotation_difference(direction)
    matrix.translation = Vector()
    bone.matrix = Matrix.Translation(head) @ rotation.to_matrix().to_4x4() @ matrix
    bpy.context.view_layer.update()


def solve_arm(rig, side, target, pole):
    upper = rig.pose.bones['UpperArm.' + side]
    lower = rig.pose.bones['LowerArm.' + side]
    head = upper.head.copy()
    delta = target - head
    axis = delta.normalized()
    first = (upper.tail - head).length
    second = (lower.tail - lower.head).length
    distance = min(delta.length, first + second - .001)
    along = (first * first - second * second + distance * distance) / (2 * distance)
    normal = Vector(pole) - head
    normal = (normal - axis * normal.dot(axis)).normalized()
    elbow = head + axis * along + normal * math.sqrt(max(0, first * first - along * along))
    aim(upper, elbow - head)
    aim(lower, target - lower.head)
    error = (lower.tail - target).length
    if error > .003:
        raise RuntimeError(f'{rig.name} {side} unreachable grip, error {error}')


def bake(rig, name, samples):
    action = bpy.data.actions.new(name)
    rig.animation_data.action = action
    samples[-1] = (121, samples[0][1])
    for frame, pose in samples:
        restore(rig, pose)
        for bone in rig.pose.bones:
            for prop in ('location', 'rotation_quaternion', 'scale'):
                bone.keyframe_insert(data_path=prop, frame=frame, group=bone.name)
    rig.animation_data.action = None
    track = rig.animation_data.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, 1, action)
    strip.action_frame_start = 1
    strip.action_frame_end = 121
    strip.extrapolation = 'NOTHING'
    track.mute = True


def weapon_geometry():
    meshes = {}
    for name in WEAPONS:
        if name in ('AK', 'Pistol', 'Knife_1'):
            before = set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=str(ROOT / 'assets/models/quaternius/Guns' / (name + '.gltf')))
            imported = set(bpy.data.objects) - before
            source = next(obj for obj in imported if obj.type == 'MESH')
            mesh = source.data.copy()
            mesh.transform(source.matrix_world)
            for obj in imported:
                bpy.data.objects.remove(obj, do_unlink=True)
        else:
            source = bpy.data.objects[name]
            mesh = source.data.copy()
            if name == 'Sniper':
                mesh.transform(Matrix.Rotation(math.pi, 4, 'Z') @ Matrix.Scale(.2, 4))
            else:
                mesh.transform(Matrix.Rotation(math.pi / 2, 4, 'X'))
        meshes[name] = mesh
    return meshes


def attach_mesh(rig, name, mesh, matrix, bone):
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = rig
    obj.parent_type = 'BONE'
    obj.parent_bone = bone
    bpy.context.view_layer.update()
    obj.matrix_world = rig.matrix_world @ matrix
    return obj


def export_actor(key, meshes):
    rig = bpy.data.objects[key + '_RIG']
    chicken = key == 'CHICKEN'
    # Remove the review-stage transform while retaining native model dimensions.
    placement = bpy.data.objects[key + '/review_placement']
    placement.location = (0, 0, 0)
    placement.scale = (1, 1, 1)
    bpy.context.view_layer.update()
    samples = []
    for segment in range(4):
        clip = []
        for frame in FRAMES:
            scene.frame_set(segment * 120 + frame)
            clip.append((frame, capture(rig)))
        samples.append(clip)
    rig.animation_data_clear()
    rig.animation_data_create()
    restore(rig, samples[0][0][1])
    # Join each accessory into one mesh, retaining its reviewed bone socket.
    accessories = []
    for group in ('headwear', 'eyewear', 'back'):
        socket = bpy.data.objects[key + '/socket_' + group]
        pieces = list(socket.children)
        if not pieces:
            continue
        bpy.ops.object.select_all(action='DESELECT')
        for obj in pieces:
            obj.hide_set(False)
            obj.select_set(True)
        bpy.context.view_layer.objects.active = pieces[0]
        bpy.ops.object.convert(target='MESH')
        bpy.ops.object.join()
        obj = bpy.context.object
        obj.name = 'accessory_' + group
        accessories.append(obj)
    body = [obj for obj in rig.children if obj.type == 'MESH' and
            any(part in obj.name for part in ('Character', 'Chicken', 'ShoulderPad', 'Body', 'Head'))]
    exported = [rig, placement, *body, *accessories]
    exported += [bpy.data.objects[key + '/socket_' + group] for group in ('headwear', 'eyewear', 'back')]
    if chicken:
        for i, name in enumerate(('Idle', 'Look', 'Walk', 'Rest')):
            bake(rig, 'Menu_' + name, samples[i])
    else:
        for pose_index, pose_name in enumerate(POSES):
            for weapon in WEAPONS:
                clip_name = 'Menu_' + pose_name + '_' + weapon
                print('Baking', key, clip_name, flush=True)
                held = pose_index < 2
                crouch = pose_index == 1
                pistol = weapon == 'Pistol'
                knife = weapon == 'Knife_1'
                scale = {'AK': .8, 'SMG': .8, 'Sniper': .78, 'Shotgun': .8,
                         'Pistol': .65, 'Knife_1': .4}[weapon]
                origin = Vector((.015, 0, -.08) if pistol else (.02, 0, -.13))
                if knife:
                    origin = Vector((0, 0, 0))
                angle = 20 if pistol else 24
                rotation = Matrix.Rotation(math.radians(angle), 4, 'X') @ Matrix.Rotation(math.pi / 2, 4, 'Z')
                grip = Vector((-.12, -.52, .84) if pistol else (-.16, -.36, .73))
                if not crouch:
                    grip += Vector((0, .08, .39))
                if knife:
                    grip = Vector((-.27, -.35, .75 if crouch else 1.12))
                    rotation = Matrix.Rotation(math.radians(-30), 4, 'Z') @ Matrix.Rotation(math.radians(100), 4, 'X')
                support_distance = {'AK': -.36, 'SMG': -.26, 'Sniper': -.38, 'Shotgun': -.38}.get(weapon, 0)
                support = Vector((support_distance, 0, .11)) - origin
                posed = []
                for frame, source in samples[pose_index]:
                    restore(rig, source)
                    if held:
                        breath = .002 * math.sin(2 * math.pi * (frame - 1) / 120)
                        current_grip = grip + Vector((0, 0, breath))
                        right = current_grip + Vector((-.025, .04, .075))
                        left = current_grip + rotation.to_3x3() @ support * scale + Vector((.055, .035, .04))
                        if pistol:
                            left = current_grip + Vector((.075, -.035, .045))
                        solve_arm(rig, 'R', right, (-.52, -.05, .81 if crouch else 1.12))
                        if not knife:
                            solve_arm(rig, 'L', left, (.53, -.14, .8 if crouch else 1.1))
                        for side in ('R', 'L'):
                            if knife and side == 'L':
                                continue
                            target = current_grip if side == 'R' else left - Vector((.02, .02, .04))
                            for finger in ('Index1', 'Middle1', 'Pinky1'):
                                bone = rig.pose.bones[finger + '.' + side]
                                if (target - bone.head).length > .001:
                                    aim(bone, target - bone.head)
                            for bone in rig.pose.bones:
                                if bone.name.endswith('.' + side) and any(bone.name.startswith(prefix) for prefix in
                                        ('Index2', 'Index3', 'Middle2', 'Middle3', 'Pinky2', 'Pinky3')):
                                    bone.rotation_quaternion @= Quaternion((1, 0, 0), -.65)
                    posed.append((frame, capture(rig)))
                restore(rig, posed[0][1])
                if held:
                    matrix = Matrix.Translation(grip) @ rotation @ Matrix.Scale(scale, 4) @ Matrix.Translation(-origin)
                    bone = 'LowerArm.R'
                else:
                    # Side-back sling leaves the backpack and both arms clear.
                    position = Vector((-.36, .20, 1.05))
                    rotation = Matrix.Rotation(math.radians(-68), 4, 'Y')
                    if pistol or knife:
                        position = Vector((-.28, .02, .65))
                        rotation = Matrix.Rotation(math.radians(-85), 4, 'Y')
                    if knife:
                        rotation = Matrix.Rotation(math.pi, 4, 'X')
                    matrix = Matrix.Translation(position) @ rotation @ Matrix.Scale(scale, 4) @ Matrix.Translation(-origin)
                    bone = 'Torso' if not (pistol or knife) else 'UpperLeg.R'
                mount = attach_mesh(rig, 'weapon_' + pose_name + '_' + weapon, meshes[weapon], matrix, bone)
                exported.append(mount)
                muzzle = Vector((min(v.co.x for v in meshes[weapon].vertices), 0, .23))
                if knife:
                    muzzle = Vector((0, 0, max(v.co.z for v in meshes[weapon].vertices)))
                for role, position in (('grip_main', origin),
                                       ('grip_support', support + origin), ('muzzle', muzzle)):
                    socket = bpy.data.objects.new(mount.name + '/' + role, None)
                    scene.collection.objects.link(socket)
                    socket.parent = mount
                    socket.location = position
                    exported.append(socket)
                bake(rig, clip_name, posed)
    # NLA track names are the engine's exact clip IDs. Only selected actor exports.
    for track in rig.animation_data.nla_tracks:
        track.mute = False
    scene.frame_start = 1
    scene.frame_end = 121
    scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in exported:
        obj.hide_set(False)
        obj.hide_render = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=str(OUT / (key.lower() + '.glb')),
        export_format='GLB', use_selection=True, export_animation_mode='NLA_TRACKS',
        export_merge_animation='NLA_TRACK', export_force_sampling=True,
        export_sampling_interpolation_fallback='LINEAR', export_skins=True,
        export_extras=False, export_cameras=False, export_lights=False,
        export_optimize_animation_size=True)
    # Blender names are global. Free the runtime node IDs for the next actor.
    for obj in exported:
        if obj.name.startswith(('accessory_', 'weapon_')):
            obj.name = key + '/' + obj.name


OUT.mkdir(parents=True, exist_ok=True)
weapons = weapon_geometry()
for actor in ('T', 'CT', 'CHICKEN'):
    export_actor(actor, weapons)
print('Menu model export complete')
