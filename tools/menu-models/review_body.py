"""Review separated limb surfaces against the trunk and the other limbs."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser()
parser.add_argument('--models', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--frame-step', type=int, default=30)
parser.add_argument('--actors', nargs='+', choices=('t', 'ct'), default=('t', 'ct'))
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
if args.frame_step < 1:
    parser.error('--frame-step must be positive')
frames = sorted(set(range(0, 120, args.frame_step)) | {119})
parts = ('Arm.L', 'Arm.R', 'Leg.L', 'Leg.R', 'Trunk')
pairs = [('Arm.L', 'Arm.R'), ('Leg.L', 'Leg.R')]
pairs += [(part, 'Trunk') for part in parts[:-1]]
pairs += [(arm, leg) for arm in parts[:2] for leg in parts[2:4]]
findings, hashes, neutral_contacts = [], {}, {}


def body_part(name):
    if name.startswith(('UpperArm.', 'LowerArm.')):
        return 'Arm.' + name[-1]
    if name.startswith(('UpperLeg.', 'LowerLeg.', 'Foot.', 'Toe.', 'Toes.')):
        return 'Leg.' + name[-1]
    return 'Trunk'


for actor in args.actors:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    data = (args.models.resolve() / (actor + '.glb')).read_bytes()
    hashes[actor] = hashlib.sha256(data).hexdigest()
    length = struct.unpack_from('<I', data, 12)[0]
    document = json.loads(data[20:20 + length])
    mesh_names = {node['name'] for node in document['nodes'] if 'mesh' in node}
    bpy.ops.import_scene.gltf(filepath=str(args.models.resolve() / (actor + '.glb')))
    rig = next(obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE')
    rig.animation_data_clear()
    rig.animation_data_create()
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    body = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH' and obj.name in mesh_names
            and obj.name != 'Hands' and not obj.name.startswith(('weapon_', 'accessory_'))]
    selected = {part: [] for part in parts}
    labels_by_face = {part: [] for part in parts}
    offsets, rest_keys, rest_points = {}, [], []
    offset = 0
    for obj in body:
        offsets[obj.name] = offset
        rest = [rig.matrix_world.inverted() @ obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
        rest_keys.extend(tuple(round(value, 5) for value in point) for point in rest)
        rest_points.extend(point.copy() for point in rest)
        labels = []
        for vertex in obj.data.vertices:
            name = obj.parent_bone if obj.parent_type == 'BONE' else ''
            if vertex.groups:
                name = obj.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
            labels.append(body_part(name))
        for polygon in obj.data.polygons:
            labels_here = {labels[index] for index in polygon.vertices}
            part = next(iter(labels_here)) if len(labels_here) == 1 else 'Trunk'
            if part != 'Trunk':
                root_name = ('UpperArm.' if part.startswith('Arm') else 'UpperLeg.') + part[-1]
                root = rig.data.bones[root_name].head_local
                seam = .14 if part.startswith('Arm') else .17
                if any((rest[index] - root).length < seam for index in polygon.vertices):
                    continue
            selected[part].append(tuple(offset + index for index in polygon.vertices))
            labels_by_face[part].append({'object': obj.name,
                'material': obj.data.materials[polygon.material_index].name,
                'vertices': list(polygon.vertices)})
        offset += len(obj.data.vertices)
    if any(len(selected[part]) < 20 for part in parts):
        raise RuntimeError(f'{actor}: incomplete semantic body partition')
    print('BODY PARTS', actor, {part: len(faces) for part, faces in selected.items()}, flush=True)
    neutral_trees = {part: BVHTree.FromPolygons(rest_points, faces) for part, faces in selected.items()}
    attachments = {(first, second): set(neutral_trees[first].overlap(neutral_trees[second]))
                   for first, second in pairs}
    neutral_contacts[actor] = {first + '/' + second: len(contacts)
                               for (first, second), contacts in attachments.items()}
    for action in [action for action in bpy.data.actions if action.name.startswith('Menu_')]:
        rig.animation_data.action = action
        rig.animation_data.action_slot = action.slots[0]
        for frame in frames:
            bpy.context.scene.frame_set(frame)
            points = []
            depsgraph = bpy.context.evaluated_depsgraph_get()
            for obj in body:
                evaluated = obj.evaluated_get(depsgraph)
                mesh = evaluated.to_mesh()
                points.extend(evaluated.matrix_world @ vertex.co for vertex in mesh.vertices)
                evaluated.to_mesh_clear()
            trees = {part: BVHTree.FromPolygons(points, faces) for part, faces in selected.items()}
            for first, second in pairs:
                hits = []
                for a, b in trees[first].overlap(trees[second]):
                    if (a, b) in attachments[first, second]:
                        continue
                    first_keys = {rest_keys[index] for index in selected[first][a]}
                    second_keys = {rest_keys[index] for index in selected[second][b]}
                    if not first_keys.intersection(second_keys):
                        hits.append((a, b))
                if hits:
                    a, b = hits[0]
                    first_center = sum((points[index] for index in selected[first][a]), points[0] * 0) / len(selected[first][a])
                    second_center = sum((points[index] for index in selected[second][b]), points[0] * 0) / len(selected[second][b])
                    findings.append({'actor': actor, 'clip': action.name, 'frame': frame,
                                     'first': first, 'second': second, 'crossing_pairs': len(hits),
                                     'first_face': labels_by_face[first][a],
                                     'second_face': labels_by_face[second][b],
                                     'first_center': [round(value, 5) for value in first_center],
                                     'second_center': [round(value, 5) for value in second_center]})
        print('BODY REVIEW', actor, action.name, flush=True)
report = {'status': 'rejected' if findings else 'requires_visual_review',
          'asset_sha256': hashes, 'sample_frames': frames, 'frames_per_second': 30,
          'neutral_attachment_contacts': neutral_contacts,
          'scope': 'Limb/trunk and opposing-limb surface intersections. Rest shoulder attachment zones within 14 cm, hip zones within 17 cm, triangles sharing rest-position seam vertices, and exact face pairs already intersecting at neutral garment assembly seams are excluded. All attachment zones require visual review.',
          'findings': findings}
args.report.resolve().parent.mkdir(parents=True, exist_ok=True)
args.report.resolve().write_text(json.dumps(report, indent=2), encoding='utf-8')
print('BODY FINDINGS', len(findings), flush=True)
if findings:
    raise RuntimeError('Body articulation review rejected candidate')
