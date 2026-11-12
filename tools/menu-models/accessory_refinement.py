"""Fit the assembled backpack while preserving its manufactured thickness."""
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def fit_backpack(rig, body, accessory):
    points, polygons = [], []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in body:
        if obj.name == 'Hands':
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        offset = len(points)
        points.extend(evaluated.matrix_world @ vertex.co for vertex in mesh.vertices)
        polygons.extend(tuple(offset + index for index in face.vertices) for face in mesh.polygons)
        evaluated.to_mesh_clear()
    surface = BVHTree.FromPolygons(points, polygons)
    matrix = accessory.matrix_world.copy()
    inverse = matrix.inverted()
    outward = (rig.matrix_world.to_3x3() @ Vector((0, 1, 0))).normalized()
    samples = [matrix @ vertex.co for vertex in accessory.data.vertices]
    samples += [matrix @ face.center for face in accessory.data.polygons]
    corrections = []
    for point in samples:
        hit, _, _, _ = surface.ray_cast(point + outward * 3., -outward, 6.)
        if hit is not None:
            corrections.append((hit - point).dot(outward) + .012)
    if not corrections:
        raise RuntimeError(f'{rig.name}: backpack has no torso contact')
    shift = inverse.to_3x3() @ (outward * max(corrections))
    for vertex in accessory.data.vertices:
        vertex.co += shift
    accessory.data.update()
