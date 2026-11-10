"""Containment checks on welded solid components, with explicit boundary-cap proxies.

Nearest-face normals alone cannot classify the interior of disconnected meshes.
Closed components use three independent ray parity tests. Simple open boundary
loops receive review-only caps. Nonmanifold components remain unresolved.
"""
from collections import Counter, defaultdict

from mathutils import Vector
from mathutils.bvhtree import BVHTree

_topology = {}
RAYS = [Vector(direction).normalized() for direction in
        ((1, .3127, .8271), (-.5321, 1, .2731), (.1732, -.4123, 1))]


def clear_cache():
    _topology.clear()


def component_topology(obj):
    key = obj.data.as_pointer()
    if key in _topology:
        return _topology[key]
    canonical, representatives, remap = {}, [], []
    for vertex in obj.data.vertices:
        position = tuple(round(value, 6) for value in vertex.co)
        if position not in canonical:
            canonical[position] = len(representatives)
            representatives.append(vertex.index)
        remap.append(canonical[position])
    parent = list(range(len(representatives)))

    def root(index):
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    faces = []
    for polygon in obj.data.polygons:
        face = tuple(remap[index] for index in polygon.vertices)
        if len(set(face)) < 3:
            continue
        faces.append(face)
        for index in face[1:]:
            parent[root(index)] = root(face[0])
    groups = defaultdict(list)
    for face in faces:
        groups[root(face[0])].append(face)
    components, unresolved, capped = [], 0, 0
    for component in groups.values():
        edges = Counter()
        for face in component:
            for first, second in zip(face, face[1:] + face[:1]):
                edges[tuple(sorted((first, second)))] += 1
        if any(count > 2 for count in edges.values()):
            unresolved += 1
            continue
        boundary = [edge for edge, count in edges.items() if count == 1]
        caps = []
        if boundary:
            adjacency = defaultdict(list)
            for first, second in boundary:
                adjacency[first].append(second)
                adjacency[second].append(first)
            if any(len(neighbors) != 2 for neighbors in adjacency.values()):
                unresolved += 1
                continue
            remaining = set(adjacency)
            while remaining:
                start = next(iter(remaining))
                loop, previous, current = [], None, start
                while current in remaining:
                    remaining.remove(current)
                    loop.append(current)
                    following = next(index for index in adjacency[current] if index != previous)
                    previous, current = current, following
                if current != start or len(loop) < 3:
                    raise RuntimeError(f'{obj.name}: invalid review boundary loop')
                caps.append(tuple(loop))
            capped += 1
        source_faces = [tuple(representatives[index] for index in face) for face in [*component, *caps]]
        components.append((source_faces, bool(caps)))
    result = components, unresolved, capped
    _topology[key] = result
    return result


def solid_surfaces(obj, points):
    topology, unresolved, capped = component_topology(obj)
    solids = []
    for faces, proxy in topology:
        used = {index for face in faces for index in face}
        lower = [min(points[index][axis] for index in used) for axis in range(3)]
        upper = [max(points[index][axis] for index in used) for axis in range(3)]
        solids.append((BVHTree.FromPolygons(points, faces), lower, upper, proxy))
    solids.sort(key=lambda solid: solid[3])
    return solids, unresolved, capped


def ray_inside(tree, point, direction):
    origin = point.copy()
    crossings = 0
    for _ in range(128):
        hit, _, _, _ = tree.ray_cast(origin, direction)
        if hit is None:
            return crossings % 2 == 1
        crossings += 1
        origin = hit + direction * .00001
    raise RuntimeError('Containment ray did not leave a finite component')


def penetration_depths(points, target, witnesses=None):
    solids, _, _ = target
    depths = []
    for point in points:
        for tree, lower, upper, proxy in solids:
            if not all(lower[axis] < point[axis] < upper[axis] for axis in range(3)):
                continue
            closest, _, _, distance = tree.find_nearest(point)
            if closest is None or distance <= .0005:
                continue
            if all(ray_inside(tree, point, direction) for direction in RAYS):
                depths.append((distance, proxy))
                if witnesses is not None and len(witnesses) < 8:
                    witnesses.append({'point': [round(value, 6) for value in point],
                                      'depth_m': round(distance, 6), 'boundary_proxy': proxy})
                break
    return depths
