"""Surface-driven hand contacts for the reconstruction candidate."""
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def hand_offset(rig, hands, side, weapon_mesh, weapon_matrix, outward):
    evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
    geometry = evaluated.to_mesh()
    transform = rig.matrix_world.inverted() @ evaluated.matrix_world
    points = [transform @ vertex.co for vertex in geometry.vertices]
    groups = [hands.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
              for vertex in hands.data.vertices]
    faces = [tuple(polygon.vertices) for polygon in geometry.polygons
             if all(groups[index].endswith('.' + side) and not groups[index].startswith('LowerArm.')
                    for index in polygon.vertices)]
    used = sorted({index for face in faces for index in face})
    remap = {index: local for local, index in enumerate(used)}
    points = [points[index] for index in used]
    faces = [tuple(remap[index] for index in face) for face in faces]
    evaluated.to_mesh_clear()
    weapon_points = [weapon_matrix @ vertex.co for vertex in weapon_mesh.vertices]
    weapon = BVHTree.FromPolygons(weapon_points, [tuple(polygon.vertices) for polygon in weapon_mesh.polygons])
    direction = Vector(outward).normalized()
    for step in range(151):
        offset = direction * (step * .002)
        moved = [point + offset for point in points]
        hand = BVHTree.FromPolygons(moved, faces)
        if hand.overlap(weapon):
            continue
        return offset + direction * .002
    raise RuntimeError(f'{rig.name} {side}: failed to fit hand to weapon surface')
