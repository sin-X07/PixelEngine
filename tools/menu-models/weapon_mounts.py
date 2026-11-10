"""Closed retention hardware for body-mounted menu weapons."""
import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

from accessory_geometry import append_tube


def rig_geometry(rig, objects):
    points, faces = [], []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        transform = rig.matrix_world.inverted() @ evaluated.matrix_world
        offset = len(points)
        points.extend(transform @ vertex.co for vertex in mesh.vertices)
        faces.extend(tuple(offset + index for index in face.vertices) for face in mesh.polygons)
        evaluated.to_mesh_clear()
    return points, faces


def rig_surface(rig, objects):
    return BVHTree.FromPolygons(*rig_geometry(rig, objects))


def append_hardware(mesh, points, faces, material=None):
    material = material or bpy.data.materials.get('Weapon retention graphite')
    if material is None:
        material = bpy.data.materials.new('Weapon retention graphite')
        material.diffuse_color = (.045, .055, .065, 1)
    slot = mesh.materials.find(material.name)
    if slot < 0:
        mesh.materials.append(material)
        slot = len(mesh.materials) - 1
    geometry = bmesh.new()
    geometry.from_mesh(mesh)
    added = [geometry.verts.new(point) for point in points]
    new_faces = []
    for indices in faces:
        face = geometry.faces.new([added[index] for index in indices])
        face.material_index = slot
        new_faces.append(face)
    bmesh.ops.recalc_face_normals(geometry, faces=new_faces)
    geometry.to_mesh(mesh)
    geometry.free()
    mesh.update()


def add_knife_sheath(mesh):
    """Cover the stowed blade while leaving the handle available above the throat."""
    points, faces = [], []
    for height, width, depth in ((.23, .19, .09), (.29, .19, .09),
                                 (.95, .17, .085), (1.23, .10, .065)):
        for x, y in ((-1, -.7), (-.7, -1), (.7, -1), (1, -.7),
                     (1, .7), (.7, 1), (-.7, 1), (-1, .7)):
            points.append(Vector((x * width, y * depth, height)))
    faces.append(tuple(reversed(range(8))))
    for row in range(3):
        for column in range(8):
            first = row * 8 + column
            following = row * 8 + (column + 1) % 8
            faces.append((first, following, following + 8, first + 8))
    faces.append(tuple(range(24, 32)))
    append_hardware(mesh, points, faces)


def add_retention_mounts(rig, body, mesh, matrix, sidearm):
    """Bridge the fitted weapon/body gap with two discrete retention fittings."""
    surface = rig_surface(rig, body)
    weapon_points = [matrix @ vertex.co for vertex in mesh.vertices]
    weapon = BVHTree.FromPolygons(weapon_points, [tuple(face.vertices) for face in mesh.polygons])
    outward = Vector((-1, 0, 0) if sidearm else (0, 1, 0))
    minimum = min(point.z for point in weapon_points)
    maximum = max(point.z for point in weapon_points)
    points, faces = [], []
    for fraction in (.35, .65):
        height = minimum + (maximum - minimum) * fraction
        if not sidearm:
            height = max(.98, min(1.20, height))
        section = [point for point in weapon_points if abs(point.z - height) < .08]
        if not section:
            raise RuntimeError('Stowed weapon has no receiver section at the mounting height')
        center = sum(section, Vector()) / len(section)
        attached = False
        for offset in (0, -.025, .025, -.05, .05, -.08, .08):
            origin = Vector((0, center.y + offset, height) if sidearm else (center.x + offset, 0, height))
            inner, _, _, _ = weapon.ray_cast(origin - outward * 2, outward, 4)
            contact, normal, _, _ = surface.ray_cast(origin + outward * 2, -outward, 4)
            if inner is None or contact is None or (inner - contact).dot(outward) < .012:
                continue
            if normal.dot(outward) < 0:
                normal = -normal
            end = inner + outward * .025
            for clearance in (.012, .018, .024):
                fitting_points, fitting_faces, fitting_slots = [], [], []
                start = contact + normal * (clearance + .012)
                append_tube(fitting_points, fitting_faces, fitting_slots, [start, end], .012, 0)
                append_tube(fitting_points, fitting_faces, fitting_slots,
                            [contact + normal * clearance, start], .022, 0)
                fitting = BVHTree.FromPolygons(fitting_points, fitting_faces)
                if fitting.overlap(surface):
                    continue
                first = len(points)
                points.extend(fitting_points)
                faces.extend(tuple(first + index for index in face) for face in fitting_faces)
                attached = True
                break
            if attached:
                break
        if not attached:
            raise RuntimeError('Stowed weapon retention fitting has no body contact')
    inverse = matrix.inverted()
    append_hardware(mesh, [inverse @ point for point in points], faces)
