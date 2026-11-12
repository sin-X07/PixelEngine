"""Wraparound eyewear with closed lenses, fitted temples and capped bridge."""
import math

from mathutils import Vector

from accessory_geometry import head_surface, convex_surface, append_tube, replace_mesh


def rebuild_eyewear(rig, accessory, body, chicken=False):
    source_materials = list(accessory.data.materials)
    frame = next(material for material in source_materials if 'graphite' in material.name.lower())
    lens = next(material for material in source_materials if 'lens' in material.name.lower())
    deform, _, head_points = head_surface(rig, body, 'DEF-head' if chicken else 'Head')
    eye_height = 1.15 if chicken else rig.data.bones['Head'].head_local.z + .22
    collider = convex_surface(head_points, eye_height - .25)
    spacing, width, height = (.15, .115, .075) if chicken else (.175, .15, .095)
    vertices, faces, slots = [], [], []

    def front(x, z):
        hit, _, _, _ = collider.ray_cast(Vector((x, -3, z)), Vector((0, 1, 0)), 6.)
        if hit is None:
            raise RuntimeError(f'{rig.name}: eyewear has no face contact at {x}, {z}')
        return hit.y

    count = 48
    rings = (1., .84, .60, .30)
    for sign in (-1, 1):
        start = len(vertices)
        layer_size = len(rings) * count + 1
        for layer in range(2):
            for radius in rings:
                for column in range(count):
                    angle = math.tau * column / count
                    cosine, sine = math.cos(angle), math.sin(angle)
                    x = sign * spacing + width * radius * math.copysign(abs(cosine) ** .7, cosine)
                    z = eye_height + height * radius * math.copysign(abs(sine) ** .7, sine)
                    depth = .05 + .006 * (1. - radius * radius) if layer == 0 else .026
                    vertices.append((x, front(x, z) - depth, z))
            vertices.append((sign * spacing, front(sign * spacing, eye_height) - (.056 if layer == 0 else .026), eye_height))
        for layer in range(2):
            base = start + layer * layer_size
            for row in range(len(rings) - 1):
                for column in range(count):
                    following = (column + 1) % count
                    face = (base + row * count + column, base + row * count + following,
                            base + (row + 1) * count + following, base + (row + 1) * count + column)
                    faces.append(face if layer == 0 else tuple(reversed(face)))
                    slots.append(0 if row == 0 else 1)
            center = base + layer_size - 1
            for column in range(count):
                face = (base + (len(rings) - 1) * count + column,
                        base + (len(rings) - 1) * count + (column + 1) % count, center)
                faces.append(face if layer == 0 else tuple(reversed(face)))
                slots.append(1)
        for column in range(count):
            following = (column + 1) % count
            faces.append((start + column, start + following,
                          start + layer_size + following, start + layer_size + column))
            slots.append(0)
        # Stems follow the head profile with retained tube thickness.
        first_x = sign * (spacing + width)
        stem = [Vector((first_x, front(first_x, eye_height) - .038, eye_height))]
        center = Vector((0, -.08, eye_height + .025))
        for degrees in range(-50, 16, 5):
            angle = math.radians(degrees)
            direction = Vector((sign * math.cos(angle), math.sin(angle), 0))
            hit, _, _, _ = collider.ray_cast(center + direction * 3., -direction, 6.)
            if hit is None:
                raise RuntimeError(f'{rig.name}: eyewear stem has no head contact')
            stem.append(hit + direction * .04)
        append_tube(vertices, faces, slots, stem, .008 if chicken else .010, 0)
    gap = spacing - width + .008
    bridge = []
    for index in range(17):
        fraction = index / 16
        x = -gap + 2 * gap * fraction
        z = eye_height + .018 + .018 * math.sin(math.pi * fraction)
        bridge.append(Vector((x, front(x, z) - .043, z)))
    append_tube(vertices, faces, slots, bridge, .008 if chicken else .011, 0)
    replace_mesh(accessory, 'Fitted wraparound eyewear', vertices, faces, slots, [frame, lens], deform)
