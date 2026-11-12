"""Bake the reviewed rigs into menu GLBs. Run with Blender --background --python.

Prepared reconstruction scenes retain editable rigs, accessories and held poses.
Weapons use Quaternius geometry. All coordinates below are Blender Z-up meters.
"""
import math
import sys
import argparse
from pathlib import Path

import bpy
import bmesh
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True
from hand_rig import prepare_hands, pose_hand, finger_clearance_offset
from contact_fit import hand_offset, fit_grip_digits
from body_refinement import (weld_deformation_seams, repair_torso_weights,
                             tailor_jacket_sides, tailor_forearms, rebuild_shoulder_armor,
                             broaden_arm_mounts, tailor_armored_trousers, tailor_vest_hem,
                             close_sleeve_roots)
from accessory_refinement import fit_backpack
from backpack_design import rebuild_backpack, add_back_padding
from headwear_design import rebuild_chicken_visor, rebuild_field_cap
from chicken_refinement import refine_chicken_materials
from arm_clearance import fit_elbow
from eyewear_design import rebuild_eyewear
from headset_design import rebuild_headset
from prepared_scene import tag_actor, export_prepared, save_prepared
from weapon_mounts import rig_surface, add_knife_sheath, add_retention_mounts
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output-dir', type=Path, default=ROOT / 'assets/models/menu')
parser.add_argument('--source', type=Path, default=HERE / 'CharacterLab.blend')
parser.add_argument('--reconstruct', action='store_true')
parser.add_argument('--save-source', type=Path)
parser.add_argument('--actors', nargs='+', choices=('T', 'CT', 'CHICKEN'), default=('T', 'CT', 'CHICKEN'))
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
OUT = args.output_dir.resolve()
WEAPONS = ('AK', 'SMG', 'Sniper', 'Shotgun', 'Pistol', 'Knife_1')
POSES = ('Idle', 'Crouch', 'HandsOnHips', 'FistPalm')
FRAMES = range(1, 122, 3)
bpy.ops.wm.open_mainfile(filepath=str(args.source.resolve()))
scene = bpy.context.scene
scene.render.fps = 30


def capture(rig):
    return {b.name: (b.location.copy(), b.rotation_quaternion.copy(), b.scale.copy())
            for b in rig.pose.bones}


def restore(rig, pose):
    for bone in rig.pose.bones:
        if bone.name in pose:
            bone.location, bone.rotation_quaternion, bone.scale = pose[bone.name]
        else:
            bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def aim(bone, direction):
    matrix = bone.matrix.copy()
    head = bone.head.copy()
    rotation = (bone.tail - head).rotation_difference(direction)
    matrix.translation = Vector()
    bone.matrix = Matrix.Translation(head) @ rotation.to_matrix().to_4x4() @ matrix
    bpy.context.view_layer.update()


def solve_arm(rig, side, target, pole):
    solve_limb(rig, 'UpperArm.' + side, 'LowerArm.' + side, target, pole)


def solve_limb(rig, upper_name, lower_name, target, pole):
    upper = rig.pose.bones[upper_name]
    lower = rig.pose.bones[lower_name]
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
        raise RuntimeError(f'{rig.name} {upper_name} unreachable target, error {error}, root {tuple(head)}, target {tuple(target)}')


def balance_crouch(rig):
    """Keep planted ankles while opening the angle between chest and thighs."""
    ankles = {side: rig.pose.bones['LowerLeg.' + side].tail.copy() for side in ('L', 'R')}
    rig.pose.bones['Body'].rotation_quaternion = Matrix.Rotation(math.radians(8), 4, 'X').to_quaternion()
    rig.pose.bones['Abdomen'].rotation_quaternion = Matrix.Rotation(math.radians(7), 4, 'X').to_quaternion()
    bpy.context.view_layer.update()
    pelvis = rig.pose.bones['Body'].matrix.copy()
    pelvis.translation.z += .025
    pelvis.translation.y += .025
    rig.pose.bones['Body'].matrix = pelvis
    bpy.context.view_layer.update()
    for side, sign in (('L', 1), ('R', -1)):
        solve_limb(rig, 'UpperLeg.' + side, 'LowerLeg.' + side,
                   ankles[side], (sign * .32, -.70, .48))


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
    strip = track.strips.new(name, 0, action)
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
                # These two meshes use -Y as their local top and Z as thickness.
                mesh.transform(Matrix.Rotation(-math.pi / 2, 4, 'X'))
        meshes[name] = mesh
        if args.reconstruct:
            if name not in ('Pistol', 'Knife_1'):
                for vertex in mesh.vertices:
                    if vertex.co.x > .1:
                        vertex.co.x = .1 + (vertex.co.x - .1) * .65
            geometry = bmesh.new()
            geometry.from_mesh(mesh)
            edges = [edge for edge in geometry.edges if edge.calc_length() > .07]
            if edges:
                bmesh.ops.subdivide_edges(geometry, edges=edges, cuts=1, use_grid_fill=True)
            geometry.to_mesh(mesh)
            geometry.free()
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


def fit_stowed_weapon(rig, body, mesh, matrix, sidearm):
    """Fit the weapon's inner face against the evaluated torso or outer thigh."""
    surface = rig_surface(rig, body)
    outward = Vector((-1, 0, 0) if sidearm else (0, 1, 0))
    corrections = []
    for vertex in mesh.vertices:
        point = matrix @ vertex.co
        hit, normal, _, _ = surface.ray_cast(point + outward * 2, -outward, 4)
        if hit is not None:
            corrections.append((hit - point).dot(outward) + .012)
    if not corrections:
        raise RuntimeError(f'{rig.name}: stowed weapon has no body contact')
    matrix.translation += outward * max(corrections)
    if args.reconstruct:
        weapon_faces = [tuple(polygon.vertices) for polygon in mesh.polygons]
        for step in range(101):
            points = [matrix @ vertex.co for vertex in mesh.vertices]
            if not surface.overlap(BVHTree.FromPolygons(points, weapon_faces)):
                matrix.translation += outward * .008
                return matrix
            matrix.translation += outward * .002
        raise RuntimeError(f'{rig.name}: stowed weapon surface clearance failed')
    return matrix


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
        # The final timeline frame belongs to the next authored pose segment.
        # Close the source loop before solving contacts and arm reach.
        clip[-1] = (121, clip[0][1])
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
    if args.reconstruct:
        weld_deformation_seams(body)
        if chicken:
            refine_chicken_materials(body[0])
        if not chicken:
            repair_torso_weights(body)
            tailor_jacket_sides(body)
            tailor_forearms(rig, body)
            tailor_armored_trousers(rig, body)
            tailor_vest_hem(body)
            broaden_arm_mounts(rig, body, .12 if key == 'CT' else .10)
    if not chicken:
        hands = prepare_hands(rig, body, .7 if args.reconstruct else 1.)[0]
        gesture_mesh = bpy.data.meshes.new(key + '_GestureContact')
        body.append(hands)
    if args.reconstruct:
        restore(rig, samples[0][0][1])
        if not chicken:
            rebuild_shoulder_armor(rig, body)
            body.extend(close_sleeve_roots(rig, body))
            rebuild_backpack(rig, next(obj for obj in accessories if obj.name == 'accessory_back'))
            if key == 'T':
                rebuild_field_cap(rig, next(obj for obj in accessories if obj.name == 'accessory_headwear'), body)
            else:
                rebuild_headset(rig, next(obj for obj in accessories if obj.name == 'accessory_headwear'), body)
        else:
            rebuild_chicken_visor(rig, next(obj for obj in accessories if obj.name == 'accessory_headwear'), body)
        rebuild_eyewear(rig, next(obj for obj in accessories if obj.name == 'accessory_eyewear'), body, chicken)
        if not chicken:
            fit_backpack(rig, body, next(obj for obj in accessories if obj.name == 'accessory_back'))
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
                if args.reconstruct:
                    origin = Vector({'AK': (.025, 0, .03), 'SMG': (.045, 0, .015),
                                     'Sniper': (.04, 0, -.005), 'Shotgun': (-.05, 0, .005),
                                     'Pistol': (.015, 0, .02), 'Knife_1': (0, 0, -.02)}[weapon])
                    scale = {'SMG': .65, 'Shotgun': .60, 'Sniper': .70}.get(weapon, scale)
                angle = 20 if pistol else 24
                rotation = Matrix.Rotation(math.radians(angle), 4, 'X') @ Matrix.Rotation(math.pi / 2, 4, 'Z')
                grip = Vector((-.12, -.52, .84) if pistol else (-.16, -.36, .73))
                if not crouch:
                    grip += Vector((0, .08, .39))
                if args.reconstruct and not knife:
                    angle = {'AK': 25, 'SMG': 18, 'Sniper': 25, 'Shotgun': 15, 'Pistol': 8}[weapon]
                    yaw = 110 if pistol else 130
                    if weapon == 'Shotgun':
                        yaw = 120
                    rotation = Matrix.Rotation(math.radians(angle), 4, 'X') @ Matrix.Rotation(math.radians(yaw), 4, 'Z')
                    # Keep the grip below the chest in each source torso pose.
                    grip = Vector((-.12, -.48, .74 if crouch else 1.08) if pistol else (-.22, -.44, .80 if crouch else .94))
                    if weapon in ('AK', 'Sniper', 'Shotgun'):
                        grip.y = {'AK': -.36, 'Sniper': -.40, 'Shotgun': (-.20 if crouch else -.32)}[weapon]
                        grip.z = (.78 if crouch else 1.13) if weapon == 'Shotgun' else (.76 if crouch else .99)
                if knife:
                    grip = Vector((-.27, -.35, .75 if crouch else 1.12))
                    if args.reconstruct:
                        grip = Vector((-.30, -.34, .75 if crouch else 1.05))
                    rotation = Matrix.Rotation(math.radians(-30), 4, 'Z') @ Matrix.Rotation(math.radians(100), 4, 'X')
                    if args.reconstruct:
                        rotation = Matrix.Rotation(math.radians(-15), 4, 'Z') @ Matrix.Rotation(math.radians(15), 4, 'X')
                if args.reconstruct and crouch:
                    grip.y += .09
                    grip.z += .05
                if args.reconstruct and weapon == 'SMG':
                    grip.y += .07
                if args.reconstruct and crouch and weapon == 'Shotgun':
                    grip.y -= .10
                    grip.z -= .04
                support_distance = {'AK': -.36, 'SMG': -.26, 'Sniper': -.38, 'Shotgun': -.38}.get(weapon, 0)
                if args.reconstruct:
                    support_distance = {'AK': -.24, 'SMG': -.22, 'Sniper': -.28, 'Shotgun': -.28}.get(weapon, 0)
                support = Vector((support_distance, 0, .11)) - origin
                if args.reconstruct and not (pistol or knife):
                    support_x = {'AK': -.58, 'SMG': -.78, 'Sniper': -.52, 'Shotgun': -.94}[weapon]
                    mesh = meshes[weapon]
                    surface = BVHTree.FromPolygons([vertex.co for vertex in mesh.vertices],
                        [tuple(face.vertices) for face in mesh.polygons])
                    bottom = min(vertex.co.z for vertex in mesh.vertices) - .1
                    contact, _, _, _ = surface.ray_cast(Vector((support_x, 0, bottom)), Vector((0, 0, 1)), 2)
                    if contact is None:
                        raise RuntimeError(f'{weapon}: support socket has no underside surface')
                    support = contact - origin
                posed = []
                elbow_rolls = {}
                digit_poses = {}
                right_pole = (-.52, -.05, .81 if crouch else 1.12)
                left_pole = (.53, -.14, .8 if crouch else 1.1)
                if args.reconstruct:
                    right_pole = (-.50, -.05, .30 if crouch else .65)
                    left_pole = (.50, -.05, .40 if crouch else .70)
                    if crouch and pistol:
                        left_pole = (.50, -.35, .70)
                    if crouch and weapon == 'Shotgun':
                        right_pole = (-.60, -.15, .90)
                for frame, source in samples[pose_index]:
                    restore(rig, source)
                    if args.reconstruct and crouch:
                        balance_crouch(rig)
                    if held:
                        breath = .002 * math.sin(2 * math.pi * (frame - 1) / 120)
                        current_grip = grip + Vector((0, 0, breath))
                        right = current_grip + Vector((-.025, .04, .075))
                        left = current_grip + rotation.to_3x3() @ support * scale + Vector((.055, .035, .04))
                        if pistol:
                            left = current_grip + Vector((.15, -.015, -.125))
                        if args.reconstruct:
                            right = current_grip + rotation.to_3x3() @ Vector((.085, .045, 0))
                            left = current_grip + rotation.to_3x3() @ (Vector((.04, -.105, -.065)) if pistol else support * scale + Vector((0, -.105, -.035)))
                        solve_arm(rig, 'R', right, right_pole)
                        if not knife:
                            solve_arm(rig, 'L', left, left_pole)
                        pose_hand(rig, 'R', current_grip - right, (-1, 0, 0), .95,
                                  index_curl=.35 if not knife else .95)
                        if knife:
                            pose_hand(rig, 'L', (0, -.1, -1), (0, -1, .1), .25)
                        elif pistol:
                            pose_hand(rig, 'L', (-1, -.1, .25), (0, 0, -1), .65)
                            if not args.reconstruct:
                                offset = finger_clearance_offset(rig, hands, 2, False)
                                solve_arm(rig, 'L', left + offset, left_pole)
                                pose_hand(rig, 'L', (-1, -.1, .25), (0, 0, -1), .65)
                        else:
                            target = current_grip + rotation.to_3x3() @ support * scale
                            pose_hand(rig, 'L', target - left, (0, 0, -1), .80)
                        if args.reconstruct:
                            weapon_matrix = Matrix.Translation(current_grip) @ rotation @ Matrix.Scale(scale, 4) @ Matrix.Translation(-origin)
                            right = current_grip + rotation.to_3x3() @ Vector((.075, .055, -.035))
                            right_forward = rotation.to_3x3() @ Vector((-.75, 0, -1))
                            right_outward = rotation.to_3x3() @ Vector((0, 1, 0))
                            if weapon == 'Shotgun':
                                # Approach the straight stock wrist from below the wider butt.
                                right = current_grip + rotation.to_3x3() @ Vector((.035, .055, -.035))
                                right_forward = rotation.to_3x3() @ Vector((-1, 0, .25))
                            elif key == 'CT' and crouch:
                                right_forward = rotation.to_3x3() @ Vector((-.35, 0, -1))
                            if knife:
                                right = current_grip + rotation.to_3x3() @ Vector((0, .09, 0))
                                right_forward = rotation.to_3x3() @ Vector((0, -1, 0))
                                right_outward = rotation.to_3x3() @ Vector((-1, 0, 0))
                            solve_arm(rig, 'R', right, right_pole)
                            pose_hand(rig, 'R', right_forward, right_outward, 1.1, index_curl=1.1 if knife else .55, thumb_curl=.65)
                            offset = hand_offset(rig, hands, 'R', meshes[weapon], weapon_matrix, right_outward, palm_only=True)
                            solve_arm(rig, 'R', right + offset, right_pole)
                            pose_hand(rig, 'R', right_forward, right_outward, 1.1, index_curl=1.1 if knife else .55, thumb_curl=.65)
                            digit_poses['R'] = fit_grip_digits(rig, hands, 'R', meshes[weapon], weapon_matrix, digit_poses.get('R'))
                            if knife:
                                free_hand = Vector((.26, -.40, .63) if crouch else (.38, -.05, .77))
                                solve_arm(rig, 'L', free_hand, (.50, 0, .55 if crouch else .85))
                                pose_hand(rig, 'L', (0, 0, -1), (1, 0, 0), .25)
                            if not knife:
                                left = current_grip + rotation.to_3x3() @ (Vector((.04, -.065, -.065)) if pistol else support * scale + Vector((0, -.065, -.035)))
                                left_forward = rotation.to_3x3() @ Vector((0, 1, 0))
                                left_outward = rotation.to_3x3() @ Vector((0, 0, -1))
                                if pistol:
                                    left = current_grip + rotation.to_3x3() @ Vector((-.01, -.09, -.03))
                                    left_forward = rotation.to_3x3() @ Vector((0, 0, -1))
                                    left_outward = rotation.to_3x3() @ Vector((0, -1, 0))
                                solve_arm(rig, 'L', left, left_pole)
                                pose_hand(rig, 'L', left_forward, left_outward, .7 if pistol else 1.)
                                offset = hand_offset(rig, hands, 'L', meshes[weapon], weapon_matrix, left_outward, palm_only=True, brace=pistol)
                                solve_arm(rig, 'L', left + offset, left_pole)
                                pose_hand(rig, 'L', left_forward, left_outward, .7 if pistol else 1.)
                                digit_poses['L'] = fit_grip_digits(rig, hands, 'L', meshes[weapon], weapon_matrix, digit_poses.get('L'), brace=pistol)
                            for side, pole in (('R', right_pole), ('L', left_pole)):
                                elbow_rolls[side] = fit_elbow(rig, body, side, pole, meshes[weapon],
                                    weapon_matrix, solve_arm, elbow_rolls.get(side))
                    elif pose_name == 'HandsOnHips':
                        for side, sign in (('L', 1), ('R', -1)):
                            if args.reconstruct:
                                breath = .002 * math.sin(2 * math.pi * (frame - 1) / 120)
                                solve_arm(rig, side, Vector((sign * .33, -.04, 1.00 + breath)),
                                          (sign * .70, -.15, .95))
                            pose_hand(rig, side, (0, .12, -1), (sign, 0, 0), .20)
                    else:
                        breath = .002 * math.sin(2 * math.pi * (frame - 1) / 120)
                        right_height = 1.14 if args.reconstruct else 1.33
                        left_target = Vector((-.055, -.72, 1.14) if args.reconstruct else (.18, -.65, 1.50))
                        left_target.z += breath
                        left_direction = (-.18, 0, 1) if args.reconstruct else (-1, -.15, 0)
                        left_normal = (0, -1, 0) if args.reconstruct else (0, 0, 1)
                        solve_arm(rig, 'R', Vector((-.07, -.58, right_height + breath)), (-.64, -.09, 1.13))
                        solve_arm(rig, 'L', left_target, (.64, -.09, 1.13))
                        pose_hand(rig, 'R', (0, -1, .1), (0, 0, -1), 1.45 if args.reconstruct else 1.35)
                        pose_hand(rig, 'L', left_direction, left_normal, .35 if args.reconstruct else .50,
                                  thumb_curl=.8 if args.reconstruct else None, finger_fan=.82 if args.reconstruct else 1.)
                        if args.reconstruct:
                            offset = hand_offset(rig, hands, 'L', gesture_mesh, Matrix.Identity(4),
                                                 left_normal, palm_only=True, brace=True)
                        else:
                            offset = finger_clearance_offset(rig, hands, 2, True)
                        left = rig.pose.bones['LowerArm.L'].tail.copy() + offset
                        solve_arm(rig, 'L', left, (.64, -.09, 1.13))
                        pose_hand(rig, 'L', left_direction, left_normal, .35 if args.reconstruct else .50,
                                  thumb_curl=.8 if args.reconstruct else None, finger_fan=.82 if args.reconstruct else 1.)
                        if args.reconstruct:
                            digit_poses['L'] = fit_grip_digits(rig, hands, 'L', gesture_mesh, Matrix.Identity(4),
                                                              digit_poses.get('L'), brace=True)
                    posed.append((frame, capture(rig)))
                restore(rig, posed[0][1])
                mount_mesh = meshes[weapon]
                if args.reconstruct and not held:
                    mount_mesh = mount_mesh.copy()
                    if knife:
                        add_knife_sheath(mount_mesh)
                if held:
                    matrix = Matrix.Translation(grip) @ rotation @ Matrix.Scale(scale, 4) @ Matrix.Translation(-origin)
                    bone = 'Hand.R' if args.reconstruct else 'LowerArm.R'
                else:
                    # Long guns lie along the back. Sidearms lie against the thigh.
                    position = Vector((.08, .20, 1.35))
                    rotation = Matrix.Rotation(math.radians(-50 if args.reconstruct else -72), 4, 'Y')
                    if pistol or knife:
                        position = Vector((-.28, .02, .68))
                        rotation = Matrix.Rotation(math.pi / 2, 4, 'Z') @ Matrix.Rotation(math.radians(-85), 4, 'Y')
                    if knife:
                        rotation = Matrix.Rotation(math.pi / 2, 4, 'Z') @ Matrix.Rotation(math.pi, 4, 'X')
                    matrix = Matrix.Translation(position) @ rotation @ Matrix.Scale(scale, 4) @ Matrix.Translation(-origin)
                    if args.reconstruct and not (pistol or knife):
                        highest = max((matrix @ vertex.co).z for vertex in mount_mesh.vertices)
                        matrix.translation.z += rig.pose.bones['Neck'].head.z - .025 - highest
                    matrix = fit_stowed_weapon(rig, body, mount_mesh, matrix, pistol or knife)
                    bone = 'Torso' if not (pistol or knife) else 'UpperLeg.R'
                pack_mesh = mount_mesh.copy() if args.reconstruct and not held and not (pistol or knife) else None
                if args.reconstruct and not held:
                    add_retention_mounts(rig, body, mount_mesh, matrix, pistol or knife)
                mount = attach_mesh(rig, 'weapon_' + pose_name + '_' + weapon, mount_mesh, matrix, bone)
                exported.append(mount)
                mounts = [mount]
                if args.reconstruct and not held and not (pistol or knife):
                    backpack = next(obj for obj in accessories if obj.name == 'accessory_back')
                    pack_matrix = fit_stowed_weapon(rig, [*body, backpack], pack_mesh, matrix.copy(), False)
                    add_retention_mounts(rig, [*body, backpack], pack_mesh, pack_matrix, False)
                    pack_mount = attach_mesh(rig, mount.name + '_Backpack', pack_mesh, pack_matrix, bone)
                    exported.append(pack_mount)
                    mounts.append(pack_mount)
                muzzle = Vector((min(v.co.x for v in meshes[weapon].vertices), 0, .23))
                if knife:
                    muzzle = Vector((0, 0, max(v.co.z for v in meshes[weapon].vertices)))
                for current_mount in mounts:
                    for role, position in (('grip_main', origin),
                                           ('grip_support', support + origin), ('muzzle', muzzle)):
                        socket = bpy.data.objects.new(current_mount.name + '/' + role, None)
                        scene.collection.objects.link(socket)
                        socket.parent = current_mount
                        socket.location = position
                        exported.append(socket)
                bake(rig, clip_name, posed)
    if args.reconstruct and not chicken:
        add_back_padding(rig, body, next(obj for obj in accessories if obj.name == 'accessory_back'))
    # NLA track names are the engine's exact clip IDs. Only selected actor exports.
    for track in rig.animation_data.nla_tracks:
        track.mute = False
    scene.frame_start = 0
    scene.frame_end = 120
    scene.frame_set(0)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in exported:
        obj.hide_set(False)
        obj.hide_render = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=str(OUT / (key.lower() + '.glb')),
        export_format='GLB', use_selection=True, use_renderable=True, export_animation_mode='NLA_TRACKS',
        export_merge_animation='NLA_TRACK', export_force_sampling=True,
        export_sampling_interpolation_fallback='LINEAR', export_skins=True,
        export_extras=False, export_cameras=False, export_lights=False,
        export_optimize_animation_size=True)
    # Blender names are global. Free the runtime node IDs for the next actor.
    tag_actor(exported, key)
    for obj in exported:
        if obj.name.startswith(('accessory_', 'weapon_')) or obj.name == 'Hands':
            obj.name = key + '/' + obj.name


OUT.mkdir(parents=True, exist_ok=True)
if scene.get('menu_prepared_version') == 2:
    export_prepared(scene, OUT, args.actors)
else:
    weapons = weapon_geometry()
    for actor in args.actors:
        export_actor(actor, weapons)
    if args.save_source:
        save_prepared(scene, args.save_source)
print('Menu model export complete')
