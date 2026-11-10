"""Keep authored grips fixed while fitting the elbow around weapon stocks."""
import math
import bmesh

import bpy
from mathutils import Quaternion, Vector
from mathutils.bvhtree import BVHTree


def arm_surface(rig, objects, side):
    points, polygons, distal, cloth = [], [], [], []
    bones = {'UpperArm.' + side, 'LowerArm.' + side}
    bone_points = {name: [] for name in bones}
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        rigid = obj.parent_type == 'BONE' and obj.parent_bone in bones
        names = [obj.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
                 if vertex.groups else '' for vertex in obj.data.vertices]
        selected = [polygon for polygon in obj.data.polygons
                    if rigid or all(names[index] in bones for index in polygon.vertices)]
        if not selected:
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        transform = rig.matrix_world.inverted() @ evaluated.matrix_world
        used = sorted({index for polygon in selected for index in polygon.vertices})
        remap = {index: len(points) + local for local, index in enumerate(used)}
        points.extend(transform @ mesh.vertices[index].co for index in used)
        for index in used:
            name = obj.parent_bone if rigid else names[index]
            bone_points[name].append(transform @ mesh.vertices[index].co)
        polygons.extend(tuple(remap[index] for index in polygon.vertices) for polygon in selected)
        if obj.name != 'Hands':
            cloth.extend(tuple(remap[index] for index in polygon.vertices) for polygon in selected)
        if not rigid:
            root = rig.data.bones['UpperArm.' + side].head_local
            rest_transform = rig.matrix_world.inverted() @ obj.matrix_world
            distal.extend(tuple(remap[index] for index in polygon.vertices) for polygon in selected
                          if all((rest_transform @ obj.data.vertices[index].co - root).length > .14
                                 for index in polygon.vertices))
        evaluated.to_mesh_clear()
    hulls = []
    for vertices in bone_points.values():
        hull = bmesh.new()
        for point in vertices:
            hull.verts.new(point)
        bmesh.ops.convex_hull(hull, input=list(hull.verts), use_existing_faces=False)
        bmesh.ops.recalc_face_normals(hull, faces=list(hull.faces))
        planes = [(face.calc_center_median(), face.normal.copy()) for face in hull.faces]
        lower = [min(point[axis] for point in vertices) - .004 for axis in range(3)]
        upper = [max(point[axis] for point in vertices) + .004 for axis in range(3)]
        hulls.append((planes, lower, upper))
        hull.free()
    distal_points = [points[index] for index in {index for face in distal for index in face}]
    return (BVHTree.FromPolygons(points, polygons), points, hulls,
            BVHTree.FromPolygons(points, distal), distal_points, BVHTree.FromPolygons(points, cloth))


def body_surface(rig, objects):
    """The torso and legs are obstacles while solving each elbow."""
    points, faces = [], []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        if obj.name == 'Hands' or obj.parent_type == 'BONE':
            continue
        names = [obj.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
                 if vertex.groups else '' for vertex in obj.data.vertices]
        selected = [polygon for polygon in obj.data.polygons
                    if all(not names[index].startswith(('UpperArm.', 'LowerArm.', 'Shoulder.', 'Hand.', 'Finger'))
                           for index in polygon.vertices)]
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        transform = rig.matrix_world.inverted() @ evaluated.matrix_world
        offset = len(points)
        points.extend(transform @ vertex.co for vertex in mesh.vertices)
        faces.extend(tuple(offset + index for index in polygon.vertices) for polygon in selected)
        evaluated.to_mesh_clear()
    return BVHTree.FromPolygons(points, faces)


def exposed_hand_surface(rig, objects, side):
    hands = next(obj for obj in objects if obj.name == 'Hands')
    inverse = rig.data.bones['Hand.' + side].matrix_local.inverted()
    allowed = []
    for vertex in hands.data.vertices:
        name = hands.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
        allowed.append(name.endswith('.' + side) and (
            name.startswith(('Index', 'Middle', 'Pinky', 'Thumb')) or
            (name == 'Hand.' + side and (inverse @ vertex.co).y > .012)))
    faces = [tuple(face.vertices) for face in hands.data.polygons
             if all(allowed[index] for index in face.vertices)]
    evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    transform = rig.matrix_world.inverted() @ evaluated.matrix_world
    points = [transform @ vertex.co for vertex in mesh.vertices]
    evaluated.to_mesh_clear()
    return BVHTree.FromPolygons(points, faces)


def enters_arm_volume(points, hulls):
    for point in points:
        for planes, lower, upper in hulls:
            if (all(lower[axis] < point[axis] < upper[axis] for axis in range(3))
                    and all((point - center).dot(normal) < .004 for center, normal in planes)):
                return True
    return False


def fit_elbow(rig, objects, side, pole, mesh, matrix, solve_arm, roll=None):
    hand = rig.pose.bones['Hand.' + side]
    hand_matrix = hand.matrix.copy()
    target = rig.pose.bones['LowerArm.' + side].tail.copy()
    shoulder = rig.pose.bones['UpperArm.' + side].head.copy()
    axis = (target - shoulder).normalized()
    weapon_points = [matrix @ vertex.co for vertex in mesh.vertices]
    weapon = BVHTree.FromPolygons(weapon_points, [tuple(face.vertices) for face in mesh.polygons])
    clearance = .004
    bounds = ([min(point[index] for point in weapon_points) - clearance for index in range(3)],
              [max(point[index] for point in weapon_points) + clearance for index in range(3)])
    body = body_surface(rig, objects)
    hand_surface = exposed_hand_surface(rig, objects, side)
    angles = [0, -10, 10, -20, 20, -30, 30, -40, 40, -50, 50, -60, 60, -70, 70, -80, 80]
    if roll is not None:
        angles = [roll] + sorted((angle for angle in range(-80, 81, 2) if angle != roll),
                                key=lambda angle: abs(angle - roll))
    for angle in angles:
        adjusted_pole = shoulder + Quaternion(axis, math.radians(angle)) @ (Vector(pole) - shoulder)
        solve_arm(rig, side, target, adjusted_pole)
        hand.matrix = hand_matrix
        bpy.context.view_layer.update()
        elbow = rig.pose.bones['LowerArm.' + side].head
        if abs(elbow.x) < .22 or elbow.z > shoulder.z - .05:
            continue
        arm, points, hulls, distal, distal_points, cloth = arm_surface(rig, objects, side)
        if distal.overlap(body):
            continue
        if cloth.overlap(hand_surface):
            continue
        margin = .008 if roll is None else .001
        if any(body.find_nearest(point, margin)[0] is not None for point in distal_points):
            continue
        if arm.overlap(weapon):
            continue
        if enters_arm_volume(weapon_points, hulls):
            continue
        near = False
        for point in points:
            if all(bounds[0][index] < point[index] < bounds[1][index] for index in range(3)):
                closest, _, _, distance = weapon.find_nearest(point, clearance)
                if closest is not None and distance < clearance:
                    near = True
                    break
        if not near:
            return angle
    raise RuntimeError(f'{rig.name} {side}: no natural elbow clearance for {mesh.name}, cached roll {roll}')
