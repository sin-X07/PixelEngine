"""Review exported menu assets with Blender. Exit nonzero on geometric failures.

Run Blender --background --python-exit-code 1 --python this_file --
    --models DIR --report DIR [--render]
Surface contact is measured separately from mesh connectivity. The report never
equates passing software tests with final art approval.
"""
import argparse
import json
import struct
import hashlib
import sys
from array import array
from pathlib import Path

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
from solid_review import clear_cache, solid_surfaces, penetration_depths

parser = argparse.ArgumentParser()
parser.add_argument('--models', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--render', action='store_true')
parser.add_argument('--render-only', action='store_true')
parser.add_argument('--frame-step', type=int, default=30)
parser.add_argument('--render-actor', choices=('t', 'ct', 'chicken'))
parser.add_argument('--render-clip')
parser.add_argument('--accessory', choices=('none', 'headwear', 'eyewear', 'back'), default='none')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
args.models = args.models.resolve()
args.report = args.report.resolve()
args.report.mkdir(parents=True, exist_ok=True)
if args.frame_step < 1:
    parser.error('--frame-step must be positive')
if args.render_only and not args.render:
    parser.error('--render-only requires --render')
sample_frames = sorted(set(range(0, 120, args.frame_step)) | {119})
results = []
normal_candidates = []
rendered_files = []
asset_hashes = {}
pair_cache = {}
pair_checks = 0
reused_checks = 0
script_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in Path(__file__).resolve().parent.glob('*.py')}


def surface(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    coordinates = array('f', [0.]) * (len(mesh.vertices) * 3)
    mesh.vertices.foreach_get('co', coordinates)
    coordinates.extend(value for column in evaluated.matrix_world for value in column)
    signature = (obj.name, len(mesh.polygons), hashlib.sha256(coordinates.tobytes()).digest())
    points = [evaluated.matrix_world @ vertex.co for vertex in mesh.vertices]
    faces = [tuple(polygon.vertices) for polygon in mesh.polygons]
    evaluated.to_mesh_clear()
    bounds = ([min(point[axis] for point in points) for axis in range(3)],
              [max(point[axis] for point in points) for axis in range(3)])
    return BVHTree.FromPolygons(points, faces), points, bounds, signature, {'object': obj, 'solids': None}


def relation(first, second):
    global pair_checks, reused_checks
    pair_checks += 1
    tree, points, bounds, signature, state = first
    other, other_points, other_bounds, other_signature, other_state = second
    key = tuple(sorted((signature, other_signature)))
    if key in pair_cache:
        reused_checks += 1
        return pair_cache[key]
    if any(bounds[1][axis] < other_bounds[0][axis] or other_bounds[1][axis] < bounds[0][axis]
           for axis in range(3)):
        return {'crossing_triangles': 0, 'penetrating_vertices': 0, 'max_depth_m': 0.,
                'normal_candidate_vertices': 0, 'normal_candidate_max_depth_m': 0.}
    # Inspect both directions. A contained tip may not cross a triangle surface.
    depths = []
    for vertices, target, target_bounds in ((points, other, other_bounds), (other_points, tree, bounds)):
        for point in vertices:
            if any(point[axis] < target_bounds[0][axis] or point[axis] > target_bounds[1][axis]
                   for axis in range(3)):
                continue
            closest, normal, _, distance = target.find_nearest(point)
            if closest is not None and (point - closest).dot(normal) < -.001:
                depths.append(distance)
    if state['solids'] is None:
        state['solids'] = solid_surfaces(state['object'], points)
    if other_state['solids'] is None:
        other_state['solids'] = solid_surfaces(other_state['object'], other_points)
    first_witnesses, second_witnesses = [], []
    contained = [*penetration_depths(points, other_state['solids'], first_witnesses),
                 *penetration_depths(other_points, state['solids'], second_witnesses)]
    measurement = {'crossing_triangles': len(tree.overlap(other)),
                   'penetrating_vertices': len(contained),
                   'max_depth_m': round(max((depth for depth, _ in contained), default=0), 5),
                   'solid_penetrating_vertices': sum(not proxy for _, proxy in contained),
                   'boundary_proxy_penetrating_vertices': sum(proxy for _, proxy in contained),
                   'unresolved_nonmanifold_components': state['solids'][1] + other_state['solids'][1],
                   'containment_witnesses': [*[dict(source=signature[0], **item) for item in first_witnesses],
                                            *[dict(source=other_signature[0], **item) for item in second_witnesses]],
                   'normal_candidate_vertices': len(depths),
                   'normal_candidate_max_depth_m': round(max(depths, default=0), 5)}
    pair_cache[key] = measurement
    return measurement


def render_views(actor, clip, objects):
    scene = bpy.context.scene
    weapon_node = 'weapon_' + clip.removeprefix('Menu_')
    if args.accessory == 'back' and bpy.data.objects.get(weapon_node + '_Backpack'):
        weapon_node += '_Backpack'
    for obj in objects:
        if obj.type == 'MESH':
            obj.hide_render = obj.name not in mesh_nodes or (
                obj.name.startswith(('weapon_', 'accessory_')) and
                obj.name not in (weapon_node, 'accessory_' + args.accessory))
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 16
    scene.render.resolution_x = 960
    scene.render.resolution_y = 960
    scene.render.resolution_percentage = 100
    scene.world = bpy.data.worlds.new('Review light')
    scene.world.color = (.18, .18, .18)
    target = Vector((0, 0, 1.05))
    lights = []
    for position in ((3, -4, 6), (-3, -2, 4), (0, 4, 5)):
        bpy.ops.object.light_add(type='AREA', location=position)
        light = bpy.context.object
        light.data.energy = 950
        light.data.shape = 'DISK'
        light.data.size = 4
        light.rotation_euler = (target - light.location).to_track_quat('-Z', 'Y').to_euler()
        lights.append(light)
    bpy.ops.object.camera_add()
    camera = bpy.context.object
    scene.camera = camera
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 2.9
    for label, position in (('front', (0, -6, 2.7)), ('side', (6, 0, 2.7)), ('back', (0, 6, 2.7))):
        camera.location = position
        camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = str(args.report / f'{actor}_{clip}_{args.accessory}_{label}.png')
        bpy.ops.render.render(write_still=True)
        rendered_files.append(scene.render.filepath)
    # Review-only scene objects are unlinked from the next render.
    for obj in [camera, *lights]:
        obj.hide_render = True


for actor in ('t', 'ct', 'chicken'):
    pair_cache.clear()
    clear_cache()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    asset = args.models / (actor + '.glb')
    data = asset.read_bytes()
    asset_hashes[actor] = hashlib.sha256(data).hexdigest()
    json_length = struct.unpack_from('<I', data, 12)[0]
    document = json.loads(data[20:20 + json_length])
    mesh_nodes = {node['name'] for node in document['nodes'] if 'mesh' in node}
    bpy.ops.import_scene.gltf(filepath=str(asset))
    objects = list(bpy.context.scene.objects)
    print('REVIEW SOURCE', actor, str(args.models / (actor + '.glb')),
          [obj.name for obj in objects if obj.type == 'MESH'], flush=True)
    rig = next(obj for obj in objects if obj.type == 'ARMATURE')
    rig.animation_data_clear()
    rig.animation_data_create()
    meshes = [obj for obj in objects if obj.type == 'MESH' and obj.name in mesh_nodes]
    body = [obj for obj in meshes if not obj.name.startswith(('weapon_', 'accessory_'))]
    accessories = [obj for obj in meshes if obj.name.startswith('accessory_')]
    actions = [action for action in bpy.data.actions if action.name.startswith('Menu_')]
    for action in actions:
        rig.animation_data.action = action
        rig.animation_data.action_slot = action.slots[0]
        weapon = bpy.data.objects.get('weapon_' + action.name.removeprefix('Menu_'))
        pack_weapon = bpy.data.objects.get('weapon_' + action.name.removeprefix('Menu_') + '_Backpack')
        pairs = []
        if weapon:
            pairs += [(weapon, part) for part in body]
        pairs += [(accessory, part) for accessory in accessories for part in body]
        if weapon:
            pairs += [(pack_weapon if accessory.name == 'accessory_back' and pack_weapon else weapon, accessory)
                      for accessory in accessories]
        if pack_weapon:
            pairs += [(pack_weapon, part) for part in body]
        for frame in ([] if args.render_only else sample_frames):
            bpy.context.scene.frame_set(frame)
            depsgraph = bpy.context.evaluated_depsgraph_get()
            surfaces = {obj.name: surface(obj, depsgraph) for obj in {part for pair in pairs for part in pair}}
            for first, second in pairs:
                measurement = relation(surfaces[first.name], surfaces[second.name])
                if measurement['crossing_triangles'] or measurement['penetrating_vertices']:
                    results.append({'actor': actor, 'clip': action.name, 'frame': frame,
                                    'first': first.name, 'second': second.name, **measurement})
                if measurement['normal_candidate_max_depth_m'] > .003:
                    normal_candidates.append({'actor': actor, 'clip': action.name, 'frame': frame,
                                              'first': first.name, 'second': second.name, **measurement})
        print('REVIEW CLIP', actor, action.name, len(sample_frames), 'samples', flush=True)
        selected_actor = args.render_actor is None or actor == args.render_actor
        selected_clip = action.name == args.render_clip if args.render_clip else action.name.endswith('_AK') or actor == 'chicken'
        if args.render and selected_actor and selected_clip:
            bpy.context.scene.frame_set(60)
            render_views(actor, action.name, objects)
    print('REVIEWED', actor, len(actions), 'clips', flush=True)

report = {'schema_version': 2,
          'status': 'preview_only' if args.render_only else ('rejected' if results else 'requires_visual_review'),
          'geometry_checked': not args.render_only,
          'asset_sha256': asset_hashes,
          'review_script_sha256': script_hashes,
          'pair_checks': pair_checks,
          'identical_geometry_checks_reused': reused_checks,
          'sample_frames': [] if args.render_only else sample_frames,
          'frames_per_second': 30,
          'rendered_files': rendered_files,
          'criteria': 'Any surface crossing or containment deeper than 0.5 mm rejects geometry. Three-ray tests use welded solid components and explicit caps on simple open boundaries. Nearest-normal candidates remain separate diagnostics. Visual inspection is required.',
          'findings': results,
          'normal_candidates': normal_candidates}
(args.report / 'geometry-review.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('REVIEW FINDINGS', len(results), flush=True)
if results:
    raise RuntimeError('Menu asset review rejected candidate geometry')
