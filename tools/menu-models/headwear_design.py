"""Open-crown chicken visor that leaves the animated comb unobstructed."""
import math

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from accessory_geometry import head_surface, append_tube, replace_mesh


def rebuild_field_cap(rig, accessory, body):
    materials = list(accessory.data.materials)
    fabric = next(material for material in materials if 'canvas' in material.name.lower())
    trim = next(material for material in materials if 'graphite' in material.name.lower())
    head = [obj for obj in body if obj.name == 'Character_Enemy_Head']
    deform, collider, points = head_surface(rig, head, 'Head')
    top = max(point.z for point in points)
    hem = top - .24
    upper = [point for point in points if point.z > hem - .1]
    center = Vector(((min(point.x for point in upper) + max(point.x for point in upper)) / 2,
                     (min(point.y for point in upper) + max(point.y for point in upper)) / 2, hem))
    vertices, faces, slots = [], [], []

    def crown(theta, longitude, offset):
        direction = Vector((math.sin(theta) * math.cos(longitude),
                            math.sin(theta) * math.sin(longitude), math.cos(theta)))
        hit, _, _, _ = collider.ray_cast(center + direction * 3., -direction, 6.)
        if hit is None:
            raise RuntimeError('Field cap crown has no hood contact')
        return hit + direction * offset

    count = 64
    latitudes = [.08 + (math.pi / 2 - .08) * row / 16 for row in range(17)]
    layer_size = 1 + len(latitudes) * count
    for layer, offset in enumerate((.032, .016)):
        vertices.append(crown(0, 0, offset))
        for theta in latitudes:
            for column in range(count):
                vertices.append(crown(theta, math.tau * column / count, offset))
        base = layer * layer_size
        for column in range(count):
            face = (base, base + 1 + column, base + 1 + (column + 1) % count)
            faces.append(face if layer == 0 else tuple(reversed(face)))
            slots.append(0)
        for row in range(len(latitudes) - 1):
            for column in range(count):
                first = base + 1 + row * count + column
                following = base + 1 + row * count + (column + 1) % count
                face = (first, following, following + count, first + count)
                faces.append(face if layer == 0 else tuple(reversed(face)))
                slots.append(1 if row == len(latitudes) - 2 else 0)
    last = 1 + (len(latitudes) - 1) * count
    for column in range(count):
        first = last + column
        following = last + (column + 1) % count
        faces.append((first, following, following + layer_size, first + layer_size))
        slots.append(1)
    for panel in range(6):
        seam = [crown(.18 + 1.30 * row / 20, math.tau * panel / 6, .035) for row in range(21)]
        append_tube(vertices, faces, slots, seam, .0015, 1)
    start = len(vertices)
    columns, rows = 32, 8
    bill_layer_size = (columns + 1) * (rows + 1)
    for layer in range(2):
        for row in range(rows + 1):
            depth = row / rows
            for column in range(columns + 1):
                across = column * 2. / columns - 1.
                x = center.x + across * (.29 - .03 * depth)
                z = hem - .004 - .025 * depth + .012 * (1. - across * across) - .014 * layer
                hit, _, _, _ = collider.ray_cast(Vector((x, -3, z)), Vector((0, 1, 0)), 6.)
                if hit is None:
                    raise RuntimeError('Field cap bill has no forehead contact')
                vertices.append((x, hit.y - .030 - .20 * depth, z))
    for layer in range(2):
        for row in range(rows):
            for column in range(columns):
                first = start + layer * bill_layer_size + row * (columns + 1) + column
                face = (first, first + 1, first + columns + 2, first + columns + 1)
                faces.append(face if layer == 0 else tuple(reversed(face)))
                slots.append(layer)
    boundary = ([start + column for column in range(columns + 1)] +
                [start + row * (columns + 1) + columns for row in range(1, rows + 1)] +
                [start + rows * (columns + 1) + column for column in range(columns - 1, -1, -1)] +
                [start + row * (columns + 1) for row in range(rows - 1, 0, -1)])
    for index, first in enumerate(boundary):
        following = boundary[(index + 1) % len(boundary)]
        faces.append((first, following, following + bill_layer_size, first + bill_layer_size))
        slots.append(1)
    replace_mesh(accessory, 'Fitted six-panel field cap', vertices, faces, slots, [fabric, trim], deform)


def rebuild_chicken_visor(rig, accessory, body):
    materials = list(accessory.data.materials)
    fabric = next(material for material in materials if 'canvas' in material.name.lower())
    trim = next(material for material in materials if 'graphite' in material.name.lower())
    vertices, faces, slots = [], [], []
    head = rig.pose.bones['DEF-head']
    head_deform = rig.matrix_world @ head.matrix @ head.bone.matrix_local.inverted()
    head_inverse = head_deform.inverted()
    points, polygons = [], []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in body:
        evaluated = obj.evaluated_get(depsgraph)
        geometry = evaluated.to_mesh()
        transform = head_inverse @ evaluated.matrix_world
        offset = len(points)
        points.extend(transform @ vertex.co for vertex in geometry.vertices)
        polygons.extend(tuple(offset + index for index in face.vertices) for face in geometry.polygons)
        evaluated.to_mesh_clear()
    collider = BVHTree.FromPolygons(points, polygons)
    # Rectangular swept band with bevelled section. The crown stays open.
    profile = [(-.012, -.014), (.012, -.014), (.018, -.008), (.018, .008),
               (.012, .014), (-.012, .014), (-.018, .008), (-.018, -.008)]
    for segment in range(64):
        angle = math.tau * segment / 64
        direction = Vector((math.cos(angle), math.sin(angle), 0))
        center = Vector((0, -.08, 1.40))
        radii = []
        for height in (1.38, 1.40, 1.42):
            hit, _, _, _ = collider.ray_cast(Vector((0, -.08, height)), direction, 2.)
            if hit is not None:
                radii.append((hit - Vector((0, -.08, height))).length)
        if not radii:
            raise RuntimeError('Chicken visor band has no head contact')
        radius = max(radii) + .032
        for radial, height in profile:
            point = center + direction * (radius + radial) + Vector((0, 0, height))
            vertices.append(tuple(point))
    for segment in range(64):
        for index in range(8):
            faces.append((segment * 8 + index, ((segment + 1) % 64) * 8 + index,
                          ((segment + 1) % 64) * 8 + (index + 1) % 8,
                          segment * 8 + (index + 1) % 8))
            slots.append(1 if index in (0, 4) else 0)
    # A closed, cambered bill follows the forehead and tapers towards its tip.
    start = len(vertices)
    columns, rows = 24, 8
    layer_size = (columns + 1) * (rows + 1)
    for layer in range(2):
        for row in range(rows + 1):
            depth = row / rows
            for column in range(columns + 1):
                across = 2. * column / columns - 1.
                width = across * (.31 - .035 * depth)
                hit, _, _, _ = collider.ray_cast(Vector((width, -2, 1.405)), Vector((0, 1, 0)), 4.)
                if hit is None:
                    raise RuntimeError('Chicken visor bill has no forehead contact')
                vertices.append((width,
                                 hit.y - .032 - .28 * depth,
                                 1.405 - .035 * depth + .025 * (1. - across * across) - .018 * layer))
    for layer in range(2):
        for row in range(rows):
            for column in range(columns):
                first = start + layer * layer_size + row * (columns + 1) + column
                face = (first, first + 1, first + columns + 2, first + columns + 1)
                faces.append(face if layer == 0 else tuple(reversed(face)))
                slots.append(layer)
    boundary = ([start + column for column in range(columns + 1)] +
                [start + row * (columns + 1) + columns for row in range(1, rows + 1)] +
                [start + rows * (columns + 1) + column for column in range(columns - 1, -1, -1)] +
                [start + row * (columns + 1) for row in range(rows - 1, 0, -1)])
    for index, first in enumerate(boundary):
        second = boundary[(index + 1) % len(boundary)]
        faces.append((first, second, second + layer_size, first + layer_size))
        slots.append(1)
    transform = accessory.matrix_world.inverted() @ head_deform
    mesh = bpy.data.meshes.new('Chicken open-crown visor')
    mesh.from_pydata([transform @ Vector(point) for point in vertices], [], faces)
    mesh.materials.append(fabric)
    mesh.materials.append(trim)
    for polygon, slot in zip(mesh.polygons, slots):
        polygon.material_index = slot
        polygon.use_smooth = True
    geometry = bmesh.new()
    geometry.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(geometry, faces=list(geometry.faces))
    geometry.to_mesh(mesh)
    geometry.free()
    accessory.data = mesh
