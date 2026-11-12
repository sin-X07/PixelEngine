"""Measure actual exported hand/weapon surface gaps using Blender's evaluated meshes."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser()
parser.add_argument('--models', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--frame-step', type=int, default=30)
parser.add_argument('--subframes', type=int, default=1)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
if args.frame_step < 1:
    parser.error('--frame-step must be positive')
if args.subframes < 1:
    parser.error('--subframes must be positive')
sample_frames = sorted({frame + subframe / args.subframes
                        for frame in sorted(set(range(0, 120, args.frame_step)) | {119})
                        for subframe in range(args.subframes)})
rows = []
hashes = {}
for actor in ('t', 'ct'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = 30
    path = args.models / (actor + '.glb')
    hashes[actor] = hashlib.sha256(path.read_bytes()).hexdigest()
    bpy.ops.import_scene.gltf(filepath=str(path.resolve()))
    rig = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
    hands = bpy.data.objects['Hands']
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.animation_data.action = None
    for action in list(bpy.data.actions):
        if not action.name.startswith(('Menu_Idle_', 'Menu_Crouch_', 'Menu_FistPalm_')):
            continue
        rig.animation_data.action = action
        if action.slots:
            rig.animation_data.action_slot = action.slots[0]
        weapon = bpy.data.objects['weapon_' + action.name.removeprefix('Menu_')]
        for frame in sample_frames:
            scene.frame_set(int(frame), subframe=frame % 1)
            depsgraph = bpy.context.evaluated_depsgraph_get()
            evaluated = hands.evaluated_get(depsgraph)
            mesh = evaluated.to_mesh()
            points = [evaluated.matrix_world @ vertex.co for vertex in mesh.vertices]
            gun = weapon.evaluated_get(depsgraph)
            geometry = gun.to_mesh()
            gun_points = [gun.matrix_world @ vertex.co for vertex in geometry.vertices]
            weapon_tree = BVHTree.FromPolygons(gun_points,
                                       [tuple(face.vertices) for face in geometry.polygons])
            for side in ('R', 'L'):
                if side == 'R' and action.name.startswith('Menu_FistPalm_'):
                    continue
                if side == 'L' and action.name.endswith('Knife_1') and not action.name.startswith('Menu_FistPalm_'):
                    continue
                tree = weapon_tree
                target_points = gun_points
                target_name = 'weapon'
                opposite_tree = None
                if side == 'L' and (action.name.endswith('Pistol') or action.name.startswith('Menu_FistPalm_')):
                    right = [hands.vertex_groups[max(vertex.groups, key=lambda item: item.weight).group].name
                             for vertex in hands.data.vertices]
                    polygons = [tuple(face.vertices) for face in mesh.polygons
                                if all(right[i].endswith('.R') and not right[i].startswith('LowerArm.')
                                       for i in face.vertices)]
                    opposite_tree = BVHTree.FromPolygons(points, polygons)
                    if action.name.startswith('Menu_FistPalm_'):
                        tree = opposite_tree
                        target_points = [points[i] for i in {index for face in polygons for index in face}]
                        target_name = 'covering_right_fist'
                eligible = []
                by_part = {}
                crossings_by_part = {}
                labels = []
                for vertex in hands.data.vertices:
                    group = hands.vertex_groups[max(vertex.groups, key=lambda item: item.weight).group].name
                    eligible.append(group.endswith('.' + side) and
                                    group.startswith(('Hand.', 'Index', 'Middle', 'Pinky', 'Thumb')))
                    labels.append(group.split('.')[0].rstrip('123') if eligible[-1] else '')
                    if eligible[-1]:
                        part = group.split('.')[0].rstrip('123')
                        by_part.setdefault(part, []).append(tree.find_nearest(points[vertex.index])[3])
                # Sample both surfaces. A narrow grip may touch the middle of a
                # broad palm face while remaining far from every palm vertex.
                for part, values in by_part.items():
                    polygons = [tuple(face.vertices) for face in mesh.polygons
                                if all(labels[i] == part for i in face.vertices)]
                    if polygons:
                        part_tree = BVHTree.FromPolygons(points, polygons)
                        crossings_by_part[part] = len(part_tree.overlap(tree))
                        values.extend(part_tree.find_nearest(point)[3] for point in target_points)
                faces = [tuple(face.vertices) for face in mesh.polygons if all(eligible[i] for i in face.vertices)]
                hand_tree = BVHTree.FromPolygons(points, faces)
                rows.append(dict(actor=actor, clip=action.name, frame=frame, side=side,
                                 minimum_gap=min(min(values) for values in by_part.values()),
                                 crossing_faces=len(hand_tree.overlap(tree)),
                                 target=target_name, weapon_crossing_faces=len(hand_tree.overlap(weapon_tree)),
                                 opposite_hand_crossing_faces=len(hand_tree.overlap(opposite_tree)) if opposite_tree else 0,
                                 opposite_hand_minimum_gap=min(opposite_tree.find_nearest(point)[3]
                                     for point, keep in zip(points, eligible) if keep) if opposite_tree else None,
                                 crossings_by_part=crossings_by_part,
                                 minimum_gap_by_part={part: min(values) for part, values in by_part.items()}))
            evaluated.to_mesh_clear()
            gun.to_mesh_clear()
args.report.parent.mkdir(parents=True, exist_ok=True)
failures = [row for row in rows if row['crossing_faces'] or row['weapon_crossing_faces'] or
            row['opposite_hand_crossing_faces'] or row['minimum_gap_by_part']['Hand'] > .003]
args.report.write_text(json.dumps(dict(hashes=hashes, samples=rows, failures=failures,
    frames_per_second=30, samples_per_frame=args.subframes, sample_frames=sample_frames,
    criteria='Palm contact within 3 mm, with no hand/target, hand/weapon or opposing-hand surface intersections.'),
    indent=2), encoding='utf-8')
print('CONTACT REVIEW SAMPLES', len(rows), 'MAX MINIMUM GAP', max(row['minimum_gap'] for row in rows), flush=True)
print('CONTACT REVIEW FAILURES', len(failures), flush=True)
if failures:
    raise SystemExit(1)
