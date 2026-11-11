"""Surface-driven hand contacts for the reconstruction candidate."""
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from hand_rig import curl_finger, oppose_thumb
from solid_review import ray_inside, RAYS


def grip_geometry(rig, hands, side, weapon_mesh, weapon_matrix, brace):
    points = [weapon_matrix @ vertex.co for vertex in weapon_mesh.vertices]
    faces = [tuple(face.vertices) for face in weapon_mesh.polygons]
    if brace:
        other = 'R' if side == 'L' else 'L'
        groups = [hands.vertex_groups[max(vertex.groups, key=lambda item: item.weight).group].name
                  for vertex in hands.data.vertices]
        evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
        geometry = evaluated.to_mesh()
        transform = rig.matrix_world.inverted() @ evaluated.matrix_world
        offset = len(points)
        opposite_faces = [tuple(face.vertices) for face in geometry.polygons
                          if all(groups[index].endswith('.' + other)
                                 and not groups[index].startswith('LowerArm.') for index in face.vertices)]
        used = sorted({index for face in opposite_faces for index in face})
        remap = {index: offset + local for local, index in enumerate(used)}
        points.extend(transform @ geometry.vertices[index].co for index in used)
        faces.extend(tuple(remap[index] for index in face) for face in opposite_faces)
        evaluated.to_mesh_clear()
    return points, faces


def hand_offset(rig, hands, side, weapon_mesh, weapon_matrix, outward, palm_only=False, brace=False):
    evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
    geometry = evaluated.to_mesh()
    transform = rig.matrix_world.inverted() @ evaluated.matrix_world
    points = [transform @ vertex.co for vertex in geometry.vertices]
    groups = [hands.vertex_groups[max(vertex.groups, key=lambda group: group.weight).group].name
              for vertex in hands.data.vertices]
    faces = [tuple(polygon.vertices) for polygon in geometry.polygons
             if all((groups[index] == 'Hand.' + side if palm_only else
                     groups[index].endswith('.' + side) and not groups[index].startswith('LowerArm.'))
                    for index in polygon.vertices)]
    used = sorted({index for face in faces for index in face})
    remap = {index: local for local, index in enumerate(used)}
    points = [points[index] for index in used]
    faces = [tuple(remap[index] for index in face) for face in faces]
    evaluated.to_mesh_clear()
    weapon_points, weapon_faces = grip_geometry(rig, hands, side, weapon_mesh, weapon_matrix, brace)
    weapon = BVHTree.FromPolygons(weapon_points, weapon_faces)
    direction = Vector(outward).normalized()
    def crossing(distance):
        offset = direction * distance
        moved = [point + offset for point in points]
        hand = BVHTree.FromPolygons(moved, faces)
        return bool(hand.overlap(weapon))

    # Find both sides of the contact boundary. An already separated hand must
    # approach the grip before clearance can be solved.
    if crossing(0):
        inside = 0.0
        outside = None
        for step in range(1, 151):
            distance = step * .002
            if not crossing(distance):
                outside = distance
                break
            inside = distance
    else:
        outside = 0.0
        inside = None
        for step in range(1, 76):
            distance = -step * .002
            if crossing(distance):
                inside = distance
                break
            outside = distance
    if inside is None or outside is None:
        inverse = weapon_matrix.inverted()
        local = [inverse @ point for point in points]
        bounds = [(round(min(point[i] for point in local), 4),
                   round(max(point[i] for point in local), 4)) for i in range(3)]
        raise RuntimeError(f'{rig.name} {side}: no grip contact along the approach axis; hand in weapon space {bounds}')
    for _ in range(12):
        middle = (inside + outside) * .5
        if crossing(middle):
            inside = middle
        else:
            outside = middle
    return direction * (outside + .0005)


def fit_grip_digits(rig, hands, side, weapon_mesh, weapon_matrix, cached=None, brace=False):
    """Fit distal finger pads after the palm has been placed at the grip surface."""
    if cached is not None:
        for finger, (angle, knuckle) in cached.items():
            if finger == 'Thumb':
                oppose_thumb(rig, side, angle, knuckle)
            else:
                curl_finger(rig, side, finger, angle, knuckle)
        bpy.context.view_layer.update()
        return cached
    fitted = {}
    weapon_points, weapon_faces = grip_geometry(rig, hands, side, weapon_mesh, weapon_matrix, brace)
    weapon = BVHTree.FromPolygons(weapon_points, weapon_faces)
    clearance_weapon = BVHTree.FromPolygons(weapon_points, weapon_faces, epsilon=.001)
    groups = [hands.vertex_groups[max(vertex.groups, key=lambda item: item.weight).group].name
              for vertex in hands.data.vertices]
    transform = rig.matrix_world.inverted() @ hands.matrix_world
    evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
    geometry = evaluated.to_mesh()
    palm = BVHTree.FromPolygons([transform @ vertex.co for vertex in geometry.vertices],
        [tuple(face.vertices) for face in geometry.polygons
         if all(groups[index] == 'Hand.' + side for index in face.vertices)])
    evaluated.to_mesh_clear()
    for finger in ('Index', 'Middle', 'Pinky', 'Thumb'):
        indices = [i for i, name in enumerate(groups) if name.startswith(finger) and name.endswith('.' + side)]
        distal = [i for i in indices if groups[i] == finger + ('2.' if finger == 'Thumb' else '3.') + side]
        tip = [i for i in distal if (rig.data.bones[groups[i]].matrix_local.inverted() @ hands.data.vertices[i].co).y
               > rig.data.bones[groups[i]].length * .78]
        selected = set(indices)
        faces = [tuple(face.vertices) for face in hands.data.polygons
                 if all(index in selected for index in face.vertices)]
        best = None
        crossings = 0
        contained = 0
        clearance_rejections = 0
        limit = 1.0 if finger == 'Thumb' else 1.7
        trials = [(spread, limit*step/24) for spread in (0, .3, .6, .9, 1.2)
                  for step in range(25)]
        if finger != 'Thumb':
            trials = [(knuckle, limit*step/24) for knuckle in (-.35, -.2, 0, .2, .4)
                      for step in range(25)]
        for knuckle, angle in trials:
            if finger == 'Thumb':
                oppose_thumb(rig, side, angle, knuckle)
            else:
                curl_finger(rig, side, finger, angle, knuckle)
            bpy.context.view_layer.update()
            evaluated = hands.evaluated_get(bpy.context.evaluated_depsgraph_get())
            geometry = evaluated.to_mesh()
            points = [transform @ vertex.co for vertex in geometry.vertices]
            evaluated.to_mesh_clear()
            surface = BVHTree.FromPolygons(points, faces)
            if surface.overlap(clearance_weapon):
                crossings += 1
                continue
            closest = [weapon.find_nearest(points[index]) for index in indices]
            lower = [min(points[index][axis] for index in indices) - .003 for axis in range(3)]
            upper = [max(points[index][axis] for index in indices) + .003 for axis in range(3)]
            nearby = [point for point in weapon_points
                      if all(lower[axis] < point[axis] < upper[axis] for axis in range(3))]
            opposite_gaps = [hit[3] for point in nearby
                             if (hit := surface.find_nearest(point, .003))[0] is not None]
            if min([hit[3] for hit in closest] + opposite_gaps) < .001:
                clearance_rejections += 1
                continue
            if any(hit is not None and distance > .0002 and (points[index]-hit).dot(normal) < -.0002
                   and all(ray_inside(weapon, points[index], ray) for ray in RAYS)
                   for index, (hit, normal, _, distance) in zip(indices, closest)):
                contained += 1
                continue
            if any(palm.find_nearest(points[index])[3] > .0005
                   and all(ray_inside(palm, points[index], ray) for ray in RAYS) for index in tip):
                contained += 1
                continue
            gap = min(weapon.find_nearest(points[index])[3] for index in distal)
            score = abs(gap-.0015) + (abs(angle-limit*.7)+abs(knuckle))*.0001
            if best is None or score < best[0]:
                best = (score, angle, gap, knuckle)
        if best is None:
            raise RuntimeError(f'{rig.name} {side} {finger}: no collision-free grip articulation; crossing={crossings}, containment={contained}, clearance={clearance_rejections}')
        if finger == 'Thumb':
            oppose_thumb(rig, side, best[1], best[3])
        else:
            curl_finger(rig, side, finger, best[1], best[3])
        bpy.context.view_layer.update()
        fitted[finger] = (best[1], best[3])
    return fitted
