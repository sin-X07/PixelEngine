"""Check exposed palms and digits against the character, including their own palm."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
from solid_review import clear_cache, solid_surfaces, penetration_depths, ray_inside, RAYS

parser = argparse.ArgumentParser()
parser.add_argument('--models', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--frame-step', type=int, default=1)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
if args.frame_step < 1:
    parser.error('--frame-step must be positive')
frames = sorted(set(range(0, 120, args.frame_step)) | {119})
findings, hashes = [], {}


def surface(obj):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    points = [evaluated.matrix_world @ vertex.co for vertex in mesh.vertices]
    faces = [tuple(polygon.vertices) for polygon in mesh.polygons]
    evaluated.to_mesh_clear()
    return points, faces


for actor in ('t', 'ct'):
    clear_cache()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    asset = args.models.resolve() / (actor + '.glb')
    hashes[actor] = hashlib.sha256(asset.read_bytes()).hexdigest()
    bpy.ops.import_scene.gltf(filepath=str(asset))
    rig = next(obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE')
    hands = bpy.data.objects['Hands']
    body = [obj for obj in rig.children if obj.type == 'MESH' and obj != hands
            and not obj.name.startswith(('weapon_', 'accessory_'))]
    # Bone-parented armor is also part of the visible character.
    body += [obj for obj in bpy.context.scene.objects if obj.type == 'MESH'
             and obj.name.startswith('ShoulderPad.') and obj not in body]
    names = [hands.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
             for vertex in hands.data.vertices]
    exposed = {}
    palm_faces, tip_vertices = {}, {}
    for side in ('L', 'R'):
        bone = rig.data.bones['Hand.' + side]
        inverse = bone.matrix_local.inverted()
        allowed = [name.endswith('.' + side) and (
            name.startswith(('Index', 'Middle', 'Pinky', 'Thumb')) or
            (name == 'Hand.' + side and (inverse @ vertex.co).y > .012))
            for vertex, name in zip(hands.data.vertices, names)]
        exposed[side] = [tuple(face.vertices) for face in hands.data.polygons
                         if all(allowed[index] for index in face.vertices)]
        palm_faces[side] = [tuple(face.vertices) for face in hands.data.polygons
                           if all(names[index] == 'Hand.' + side for index in face.vertices)]
        tip_vertices[side] = []
        for index, name in enumerate(names):
            if name.endswith('3.' + side) or name == 'Thumb2.' + side:
                digit = rig.data.bones[name]
                along = (digit.matrix_local.inverted() @ hands.data.vertices[index].co).y
                if along > digit.length * .78:
                    tip_vertices[side].append(index)
    rig.animation_data_clear()
    rig.animation_data_create()
    actions = [action for action in bpy.data.actions if action.name.startswith('Menu_')]
    for action in actions:
        rig.animation_data.action = action
        rig.animation_data.action_slot = action.slots[0]
        for frame in frames:
            bpy.context.scene.frame_set(frame)
            hand_points, _ = surface(hands)
            for side in ('L', 'R'):
                hand = BVHTree.FromPolygons(hand_points, exposed[side])
                used = sorted({index for face in exposed[side] for index in face})
                for obj in body:
                    points, faces = surface(obj)
                    target = BVHTree.FromPolygons(points, faces)
                    overlap = hand.overlap(target)
                    crossing = len(overlap)
                    witnesses = []
                    contained = penetration_depths([hand_points[index] for index in used],
                                                  solid_surfaces(obj, points), witnesses)
                    if crossing or contained:
                        contact_groups = sorted({names[index] for first, _ in overlap
                                                 for index in exposed[side][first]})
                        findings.append({'actor': actor, 'clip': action.name, 'frame': frame,
                                         'hand': side, 'body': obj.name, 'crossing_triangles': crossing,
                                         'contact_groups': contact_groups,
                                         'contained_vertices': len(contained), 'witnesses': witnesses})
                palm = BVHTree.FromPolygons(hand_points, palm_faces[side])
                for index in tip_vertices[side]:
                    point = hand_points[index]
                    _, _, _, distance = palm.find_nearest(point)
                    if distance > .0005 and all(ray_inside(palm, point, direction) for direction in RAYS):
                        findings.append({'actor': actor, 'clip': action.name, 'frame': frame,
                                         'hand': side, 'body': 'own_palm', 'digit': names[index],
                                         'vertex': index, 'depth_m': round(distance, 6)})
        print('HANDS BODY REVIEW', actor, action.name, flush=True)
report = {'status': 'rejected' if findings else 'requires_visual_review',
          'asset_sha256': hashes, 'sample_frames': frames, 'frames_per_second': 30,
          'scope': 'Exposed palms and all digits against the character, plus distal digits inside their own palm. Proximal wrist attachment surfaces are excluded.',
          'findings': findings}
args.report.resolve().parent.mkdir(parents=True, exist_ok=True)
args.report.resolve().write_text(json.dumps(report, indent=2), encoding='utf-8')
print('HANDS BODY FINDINGS', len(findings), flush=True)
if findings:
    raise RuntimeError('Hands/body articulation review rejected candidate')
