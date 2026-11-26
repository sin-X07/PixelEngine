"""Rear archaeological & rainforest remaster for PixelEngine's Ancient Ruins (Observatory of Rain).

Follows `blender-hardsurface-modeling`, `blender-environment-kits`, `blender-pbr-materials`,
and `blender-asset-qa` principles:
  - Enriches the empty rear corridor (y in [7.0, 24.5], x in [-16.0, 16.0]) with multi-layered
    Mayan rainforest architecture: Corbelled Sanctuary Gatehouse, Calendar Rear Gallery,
    a 3-span Ruined Aqueduct Viaduct, Distant Grand Stepped Pyramid & Watchtower, carved stelae,
    ceremonial braziers, buttress-rooted Ceiba trees, ferns, broadleaves & hanging lianas.
  - Per-zone object grouping so no AABB ever enters the protected central arena
    (x in [-6.3, 6.3], y in [-6.1, 2.7], z in [0, 6.5]).
  - Preserves camera, foreground focal masonry, and all 18 `Wind |` animated foliage channels.
"""
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix

TAU = math.tau

REMASTER_PREFIX = 'Observatory of Rain | RearRemaster '


def _mat(scene, short_key):
    prefix = scene.get('material_prefix', 'Observatory of Rain | ')
    m = bpy.data.materials.get(prefix + short_key)
    if m:
        return m
    for mat in bpy.data.materials:
        if mat.name.endswith(' | ' + short_key) or mat.name == short_key:
            return mat
    return bpy.data.materials.get(prefix + 'rock')


def _uv_name(scene):
    for obj in scene.objects:
        if obj.type == 'MESH' and obj.data.uv_layers:
            return obj.data.uv_layers[0].name
    return 'Surface'


def _box_project_uv(obj, tile_size=2.0, uv_name='Surface'):
    """Assign distortion-free world-space box-projected UVs."""
    me = obj.data
    if not me.polygons:
        return
    uv_layer = me.uv_layers.get(uv_name) or me.uv_layers.new(name=uv_name)
    mw = obj.matrix_world
    rot_m = mw.to_3x3()
    for poly in me.polygons:
        wn = (rot_m @ poly.normal).normalized()
        ax, ay, az = abs(wn.x), abs(wn.y), abs(wn.z)
        for li in poly.loop_indices:
            vi = me.loops[li].vertex_index
            wp = mw @ me.vertices[vi].co
            if az >= ax and az >= ay:
                uv_layer.data[li].uv = (wp.x / tile_size, wp.y / tile_size)
            elif ay >= ax:
                uv_layer.data[li].uv = (wp.x / tile_size, wp.z / tile_size)
            else:
                uv_layer.data[li].uv = (wp.y / tile_size, wp.z / tile_size)


def _new_mesh_obj(scene, name, bm, mat, tile_size=2.0, uv_name='Surface', sharp_deg=44.0):
    if len(bm.verts) == 0 or len(bm.faces) == 0:
        bm.free()
        return None
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=list(bm.edges))
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-4)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.normal_update()
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.clear()
    if mat:
        me.materials.append(mat)
    if uv_name not in me.uv_layers:
        me.uv_layers.new(name=uv_name)
    if hasattr(me, 'shade_smooth'):
        me.shade_smooth()
    if hasattr(me, 'set_sharp_from_angle'):
        me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    obj = bpy.data.objects.new(name, me)
    scene.collection.objects.link(obj)
    _box_project_uv(obj, tile_size=tile_size, uv_name=uv_name)
    return obj


def _merge_submesh(dst_bm, sub_bm):
    bmesh.ops.recalc_face_normals(sub_bm, faces=list(sub_bm.faces))
    me = bpy.data.meshes.new('_tmp_ruins_sub')
    sub_bm.to_mesh(me)
    sub_bm.free()
    dst_bm.from_mesh(me)
    bpy.data.meshes.remove(me)


def _add_box(bm, cx, cy, cz, sx, sy, sz, rot_z=0.0, rot_y=0.0, rot_x=0.0, bevel_w=0.0):
    sub = bmesh.new()
    bmesh.ops.create_cube(sub, size=1.0)
    mat = (
        Matrix.Translation((cx, cy, cz))
        @ Matrix.Rotation(rot_z, 4, 'Z')
        @ Matrix.Rotation(rot_y, 4, 'Y')
        @ Matrix.Rotation(rot_x, 4, 'X')
    )
    for v in sub.verts:
        v.co = mat @ Vector((v.co.x * sx, v.co.y * sy, v.co.z * sz))
    bw = min(bevel_w, min(sx, sy, sz) * 0.16) if bevel_w > 0.0005 else 0.0
    if bw > 0.0005:
        bmesh.ops.bevel(
            sub, geom=list(sub.edges), offset=bw, segments=2,
            profile=0.5, affect='EDGES', clamp_overlap=True
        )
    _merge_submesh(bm, sub)


def _add_frustum(bm, cx, cy, z0, z1, sx0, sy0, sx1, sy1, rot_z=0.0, bevel_w=0.0):
    """Tapered Mayan talud/prism block from z0 (bottom) to z1 (top)."""
    sub = bmesh.new()
    rz = Matrix.Rotation(rot_z, 3, 'Z')
    hx0, hy0 = sx0 * 0.5, sy0 * 0.5
    hx1, hy1 = sx1 * 0.5, sy1 * 0.5
    pts = [
        (-hx0, -hy0, z0), (hx0, -hy0, z0), (hx0, hy0, z0), (-hx0, hy0, z0),
        (-hx1, -hy1, z1), (hx1, -hy1, z1), (hx1, hy1, z1), (-hx1, hy1, z1),
    ]
    vs = [sub.verts.new(Vector((cx, cy, 0.0)) + rz @ Vector(p)) for p in pts]
    faces = [
        (0, 3, 2, 1), (4, 5, 6, 7),
        (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7),
    ]
    for f in faces:
        sub.faces.new([vs[i] for i in f])
    if bevel_w > 0.001:
        bmesh.ops.bevel(
            sub, geom=list(sub.edges), offset=bevel_w, segments=2,
            profile=0.5, affect='EDGES', clamp_overlap=True
        )
    _merge_submesh(bm, sub)


def _add_cyl(bm, p0, p1, r0, r1, segs=10):
    sub = bmesh.new()
    vec = Vector(p1) - Vector(p0)
    length = vec.length
    if length < 1e-5:
        sub.free()
        return
    bmesh.ops.create_cone(
        sub, cap_ends=True, cap_tris=False, segments=segs,
        radius1=r0, radius2=r1, depth=length
    )
    mid = (Vector(p0) + Vector(p1)) * 0.5
    rot = vec.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
    mat = Matrix.Translation(mid) @ rot
    for v in sub.verts:
        v.co = mat @ v.co
    _merge_submesh(bm, sub)


def _add_lathe(bm, cx, cy, z0, profile, segs=14):
    """Profile = list of (dz, radius) ordered bottom -> top."""
    sub = bmesh.new()
    rings = []
    for dz, r in profile:
        rr = max(0.004, r)
        ring = [
            sub.verts.new((
                cx + rr * math.cos(k * TAU / segs),
                cy + rr * math.sin(k * TAU / segs),
                z0 + dz,
            ))
            for k in range(segs)
        ]
        rings.append(ring)
    for r_idx in range(len(rings) - 1):
        a, b = rings[r_idx], rings[r_idx + 1]
        for k in range(segs):
            kn = (k + 1) % segs
            sub.faces.new((a[k], a[kn], b[kn], b[k]))
    if len(rings[0]) >= 3:
        sub.faces.new(list(reversed(rings[0])))
    if len(rings[-1]) >= 3:
        sub.faces.new(rings[-1])
    _merge_submesh(bm, sub)


def _add_leaf_blade(bm, origin, direction, length, width, droop=0.22, arch=0.14, segs=5, roll=0.0):
    """Create a clean, non-self-intersecting arched 3D tropical leaf blade."""
    sub = bmesh.new()
    d = Vector(direction).normalized()
    up = Vector((0, 0, 1))
    side = d.cross(up)
    if side.length < 1e-4:
        side = Vector((1, 0, 0))
    side.normalize()
    bend_up = side.cross(d).normalized()
    if abs(roll) > 1e-4:
        c, s = math.cos(roll), math.sin(roll)
        side, bend_up = side * c + bend_up * s, -side * s + bend_up * c
    top_rows, bot_rows = [], []
    for i in range(segs + 1):
        t = i / segs
        w = max(0.015, width * math.sin(math.pi * (0.14 + 0.80 * t)) * (1.0 - 0.30 * t * t))
        thick = max(0.006, 0.016 * (1.0 - 0.55 * t))
        pos = Vector(origin) + d * (length * t) + bend_up * (arch * math.sin(math.pi * t) - droop * t * t * length)
        tl = sub.verts.new(pos - side * w + bend_up * (0.014 * w + thick))
        tm = sub.verts.new(pos + bend_up * thick)
        tr = sub.verts.new(pos + side * w + bend_up * (0.014 * w + thick))
        bl = sub.verts.new(pos - side * (w * 0.82) + bend_up * (0.008 * w))
        bm_v = sub.verts.new(pos - bend_up * (0.012 * w))
        br = sub.verts.new(pos + side * (w * 0.82) + bend_up * (0.008 * w))
        top_rows.append((tl, tm, tr))
        bot_rows.append((bl, bm_v, br))
    for i in range(segs):
        ta, tb = top_rows[i], top_rows[i + 1]
        ba, bb = bot_rows[i], bot_rows[i + 1]
        sub.faces.new((ta[0], ta[1], tb[1], tb[0]))
        sub.faces.new((ta[1], ta[2], tb[2], tb[1]))
        sub.faces.new((ba[1], ba[0], bb[0], bb[1]))
        sub.faces.new((ba[2], ba[1], bb[1], bb[2]))
        sub.faces.new((ba[0], ta[0], tb[0], bb[0]))
        sub.faces.new((ta[2], ba[2], bb[2], tb[2]))
    # Base & tip end caps
    sub.faces.new((bot_rows[0][0], bot_rows[0][1], top_rows[0][1], top_rows[0][0]))
    sub.faces.new((bot_rows[0][1], bot_rows[0][2], top_rows[0][2], top_rows[0][1]))
    sub.faces.new((top_rows[-1][0], top_rows[-1][1], bot_rows[-1][1], bot_rows[-1][0]))
    sub.faces.new((top_rows[-1][1], top_rows[-1][2], bot_rows[-1][2], bot_rows[-1][1]))
    _merge_submesh(bm, sub)


def _add_moss_cap(bm, cx, cy, cz, sx, sy, thick=0.09, seed=1):
    """Organic draped moss pad on top of stone cornices or ruins."""
    rng = random.Random(seed)
    sub = bmesh.new()
    bmesh.ops.create_icosphere(sub, subdivisions=2, radius=1.0)
    for v in sub.verts:
        nx, ny, nz = v.co.x, v.co.y, v.co.z
        wobble = 0.85 + 0.25 * rng.random()
        vx = cx + nx * (sx * 0.52) * wobble
        vy = cy + ny * (sy * 0.52) * wobble
        vz = cz + (max(-0.2, nz) * thick * (0.8 + 0.35 * rng.random()))
        v.co = Vector((vx, vy, vz))
    _merge_submesh(bm, sub)


def _remove_obsolete_rear_placeholders(scene):
    """Remove coarse vertical Rear canopy stems & bare Upstream piers before rebuilding rich versions."""
    obsolete_prefixes = (
        'Observatory of Rain | Rear canopy stem',
        'Observatory of Rain | Distant canopy scallop',
        'Observatory of Rain | Upstream pier',
        'Observatory of Rain | Upstream cap',
        'Observatory of Rain | Broken crossing',
        REMASTER_PREFIX,
    )
    for obj in list(scene.objects):
        if obj.type == 'MESH' and obj.name.startswith(obsolete_prefixes):
            me = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if me and me.users == 0:
                bpy.data.meshes.remove(me)


def _build_zone_meshes(scene, zone_name, parts_dict, uv_name):
    """parts_dict: mat_key -> (bmesh, tile_size)"""
    for tag, (bm, tile) in parts_dict.items():
        _new_mesh_obj(
            scene,
            f'{REMASTER_PREFIX}{zone_name} {tag}',
            bm,
            _mat(scene, tag),
            tile_size=tile,
            uv_name=uv_name,
        )


def _build_corbelled_sanctuary_gate(scene, uv_name):
    """Central rear sanctuary portal (x=1.05, y=10.8..13.2) bridging the gap left of Calendar."""
    parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.0), ('basalt', 1.8), ('carved_stone', 1.25),
        ('edge', 1.7), ('jade', 0.9), ('gold', 0.8), ('lichen', 2.0), ('deep', 1.5),
    )}
    bm_r, bm_b, bm_c, bm_e, bm_j, bm_g, bm_l, bm_d = [parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'jade', 'gold', 'lichen', 'deep'
    )]

    gx, gy = 0.80, 11.45
    # 3-stepped ceremonial stone plinth
    for step in range(3):
        w = 3.35 - step * 0.28
        d = 2.65 - step * 0.20
        _add_frustum(bm_b, gx, gy, step * 0.18, (step + 1) * 0.18, w, d, w - 0.08, d - 0.08, bevel_w=0.015)

    # Left and right stepped Mayan jamb piers (open center arch corridor 1.25m wide)
    for sx in (-1.02, 1.02):
        px = gx + sx
        _add_frustum(bm_r, px, gy, 0.54, 2.75, 0.98, 1.85, 0.90, 1.75, bevel_w=0.022)
        # Carved basalt relief inset panels facing camera (distinct offset forward, zero coplanar)
        _add_box(bm_c, px, gy - 0.94, 1.65, 0.62, 0.12, 1.42, bevel_w=0.012)
        _add_box(bm_e, px, gy - 0.90, 0.72, 1.04, 1.94, 0.16, bevel_w=0.015)
        _add_box(bm_e, px, gy - 0.90, 2.76, 1.02, 1.90, 0.18, bevel_w=0.014)
        # Moss tufts hugging pier corners (embed 0.04m into stone cap)
        _add_moss_cap(bm_l, px + sx * 0.10, gy - 0.68, 2.82, 0.72, 0.75, thick=0.10, seed=int(100 + sx * 10))

    # Mayan stepped Corbelled Arch (5 cantilevered inward tiers meeting near top lintel)
    for tier in range(5):
        z0 = 2.75 + tier * 0.26
        z1 = z0 + 0.26
        span_in = 0.24 + tier * 0.12
        bw = 0.90 - tier * 0.04
        for sx in (-1.0, 1.0):
            cx_b = gx + sx * (1.02 - tier * 0.11)
            _add_box(bm_r, cx_b, gy, (z0 + z1) * 0.5, bw + span_in * 0.45, 1.68, 0.25, bevel_w=0.014)
            _add_box(bm_e, cx_b, gy - 0.86, (z0 + z1) * 0.5, 0.34, 0.08, 0.22, bevel_w=0.008)

    # Recessed inner sanctuary altar visible inside the corbelled tunnel (embedded to avoid detached islands)
    _add_frustum(bm_b, gx, gy + 1.55, 0.50, 1.45, 1.10, 0.85, 0.92, 0.72, bevel_w=0.014)
    _add_box(bm_j, gx, gy + 1.15, 1.15, 0.32, 0.14, 0.32, rot_y=math.pi / 4, bevel_w=0.006)
    _add_box(bm_d, gx, gy + 1.94, 2.35, 1.55, 0.24, 3.60)

    # Heavy carved entablature lintel & cornice frieze
    _add_box(bm_e, gx, gy - 0.04, 4.18, 3.45, 1.92, 0.34, bevel_w=0.022)
    _add_box(bm_c, gx, gy - 0.06, 4.56, 3.20, 1.84, 0.48, bevel_w=0.018)
    _add_box(bm_e, gx, gy - 0.06, 4.86, 3.55, 2.02, 0.22, bevel_w=0.018)

    # Central Mayan Rain-Deity Emblem & Jade-Gold medallion on frieze
    _add_box(bm_b, gx, gy - 1.00, 4.56, 0.74, 0.16, 0.68, bevel_w=0.015)
    _add_box(bm_j, gx, gy - 1.09, 4.56, 0.34, 0.08, 0.34, rot_y=math.pi / 4, bevel_w=0.01)
    _add_box(bm_g, gx, gy - 1.13, 4.56, 0.14, 0.06, 0.14, rot_y=math.pi / 4, bevel_w=0.006)
    for ox in (-0.95, 0.95):
        _add_box(bm_j, gx + ox, gy - 1.01, 4.56, 0.22, 0.08, 0.22, bevel_w=0.008)

    # Openwork Mayan Roof Comb (cresteria) crowning the gatehouse
    for i in range(5):
        px = gx - 1.10 + i * 0.55
        h_fin = 0.92 if i == 2 else (0.76 if i in (1, 3) else 0.54)
        _add_frustum(bm_r, px, gy + 0.1, 4.96, 4.96 + h_fin, 0.32, 0.46, 0.22, 0.36, bevel_w=0.012)
        if i % 2 == 0:
            _add_moss_cap(bm_l, px, gy + 0.1, 4.96 + h_fin + 0.02, 0.28, 0.38, thick=0.07, seed=30 + i)
    _add_box(bm_c, gx, gy + 0.1, 5.44, 2.10, 0.32, 0.18, bevel_w=0.010)
    _add_moss_cap(bm_l, gx - 0.6, gy - 0.75, 4.98, 1.35, 0.85, thick=0.10, seed=12)
    _add_moss_cap(bm_l, gx + 0.8, gy - 0.70, 4.98, 1.15, 0.75, thick=0.09, seed=13)

    _build_zone_meshes(scene, 'Central Gatehouse', parts, uv_name)


def _build_calendar_rear_gallery(scene, uv_name):
    """Colonnaded rain sanctuary directly behind the Calendar circle (x=1.8..5.9, y=11.6..15.8)."""
    parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.0), ('basalt', 1.8), ('carved_stone', 1.25),
        ('edge', 1.7), ('jade', 0.9), ('gold', 0.8), ('lichen', 2.0), ('deep', 1.5),
    )}
    bm_r, bm_b, bm_c, bm_e, bm_j, bm_g, bm_l, bm_d = [parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'jade', 'gold', 'lichen', 'deep'
    )]

    cx, cy = 4.05, 13.65
    # Raised dual terrace podium (clear of Gatehouse x<=2.6)
    _add_frustum(bm_r, cx, cy, 0.0, 0.95, 4.40, 3.80, 4.18, 3.55, bevel_w=0.022)
    _add_frustum(bm_b, cx, cy - 2.25, -0.02, 0.31, 2.16, 0.68, 2.02, 0.60, bevel_w=0.015)
    _add_frustum(bm_b, cx, cy - 1.92, 0.30, 0.63, 1.96, 0.60, 1.84, 0.54, bevel_w=0.015)
    _add_frustum(bm_b, cx, cy - 1.60, 0.62, 0.96, 1.80, 0.56, 1.70, 0.50, bevel_w=0.015)
    _add_box(bm_e, cx, cy, 0.99, 4.32, 3.72, 0.14, bevel_w=0.015)

    # 4 carved basalt square pillars forming a colonnade visible right through Calendar ring!
    pillar_xs = [cx - 1.55, cx - 0.55, cx + 0.55, cx + 1.55]
    for idx, px in enumerate(pillar_xs):
        _add_box(bm_b, px, cy - 1.15, 1.16, 0.56, 0.56, 0.28, bevel_w=0.015)
        _add_frustum(bm_r, px, cy - 1.15, 1.28, 3.42, 0.44, 0.44, 0.38, 0.38, bevel_w=0.014)
        _add_box(bm_c, px, cy - 1.36, 2.34, 0.32, 0.12, 1.38, bevel_w=0.008)
        _add_box(bm_e, px, cy - 1.15, 3.50, 0.58, 0.58, 0.22, bevel_w=0.014)
        # Rear matching pier row
        _add_frustum(bm_r, px, cy + 1.15, 1.03, 3.50, 0.48, 0.48, 0.42, 0.42, bevel_w=0.014)
        if idx in (0, 3):
            _add_moss_cap(bm_l, px, cy - 1.15, 3.60, 0.58, 0.58, thick=0.10, seed=40 + idx)

    # Upper cornice beam spanning colonnade + sunken sanctuary shrine back wall
    _add_box(bm_e, cx, cy - 1.15, 3.76, 4.10, 0.72, 0.32, bevel_w=0.018)
    _add_box(bm_c, cx, cy - 1.15, 4.06, 3.90, 0.64, 0.28, bevel_w=0.015)
    _add_box(bm_r, cx, cy + 1.15, 2.53, 4.00, 0.85, 3.09, bevel_w=0.020)
    _add_box(bm_d, cx, cy + 0.76, 2.25, 2.25, 0.16, 2.05)

    # Central Sacred Pedestal & Jade-Gold Rain Altar visible at heart of circle (embedded -0.03m into terrace)
    _add_frustum(bm_b, cx, cy - 0.15, 1.03, 1.88, 1.05, 0.88, 0.82, 0.72, bevel_w=0.015)
    _add_lathe(bm_b, cx, cy - 0.15, 1.88, [(0, 0.36), (0.14, 0.48), (0.36, 0.54), (0.42, 0.42)], segs=12)
    _add_box(bm_j, cx, cy - 0.58, 1.48, 0.34, 0.08, 0.34, bevel_w=0.008)
    _add_box(bm_g, cx, cy - 0.63, 1.48, 0.14, 0.05, 0.14, rot_y=math.pi / 4, bevel_w=0.005)
    _add_moss_cap(bm_l, cx - 0.95, cy - 1.15, 4.16, 1.55, 0.68, thick=0.12, seed=45)
    _add_moss_cap(bm_l, cx + 1.05, cy - 1.15, 4.16, 1.35, 0.64, thick=0.11, seed=46)

    _build_zone_meshes(scene, 'Calendar Gallery', parts, uv_name)


def _build_grand_aqueduct_viaduct(scene, uv_name):
    """Massive 3-span Mayan broken aqueduct viaduct across y=17.4..19.2, x=-10.2..+10.5."""
    parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.2), ('basalt', 2.0), ('carved_stone', 1.3),
        ('edge', 1.9), ('lichen', 2.2), ('jade', 0.9),
    )}
    bm_r, bm_b, bm_c, bm_e, bm_l, bm_j = [parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'jade'
    )]

    vy = 18.05
    pier_xs = [-9.4, -4.7, 0.0, 4.7, 9.4]
    spring_z = 3.55
    arch_r = 1.78

    for i, px in enumerate(pier_xs):
        # Stepped buttressed pier plinth
        _add_frustum(bm_b, px, vy, -0.05, 0.65, 1.68, 1.88, 1.50, 1.68, bevel_w=0.022)
        # Main masonry pier shaft rising gracefully above foreground sightlines
        h_top = 5.95 if i < 4 else 4.55  # Rightmost pier partially collapsed
        _add_frustum(bm_r, px, vy, 0.65, h_top, 1.28, 1.46, 1.14, 1.30, bevel_w=0.020)
        # Front buttress pilaster + cornice ring at springline
        _add_box(bm_e, px, vy - 0.72, 2.25, 0.62, 0.24, 2.55, bevel_w=0.014)
        _add_box(bm_e, px, vy, spring_z, 1.36, 1.52, 0.24, bevel_w=0.015)
        _add_box(bm_c, px, vy - 0.80, spring_z, 0.72, 0.14, 0.20, bevel_w=0.008)
        _add_moss_cap(bm_l, px + 0.1, vy - 0.56, 0.66, 1.18, 0.68, thick=0.11, seed=60 + i)

    # 3 intact voussoir arches (spans 0-1, 1-2, 2-3) + 1 half-collapsed jagged ruin span (3-4)
    for span in range(4):
        x_a, x_b = pier_xs[span], pier_xs[span + 1]
        mid_x = 0.5 * (x_a + x_b)
        max_k = 13 if span < 3 else 5  # Span 3 collapses midway!
        for k in range(max_k):
            t = (k + 0.5) / 13.0
            ang = math.pi * (1.0 - t)
            vx = mid_x + math.cos(ang) * arch_r
            vz = spring_z + math.sin(ang) * arch_r
            rot_y = -(ang - math.pi * 0.5)
            # Individual wedge voussoir block
            bm_v = bm_e if k % 2 == 0 else bm_r
            _add_box(bm_v, vx, vy, vz, 0.43, 1.24, 0.48, rot_y=rot_y, bevel_w=0.014)
        # Spandrel masonry infill above intact arches + overhanging aqueduct channel
        if span < 3:
            _add_box(bm_r, mid_x, vy, 5.45, 3.65, 1.16, 0.84, bevel_w=0.018)
            _add_box(bm_e, mid_x, vy, 5.96, 3.90, 1.40, 0.22, bevel_w=0.016)
            _add_box(bm_c, mid_x, vy - 0.68, 5.96, 3.45, 0.12, 0.20, bevel_w=0.008)
            # Upper water channel side parapets (twin parallel walls forming true trough silhouette)
            _add_box(bm_r, mid_x, vy - 0.48, 6.32, 3.85, 0.26, 0.52, bevel_w=0.014)
            _add_box(bm_r, mid_x, vy + 0.48, 6.32, 3.85, 0.26, 0.52, bevel_w=0.014)
            _add_moss_cap(bm_l, mid_x - 0.8, vy - 0.48, 6.55, 1.65, 0.38, thick=0.11, seed=70 + span)
            _add_moss_cap(bm_l, mid_x + 0.9, vy - 0.48, 6.55, 1.35, 0.36, thick=0.10, seed=80 + span)
            _add_box(bm_j, mid_x, vy - 0.64, 5.45, 0.26, 0.10, 0.26, rot_y=math.pi / 4, bevel_w=0.006)
        else:
            # Jagged broken rubble blocks along collapsed span edge
            _add_box(bm_r, x_a + 1.15, vy, 5.32, 1.35, 1.12, 0.65, rot_y=0.10, bevel_w=0.015)
            _add_box(bm_r, x_a + 1.85, vy - 0.12, 5.65, 0.75, 0.95, 0.42, rot_y=0.22, bevel_w=0.015)
            _add_moss_cap(bm_l, x_a + 1.45, vy, 5.88, 1.1, 0.95, thick=0.12, seed=88)

    # Fallen broken voussoir chunks resting below the collapsed 4th span
    for j, (fx, fy, fz, rz, ry) in enumerate([
        (6.4, 17.7, 0.28, 0.45, 0.18),
        (7.1, 18.4, 0.22, -0.60, -0.25),
        (7.9, 17.5, 0.32, 0.95, 0.14),
    ]):
        _add_box(bm_r, fx, fy, fz, 0.78, 0.62, 0.52, rot_z=rz, rot_y=ry, bevel_w=0.018)
        _add_moss_cap(bm_l, fx, fy, fz + 0.24, 0.65, 0.55, thick=0.08, seed=90 + j)

    _build_zone_meshes(scene, 'Grand Aqueduct', parts, uv_name)


def _build_grand_rear_temple_and_watchtower(scene, uv_name):
    """Towering stepped Mayan Pyramid behind the aqueduct (x=1.5..8.5, y=22.4) & Left Watchtower (x=-8.8, y=21.0)."""
    # Pyramid Group (East-Center skyline backdrop)
    p_parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.4), ('basalt', 2.1), ('carved_stone', 1.4),
        ('edge', 2.0), ('lichen', 2.4), ('jade', 1.0), ('deep', 1.6),
    )}
    bm_r, bm_b, bm_c, bm_e, bm_l, bm_j, bm_d = [p_parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'jade', 'deep'
    )]

    px, py = 1.85, 23.15
    # 4 massive battered pyramid tiers climbing proudly to z=7.25 so summit frames skyline cleanly
    tiers = [
        (0.0, 1.95, 10.6, 8.2, 9.4, 7.2),
        (1.95, 3.85, 8.8, 6.8, 7.7, 6.0),
        (3.85, 5.65, 7.1, 5.5, 6.1, 4.8),
        (5.65, 7.25, 5.4, 4.3, 4.7, 3.7),
    ]
    for idx, (z0, z1, sx0, sy0, sx1, sy1) in enumerate(tiers):
        _add_frustum(bm_r, px, py, z0, z1, sx0, sy0, sx1, sy1, bevel_w=0.025)
        _add_box(bm_e, px, py, z1 + 0.04, sx1 + 0.18, sy1 + 0.18, 0.16, bevel_w=0.015)
        _add_moss_cap(bm_l, px - sx1 * 0.30, py - sy1 * 0.45, z1 + 0.10, 1.85, 0.85, thick=0.12, seed=110 + idx)
        _add_moss_cap(bm_l, px + sx1 * 0.28, py - sy1 * 0.45, z1 + 0.10, 1.55, 0.75, thick=0.11, seed=120 + idx)

    # Grand central monumental staircase ascending all 4 tiers (non-overlapping distinct treads!)
    steps = 18
    for s in range(steps):
        t = s / (steps - 1)
        sy = (py - 4.35) + t * 2.35
        z_top = 0.38 + t * 6.85
        tread_h = 0.44 if s > 0 else 0.38
        step_w = 2.32 - (s % 2) * 0.006  # zero coplanar faces between stacked treads
        _add_box(bm_b, px, sy, z_top - tread_h * 0.5, step_w, 0.38, tread_h, bevel_w=0.012)
    # Sloped alfardas (staircase side balustrades cleanly outside tread width x = px +/- 1.42)
    for bx in (-1.42, 1.42):
        _add_box(bm_e, px + bx, py - 3.18, 3.80, 0.42, 2.95, 0.52, rot_x=math.radians(68.0), bevel_w=0.015)

    # Summit Sanctuary Temple & Roof Comb (z=7.25 .. 10.65)
    _add_frustum(bm_r, px, py, 7.25, 9.10, 3.85, 3.00, 3.55, 2.78, bevel_w=0.020)
    _add_box(bm_d, px, py - 1.40, 8.10, 1.38, 0.14, 1.52)
    _add_box(bm_e, px, py - 1.42, 8.92, 1.78, 0.28, 0.22, bevel_w=0.014)
    _add_box(bm_c, px, py - 0.05, 9.32, 3.85, 3.08, 0.40, bevel_w=0.018)
    _add_box(bm_j, px, py - 1.56, 9.32, 0.44, 0.16, 0.44, rot_y=math.pi / 4, bevel_w=0.008)
    for k in range(5):
        kx = px - 1.10 + k * 0.55
        kh = 1.28 if k == 2 else (1.02 if k in (1, 3) else 0.78)
        _add_frustum(bm_r, kx, py + 0.1, 9.52, 9.52 + kh, 0.36, 0.44, 0.24, 0.34, bevel_w=0.012)

    _build_zone_meshes(scene, 'Grand Pyramid', p_parts, uv_name)

    # West Watchtower / Observatory Bastion (x=-9.4, y=21.2) + East Upper Shrine Tower (x=9.8, y=19.8)
    w_parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.2), ('basalt', 2.0), ('carved_stone', 1.35), ('edge', 1.9), ('lichen', 2.2), ('deep', 1.5),
    )}
    wb_r, wb_b, wb_c, wb_e, wb_l, wb_d = [w_parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'deep'
    )]
    wx, wy = -9.2, 21.4
    _add_frustum(wb_r, wx, wy, 0.0, 2.35, 4.8, 4.4, 4.3, 3.9, bevel_w=0.025)
    _add_box(wb_e, wx, wy, 2.42, 4.5, 4.1, 0.20, bevel_w=0.016)
    _add_frustum(wb_r, wx, wy, 2.52, 5.25, 3.8, 3.4, 3.4, 3.0, bevel_w=0.022)
    _add_box(wb_c, wx, wy - 1.66, 3.85, 2.1, 0.22, 0.95, bevel_w=0.012)
    _add_box(wb_d, wx, wy - 1.65, 4.30, 0.85, 0.14, 1.35)
    _add_box(wb_e, wx, wy, 5.36, 3.65, 3.25, 0.24, bevel_w=0.016)
    for cx_off in (-1.2, -0.4, 0.4, 1.2):
        _add_frustum(wb_b, wx + cx_off, wy - 1.15, 5.48, 7.05, 0.42, 0.42, 0.36, 0.36, bevel_w=0.014)
    _add_box(wb_r, wx, wy - 0.4, 7.28, 3.20, 2.20, 0.45, bevel_w=0.016)
    _add_moss_cap(wb_l, wx - 0.8, wy - 1.1, 7.50, 1.4, 0.9, thick=0.12, seed=130)
    _add_moss_cap(wb_l, wx + 0.9, wy - 0.9, 5.48, 1.2, 0.8, thick=0.11, seed=131)

    _build_zone_meshes(scene, 'West Watchtower', w_parts, uv_name)

    # East High Sanctuary Pavillion balancing upper right skyline (x=10.6, y=16.5..20.5)
    e_parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.2), ('basalt', 2.0), ('carved_stone', 1.35), ('edge', 1.9), ('lichen', 2.2), ('jade', 0.9), ('deep', 1.5),
    )}
    eb_r, eb_b, eb_c, eb_e, eb_l, eb_j, eb_d = [e_parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'jade', 'deep'
    )]
    ex, ey = 10.8, 18.6
    _add_frustum(eb_r, ex, ey, 0.0, 2.85, 4.4, 4.0, 3.9, 3.5, bevel_w=0.025)
    _add_box(eb_e, ex, ey, 2.92, 4.1, 3.7, 0.22, bevel_w=0.016)
    _add_frustum(eb_r, ex, ey, 3.04, 5.65, 3.4, 3.0, 3.0, 2.6, bevel_w=0.022)
    _add_box(eb_c, ex, ey - 1.45, 4.35, 1.95, 0.20, 0.85, bevel_w=0.012)
    _add_box(eb_d, ex, ey - 1.38, 4.15, 0.95, 0.14, 1.55)
    _add_box(eb_e, ex, ey, 5.76, 3.30, 2.85, 0.24, bevel_w=0.016)
    _add_box(eb_j, ex, ey - 1.44, 5.76, 0.34, 0.14, 0.34, rot_y=math.pi / 4, bevel_w=0.006)
    for k in range(3):
        _add_frustum(eb_b, ex - 0.75 + k * 0.75, ey, 5.88, 6.95 + (0.35 if k == 1 else 0.0), 0.38, 0.42, 0.26, 0.32, bevel_w=0.012)
    _add_moss_cap(eb_l, ex - 0.7, ey - 0.9, 5.88, 1.3, 0.8, thick=0.11, seed=135)
    _build_zone_meshes(scene, 'East Sanctuary Tower', e_parts, uv_name)


def _build_side_wing_ruins_and_terrace_ornaments(scene, uv_name):
    """Rich details on existing Distant terraces + Left/Right rear ruined cloisters & aqueducts."""
    # Ornate cornices, pilasters, roof combs & rubble slopes separated per terrace
    terrace_specs = [
        ('CenterLeft', -2.80, 13.10, 5.70, 4.80, 0.98, 0.35),
        ('FarLeft', -7.90, 15.50, 5.80, 4.80, 1.14, 0.35),
        ('FarRight', 8.40, 14.80, 5.10, 4.80, 0.86, 0.35),
    ]
    for tidx, (tlabel, cx, cy, w0, d0, dz, shrink) in enumerate(terrace_specs):
        t_parts = {k: (bmesh.new(), t) for k, t in (
            ('rock', 2.0), ('basalt', 1.8), ('carved_stone', 1.25), ('edge', 1.7), ('lichen', 2.0), ('jade', 0.9),
        )}
        bm_r, bm_b, bm_c, bm_e, bm_l, bm_j = [t_parts[k][0] for k in (
            'rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'jade'
        )]
        for lvl in range(5):
            w = w0 - lvl * shrink * 2
            d = d0 - lvl * shrink * 2
            z_top = (lvl + 1) * dz
            # Overhanging carved stone cornice lip & corner pilasters (proud by 0.06m, zero coplanar)
            _add_box(bm_e, cx, cy - d * 0.5 - 0.04, z_top - 0.08, w + 0.14, 0.16, 0.14, bevel_w=0.012)
            _add_box(bm_c, cx, cy - d * 0.5 - 0.07, z_top - 0.25, w * 0.72, 0.08, 0.16, bevel_w=0.008)
            for sx in (-1.0, 1.0):
                _add_box(bm_b, cx + sx * (w * 0.5 - 0.14), cy - d * 0.5 - 0.06, z_top - dz * 0.52, 0.32, 0.16, dz * 0.88, bevel_w=0.012)
            if lvl in (1, 3):
                _add_moss_cap(bm_l, cx + ((-1) ** (tidx + lvl)) * (w * 0.24), cy - d * 0.44, z_top - 0.02, 1.25, 0.55, thick=0.11, seed=150 + tidx * 10 + lvl)
        # Crown roof comb & side wing finials
        top_z = 5.0 * dz
        for k in range(3):
            kx = cx - 0.9 + k * 0.9
            _add_frustum(bm_r, kx, cy, top_z - 0.04, top_z + (0.85 if k == 1 else 0.58), 0.45, 0.55, 0.32, 0.42, bevel_w=0.012)
        _add_box(bm_j, cx, cy - (d0 - 4 * shrink * 2) * 0.5 - 0.09, top_z - dz * 0.4, 0.28, 0.08, 0.28, rot_y=math.pi / 4, bevel_w=0.006)
        # Fallen rubble stones leaning at base front corners
        _add_box(bm_r, cx - w0 * 0.44, cy - d0 * 0.52, 0.26, 0.72, 0.58, 0.50, rot_z=0.35, rot_y=0.14, bevel_w=0.018)
        _add_box(bm_r, cx + w0 * 0.42, cy - d0 * 0.54, 0.22, 0.64, 0.52, 0.42, rot_z=-0.42, rot_y=-0.12, bevel_w=0.018)
        _build_zone_meshes(scene, f'Terrace {tlabel}', t_parts, uv_name)

    # Left Ruined Cloister Wing (x=-15.5..-10.4, y=10.5..16.2)
    l_parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.0), ('basalt', 1.8), ('carved_stone', 1.3), ('edge', 1.8), ('lichen', 2.0), ('deep', 1.5),
    )}
    lr, lb, lc, le, ll, ld = [l_parts[k][0] for k in ('rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'deep')]
    _add_frustum(lr, -13.3, 13.0, 0.0, 4.30, 4.2, 4.8, 3.7, 4.3, bevel_w=0.025)
    _add_box(le, -13.3, 13.0, 4.40, 3.95, 4.55, 0.24, bevel_w=0.016)
    _add_box(lc, -13.3, 10.80, 3.75, 3.25, 0.22, 0.52, bevel_w=0.012)
    _add_box(ld, -12.8, 10.74, 1.95, 1.35, 0.14, 2.35)
    for col_y in (10.7, 12.2, 13.8):
        _add_frustum(lb, -10.80, col_y, 0.0, 3.85, 0.58, 0.58, 0.48, 0.48, bevel_w=0.015)
    _add_box(le, -10.80, 12.25, 3.96, 0.70, 3.75, 0.26, bevel_w=0.014)
    _add_moss_cap(ll, -12.8, 11.4, 4.52, 2.2, 1.6, thick=0.14, seed=170)
    _build_zone_meshes(scene, 'Left Sanctuary', l_parts, uv_name)

    # Right Mossy Sanctuary Wing (x=10.4..15.8, y=10.5..17.0)
    r_parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 2.0), ('basalt', 1.8), ('carved_stone', 1.3), ('edge', 1.8), ('lichen', 2.0), ('deep', 1.5),
    )}
    rr, rb, rc, re, rl, rd = [r_parts[k][0] for k in ('rock', 'basalt', 'carved_stone', 'edge', 'lichen', 'deep')]
    _add_frustum(rr, 13.2, 12.6, 0.0, 3.85, 4.0, 4.4, 3.5, 3.9, bevel_w=0.025)
    _add_box(re, 13.2, 12.6, 3.96, 3.75, 4.15, 0.22, bevel_w=0.016)
    _add_box(rc, 13.2, 10.60, 3.30, 3.05, 0.22, 0.48, bevel_w=0.012)
    _add_box(rd, 12.6, 10.48, 1.75, 1.25, 0.14, 2.05)
    for col_y in (10.5, 12.0, 13.6):
        _add_frustum(rb, 10.72, col_y, 0.0, 3.45, 0.56, 0.56, 0.46, 0.46, bevel_w=0.015)
    _add_box(re, 10.72, 12.05, 3.56, 0.68, 3.65, 0.24, bevel_w=0.014)
    _add_moss_cap(rl, 12.7, 11.2, 4.08, 2.1, 1.5, thick=0.14, seed=180)
    _build_zone_meshes(scene, 'Right Sanctuary', r_parts, uv_name)


def _build_stelae_and_braziers(scene, uv_name):
    """Carved Mayan stelae, rain offerings & stone braziers framing mid-to-rear sightlines."""
    parts = {k: (bmesh.new(), t) for k, t in (
        ('rock', 1.6), ('basalt', 1.5), ('carved_stone', 1.0), ('edge', 1.5),
        ('jade', 0.8), ('gold', 0.7), ('clay', 1.0), ('lichen', 1.6),
    )}
    bm_r, bm_b, bm_c, bm_e, bm_j, bm_g, bm_cl, bm_l = [parts[k][0] for k in (
        'rock', 'basalt', 'carved_stone', 'edge', 'jade', 'gold', 'clay', 'lichen'
    )]

    # Stelae safely placed at y >= 7.2 (outside central arena y<2.7)
    stelae = [
        (-1.25, 8.15, 2.45, 0.26),   # Flanks Processional stair corridor
        (0.15, 8.75, 2.10, -0.18),   # Just inside central corridor approach
        (6.45, 8.60, 2.65, -0.32),   # Right of Calendar shrine
        (-6.80, 9.60, 2.75, 0.22),   # Left rear terrace base
        (7.25, 11.80, 2.35, -0.24),  # Further back right
    ]
    for idx, (sx, sy, h, rz) in enumerate(stelae):
        # Stepped stone altar base
        _add_frustum(bm_b, sx, sy, 0.0, 0.32, 0.92, 0.72, 0.82, 0.62, rot_z=rz, bevel_w=0.012)
        # Main carved stela monolith
        _add_frustum(bm_c, sx, sy, 0.32, 0.32 + h, 0.58, 0.34, 0.50, 0.30, rot_z=rz, bevel_w=0.014)
        # Side frame moldings + top crown stone
        _add_box(bm_e, sx, sy, 0.32 + h + 0.08, 0.56, 0.36, 0.16, rot_z=rz, bevel_w=0.012)
        # Front inlaid jade & gold ceremonial boss
        fwd = Vector((math.sin(rz), -math.cos(rz), 0.0)) * 0.175
        _add_box(bm_j, sx + fwd.x, sy + fwd.y, 0.32 + h * 0.65, 0.18, 0.06, 0.18, rot_z=rz, bevel_w=0.006)
        _add_box(bm_g, sx + fwd.x * 1.08, sy + fwd.y * 1.08, 0.32 + h * 0.65, 0.08, 0.04, 0.08, rot_z=rz, bevel_w=0.004)
        _add_moss_cap(bm_l, sx, sy, 0.32 + h + 0.18, 0.45, 0.32, thick=0.07, seed=200 + idx)

    # Ceremonial carved basalt braziers & terracotta offering jars along y = 7.4 .. 10.6
    brazier_spots = [
        (-0.35, 7.65, 0.95),
        (2.15, 9.65, 0.90),
        (-3.95, 7.75, 1.05),
        (5.65, 9.85, 0.95),
    ]
    for idx, (bx, by, bh) in enumerate(brazier_spots):
        _add_lathe(bm_b, bx, by, 0.0, [
            (0.0, 0.34), (0.14, 0.30), (0.28, 0.19),
            (bh * 0.72, 0.21), (bh * 0.85, 0.38), (bh, 0.42), (bh + 0.04, 0.34)
        ], segs=12)
        _add_box(bm_g, bx, by - 0.22, bh * 0.48, 0.10, 0.08, 0.10, bevel_w=0.005)
        # Paired terracotta jars nearby
        px = bx + (0.58 if idx % 2 == 0 else -0.58)
        _add_lathe(bm_cl, px, by + 0.18, 0.0, [
            (0.0, 0.10), (0.12, 0.19), (0.26, 0.20), (0.36, 0.11), (0.42, 0.14)
        ], segs=12)

    _build_zone_meshes(scene, 'Rear Stelae Props', parts, uv_name)


def _add_buttress_tree(bm_bark, bm_canopy, bm_moss, bx, by, height, seed=1):
    """Realistic rainforest Ceiba tree with radiating buttress roots, forked limbs & layered canopy."""
    rng = random.Random(seed)
    trunk_top = Vector((bx + rng.uniform(-0.35, 0.35), by + rng.uniform(-0.25, 0.25), height * 0.68))
    _add_cyl(bm_bark, Vector((bx, by, -0.15)), trunk_top, 0.44, 0.24, segs=10)

    # 5 radiating buttress roots anchoring into masonry/soil
    for r in range(5):
        ang = (r / 5.0) * TAU + rng.uniform(-0.25, 0.25)
        foot = Vector((bx + math.cos(ang) * 1.15, by + math.sin(ang) * 1.15, -0.08))
        knee = Vector((bx + math.cos(ang) * 0.45, by + math.sin(ang) * 0.45, 0.72 + rng.uniform(0.0, 0.4)))
        top_j = Vector((bx + math.cos(ang) * 0.22, by + math.sin(ang) * 0.22, 1.65))
        _add_cyl(bm_bark, foot, knee, 0.16, 0.14, segs=7)
        _add_cyl(bm_bark, knee, top_j, 0.14, 0.09, segs=7)
    _add_moss_cap(bm_moss, bx, by, 0.15, 1.35, 1.35, thick=0.14, seed=seed)

    # Forked upper branches & lush overlapping frond-cluster canopy tiers
    num_branches = 4
    for b in range(num_branches):
        ang = (b / num_branches) * TAU + rng.uniform(-0.35, 0.35)
        b_len = rng.uniform(1.8, 2.8)
        b_rise = rng.uniform(1.1, 2.0)
        b_end = trunk_top + Vector((math.cos(ang) * b_len, math.sin(ang) * b_len, b_rise))
        _add_cyl(bm_bark, trunk_top, b_end, 0.20, 0.08, segs=7)

        # Tiered radiating leaf sprays at branch tip + mid-branch (radially offset outwards so roots never overlap)
        for s_idx, (spray_origin, n_fronds, f_len) in enumerate(
            ((b_end, 7, 1.55), (trunk_top.lerp(b_end, 0.62), 5, 1.25))
        ):
            for f in range(n_fronds):
                fang = (f / n_fronds) * TAU + rng.uniform(-0.14, 0.14)
                fdir = Vector((math.cos(fang), math.sin(fang), rng.uniform(0.02, 0.28) + 0.05 * (f % 3))).normalized()
                radial_xy = Vector((math.cos(fang), math.sin(fang), 0.0)) * 0.16
                foff = spray_origin + radial_xy + Vector((0, 0, 0.08 * (f % 3) + 0.14 * s_idx))
                _add_leaf_blade(
                    bm_canopy, foff, fdir,
                    length=f_len * rng.uniform(0.85, 1.15),
                    width=0.36, droop=0.22, arch=0.18, segs=5,
                    roll=(((f % 2) * 2 - 1) * (0.14 + 0.05 * (b % 3)))
                )


def _add_vine_curtain(bm_bark, bm_leaf, p_a, p_b, count=6, length_range=(1.4, 2.9), seed=1):
    """Hanging liana curtains with small broadleaves along masonry arches & aqueduct soffits."""
    rng = random.Random(seed)
    pa, pb = Vector(p_a), Vector(p_b)
    for i in range(count):
        t = (i + 0.5) / count + rng.uniform(-0.04, 0.04)
        top = pa.lerp(pb, max(0.0, min(1.0, t)))
        v_len = rng.uniform(*length_range)
        segs = 5
        prev = top
        for s in range(1, segs + 1):
            st = s / segs
            sway_x = math.sin(st * math.pi + i) * 0.08
            sway_y = math.cos(st * math.pi * 1.3 + i) * 0.06
            cur = Vector((top.x + sway_x, top.y + sway_y, top.z - v_len * st))
            _add_cyl(bm_bark, prev, cur, 0.022 * (1.0 - 0.35 * st), 0.015 * (1.0 - 0.35 * st), segs=5)
            if s % 2 == 1:
                fang = rng.uniform(0.0, TAU)
                ldir = Vector((math.cos(fang), math.sin(fang), -0.35)).normalized()
                _add_leaf_blade(
                    bm_leaf, cur + ldir * 0.04,
                    ldir,
                    length=0.26, width=0.11, droop=0.18, arch=0.05, segs=3
                )
            prev = cur


def _add_fern_cluster(bm_fern, cx, cy, cz, scale=1.0, seed=1):
    rng = random.Random(seed)
    fronds = 7
    for i in range(fronds):
        ang = (i / fronds) * TAU + rng.uniform(-0.22, 0.22)
        elev = rng.uniform(0.28, 0.62)
        d = Vector((math.cos(ang), math.sin(ang), elev)).normalized()
        _add_leaf_blade(
            bm_fern, Vector((cx, cy, cz + 0.04)), d,
            length=0.88 * scale * rng.uniform(0.85, 1.15),
            width=0.22 * scale, droop=0.30, arch=0.24, segs=5
        )


def _build_rear_rainforest_ecology(scene, uv_name):
    """Buttress rainforest trees, dense fern undergrowth & hanging lianas across y=7.6..23.5."""
    # Split Left/Center/Right zones so no AABB ever crosses protected arena
    zones = {
        'Rear Left Jungle': [
            (-11.8, 11.4, 6.8, 301),
            (-14.6, 14.8, 7.6, 302),
            (-6.9, 12.2, 6.2, 303),
            (-10.5, 17.8, 7.8, 304),
            (-4.2, 16.4, 6.9, 305),
        ],
        'Rear Center Jungle': [
            (0.9, 15.8, 6.4, 311),
            (6.2, 16.0, 6.8, 312),
            (-1.2, 20.6, 8.1, 313),
            (7.4, 20.4, 7.9, 314),
        ],
        'Rear Right Jungle': [
            (11.4, 11.6, 7.0, 321),
            (14.5, 15.0, 7.5, 322),
            (10.8, 17.6, 7.6, 323),
        ],
    }
    for zname, tree_list in zones.items():
        bm_bark, bm_canopy, bm_moss = bmesh.new(), bmesh.new(), bmesh.new()
        for bx, by, h, sd in tree_list:
            _add_buttress_tree(bm_bark, bm_canopy, bm_moss, bx, by, h, seed=sd)
        _build_zone_meshes(scene, zname, {
            'bark': (bm_bark, 1.4),
            'canopy': (bm_canopy, 1.2),
            'lichen': (bm_moss, 1.8),
        }, uv_name)

    # Hanging liana curtains on gatehouse, Calendar colonnade & aqueduct arches
    bm_vb, bm_vl, bm_fern = bmesh.new(), bmesh.new(), bmesh.new()
    vine_spans = [
        ((-0.25, 10.55, 4.10), (1.95, 10.55, 4.10), 7, (1.1, 2.3), 401),   # Gatehouse lintel
        ((1.85, 12.25, 3.62), (5.85, 12.25, 3.62), 9, (1.0, 2.2), 402),    # Calendar Gallery front beam
        ((-9.0, 17.45, 5.55), (-5.0, 17.45, 5.55), 8, (1.6, 3.0), 403),    # Aqueduct span 1
        ((-4.2, 17.45, 5.55), (-0.4, 17.45, 5.55), 9, (1.7, 3.2), 404),    # Aqueduct span 2
        ((0.4, 17.45, 5.55), (4.2, 17.45, 5.55), 8, (1.5, 2.9), 405),      # Aqueduct span 3
        ((4.8, 17.45, 5.30), (6.8, 17.45, 5.30), 5, (1.4, 2.5), 406),      # Aqueduct broken span 4
        ((-12.2, 10.65, 4.20), (-10.8, 10.65, 3.85), 5, (1.3, 2.4), 407),  # Left wing
        ((10.8, 10.45, 3.45), (12.8, 10.45, 3.85), 5, (1.2, 2.3), 408),    # Right wing
    ]
    for pa, pb, cnt, lr, sd in vine_spans:
        _add_vine_curtain(bm_vb, bm_vl, pa, pb, count=cnt, length_range=lr, seed=sd)

    # Dense rainforest undergrowth ferns & broadleaves along rear terraces, paths & walls (all y >= 7.0)
    fern_spots = [
        (-1.75, 7.6, 0.0, 1.05), (-0.75, 8.9, 0.0, 0.95), (1.35, 9.2, 0.0, 1.15),
        (2.45, 7.9, 0.0, 0.90), (5.85, 8.2, 0.0, 1.05), (6.85, 10.4, 0.0, 1.25),
        (-6.20, 8.6, 0.0, 1.20), (-8.40, 10.2, 0.0, 1.30), (-3.80, 10.4, 0.0, 1.10),
        (0.85, 10.2, 0.54, 0.85), (3.85, 11.4, 0.98, 0.90), (-11.4, 10.6, 0.0, 1.35),
        (11.2, 10.5, 0.0, 1.30), (-5.2, 17.4, 0.0, 1.35), (0.3, 17.4, 0.0, 1.25),
        (5.2, 17.4, 0.0, 1.40), (7.6, 17.2, 0.0, 1.30),
    ]
    for idx, (fx, fy, fz, sc) in enumerate(fern_spots):
        _add_fern_cluster(bm_fern, fx, fy, fz, scale=sc, seed=500 + idx)

    _build_zone_meshes(scene, 'Rear Vines & Ferns', {
        'bark': (bm_vb, 1.1),
        'leaf': (bm_vl, 0.8),
        'fern': (bm_fern, 0.9),
    }, uv_name)


def _reproject_ruins_uvs(scene, uv_name):
    """Apply uniform box-projected UVs to existing & new masonry while preserving floor & wind plants."""
    skip_prefixes = (
        'ENV_',
        'Observatory of Rain | Wind |',
        'Observatory of Rain | Continuous level courtyard',
        'Observatory of Rain | Rain collector water',
        'Observatory of Rain | Rain lozenge',
        'Observatory of Rain | Incised calendar marker',
        REMASTER_PREFIX,
    )
    for obj in scene.objects:
        if obj.type != 'MESH' or obj.name.startswith(skip_prefixes):
            continue
        _box_project_uv(obj, tile_size=1.9, uv_name=uv_name)


def remaster_ruins_rear(scene, tex_dir):
    """Upgrade Ancient Ruins PBR materials and build rich multi-layered rear architecture & jungle."""
    import ruins_pbr
    uv_name = _uv_name(scene)

    _remove_obsolete_rear_placeholders(scene)
    ruins_pbr.upgrade_ruins_materials(scene, tex_dir)
    _build_corbelled_sanctuary_gate(scene, uv_name)
    _build_calendar_rear_gallery(scene, uv_name)
    _build_grand_aqueduct_viaduct(scene, uv_name)
    _build_grand_rear_temple_and_watchtower(scene, uv_name)
    _build_side_wing_ruins_and_terrace_ornaments(scene, uv_name)
    _build_stelae_and_braziers(scene, uv_name)
    _build_rear_rainforest_ecology(scene, uv_name)
    _reproject_ruins_uvs(scene, uv_name)
