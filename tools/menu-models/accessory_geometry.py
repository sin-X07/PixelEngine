"""Closed accessory surfaces authored in the head's undeformed coordinate frame."""
import math

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def head_surface(rig, body, bone_name):
    head = rig.pose.bones[bone_name]
    deform = rig.matrix_world @ head.matrix @ head.bone.matrix_local.inverted()
    inverse = deform.inverted()
    points, polygons = [], []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in body:
        evaluated = obj.evaluated_get(depsgraph)
        geometry = evaluated.to_mesh()
        transform = inverse @ evaluated.matrix_world
        offset = len(points)
        points.extend(transform @ vertex.co for vertex in geometry.vertices)
        polygons.extend(tuple(offset + index for index in face.vertices) for face in geometry.polygons)
        evaluated.to_mesh_clear()
    return deform, BVHTree.FromPolygons(points, polygons), points


def convex_surface(points, minimum_height, half_width=.55):
    hull = bmesh.new()
    for point in points:
        if point.z > minimum_height and abs(point.x) < half_width:
            hull.verts.new(point)
    bmesh.ops.convex_hull(hull, input=list(hull.verts), use_existing_faces=False)
    hull.verts.index_update()
    surface = BVHTree.FromPolygons([vertex.co.copy() for vertex in hull.verts],
        [tuple(vertex.index for vertex in face.verts) for face in hull.faces])
    hull.free()
    return surface


def append_tube(vertices, faces, slots, points, radius, slot):
    start = len(vertices)
    count = 8
    for index, point in enumerate(points):
        before = points[max(0, index - 1)]
        after = points[min(len(points) - 1, index + 1)]
        tangent = (after - before).normalized()
        across = tangent.cross(Vector((0, 0, 1))).normalized()
        normal = tangent.cross(across).normalized()
        for column in range(count):
            angle = math.tau * column / count
            vertices.append(point + radius * (across * math.cos(angle) + normal * math.sin(angle)))
    faces.append(tuple(start + column for column in reversed(range(count))))
    slots.append(slot)
    for row in range(len(points) - 1):
        for column in range(count):
            first = start + row * count + column
            following = start + row * count + (column + 1) % count
            faces.append((first, following, following + count, first + count))
            slots.append(slot)
    faces.append(tuple(start + (len(points) - 1) * count + column for column in range(count)))
    slots.append(slot)


def replace_mesh(accessory, name, vertices, faces, slots, materials, deform):
    transform = accessory.matrix_world.inverted() @ deform
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([transform @ Vector(point) for point in vertices], [], faces)
    for material in materials:
        mesh.materials.append(material)
    for polygon, slot in zip(mesh.polygons, slots):
        polygon.material_index = slot
        polygon.use_smooth = True
    geometry = bmesh.new()
    geometry.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(geometry, faces=list(geometry.faces))
    geometry.to_mesh(mesh)
    geometry.free()
    accessory.data = mesh
