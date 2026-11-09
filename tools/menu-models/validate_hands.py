"""Run with Blender --background --python to inspect exported hand deformation."""
import itertools
from pathlib import Path

import bpy
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
FINGERS = ('Index', 'Middle', 'Pinky', 'Thumb')


def group_name(obj, vertex):
    group = max(vertex.groups, key=lambda item: item.weight)
    name = obj.vertex_groups[group.group].name
    for finger in FINGERS:
        if name.startswith(finger):
            return finger + '.' + name[-1]
    return name


errors = []
for actor in ('t', 'ct'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / 'assets/models/menu' / (actor + '.glb')))
    rig = next(obj for obj in bpy.data.objects if obj.type == 'ARMATURE')
    hands = bpy.data.objects['Hands']
    rig.animation_data_clear()
    rig.animation_data_create()
    groups = [group_name(hands, vertex) for vertex in hands.data.vertices]
    names = [part + '.' + side for side in ('L', 'R') for part in (*FINGERS, 'Hand')]
    polygons = {name: [tuple(polygon.vertices) for polygon in hands.data.polygons
                      if all(groups[index] == name for index in polygon.vertices)] for name in names}
    if any(not polygons[name] for name in names):
        raise RuntimeError(f'{actor}: missing palm or finger geometry')
    actions = [action for action in bpy.data.actions if action.name.startswith('Menu_')]
    if len(actions) != 24:
        raise RuntimeError(f'{actor}: expected 24 weapon-pose clips')
    for action in actions:
        rig.animation_data.action = action
        rig.animation_data.action_slot = action.slots[0]
        for frame in (0, 15, 30, 45, 60, 75, 90, 105, 119):
            bpy.context.scene.frame_set(frame)
            evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
            geometry = evaluated.to_mesh()
            points = [evaluated.matrix_world @ vertex.co for vertex in geometry.vertices]
            trees = {name: BVHTree.FromPolygons(points, polygons[name]) for name in names}
            for first, second in itertools.combinations(names, 2):
                # Digit roots intentionally join their own palm. Opposing palms
                # and digits must have independent, non-intersecting surfaces.
                if first[-1] == second[-1] and (first.startswith('Hand') or second.startswith('Hand')):
                    continue
                intersections = trees[first].overlap(trees[second])
                if intersections:
                    errors.append((actor, action.name, frame, first, second, len(intersections)))
            evaluated.to_mesh_clear()
    print('HAND AUDIT', actor, '24 clips x 9 samples')
for error in errors[:40]:
    print('HAND INTERSECTION', error)
print('HAND INTERSECTION COUNT', len(errors))
if errors:
    raise RuntimeError('Exported finger surfaces intersect')
