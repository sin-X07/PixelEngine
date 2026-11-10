"""Run with Blender --background --python-exit-code 1 --python this_file."""
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
from solid_review import clear_cache, solid_surfaces, penetration_depths

bpy.ops.wm.read_factory_settings(use_empty=True)


def cube(name, reverse=False, opened=False):
    vertices = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    if opened:
        faces.pop(1)
    if reverse:
        faces = [tuple(reversed(face)) for face in faces]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def solids(obj):
    clear_cache()
    return solid_surfaces(obj, [vertex.co.copy() for vertex in obj.data.vertices])


for reverse in (False, True):
    target = solids(cube('Normal-independent cube', reverse=reverse))
    assert penetration_depths([Vector((0, 0, 0))], target)
    assert not penetration_depths([Vector((2, 0, 0))], target)
    assert not penetration_depths([Vector((.9999, 0, 0))], target)
    assert penetration_depths([Vector((.999, 0, 0))], target)

target = solids(cube('Open garment proxy', opened=True))
hits = penetration_depths([Vector((0, 0, 0))], target)
assert hits and all(proxy for _, proxy in hits)
assert target[2] == 1

outer = cube('Nested solids')
inner = cube('Inner solid')
vertices = [tuple(vertex.co) for vertex in outer.data.vertices]
vertices += [tuple(vertex.co * .5) for vertex in inner.data.vertices]
faces = [tuple(face.vertices) for face in outer.data.polygons]
faces += [tuple(index + 8 for index in face.vertices) for face in inner.data.polygons]
mesh = bpy.data.meshes.new('Nested geometry')
mesh.from_pydata(vertices, [], faces)
outer.data = mesh
assert penetration_depths([Vector((0, 0, 0))], solids(outer))

bpy.ops.mesh.primitive_torus_add(major_segments=48, minor_segments=16, major_radius=1, minor_radius=.2)
target = solids(bpy.context.object)
assert not penetration_depths([Vector((0, 0, 0))], target)
assert penetration_depths([Vector((1, 0, 0))], target)
print('SOLID REVIEW REGRESSIONS PASSED')
