"""Refine the original low-poly hands and author gestures through a wrist joint."""
import bpy
import bmesh
import math
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree

FINGERS = ('Index', 'Middle', 'Pinky', 'Thumb')


def is_finger(name):
    return name.startswith(FINGERS)


def prepare_hands(rig, body):
    """Replace the source hand region with clean, separately weighted geometry."""
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    wrists = {side: rig.data.bones['LowerArm.' + side].tail_local.copy() for side in ('L', 'R')}
    rest_geometry = {bone.name: (bone.head_local.copy(), bone.tail_local.copy())
                     for bone in rig.data.bones if is_finger(bone.name)}
    factor = Vector((.80, .86, .60))
    for obj in body:
        if obj.type != 'MESH' or not any(is_finger(group.name) for group in obj.vertex_groups):
            continue
        # Keep the sleeve and forearm, removing only vertices weighted to digits.
        removed = {vertex.index for vertex in obj.data.vertices
                   if sum(item.weight for item in vertex.groups
                          if is_finger(obj.vertex_groups[item.group].name)) > .12}
        geometry = bmesh.new()
        geometry.from_mesh(obj.data)
        geometry.verts.ensure_lookup_table()
        bmesh.ops.delete(geometry, geom=[geometry.verts[index] for index in removed], context='VERTS')
        geometry.to_mesh(obj.data)
        geometry.free()
        obj.data.update()
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for bone in rig.data.edit_bones:
        if is_finger(bone.name):
            bone.use_connect = False
    for side in ('L', 'R'):
        wrist = wrists[side]
        hand = rig.data.edit_bones.new('Hand.' + side)
        hand.head = wrist
        hand.tail = wrist + Vector((.14 if side == 'L' else -.14, 0, 0))
        hand.parent = rig.data.edit_bones['LowerArm.' + side]
        hand.use_connect = True
        hand.align_roll(Vector((0, 0, 1)))
        for bone in rig.data.edit_bones:
            if not is_finger(bone.name) or not bone.name.endswith('.' + side):
                continue
            for attribute, point in zip(('head', 'tail'), rest_geometry[bone.name]):
                delta = point - wrist
                setattr(bone, attribute, wrist + Vector((delta.x * factor.x, delta.y * factor.y, delta.z * factor.z)))
            if bone.name in [finger + '1.' + side for finger in FINGERS]:
                bone.parent = hand
        thumb_base = rig.data.edit_bones['Thumb1.' + side]
        thumb_tip = rig.data.edit_bones['Thumb2.' + side]
        tip_direction = thumb_tip.tail - thumb_tip.head
        thumb_base.tail = thumb_base.head + (thumb_base.tail - thumb_base.head) * .65
        thumb_tip.head = thumb_base.tail
        thumb_tip.tail = thumb_tip.head + tip_direction * .55
    bpy.ops.object.mode_set(mode='OBJECT')
    for side in ('L', 'R'):
        rig.pose.bones['Hand.' + side].rotation_mode = 'QUATERNION'
    bpy.context.view_layer.update()
    return [build_hand_mesh(rig, body)]


def build_hand_mesh(rig, body):
    vertices, faces, bindings = [], [], []

    def loft(rings, weights):
        start = len(vertices)
        count = len(rings[0])
        for ring, binding in zip(rings, weights):
            vertices.extend(ring)
            bindings.extend([binding] * count)
        faces.append(tuple(start + index for index in reversed(range(count))))
        for row in range(len(rings) - 1):
            for column in range(count):
                first = start + row * count + column
                next_column = start + row * count + (column + 1) % count
                faces.append((first, next_column, next_column + count, first + count))
        faces.append(tuple(start + (len(rings) - 1) * count + index for index in range(count)))

    for side in ('L', 'R'):
        hand = rig.data.bones['Hand.' + side]
        forearm = rig.data.bones['LowerArm.' + side]
        bridge = []
        for offset, radius in ((-.16, .075), (-.08, .075), (0, .067), (.025, .060)):
            bridge.append([forearm.matrix_local @ Vector((radius * math.cos(index * math.tau / 8),
                forearm.length + offset, radius * math.sin(index * math.tau / 8))) for index in range(8)])
        loft(bridge, [{forearm.name: 1}] * len(bridge))
        rings = []
        for along, width, depth in [(-.065, .065, .040), (0, .072, .043), (.055, .093, .041), (.12, .090, .035), (.145, .082, .028)]:
            radius = .018
            profile = [(-width + radius, -depth), (width - radius, -depth),
                       (width, -depth + radius), (width, depth - radius),
                       (width - radius, depth), (-width + radius, depth),
                       (-width, depth - radius), (-width, -depth + radius)]
            rings.append([hand.matrix_local @ Vector((across, along, normal)) for across, normal in profile])
        loft(rings, [{hand.name: 1}] * len(rings))
        for finger in FINGERS:
            joints = (1, 2) if finger == 'Thumb' else (2, 3)
            for joint in joints:
                bone = rig.data.bones[finger + str(joint) + '.' + side]
                radius = .024 if finger != 'Pinky' else .021
                if finger == 'Thumb':
                    radius = .027
                last_joint = joint == joints[-1]
                start_at = .35 if finger == 'Thumb' and joint == 1 else 0
                rows = [(start_at, radius * .93), (.25, radius), (.78, radius * .94), (1, radius * .85)]
                if start_at:
                    rows = [(start_at, radius * 1.12), (.65, radius * 1.04), (1, radius)]
                if last_joint:
                    rows += [(1.08, radius * .65), (1.12, radius * .15)]
                rings, weights = [], []
                for along, width in rows:
                    ring = [bone.matrix_local @ Vector((width * math.cos(index * math.tau / 8),
                            bone.length * along, width * .86 * math.sin(index * math.tau / 8)))
                            for index in range(8)]
                    rings.append(ring)
                    weights.append({bone.name: 1})
                loft(rings, weights)
                if not last_joint:
                    # A small knuckle volume keeps the joint closed as its two
                    # neighbouring finger segments rotate around the same pivot.
                    joint_rings = []
                    for latitude in (-1.48, -.74, 0, .74, 1.48):
                        joint_rings.append([bone.matrix_local @ Vector((radius * math.cos(latitude) * math.cos(index * math.tau / 8),
                            bone.length + radius * math.sin(latitude),
                            radius * math.cos(latitude) * math.sin(index * math.tau / 8))) for index in range(8)])
                    loft(joint_rings, [{bone.name: 1}] * len(joint_rings))
    mesh = bpy.data.meshes.new(rig.name + '_HandGeometry')
    mesh.from_pydata(vertices, [], [tuple(reversed(face)) for face in faces])
    mesh.update()
    obj = bpy.data.objects.new('Hands', mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = rig
    modifier = obj.modifiers.new('Hand skin', 'ARMATURE')
    modifier.object = rig
    material = next(material for part in body for material in part.data.materials
                    if material and 'skin' in material.name.lower())
    mesh.materials.append(material)
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    groups = {}
    for index, binding in enumerate(bindings):
        for name, weight in binding.items():
            if name not in groups:
                groups[name] = obj.vertex_groups.new(name=name)
            groups[name].add([index], weight, 'REPLACE')
    return obj


def finger_clearance_offset(rig, hands, axis, left_positive):
    """Find the smallest offset that separates opposing finger surfaces."""
    evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
    geometry = evaluated.to_mesh()
    transform = rig.matrix_world.inverted() @ evaluated.matrix_world
    points = [transform @ vertex.co for vertex in geometry.vertices]
    names = []
    for original, deformed in zip(hands.data.vertices, geometry.vertices):
        name = hands.vertex_groups[max(original.groups, key=lambda group: group.weight).group].name
        names.append(name.split('.')[0].rstrip('123') + '.' + name[-1])
    parts = {}
    for side in ('L', 'R'):
        for finger in FINGERS:
            name = finger + '.' + side
            polygons = [tuple(polygon.vertices) for polygon in geometry.polygons
                        if all(names[index] == name for index in polygon.vertices)]
            used = sorted({index for polygon in polygons for index in polygon})
            remap = {index: local for local, index in enumerate(used)}
            parts[name] = ([points[index] for index in used],
                           [tuple(remap[index] for index in polygon) for polygon in polygons])
    evaluated.to_mesh_clear()
    right = [BVHTree.FromPolygons(*parts[finger + '.R']) for finger in FINGERS]
    direction = Vector()
    direction[axis] = 1 if left_positive else -1
    for step in range(81):
        offset = direction * (step * .002)
        left = [BVHTree.FromPolygons([point + offset for point in parts[finger + '.L'][0]],
                                    parts[finger + '.L'][1]) for finger in FINGERS]
        if not any(first.overlap(second) for first in left for second in right):
            return offset + direction * .002
    raise RuntimeError(f'{rig.name}: two-hand contact cannot be separated')


def pose_hand(rig, side, direction, outward, curl, index_curl=None):
    """Rotate the whole palm, retaining the source finger fan and joint hinges."""
    for bone in rig.pose.bones:
        if is_finger(bone.name) and bone.name.endswith('.' + side):
            bone.matrix_basis = Matrix.Identity(4)
    hand = rig.pose.bones['Hand.' + side]
    forward = Vector(direction).normalized()
    normal = Vector(outward)
    normal = (normal - forward * normal.dot(forward)).normalized()
    across = forward.cross(normal).normalized()
    normal = across.cross(forward).normalized()
    matrix = Matrix((across, forward, normal)).transposed().to_4x4()
    matrix.translation = hand.head.copy()
    hand.matrix = matrix
    for finger in ('Index', 'Middle', 'Pinky'):
        angle = index_curl if finger == 'Index' and index_curl is not None else curl
        for joint, proportion in ((2, 1.0), (3, .80)):
            bone = rig.pose.bones[finger + str(joint) + '.' + side]
            rest = bone.bone
            direction = (rest.tail_local - rest.head_local).normalized()
            hinge = direction.cross(Vector((0, 0, -1))).normalized()
            local_hinge = rest.matrix_local.to_3x3().inverted() @ hinge
            bone.rotation_quaternion = Quaternion(local_hinge, angle * proportion)
    # The thumb opposes across the palm. Its distal joint returns toward the
    # knuckles instead of extending in the same direction as its metacarpal.
    thumb_side = 1 if side == 'L' else -1
    closed = min(1, curl)
    base_open = Vector((thumb_side * .9, .3, -.15))
    base_closed = Vector((thumb_side * .25, .35, -.9))
    tip_open = Vector((thumb_side * .6, .8, -.1))
    tip_closed = Vector((-thumb_side * .6, .45, -.65))
    for name, direction in (('Thumb1.' + side, base_open.lerp(base_closed, closed)),
                            ('Thumb2.' + side, tip_open.lerp(tip_closed, closed))):
        bone = rig.pose.bones[name]
        matrix = bone.matrix.copy()
        head = bone.head.copy()
        target = hand.matrix.to_3x3() @ direction.normalized()
        rotation = (bone.tail - head).rotation_difference(target)
        matrix.translation = Vector()
        bone.matrix = Matrix.Translation(head) @ rotation.to_matrix().to_4x4() @ matrix
        bpy.context.view_layer.update()
