"""Realized, baked sunlight motes for the desert courtyards."""
import math
import random

import bpy
import numpy as np
from bpy_extras import anim_utils
from mathutils import Vector


MOTE_PREFIX = 'Sunlit dust | '
CAMERA = Vector((0, -8, 1.05))
# Each tuple gives the shallow, middle and far pockets in Blender coordinates.
# The windows, canopy edge and outer lanes form different profiles in each yard.
POCKETS = {
    'dust_2': (
        [(-2.15, 3.15, 1.20), (1.25, 3.35, 1.46), (3.65, 3.38, 1.12)],
        [(-4.7, 4.35, 2.34), (-2.4, 5.45, 1.62), (1.00, 5.14, 2.12), (4.95, 5.28, 2.65)],
        [(-6.4, 8.55, 2.74), (-1.23, 8.7, 2.64), (2.7, 8.35, 3.05), (5.4, 9.35, 2.89)],
    ),
    'mirage': (
        [(-1.9, 3.31, 2.55), (3.35, 3.35, 1.85), (4.0, 3.28, 1.60)],
        [(-3.7, 4.56, 3.36), (1.38, 5.03, 2.78), (.92, 5.67, 3.42), (2.4, 4.45, 2.62)],
        [(.95, 8.70, 2.60), (.20, 8.56, 3.64), (1.22, 10.05, 2.17), (-.42, 10.18, 3.32)],
    ),
}


def dust_material():
    mat = bpy.data.materials.new('Sunlit dust')
    mat.use_nodes = True
    mat.surface_render_method = 'BLENDED'
    mat.use_backface_culling = True
    bsdf = next(node for node in mat.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Roughness'].default_value = 0.92
    bsdf.inputs['Metallic'].default_value = 0.0
    image = bpy.data.images.new('Warm dust soft coverage', width=64, height=32, alpha=True)
    u = np.linspace(0.0, 1.0, image.size[0])
    v = np.linspace(0.12, 0.30, image.size[1])
    coverage = np.clip((1.0 - u) / 0.72, 0.0, 1.0)
    coverage = coverage * coverage * (3.0 - 2.0 * coverage)
    pixels = np.empty((32, 64, 4), dtype=np.float32)
    pixels[:, :, :3] = (0.99, 0.87, 0.68)
    pixels[:, :, 3] = v[:, None] * coverage[None, :]
    image.pixels.foreach_set(pixels.ravel())
    image.pack()
    tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = image
    tex.extension = 'EXTEND'
    uv = mat.node_tree.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'Menu surface'
    mat.node_tree.links.new(uv.outputs['UV'], tex.inputs['Vector'])
    mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    mat.node_tree.links.new(tex.outputs['Alpha'], bsdf.inputs['Alpha'])
    return mat


def dust_nodes():
    tree = bpy.data.node_groups.new('Sunlit dust · realized 4D drift', 'GeometryNodeTree')
    tree.use_fake_user = True
    for direction in ('INPUT', 'OUTPUT'):
        tree.interface.new_socket(name='Geometry', in_out=direction, socket_type='NodeSocketGeometry')
    seed = tree.interface.new_socket(name='Seed', in_out='INPUT', socket_type='NodeSocketVector')
    amplitude = tree.interface.new_socket(name='Drift', in_out='INPUT', socket_type='NodeSocketFloat')
    nodes, links = tree.nodes, tree.links
    inp = nodes.new('NodeGroupInput')
    out = nodes.new('NodeGroupOutput')

    def math_node(op, source, value):
        node = nodes.new('ShaderNodeMath')
        node.operation = op
        links.new(source, node.inputs[0])
        node.inputs[1].default_value = value
        return node.outputs[0]

    time = nodes.new('GeometryNodeInputSceneTime')
    angle = math_node('MULTIPLY', time.outputs['Frame'], math.tau / 120)
    sin = math_node('SINE', angle, 0)
    cos = math_node('COSINE', angle, 0)
    periodic = nodes.new('ShaderNodeCombineXYZ')
    links.new(math_node('MULTIPLY', cos, 0.38), periodic.inputs['X'])
    links.new(math_node('MULTIPLY', sin, 0.28), periodic.inputs['Z'])
    offset_seed = nodes.new('ShaderNodeVectorMath')
    offset_seed.operation = 'ADD'
    links.new(inp.outputs['Seed'], offset_seed.inputs[0])
    links.new(periodic.outputs[0], offset_seed.inputs[1])
    noise = nodes.new('ShaderNodeTexNoise')
    noise.noise_dimensions = '4D'
    noise.inputs['Scale'].default_value = 1
    noise.inputs['Detail'].default_value = 2
    links.new(offset_seed.outputs[0], noise.inputs['Vector'])
    links.new(math_node('MULTIPLY', sin, 0.35), noise.inputs['W'])
    center = nodes.new('ShaderNodeVectorMath')
    center.operation = 'SUBTRACT'
    links.new(noise.outputs['Color'], center.inputs[0])
    center.inputs[1].default_value = (.5, .5, .5)
    scale = nodes.new('ShaderNodeVectorMath')
    scale.operation = 'SCALE'
    links.new(center.outputs[0], scale.inputs[0])
    links.new(inp.outputs['Drift'], scale.inputs['Scale'])
    points = nodes.new('GeometryNodeMeshToPoints')
    points.mode = 'VERTICES'
    links.new(inp.outputs['Geometry'], points.inputs['Mesh'])
    position = nodes.new('GeometryNodeSetPosition')
    links.new(points.outputs['Points'], position.inputs['Geometry'])
    links.new(scale.outputs[0], position.inputs['Offset'])
    ico = nodes.new('GeometryNodeMeshIcoSphere')
    ico.inputs['Subdivisions'].default_value = 2
    ico.inputs['Radius'].default_value = 1.0
    radius = nodes.new('GeometryNodeInputNamedAttribute')
    radius.data_type = 'FLOAT'
    radius.inputs['Name'].default_value = 'mote_radius'
    instance = nodes.new('GeometryNodeInstanceOnPoints')
    links.new(position.outputs['Geometry'], instance.inputs['Points'])
    links.new(ico.outputs['Mesh'], instance.inputs['Instance'])
    links.new(radius.outputs['Attribute'], instance.inputs['Scale'])
    realize = nodes.new('GeometryNodeRealizeInstances')
    links.new(instance.outputs['Instances'], realize.inputs['Geometry'])
    links.new(realize.outputs['Geometry'], out.inputs['Geometry'])
    for i, node in enumerate(nodes):
        node.location = (230*(i % 6), -210*(i//6))
    return tree, seed.identifier, amplitude.identifier


def sample_clear(scene, depsgraph, position, sun):
    if position.y < 2.95:
        return False
    # Three rays keep an entire moving pocket out of hard shadow boundaries.
    for offset in (Vector((0,0,0)), Vector((.13,0,-.13)), Vector((-.13,0,.13))):
        if scene.ray_cast(depsgraph, position+offset, sun, distance=60)[0]:
            return False
    direction = position - CAMERA
    direction_length = direction.length
    hit = scene.ray_cast(depsgraph, CAMERA, direction.normalized(), distance=direction_length-.08)
    return not hit[0]


def bake_dust(theme):
    scene = bpy.context.scene
    if any(obj.name.startswith(MOTE_PREFIX) for obj in scene.objects):
        raise ValueError("This scene already contains baked dust")
    scene.frame_set(0)
    rng = random.Random(19231 if theme == 'dust_2' else 49304)
    sun = Vector((-1.12, -.55 if theme=='dust_2' else .83, .71)).normalized()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    pockets = []
    for layer, centers in enumerate(POCKETS[theme]):
        for place in range(11 - (layer == 0)):
            center = Vector(centers[place % len(centers)])
            positions, sizes, opacity = [], [], []
            for _ in range(500):
                if len(positions) == 8:
                    break
                point = center + Vector((rng.uniform(-.71,.71), rng.uniform(-.42,.42),
                                         rng.uniform(-.65,.75)))
                if not sample_clear(scene, depsgraph, point, sun):
                    continue
                if any((point - old).length < .10 for old in positions):
                    continue
                positions.append(point)
                radius = (.030, .024, .020)[layer] * math.exp(rng.random()**2 * math.log(1.95))
                sizes.append(radius)
                opacity.append(min(1, (.45,.64,.91)[layer] + rng.uniform(-.19,.12)
                                      - .38*(radius/(.030,.024,.020)[layer]-1)))
            if positions:
                pockets.append((layer, center, positions, sizes, opacity))
    counts = [sum(len(positions) for level, _, positions, _, _ in pockets if level==i)
              for i in range(3)]
    if sum(counts) < 200 or not all(counts):
        raise ValueError(f"Insufficient sunlit candidates in the three depth ranges: {counts}")
    tree, seed_socket, amplitude_socket = dust_nodes()
    mat = dust_material()
    collection = bpy.data.collections.new('Sunlit dust · baked motes')
    scene.collection.children.link(collection)
    work = []
    for i, (layer, center, positions, radii, opacity) in enumerate(pockets):
        label = MOTE_PREFIX + f'{2-layer}-{i:02d}'
        data = bpy.data.meshes.new(label+' emitter')
        data.from_pydata([p-center for p in positions], [], [])
        attr = data.attributes.new('mote_radius', 'FLOAT', 'POINT')
        attr.data.foreach_set('value', radii)
        data.use_fake_user = True
        ob = bpy.data.objects.new(label, data)
        ob.location = center
        ob['dust_layer'] = layer
        ob['mote_count'] = len(positions)
        ob['radius_min'] = min(radii)
        ob['radius_max'] = max(radii)
        collection.objects.link(ob)
        mod = ob.modifiers.new('Dust scatter and drift', 'NODES')
        mod.node_group = tree
        getattr(mod.properties.inputs, seed_socket).value = (i*.951 + 2.3, 5.0+layer*.73, 7.25)
        getattr(mod.properties.inputs, amplitude_socket).value = (.41,.34,.28)[layer]
        mod.id_data.update_tag()
        work.append((ob, center, positions, radii, opacity, []))

    for frame in range(0, 121, 4):
        scene.frame_set(frame)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        for ob, _, _, _, _, samples in work:
            point = ob.evaluated_get(depsgraph).data.vertices[0].co.copy()
            samples.append(point)
    scene.frame_set(0)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    meshes = [bpy.data.meshes.new_from_object(ob.evaluated_get(depsgraph), depsgraph=depsgraph)
              for ob, *_ in work]
    maximum_seam = 0.0
    max_drift = 0.0
    for (ob, center, positions, radii, opacity, samples), output in zip(work, meshes):
        ob.modifiers.clear()
        ob.data = output
        output.name = ob.name + ' realized icospheres'
        output.materials.clear()
        output.materials.append(mat)
        maximum_seam = max(maximum_seam, (samples[0]-samples[-1]).length)
        max_drift = max(max_drift, max((p-samples[0]).length for p in samples))
        assert len(output.vertices) == 42*len(positions)
        values = {}
        for idx, (world, radius, alpha) in enumerate(zip(positions, radii, opacity)):
            part = list(output.vertices)[42*idx:42*(idx+1)]
            actual_center = sum((v.co for v in part), Vector())/42
            view = (CAMERA-world).normalized()
            tangent = view.orthogonal().normalized()
            alpha = min(.91, max(.09,alpha))
            for v in part:
                normal = (v.co-actual_center)/radius
                projected = math.sqrt(max(0, normal.length_squared - normal.dot(view)**2))
                # A small cross-mote gradient keeps coverage UVs two-dimensional.
                brightness = alpha + .064*normal.dot(tangent) + .018*normal.dot(view)
                values[v.index] = (min(1, projected), brightness)
        for layer in list(output.uv_layers):
            output.uv_layers.remove(layer)
        uv = output.uv_layers.new(name='Menu surface')
        uv.active_render = True
        output.uv_layers.active = uv
        for loop in output.loops:
            uv.data[loop.index].uv = values[loop.vertex_index]
        for poly in output.polygons:
            poly.use_smooth = True
        samples[-1] = samples[0].copy()
        for frame, point in zip(range(0,121,4), samples):
            ob.location = center + point - samples[0]
            ob.keyframe_insert(data_path='location', frame=frame)
        anim = ob.animation_data
        action = anim.action
        action.name = ob.name + ' slow convection'
        for curve in anim_utils.action_ensure_channelbag_for_slot(action, anim.action_slot).fcurves:
            for key in curve.keyframe_points:
                key.interpolation = 'LINEAR'
        anim.action = None
        track = anim.nla_tracks.new()
        track.name = 'Menu_Environment'
        track.strips.new('Menu_Environment', 0, action)
    scene.frame_set(0)
    result = {
        'particles': sum(ob['mote_count'] for ob, *_ in work), 'groups':len(work),
        'layers':[sum(ob['mote_count'] for ob, *_ in work if ob['dust_layer']==i) for i in range(3)],
        'radius_m':[min(ob['radius_min'] for ob, *_ in work),max(ob['radius_max'] for ob, *_ in work)],
        'max_drift_m':max_drift, 'pre_bake_seam_m':maximum_seam, 'baked_seam_m':0.0,
    }
    return result
