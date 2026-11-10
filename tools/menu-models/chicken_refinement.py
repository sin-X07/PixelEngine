"""Assign chicken materials from anatomical mesh parts and deform groups."""
import bmesh


def refine_chicken_materials(obj):
    geometry = bmesh.new()
    geometry.from_mesh(obj.data)
    bmesh.ops.bisect_plane(geometry,
        geom=[*geometry.verts, *geometry.edges, *geometry.faces],
        plane_co=(0, -.54, 0), plane_no=(0, 1, 0), dist=.000001,
        clear_inner=False, clear_outer=False)
    geometry.to_mesh(obj.data)
    geometry.free()
    obj.data.update()
    materials = list(obj.data.materials)
    feather = next(i for i, material in enumerate(materials) if 'ivory' in material.name.lower())
    comb = next(i for i, material in enumerate(materials) if 'comb' in material.name.lower())
    beak = next(i for i, material in enumerate(materials) if 'ochre' in material.name.lower())
    eye = next(i for i, material in enumerate(materials) if 'graphite' in material.name.lower())
    adjacency = [[] for _ in obj.data.vertices]
    for edge in obj.data.edges:
        first, second = edge.vertices
        adjacency[first].append(second)
        adjacency[second].append(first)
    seed = next(vertex.index for vertex in obj.data.vertices if vertex.co.z > 1.75)
    comb_vertices = {seed}
    pending = [seed]
    while pending:
        for index in adjacency[pending.pop()]:
            if index not in comb_vertices:
                comb_vertices.add(index)
                pending.append(index)
    for polygon in obj.data.polygons:
        groups = {}
        for index in polygon.vertices:
            for binding in obj.data.vertices[index].groups:
                name = obj.vertex_groups[binding.group].name
                groups[name] = groups.get(name, 0.) + binding.weight / len(polygon.vertices)
        eyes = sum(weight for name, weight in groups.items() if name.startswith('DEF-eye.'))
        feet = sum(weight for name, weight in groups.items()
                   if name.startswith(('DEF-foot.', 'DEF-toe.', 'DEF-thigh.', 'DEF-shin.')))
        tongue = sum(weight for name, weight in groups.items() if name.startswith('DEF-tongue.'))
        # The comb is a separate connected shell, including its low head-bound root.
        red = all(index in comb_vertices for index in polygon.vertices)
        polygon.material_index = feather
        if red:
            polygon.material_index = comb
        if eyes > .8:
            polygon.material_index = eye
        keratin = polygon.center.y < -.540001 and .91 < polygon.center.z < 1.11 and abs(polygon.center.x) < .16
        if feet > .8 or keratin:
            polygon.material_index = beak
        if tongue > .8:
            polygon.material_index = comb
