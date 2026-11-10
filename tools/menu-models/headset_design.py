"""Helmet-fitted communications headset with closed cushions and curved boom."""
import math

import bmesh
from mathutils import Vector

from accessory_geometry import head_surface, convex_surface, append_tube, replace_mesh


def rebuild_headset(rig, accessory, body):
    source = list(accessory.data.materials)
    materials = [next(material for material in source if token in material.name.lower())
                 for token in ('graphite', 'canvas', 'hardware')]
    deform, _, points = head_surface(rig, body, 'Head')
    eye_height = rig.data.bones['Head'].head_local.z + .22
    collider = convex_surface(points, eye_height - .25)
    vertices, faces, slots = [], [], []

    def box(position, size, radius, slot):
        geometry = bmesh.new()
        bmesh.ops.create_cube(geometry, size=1.)
        for vertex in geometry.verts:
            vertex.co = Vector(position) + Vector(tuple(vertex.co[axis] * size[axis] for axis in range(3)))
        bmesh.ops.bevel(geometry, geom=list(geometry.edges), offset=radius, segments=3, affect='EDGES')
        geometry.verts.index_update()
        offset = len(vertices)
        vertices.extend(vertex.co.copy() for vertex in geometry.verts)
        for face in geometry.faces:
            faces.append(tuple(offset + vertex.index for vertex in face.verts))
            slots.append(slot)
        geometry.free()

    cup_height = eye_height + .06
    cup_edges = {}
    for side in (-1, 1):
        widths = []
        for y in (-.20, -.10, 0):
            for z in (cup_height - .13, cup_height, cup_height + .13):
                hit, _, _, _ = collider.ray_cast(Vector((side * 3., y, z)), Vector((-side, 0, 0)), 6.)
                if hit is None:
                    raise RuntimeError('Headset cushion has no helmet contact')
                widths.append(side * hit.x)
        inner = max(widths) + .014
        cup_edges[side] = inner
        box((side * (inner + .015), -.10, cup_height), (.032, .17, .21), .015, 0)
        box((side * (inner + .052), -.10, cup_height), (.055, .205, .255), .022, 1)
        box((side * (inner + .082), -.10, cup_height), (.014, .13, .17), .006, 2)
    # The rectangular band follows the helmet instead of spanning it with straight corners.
    band_start = len(vertices)
    profile = [(-.022, -.006), (.022, -.006), (.026, -.002), (.026, .002),
               (.022, .006), (-.022, .006), (-.026, .002), (-.026, -.002)]
    center = Vector((0, -.10, cup_height))
    sections = 65
    for section in range(sections):
        angle = math.pi * section / (sections - 1)
        direction = Vector((math.cos(angle), 0, math.sin(angle)))
        hit, _, _, _ = collider.ray_cast(center + direction * 3., -direction, 6.)
        if hit is None:
            raise RuntimeError('Headset band has no helmet contact')
        for across, radial in profile:
            vertices.append(hit + direction * (.030 + radial) + Vector((0, across, 0)))
    faces.append(tuple(band_start + index for index in reversed(range(8))))
    slots.append(0)
    for section in range(sections - 1):
        for index in range(8):
            first = band_start + section * 8 + index
            following = band_start + section * 8 + (index + 1) % 8
            faces.append((first, following, following + 8, first + 8))
            slots.append(0)
    faces.append(tuple(band_start + (sections - 1) * 8 + index for index in range(8)))
    slots.append(0)
    # Route the microphone around the cheek with a padded windscreen near the mouth.
    boom_height = eye_height - .17
    boom = [Vector((cup_edges[1] + .05, -.16, cup_height - .09))]
    center = Vector((0, -.08, boom_height))
    for degrees in range(0, -81, -4):
        angle = math.radians(degrees)
        direction = Vector((math.cos(angle), math.sin(angle), 0))
        hit, _, _, _ = collider.ray_cast(center + direction * 3., -direction, 6.)
        if hit is None:
            raise RuntimeError('Microphone boom has no cheek contact')
        boom.append(hit + direction * .040)
    append_tube(vertices, faces, slots, boom, .007, 2)
    microphone = boom[-1] + Vector((-.015, -.003, -.004))
    box(microphone, (.085, .045, .045), .019, 0)
    box(microphone + Vector((0, -.024, 0)), (.018, .005, .008), .002, 1)
    replace_mesh(accessory, 'Fitted communications headset', vertices, faces, slots, materials, deform)
