"""Repair deformation seams before authoring the reconstruction rig."""
import bmesh
import math
import bpy
from mathutils import Vector


def close_sleeve_roots(rig, objects):
    """Cover the open sleeve roots with rounded, upper-arm-mounted shoulder joints."""
    material = next(material for obj in objects for material in obj.data.materials
                    if material.name.startswith('Black'))
    created = []
    for side in ('L', 'R'):
        bone = rig.pose.bones['UpperArm.' + side]
        geometry = bmesh.new()
        bmesh.ops.create_uvsphere(geometry, u_segments=16, v_segments=8, radius=.095)
        mesh = bpy.data.meshes.new(rig.name + ' closed sleeve root ' + side)
        geometry.to_mesh(mesh)
        geometry.free()
        mesh.materials.append(material)
        obj = bpy.data.objects.new('SleeveRoot.' + rig.name + '.' + side, mesh)
        bpy.context.scene.collection.objects.link(obj)
        obj.parent, obj.parent_type, obj.parent_bone = rig, 'BONE', bone.name
        bpy.context.view_layer.update()
        transform = obj.matrix_world.inverted() @ rig.matrix_world
        for vertex in mesh.vertices:
            vertex.co = transform @ (bone.head + vertex.co)
        for polygon in mesh.polygons:
            polygon.use_smooth = True
        mesh.update()
        created.append(obj)
    return created


def broaden_arm_mounts(rig, objects, offset=.10):
    """Give the sleeves room outside the vest while retaining arm and hand lengths."""
    chains, rest = {}, {}
    for side in ('L', 'R'):
        upper = rig.data.bones['UpperArm.' + side]
        chains[side] = [upper.name, *[bone.name for bone in upper.children_recursive]]
        for name in chains[side]:
            bone = rig.data.bones[name]
            rest[name] = (bone.head_local.copy(), bone.tail_local.copy())
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for side, sign in (('L', 1), ('R', -1)):
        delta = Vector((sign * offset, 0, 0))
        rig.data.edit_bones['Shoulder.' + side].tail += delta
        for name in chains[side]:
            bone = rig.data.edit_bones[name]
            bone.head = rest[name][0] + delta
            bone.tail = rest[name][1] + delta
    bpy.ops.object.mode_set(mode='OBJECT')
    for obj in objects:
        for vertex in obj.data.vertices:
            for side, sign in (('L', 1), ('R', -1)):
                weight = sum(binding.weight for binding in vertex.groups
                             if obj.vertex_groups[binding.group].name in chains[side])
                vertex.co.x += sign * offset * min(1., weight)
        obj.data.update()


def rebuild_shoulder_armor(rig, objects):
    """Round the CT pauldrons into closed, upper-arm-mounted armor shells."""
    for obj in objects:
        if not obj.name.startswith('ShoulderPad.'):
            continue
        side = obj.name[-1]
        bone = rig.pose.bones['UpperArm.' + side]
        rest = bone.bone
        along = (rest.tail_local - rest.head_local).normalized()
        rings = ((.02, .095), (.045, .12), (.15, .12), (.20, .105), (.23, .085))
        vertices, faces = [], []
        columns = 25
        layer_size = len(rings) * columns
        for layer in range(2):
            for distance, radius in rings:
                for column in range(columns):
                    angle = math.radians(-82 + 164 * column / (columns - 1))
                    point = rest.head_local + along * distance
                    point += Vector((0, math.sin(angle), math.cos(angle))) * (radius - layer * .012)
                    vertices.append(point)
        for layer in range(2):
            for row in range(len(rings) - 1):
                for column in range(columns - 1):
                    first = layer * layer_size + row * columns + column
                    face = (first, first + 1, first + columns + 1, first + columns)
                    faces.append(face if layer == 0 else tuple(reversed(face)))
        for row in (0, len(rings) - 1):
            for column in range(columns - 1):
                first = row * columns + column
                faces.append((first, first + 1, first + 1 + layer_size, first + layer_size))
        for column in (0, columns - 1):
            for row in range(len(rings) - 1):
                first = row * columns + column
                faces.append((first, first + columns, first + columns + layer_size, first + layer_size))
        materials = list(obj.data.materials)
        obj.parent = rig
        obj.parent_type = 'BONE'
        obj.parent_bone = bone.name
        bpy.context.view_layer.update()
        transform = obj.matrix_world.inverted() @ rig.matrix_world @ bone.matrix @ rest.matrix_local.inverted()
        mesh = bpy.data.meshes.new(obj.name + ' tailored armor')
        mesh.from_pydata([transform @ point for point in vertices], [], faces)
        for material in materials:
            mesh.materials.append(material)
        for polygon in mesh.polygons:
            polygon.use_smooth = True
        geometry = bmesh.new()
        geometry.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(geometry, faces=list(geometry.faces))
        geometry.to_mesh(mesh)
        geometry.free()
        obj.data = mesh


def tailor_forearms(rig, objects):
    """Taper sleeve cuffs around the forearm without moving the elbow seam."""
    for obj in objects:
        if obj.name not in ('Character_Enemy', 'Body'):
            continue
        for vertex in obj.data.vertices:
            bindings = {obj.vertex_groups[group.group].name: group.weight for group in vertex.groups}
            for side in ('L', 'R'):
                if bindings.get('LowerArm.' + side, 0.) < .65:
                    continue
                bone = rig.data.bones['LowerArm.' + side]
                axis = bone.tail_local - bone.head_local
                along = max(0., min(1., (vertex.co - bone.head_local).dot(axis) / axis.length_squared))
                center = bone.head_local + axis * along
                taper = .48 * min(1., along / .25)
                vertex.co = center + (vertex.co - center) * (1. - taper)


def tailor_armored_trousers(rig, objects):
    """Give the bulky tactical thighs clearance under the vest and at the crotch."""
    for obj in objects:
        if obj.name != 'Body':
            continue
        pants = {index for face in obj.data.polygons
                 if obj.data.materials[face.material_index].name == 'Pants'
                 for index in face.vertices}
        for index in pants:
            vertex = obj.data.vertices[index]
            weights = {obj.vertex_groups[group.group].name: group.weight for group in vertex.groups}
            side = 'L' if vertex.co.x >= 0 else 'R'
            weight = weights.get('UpperLeg.' + side, 0.)
            bone = rig.data.bones['UpperLeg.' + side]
            axis = bone.tail_local - bone.head_local
            along = max(0., min(1., (vertex.co - bone.head_local).dot(axis) / axis.length_squared))
            center = bone.head_local + axis * along
            strength = .28 * weight * min(1., along / .30)
            vertex.co = center + (vertex.co - center) * (1. - strength)
        obj.data.update()


def tailor_vest_hem(objects):
    """Shorten the tactical vest below the waist so raised thighs stay free."""
    for obj in objects:
        if obj.name != 'Body':
            continue
        vest = {index for face in obj.data.polygons
                if obj.data.materials[face.material_index].name == 'Character_Main'
                for index in face.vertices}
        for vertex in obj.data.vertices:
            if not .92 < vertex.co.z < 1.16:
                continue
            spine = sum(group.weight for group in vertex.groups
                        if obj.vertex_groups[group.group].name in ('Abdomen', 'Torso'))
            if vertex.index in vest or spine > .65:
                vertex.co.z += .12 * min(1., (1.16 - vertex.co.z) / .20)
        obj.data.update()


def tailor_jacket_sides(objects):
    """Replace the red jacket's recessed side ribs with a continuous cloth profile."""
    for obj in objects:
        if obj.name != 'Character_Enemy':
            continue
        cloth = obj.data.materials.find('Enemy_Red')
        for face in obj.data.polygons:
            if (obj.data.materials[face.material_index].name == 'DarkGrey'
                    and abs(face.center.x) > .20 and .98 < face.center.z < 1.26):
                face.material_index = cloth
        cloth_vertices = {index for face in obj.data.polygons
                          if obj.data.materials[face.material_index].name == 'Enemy_Red'
                          for index in face.vertices}
        for index in cloth_vertices:
            point = obj.data.vertices[index].co
            if not .94 < point.z < 1.28 or abs(point.x) < .14:
                continue
            height = max(0., min(1., (point.z - .96) / .30))
            radius_x = .295 - .055 * height
            radius_y = .19 - .035 * height
            strength = min(1., (abs(point.x) - .14) / .06,
                           (point.z - .94) / .03, (1.28 - point.z) / .03)
            side = math.copysign(1., point.x)
            profile = radius_x * math.sqrt(max(0., 1. - ((point.y - .025) / radius_y) ** 2))
            point.x += (side * profile - point.x) * strength


def repair_torso_weights(objects):
    """Keep jacket panels on the spine while preserving sleeve and hem motion."""
    for obj in objects:
        if obj.name not in ('Character_Enemy', 'Body'):
            continue
        for name in ('Abdomen', 'Torso'):
            if name not in obj.vertex_groups:
                obj.vertex_groups.new(name=name)
        for vertex in obj.data.vertices:
            x, _, height = vertex.co
            if abs(x) > .30 or not .94 < height < 1.40:
                continue
            weights = {obj.vertex_groups[group.group].name: group.weight
                       for group in vertex.groups}
            # The sleeve's inner root shares the chest's spatial bounds.
            if sum(weight for name, weight in weights.items()
                   if name.startswith(('UpperArm.', 'LowerArm.', 'Head', 'Neck'))) > .65:
                continue
            strength = min(1., (height - .94) / .10, (1.40 - height) / .13)
            spine = max(0., min(1., (height - 1.02) / .20))
            corrected = {name: weight * (1. - strength) for name, weight in weights.items()}
            corrected['Abdomen'] = corrected.get('Abdomen', 0.) + strength * (1. - spine)
            corrected['Torso'] = corrected.get('Torso', 0.) + strength * spine
            for group in obj.vertex_groups:
                if group.name in weights or group.name in corrected:
                    group.remove([vertex.index])
            for name, weight in corrected.items():
                if weight > .00001:
                    obj.vertex_groups[name].add([vertex.index], weight, 'REPLACE')


def weld_deformation_seams(objects):
    for obj in objects:
        geometry = bmesh.new()
        geometry.from_mesh(obj.data)
        bmesh.ops.remove_doubles(geometry, verts=list(geometry.verts), dist=.00001)
        geometry.to_mesh(obj.data)
        geometry.free()
        obj.data.update()
