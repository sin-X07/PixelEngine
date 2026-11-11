"""Regression for approaching and separating a hand at the weapon surface in Blender."""
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
from contact_fit import hand_offset

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.object.empty_add()
rig = bpy.context.object
bpy.ops.mesh.primitive_cube_add(size=1)
weapon = bpy.context.object
bpy.ops.mesh.primitive_cube_add(size=.2)
hands = bpy.context.object
group = hands.vertex_groups.new(name='Hand.R')
group.add(list(range(len(hands.data.vertices))), 1, 'REPLACE')
for center, expected in ((.7, -.0995), (.55, .0505)):
    hands.location = (center, 0, 0)
    bpy.context.view_layer.update()
    offset = hand_offset(rig, hands, 'R', weapon.data, weapon.matrix_world, Vector((1, 0, 0)))
    assert abs(offset.x-expected) < .00001, (center, offset.x, expected)
    assert abs(offset.y)+abs(offset.z) < .000001
print('CONTACT APPROACH AND SEPARATION PASS', flush=True)

# A protruding thumb must not determine the palm's offset.
vertices = [vertex.co.copy() for vertex in hands.data.vertices]
faces = [tuple(face.vertices) for face in hands.data.polygons]
thumb = [Vector((-.2, 0, 0)) + point*.25 for point in vertices]
mesh = bpy.data.meshes.new('PalmAndProtrudingThumb')
mesh.from_pydata(vertices + thumb, [], faces + [tuple(index+8 for index in face) for face in faces])
hands = bpy.data.objects.new('PalmThumbTest', mesh)
bpy.context.scene.collection.objects.link(hands)
group = hands.vertex_groups.new(name='Hand.R')
group.add(list(range(8)), 1, 'REPLACE')
thumb_group = hands.vertex_groups.new(name='Thumb1.R')
thumb_group.add(list(range(8, 16)), 1, 'REPLACE')
hands.location = (.7, 0, 0)
bpy.context.view_layer.update()
offset = hand_offset(rig, hands, 'R', weapon.data, weapon.matrix_world, Vector((1, 0, 0)), palm_only=True)
assert abs(offset.x + .0995) < .00001, offset.x
print('PALM CONTACT INDEPENDENT OF THUMB PASS', flush=True)
