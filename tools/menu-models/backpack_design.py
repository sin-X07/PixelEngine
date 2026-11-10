"""Compact back-panel pack with vest-mounted quick-release fittings."""
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

from weapon_mounts import rig_geometry, append_hardware


def clip_padding_region(points, minimum_x, maximum_x, minimum_z, maximum_z):
    """Clip a body face to the cushion's projected footprint, retaining its depth."""
    for axis, boundary, sign in ((0, minimum_x, 1), (0, maximum_x, -1),
                                  (2, minimum_z, 1), (2, maximum_z, -1)):
        if not points:
            break
        clipped = []
        previous = points[-1]
        for point in points:
            inside = (point[axis] - boundary) * sign >= 0
            previous_inside = (previous[axis] - boundary) * sign >= 0
            if inside != previous_inside:
                fraction = (boundary - previous[axis]) / (point[axis] - previous[axis])
                clipped.append(previous.lerp(point, fraction))
            if inside:
                clipped.append(point)
            previous = point
        points = clipped
    return points


def add_back_padding(rig, body, accessory):
    """Fit a closed back cushion to the torso envelope of every authored pose."""
    scene = bpy.context.scene
    animation = rig.animation_data
    previous_action, previous_frame = animation.action, scene.frame_current
    tracks = list(animation.nla_tracks)
    previous_mutes = [track.mute for track in tracks]
    for track in tracks:
        track.mute = True
    try:
        animation.action = tracks[0].strips[0].action
        animation.action_slot = animation.action.slots[0]
        scene.frame_set(1)
        torso = rig.pose.bones['Torso']
        rest_inverse = torso.bone.matrix_local.inverted()
        reference = torso.matrix @ rest_inverse
        reference_inverse = reference.inverted()
        pack_transform = rig.matrix_world.inverted() @ accessory.matrix_world
        pack_inverse = pack_transform.inverted()
        fabric_faces = [tuple(face.vertices) for face in accessory.data.polygons
                        if 'canvas' in accessory.data.materials[face.material_index].name.lower()]
        fabric = BVHTree.FromPolygons([pack_transform @ vertex.co for vertex in accessory.data.vertices],
                                     fabric_faces)
        grid, backs = [], []
        outward = (reference.to_3x3() @ Vector((0, 1, 0))).normalized()
        for row in range(5):
            for column in range(5):
                point = reference_inverse @ Vector((-.16 + .08 * column, 0, .94 + .06 * row))
                origin = reference @ Vector((point.x, -3, point.z))
                hit, _, _, _ = fabric.ray_cast(origin, outward, 6)
                if hit is None:
                    raise RuntimeError(f'{rig.name}: back cushion misses the pack shell')
                grid.append(point)
                backs.append((reference_inverse @ hit).y + .006)
        bands = [float('-inf')] * 4
        minimum_x, maximum_x = min(point.x for point in grid), max(point.x for point in grid)
        heights = [grid[row * 5].z for row in range(5)]
        for track in tracks:
            animation.action = track.strips[0].action
            animation.action_slot = animation.action.slots[0]
            for frame in (1, 31, 61, 91):
                scene.frame_set(frame)
                deform = torso.matrix @ rest_inverse
                inverse = deform.inverted()
                vertices, body_faces = rig_geometry(rig, [obj for obj in body if obj.name != 'Hands'])
                vertices = [inverse @ point for point in vertices]
                for indices in body_faces:
                    face = [vertices[index] for index in indices]
                    if max(point.x for point in face) < minimum_x or min(point.x for point in face) > maximum_x:
                        continue
                    for band in range(4):
                        if max(point.z for point in face) < heights[band] or min(point.z for point in face) > heights[band + 1]:
                            continue
                        clipped = clip_padding_region(face, minimum_x, maximum_x, heights[band], heights[band + 1])
                        if clipped:
                            bands[band] = max(bands[band], max(point.y for point in clipped))
        if any(value == float('-inf') for value in bands):
            raise RuntimeError(f'{rig.name}: back cushion misses the torso')
        row_depths = [max(bands[max(0, row - 1):min(4, row + 1)]) + .008 for row in range(5)]
        fronts = [row_depths[row] for row in range(5) for _ in range(5)]
        if any(front >= back - .004 for front, back in zip(fronts, backs)):
            raise RuntimeError(f'{rig.name}: back cushion has insufficient torso clearance')
        points = [pack_inverse @ reference @ Vector((point.x, depth, point.z))
                  for depths in (fronts, backs) for point, depth in zip(grid, depths)]
        faces = []
        for layer in range(2):
            for row in range(4):
                for column in range(4):
                    first = layer * 25 + row * 5 + column
                    face = (first, first + 1, first + 6, first + 5)
                    faces.append(tuple(reversed(face)) if layer else face)
        boundary = [*range(5), 9, 14, 19, 24, 23, 22, 21, 20, 15, 10, 5]
        for first, second in zip(boundary, boundary[1:] + boundary[:1]):
            faces.append((first, first + 25, second + 25, second))
        material = next(material for material in accessory.data.materials
                        if 'graphite' in material.name.lower())
        original_faces = len(accessory.data.polygons)
        append_hardware(accessory.data, points, faces, material)
        for face in accessory.data.polygons[original_faces:]:
            face.use_smooth = True
    finally:
        animation.action = previous_action
        for track, muted in zip(tracks, previous_mutes):
            track.mute = muted
        scene.frame_set(previous_frame)


def rebuild_backpack(rig, accessory):
    materials = list(accessory.data.materials)
    fabric = next(material for material in materials if 'canvas' in material.name.lower())
    hardware = next(material for material in materials if 'hardware' in material.name.lower())
    webbing = next(material for material in materials if 'graphite' in material.name.lower())
    pieces = []

    def box(label, position, size, material, radius):
        bpy.ops.mesh.primitive_cube_add(size=1, location=rig.matrix_world @ Vector(position))
        obj = bpy.context.object
        obj.name = label
        obj.scale = size
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        obj.data.materials.append(material)
        bevel = obj.modifiers.new('Tailored edge', 'BEVEL')
        bevel.width = radius
        bevel.segments = 3
        bpy.ops.object.modifier_apply(modifier=bevel.name)
        pieces.append(obj)

    box('Pack shell', (0, .40, 1.03), (.40, .19, .44), fabric, .045)
    box('Utility pocket', (0, .515, .99), (.30, .06, .22), fabric, .025)
    box('Rain flap', (0, .49, 1.22), (.34, .075, .045), webbing, .012)
    box('Carry tab', (0, .39, 1.25), (.13, .04, .025), webbing, .01)
    for side in (-1, 1):
        for height in (.87, 1.19):
            box('Vest mount', (side * .145, .28, height), (.045, .045, .065), hardware, .009)
        box('Pocket pull', (side * .11, .555, 1.105), (.026, .015, .05), hardware, .005)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in pieces:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = pieces[0]
    bpy.ops.object.join()
    combined = bpy.context.object
    mesh = combined.data.copy()
    mesh.transform(accessory.matrix_world.inverted() @ combined.matrix_world)
    accessory.data = mesh
    bpy.data.objects.remove(combined, do_unlink=True)
