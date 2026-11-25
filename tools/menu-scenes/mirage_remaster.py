"""Full architectural, prop & life-scene remaster for PixelEngine's Mirage (Indigo Caravanserai).

Follows `blender-hardsurface-modeling`, `blender-pbr-materials`, `blender-texturing`,
and `blender-asset-qa` principles:
  - Per-zone/per-prop objects so AABBs never cross the central protected arena
    (x in [-6.3, 6.3], y in [-6.1, 2.7], z in [0, 6.5]).
  - Zero coplanar faces / z-fighting, outward consistent normals, clean deg/double cleanup.
  - World-space box-projected UVs paired with 1K/1.5K seamless BaseColor + ORM + Normal maps.
"""
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix

TAU = math.tau


def _mat(scene, short_key):
    prefix = scene.get('material_prefix', 'Indigo Caravanserai | ')
    m = bpy.data.materials.get(prefix + short_key)
    if m:
        return m
    for mat in bpy.data.materials:
        if mat.name.endswith(' | ' + short_key) or mat.name == short_key:
            return mat
    return bpy.data.materials.get(prefix + 'stucco')


def _uv_name(scene):
    for obj in scene.objects:
        if obj.type == 'MESH' and obj.data.uv_layers:
            return obj.data.uv_layers[0].name
    return 'Surface'


def _box_project_uv(obj, tile_size=1.9, uv_name='Surface'):
    """Assign distortion-free world-space box-projected UVs preserving seamless 1K textures."""
    me = obj.data
    if not me.polygons:
        return
    uv_layer = me.uv_layers.get(uv_name) or me.uv_layers.new(name=uv_name)
    mw = obj.matrix_world
    rot_m = mw.to_3x3()
    is_floor = obj.name.removeprefix('Indigo Caravanserai | ').startswith('Continuous level courtyard')
    for poly in me.polygons:
        wn = (rot_m @ poly.normal).normalized()
        ax, ay, az = abs(wn.x), abs(wn.y), abs(wn.z)
        for li in poly.loop_indices:
            vi = me.loops[li].vertex_index
            wp = mw @ me.vertices[vi].co
            if is_floor:
                uv_layer.data[li].uv = ((wp.x + 12.0) / 24.0, (wp.y + 9.0) / 24.0)
            elif az >= ax and az >= ay:
                uv_layer.data[li].uv = (wp.x / tile_size, wp.y / tile_size)
            elif ay >= ax:
                uv_layer.data[li].uv = (wp.x / tile_size, wp.z / tile_size)
            else:
                uv_layer.data[li].uv = (wp.y / tile_size, wp.z / tile_size)


def _new_mesh_obj(scene, name, bm, mat, tile_size=1.9, uv_name='Surface', sharp_deg=44.0):
    if len(bm.verts) == 0 or len(bm.faces) == 0:
        bm.free()
        return None
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=list(bm.edges))
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-4)
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


def _add_box_to_bm(bm, cx, cy, cz, sx, sy, sz, rot_z=0.0, rot_y=0.0, bevel_w=0.0):
    """Create a box with outward normals and optional isolated edge bevel."""
    sub = bmesh.new()
    bmesh.ops.create_cube(sub, size=1.0)
    mat = Matrix.Translation((cx, cy, cz)) @ Matrix.Rotation(rot_z, 4, 'Z') @ Matrix.Rotation(rot_y, 4, 'Y')
    for v in sub.verts:
        v.co = mat @ Vector((v.co.x * sx, v.co.y * sy, v.co.z * sz))
    bw = min(bevel_w, min(sx, sy, sz) * 0.18) if bevel_w > 0.0005 else 0.0
    if bw > 0.0005:
        bmesh.ops.bevel(sub, geom=list(sub.edges), offset=bw, segments=2,
                        profile=0.5, affect='EDGES', clamp_overlap=True)
    bmesh.ops.recalc_face_normals(sub, faces=list(sub.faces))
    me = bpy.data.meshes.new('_tmp_box')
    sub.to_mesh(me)
    sub.free()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)


def _add_cyl_to_bm(bm, p0, p1, r0, r1, segs=12):
    """Create a manifold cylinder from p0 to p1 with guaranteed outward normals."""
    a, b = Vector(p0), Vector(p1)
    d = b - a
    if d.length < 1e-5:
        return
    z_ax = d.normalized()
    up = Vector((0, 0, 1)) if abs(z_ax.z) < 0.92 else Vector((1, 0, 0))
    x_ax = up.cross(z_ax).normalized()
    y_ax = z_ax.cross(x_ax).normalized()
    ring0 = [bm.verts.new(a + x_ax * (r0 * math.cos(TAU * i / segs)) + y_ax * (r0 * math.sin(TAU * i / segs))) for i in range(segs)]
    ring1 = [bm.verts.new(b + x_ax * (r1 * math.cos(TAU * i / segs)) + y_ax * (r1 * math.sin(TAU * i / segs))) for i in range(segs)]
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((ring0[i], ring0[j], ring1[j], ring1[i]))
    bm.faces.new(list(reversed(ring0)))
    bm.faces.new(ring1)


def _add_lathe_to_bm(bm, cx, cy, cz, profile, segs=16, scale_x=1.0, scale_y=1.0):
    """Create a manifold surface-of-revolution centered at (cx,cy,cz) with outward normals."""
    rings = []
    # Deduplicate consecutive identical heights to prevent zero-area bands
    clean_prof = []
    for h_off, rad in profile:
        r_safe = max(0.008, float(rad))
        if clean_prof and abs(h_off - clean_prof[-1][0]) < 1e-4:
            continue
        clean_prof.append((float(h_off), r_safe))
    if len(clean_prof) < 2:
        return
    for h_off, rad in clean_prof:
        ring = [bm.verts.new((cx + rad * scale_x * math.cos(TAU * i / segs),
                              cy + rad * scale_y * math.sin(TAU * i / segs),
                              cz + h_off)) for i in range(segs)]
        rings.append(ring)
    for r_i in range(len(rings) - 1):
        r0, r1 = rings[r_i], rings[r_i + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])


# ---------------------------------------------------------------------------
# 1. Per-Wing Architectural Stonework, Exposed Bricks, Joist Beams & Tiles
# ---------------------------------------------------------------------------

def _add_wing_masonry_and_eaves(scene, uv_name):
    """Per-wing masonry plinths, exposed mudbrick relief, rafters, barrel roof tiles & niches."""
    wings = [
        # tag, (x, y, w, d, h, wall_mat) -- all have y_min >= 2.75 or x_max <= -6.4
        ('L1', -8.5, 7.4, 4.4, 3.8, 4.35, 'rose'),
        ('R1', -3.7, 9.05, 4.3, 3.8, 3.45, 'stucco'),
        ('R2', 4.32, 7.7, 5.0, 2.45, 3.73, 'ivory'),
        ('R3', 9.0, 7.2, 4.22, 3.2, 4.80, 'stucco'),
        ('LC', -10.96, 4.61, 1.95, 2.06, 4.70, 'rose'),
    ]
    rng = random.Random(2026)

    for tag, wx, wy, ww, wd, wh, wkey in wings:
        bm_plinth = bmesh.new()
        bm_bricks = bmesh.new()
        bm_timber = bmesh.new()
        bm_tiles = bmesh.new()
        bm_deep = bmesh.new()
        bm_brass = bmesh.new()
        bm_clay = bmesh.new()

        front_y = wy - wd * 0.5
        # Plinth embeds slightly (-0.02..+0.60) and wraps walls cleanly (+0.08m outward)
        # Use slight unique height/depth offsets on LC so overlapping wing corners have zero coplanar z-fighting
        plinth_z_adj = 0.012 if tag == 'LC' else 0.0
        plinth_pad = 0.16 if tag == 'LC' else 0.14
        _add_box_to_bm(bm_plinth, wx, wy, 0.29 + plinth_z_adj * 0.5, ww + plinth_pad, wd + plinth_pad, 0.62 + plinth_z_adj, bevel_w=0.012)
        _add_box_to_bm(bm_plinth, wx, wy, 0.64 + plinth_z_adj, ww + plinth_pad + 0.06, wd + plinth_pad + 0.06, 0.09, bevel_w=0.012)
        if wh > 4.0 and tag != 'LC':
            _add_box_to_bm(bm_plinth, wx, front_y - 0.055, 2.55, ww + 0.08, 0.14, 0.13, bevel_w=0.010)

        # Exposed protruding adobe brick patches on front wall (sunken 0.015 into wall, protruding 0.045 out)
        # Skip arched loggia (R2) and wall niche centers on R3 (x=7.45, 10.12)
        for c_i in range(2):
            if tag == 'R3':
                bx_c = 8.75 if c_i == 0 else 8.80
                bz_c = 1.65 if c_i == 0 else 3.95
            else:
                bx_c = wx + (-0.30 if c_i == 0 else 0.28) * ww
                bz_c = 1.25 + 1.55 * (c_i % 2)
            if abs(wx - 4.32) < 0.2 and bz_c < 3.0:
                continue
            for r in range(3):
                for b in range(3):
                    if rng.random() < 0.30:
                        continue
                    ox = bx_c + (b - 1) * 0.27 + (r % 2) * 0.12
                    oz = bz_c + r * 0.125
                    _add_box_to_bm(bm_bricks, ox, front_y - 0.02, oz, 0.23, 0.08, 0.095, bevel_w=0.008)

        # Cedar joist beams & barrel eave tiles seated into cornice
        beam_cnt = max(4, int(ww / 0.65))
        for j in range(beam_cnt):
            frac = (j + 0.5) / beam_cnt - 0.5
            jx = wx + frac * (ww - 0.35)
            _add_box_to_bm(bm_timber, jx, front_y - 0.15, wh - 0.30, 0.11, 0.44, 0.12, bevel_w=0.010)
        _add_box_to_bm(bm_timber, wx + ww * 0.34, front_y - 0.22, wh - 0.06, 0.14, 0.56, 0.10, bevel_w=0.008)

        tile_cnt = max(6, int(ww / 0.28))
        for t in range(tile_cnt):
            tx = wx - ww * 0.48 + (t + 0.5) * (ww * 0.96 / tile_cnt)
            _add_cyl_to_bm(bm_tiles, (tx, front_y + 0.16, wh + 0.04), (tx, front_y - 0.17, wh - 0.03), 0.092, 0.085, segs=8)

        # Wind-tower crenellations & corner quoins embedded cleanly into masonry
        if tag == 'R1':
            for s in range(4):
                mx = -5.52 + s * 0.53
                _add_box_to_bm(bm_plinth, mx, 6.77, 5.52, 0.26, 0.24, 0.30, bevel_w=0.010)
                _add_box_to_bm(bm_plinth, mx, 8.85, 5.52, 0.26, 0.24, 0.30, bevel_w=0.010)
            for cx_q in (-5.66, -3.78):
                for tier in range(9):
                    qz = 0.88 + tier * 0.43
                    w_q = 0.26 if tier % 2 == 0 else 0.18
                    _add_box_to_bm(bm_plinth, cx_q, 6.75, qz, w_q, 0.16, 0.24, bevel_w=0.008)

        # Blind arched niches recessed smoothly into left/right wings (away from windows)
        niche_list = {
            'L1': [(-7.42, 5.49, 1.38, 0.62, 1.05)],
            'R3': [(7.45, 5.59, 1.68, 0.66, 1.08), (10.12, 5.59, 1.68, 0.66, 1.08)],
        }.get(tag, [])
        for nx, ny, nz, nw, nh in niche_list:
            # Distinct depth offsets on sill/lintel/jambs eliminate any coplanar overlap
            _add_box_to_bm(bm_deep, nx, ny - 0.015, nz, nw, 0.06, nh)
            for side in (-1, 1):
                _add_box_to_bm(bm_plinth, nx + side * (nw * 0.5 + 0.055), ny - 0.045, nz, 0.12, 0.15, nh + 0.02, bevel_w=0.010)
            _add_box_to_bm(bm_plinth, nx, ny - 0.085, nz - nh * 0.5 - 0.065, nw + 0.30, 0.22, 0.12, bevel_w=0.010)
            _add_box_to_bm(bm_plinth, nx, ny - 0.065, nz + nh * 0.5 + 0.075, nw + 0.28, 0.18, 0.14, bevel_w=0.010)
            _add_lathe_to_bm(bm_clay, nx - 0.11, ny - 0.08, nz - nh * 0.5 + 0.01,
                             [(0, 0.042), (0.08, 0.088), (0.19, 0.076), (0.24, 0.040), (0.27, 0.052)], segs=12)
            _add_lathe_to_bm(bm_brass, nx + 0.13, ny - 0.08, nz - nh * 0.5 + 0.01,
                             [(0, 0.050), (0.04, 0.062), (0.10, 0.025), (0.16, 0.048)], segs=10)

        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} plinth', bm_plinth, _mat(scene, 'ivory'), 1.8, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} bricks', bm_bricks, _mat(scene, 'clay' if wkey == 'rose' else 'stucco'), 1.2, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} timber', bm_timber, _mat(scene, 'bark'), 1.4, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} tiles', bm_tiles, _mat(scene, 'clay'), 1.2, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} deep', bm_deep, _mat(scene, 'deep'), 1.5, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} clay', bm_clay, _mat(scene, 'clay'), 1.0, uv_name)
        _new_mesh_obj(scene, f'Indigo Caravanserai | Masonry {tag} brass', bm_brass, _mat(scene, 'brass'), 1.0, uv_name)


# ---------------------------------------------------------------------------
# 2. Expanded Flanking Streets, Skyline Extensions & Bunting Overpasses
# ---------------------------------------------------------------------------

def _expand_flanking_architecture(scene, uv_name):
    """Expand Left Potters' Annex (x<-6.5), Right Spice Exchange (x>6.5) & Rear Overpass (y>=2.8)."""
    # Keep Left, Right, and Rear zones in separate objects so no AABB crosses [-6.3, 6.3]x[-6.1, 2.7]

    # ---- A. LEFT FLANKING POTTERS' ANNEX (x in [-15.8, -11.6], cleanly abutting corner return) ----
    l_stucco, l_rose, l_ivory, l_bark, l_ind, l_tile, l_cloth, l_deep, l_gold, l_s_ind, l_s_ivo = [bmesh.new() for _ in range(11)]
    _add_box_to_bm(l_stucco, -13.85, 5.8, 2.70, 3.85, 4.2, 5.40, bevel_w=0.025)
    _add_box_to_bm(l_ivory, -13.85, 5.8, 0.30, 4.02, 4.36, 0.64, bevel_w=0.015)
    _add_box_to_bm(l_ivory, -13.85, 5.8, 5.42, 4.10, 4.44, 0.18, bevel_w=0.012)
    _add_box_to_bm(l_rose, -13.85, 5.8, 5.65, 3.92, 4.26, 0.30, bevel_w=0.012)
    for j in range(6):
        _add_box_to_bm(l_bark, -15.2 + j * 0.56, 3.55, 5.15, 0.11, 0.48, 0.13, bevel_w=0.008)
    # Recessed wooden shuttered upper window (distinct depth offsets, zero coplanar)
    _add_box_to_bm(l_deep, -13.6, 3.69, 3.65, 1.05, 0.08, 1.35)
    _add_box_to_bm(l_ivory, -13.6, 3.60, 2.94, 1.30, 0.24, 0.12, bevel_w=0.010)
    _add_box_to_bm(l_ind, -14.28, 3.60, 3.65, 0.36, 0.08, 1.30, bevel_w=0.008)
    _add_box_to_bm(l_ind, -12.92, 3.60, 3.65, 0.36, 0.08, 1.30, bevel_w=0.008)
    # Flush rear corridor archway connecting Corner Return (x=-10.96, y=4.61) to Annex (x=-13.85, y=5.8)
    _add_box_to_bm(l_rose, -11.55, 4.90, 4.10, 1.35, 1.85, 1.25, bevel_w=0.020)
    _add_box_to_bm(l_ivory, -11.55, 4.90, 4.76, 1.42, 1.94, 0.14, bevel_w=0.010)
    # Masonry stepped terrace & wooden pergola at left-front (x=-13.2..-11.4, y=1.6..3.6)
    for i in range(5):
        sx = -12.15 + i * 0.20
        _add_box_to_bm(l_ivory, sx, 2.25, 0.12 + i * 0.14, 0.24, 0.88, 0.24 + i * 0.28, bevel_w=0.010)
    _add_box_to_bm(l_bark, -12.40, 2.55, 2.54, 1.90, 1.80, 0.10, bevel_w=0.008)
    for px in (-13.22, -11.58):
        _add_box_to_bm(l_bark, px, 1.74, 1.24, 0.13, 0.13, 2.52, bevel_w=0.008)
    for k in range(6):
        bm_k = l_s_ind if k % 2 == 0 else l_s_ivo
        _add_box_to_bm(bm_k, -13.12 + k * 0.29, 2.48, 2.64, 0.24, 1.72, 0.04, bevel_w=0.005)
    # Rear-left mid-ground courtyard house + dome behind left wing
    _add_box_to_bm(l_rose, -13.4, 11.2, 3.1, 4.2, 3.8, 6.2, bevel_w=0.025)
    _add_box_to_bm(l_ivory, -13.4, 11.2, 6.26, 4.38, 3.98, 0.20, bevel_w=0.012)
    _add_lathe_to_bm(l_tile, -13.4, 11.2, 6.32,
                     [(0, 1.32), (0.28, 1.26), (0.72, 0.92), (1.08, 0.42), (1.25, 0.05)], segs=16)
    _add_lathe_to_bm(l_gold, -13.4, 11.2, 7.55, [(0, 0.09), (0.16, 0.14), (0.48, 0.02)], segs=10)

    for tag, bm, key in (
        ('stucco', l_stucco, 'stucco'), ('rose', l_rose, 'rose'), ('ivory', l_ivory, 'ivory'),
        ('timber', l_bark, 'bark'), ('indigo', l_ind, 'indigo'), ('tile', l_tile, 'tile'),
        ('cloth', l_cloth, 'cloth'), ('deep', l_deep, 'deep'), ('gold', l_gold, 'gold'),
        ('sail_ind', l_s_ind, 'sail_indigo'), ('sail_ivo', l_s_ivo, 'sail_ivory'),
    ):
        _new_mesh_obj(scene, f'Indigo Caravanserai | Expanded Left {tag}', bm, _mat(scene, key), 1.9, uv_name)

    # ---- B. RIGHT FLANKING SPICE EXCHANGE & MINARET (x in [11.0, 16.0], clear of x=6.3) ----
    r_stucco, r_rose, r_ivory, r_bark, r_ind, r_tile, r_deep, r_gold = [bmesh.new() for _ in range(8)]
    _add_box_to_bm(r_rose, 13.3, 5.9, 2.55, 3.8, 4.6, 5.10, bevel_w=0.025)
    _add_box_to_bm(r_ivory, 13.3, 5.9, 0.30, 3.96, 4.76, 0.64, bevel_w=0.015)
    _add_box_to_bm(r_ivory, 13.3, 5.9, 5.12, 4.04, 4.84, 0.18, bevel_w=0.012)
    _add_box_to_bm(r_stucco, 13.3, 5.9, 5.35, 3.86, 4.66, 0.30, bevel_w=0.012)
    for j in range(6):
        _add_box_to_bm(r_bark, 11.8 + j * 0.58, 3.45, 4.86, 0.11, 0.48, 0.13, bevel_w=0.008)
    # Recessed wooden mashrabiya bay window (distinct front depth z/y offsets)
    _add_box_to_bm(r_bark, 12.8, 3.34, 3.35, 1.46, 0.52, 1.50, bevel_w=0.012)
    _add_box_to_bm(r_deep, 12.8, 3.06, 3.35, 1.20, 0.06, 1.22)
    for sp in range(5):
        _add_box_to_bm(r_ivory, 12.32 + sp * 0.24, 3.00, 3.35, 0.05, 0.04, 1.18, bevel_w=0.005)
    for cx_b in (12.16, 13.44):
        _add_box_to_bm(r_bark, cx_b, 3.42, 2.45, 0.13, 0.44, 0.32, bevel_w=0.008)
    # Ground-floor arched doorway recess on right annex (door proud of recess)
    _add_box_to_bm(r_deep, 12.7, 3.57, 1.15, 1.15, 0.08, 2.15)
    _add_box_to_bm(r_ind, 12.7, 3.50, 1.12, 0.98, 0.08, 2.02, bevel_w=0.008)
    _add_box_to_bm(r_ivory, 12.7, 3.48, 2.30, 1.48, 0.24, 0.22, bevel_w=0.012)
    # Octagonal wind-tower / minaret landmark behind right wing
    mx, my = 13.4, 10.6
    _add_lathe_to_bm(r_stucco, mx, my, -0.02, [(0, 1.45), (5.25, 1.28)], segs=8)
    _add_lathe_to_bm(r_ivory, mx, my, 5.18, [(0, 1.32), (0.35, 1.52), (0.55, 1.46)], segs=8)
    _add_lathe_to_bm(r_rose, mx, my, 5.70, [(0, 1.10), (1.78, 1.02)], segs=8)
    _add_lathe_to_bm(r_tile, mx, my, 7.45, [(0, 1.16), (0.45, 0.96), (1.15, 0.36), (1.42, 0.06)], segs=12)
    _add_lathe_to_bm(r_gold, mx, my, 8.85, [(0, 0.08), (0.18, 0.14), (0.65, 0.02)], segs=10)

    for tag, bm, key in (
        ('stucco', r_stucco, 'stucco'), ('rose', r_rose, 'rose'), ('ivory', r_ivory, 'ivory'),
        ('timber', r_bark, 'bark'), ('indigo', r_ind, 'indigo'), ('tile', r_tile, 'tile'),
        ('deep', r_deep, 'deep'), ('gold', r_gold, 'gold'),
    ):
        _new_mesh_obj(scene, f'Indigo Caravanserai | Expanded Right {tag}', bm, _mat(scene, key), 1.9, uv_name)

    # ---- C. REAR OVERPASS ARCH & SKYLINE BUNTING (all y >= 5.9 >> 2.7) ----
    c_stucco, c_ivory, c_ind, c_deep, c_bark, c_cloth = [bmesh.new() for _ in range(6)]
    _add_box_to_bm(c_stucco, 0.1, 9.2, 3.35, 3.6, 1.6, 1.25, bevel_w=0.020)
    _add_box_to_bm(c_ivory, 0.1, 8.34, 4.02, 3.7, 0.16, 0.15, bevel_w=0.010)
    _add_box_to_bm(c_deep, 0.1, 8.37, 3.22, 0.88, 0.08, 0.66)
    _add_box_to_bm(c_ind, 0.1, 8.32, 3.22, 0.72, 0.06, 0.54, bevel_w=0.005)

    strings = [
        (Vector((-7.4, 6.2, 4.45)), Vector((2.2, 6.0, 4.25)), 0.42, 11),
        (Vector((-3.8, 6.7, 5.10)), Vector((7.6, 6.1, 4.85)), 0.48, 12),
    ]
    for p_a, p_b, sag, flag_cnt in strings:
        prev = p_a
        for k in range(1, flag_cnt + 2):
            t = k / (flag_cnt + 1)
            cur = p_a.lerp(p_b, t) - Vector((0, 0, sag * math.sin(math.pi * t)))
            _add_cyl_to_bm(c_bark, prev, cur, 0.012, 0.012, segs=6)
            if k <= flag_cnt:
                mid = prev.lerp(cur, 0.5)
                tang = (cur - prev).normalized()
                target = c_cloth if k % 3 == 0 else (c_ind if k % 3 == 1 else c_ivory)
                v0 = target.verts.new(mid - tang * 0.11 - Vector((0, 0, 0.01)))
                v1 = target.verts.new(mid + tang * 0.11 - Vector((0, 0, 0.01)))
                v2 = target.verts.new(mid - Vector((0, 0.04, 0.25)))
                target.faces.new((v0, v1, v2))
            prev = cur

    for tag, bm, key in (
        ('stucco', c_stucco, 'stucco'), ('ivory', c_ivory, 'ivory'), ('indigo', c_ind, 'indigo'),
        ('deep', c_deep, 'deep'), ('timber', c_bark, 'bark'), ('cloth', c_cloth, 'cloth'),
    ):
        _new_mesh_obj(scene, f'Indigo Caravanserai | Expanded Rear {tag}', bm, _mat(scene, key), 1.6, uv_name)


# ---------------------------------------------------------------------------
# 3. Artisan Market & Everyday Storytelling Props (no stylized people meshes)
# ---------------------------------------------------------------------------

def _add_market_and_craft_props(scene, uv_name):
    """Per-group market & life props cleanly placed outside arena clearance and prop overlaps."""
    prop_keys = (
        'bark', 'ivory', 'cloth', 'indigo', 'clay', 'tile', 'brass', 'gold', 'leaf', 'rose',
        'sail_indigo', 'sail_ivory', 'robe_gold'
    )

    def empty_bms():
        return {k: bmesh.new() for k in prop_keys}

    def emit_group(prefix, bms):
        for k, bm in bms.items():
            _new_mesh_obj(scene, f'Indigo Caravanserai | Living props {prefix} {k}', bm, _mat(scene, k), 1.4, uv_name)

    # 1. RIGHT OPEN SPICE DISPLAY TABLE + SCALE + DYE RACK (clear of Return Canopy)
    r_bms = empty_bms()
    sx, sy = 10.62, 2.35
    _add_box_to_bm(r_bms['bark'], sx, sy, 0.34, 1.40, 0.82, 0.68, bevel_w=0.010)
    _add_box_to_bm(r_bms['cloth'], sx, sy, 0.72, 1.48, 0.90, 0.06, bevel_w=0.008)
    for bx, by, sp_key in (
        (sx - 0.45, sy - 0.14, 'cloth'), (sx - 0.14, sy - 0.14, 'gold'),
        (sx + 0.17, sy - 0.14, 'leaf'), (sx + 0.48, sy - 0.14, 'rose'),
        (sx - 0.30, sy + 0.18, 'sail_indigo'),
    ):
        _add_lathe_to_bm(r_bms['sail_ivory'], bx, by, 0.74, [(0, 0.10), (0.07, 0.14), (0.12, 0.145)], segs=12)
        _add_lathe_to_bm(r_bms[sp_key], bx, by, 0.82, [(0, 0.125), (0.07, 0.08), (0.13, 0.014)], segs=12)
    sc_x, sc_y = sx + 0.36, sy + 0.18
    _add_cyl_to_bm(r_bms['brass'], (sc_x, sc_y, 0.74), (sc_x, sc_y, 1.20), 0.02, 0.015, segs=8)
    _add_cyl_to_bm(r_bms['brass'], (sc_x - 0.16, sc_y, 1.18), (sc_x + 0.16, sc_y, 1.18), 0.010, 0.010, segs=6)
    for pan_dx in (-0.15, 0.15):
        _add_cyl_to_bm(r_bms['brass'], (sc_x + pan_dx, sc_y, 1.18), (sc_x + pan_dx, sc_y, 0.96), 0.005, 0.005, segs=5)
        _add_lathe_to_bm(r_bms['brass'], sc_x + pan_dx, sc_y, 0.92, [(0, 0.03), (0.03, 0.08)], segs=10)

    # Wooden dye drying rack beside right annex
    dx, dy = 11.35, 3.65
    for py in (dy - 0.65, dy + 0.65):
        _add_box_to_bm(r_bms['bark'], dx, py, 1.05, 0.09, 0.09, 2.10, bevel_w=0.008)
    _add_cyl_to_bm(r_bms['bark'], (dx, dy - 0.72, 2.02), (dx, dy + 0.72, 2.02), 0.035, 0.035, segs=8)
    for idx, dkey in enumerate(('sail_indigo', 'cloth', 'robe_gold')):
        _add_box_to_bm(r_bms[dkey], dx, dy - 0.42 + idx * 0.42, 1.48, 0.05, 0.32, 1.06, bevel_w=0.005)
    emit_group('Right', r_bms)

    # 2. LEFT TEA HOUSE, HOOKAH, TEA STOOLS, POTTERS' WHEEL & LADDER (all x <= -6.45)
    l_bms = empty_bms()
    hx, hy, hz = -7.52, 0.32, 0.48
    _add_lathe_to_bm(l_bms['tile'], hx, hy, hz, [(0, 0.07), (0.08, 0.10), (0.18, 0.06), (0.22, 0.03)], segs=12)
    _add_lathe_to_bm(l_bms['brass'], hx, hy, hz + 0.21, [(0, 0.025), (0.18, 0.018), (0.24, 0.055), (0.30, 0.03)], segs=12)
    for st_x, st_y in ((-7.85, -0.42), (-6.78, -0.35)):
        _add_box_to_bm(l_bms['bark'], st_x, st_y, 0.14, 0.42, 0.42, 0.28, bevel_w=0.010)
        _add_box_to_bm(l_bms['cloth'], st_x, st_y, 0.33, 0.38, 0.38, 0.07, bevel_w=0.008)
    wx, wy = -10.35, 2.55
    _add_box_to_bm(l_bms['bark'], wx, wy, 0.32, 0.85, 0.65, 0.64, bevel_w=0.010)
    _add_cyl_to_bm(l_bms['bark'], (wx, wy, 0.63), (wx, wy, 0.72), 0.26, 0.26, segs=16)
    _add_lathe_to_bm(l_bms['clay'], wx, wy, 0.71, [(0, 0.11), (0.12, 0.18), (0.26, 0.13), (0.34, 0.08), (0.38, 0.11)], segs=14)
    for rx in (-10.42, -9.96):
        _add_cyl_to_bm(l_bms['bark'], (rx, 3.05, 0.0), (rx, 3.55, 2.45), 0.032, 0.028, segs=7)
    for r_i in range(6):
        t = (r_i + 1) / 7.0
        _add_cyl_to_bm(l_bms['bark'], (-10.42, 3.05 + 0.50 * t, 2.45 * t), (-9.96, 3.05 + 0.50 * t, 2.45 * t), 0.020, 0.020, segs=6)
    emit_group('Left', l_bms)

    # 3. REAR ROLLED RUGS (all y >= 5.2 >> 2.7)
    u_bms = empty_bms()
    for rx, ry, rz, rkey in (
        (-6.02, 5.95, 0.13, 'cloth'), (-5.78, 6.08, 0.13, 'sail_indigo'), (-5.90, 6.02, 0.35, 'robe_gold')
    ):
        _add_cyl_to_bm(u_bms[rkey], (rx, ry - 0.45, rz), (rx, ry + 0.38, rz), 0.105, 0.105, segs=12)
        _add_cyl_to_bm(u_bms['sail_ivory'], (rx, ry - 0.47, rz), (rx, ry + 0.40, rz), 0.065, 0.065, segs=10)
    emit_group('Rear', u_bms)


# ---------------------------------------------------------------------------
# 4. World-Space UV Reprojection + Dedicated Sail Fabric Material Swap
# ---------------------------------------------------------------------------

def _reassign_canvas_materials(scene):
    """Replace shared stone/door materials on fabric awnings & flags with true woven sail fabrics."""
    mat_map = {
        'indigo': _mat(scene, 'sail_indigo'),
        'ivory': _mat(scene, 'sail_ivory'),
    }
    fabric_prefixes = (
        'Indigo Caravanserai | Canvas sail panel',
        'Indigo Caravanserai | Scalloped sail hem',
        'Indigo Caravanserai | Half shade valance',
        'Indigo Caravanserai | Expanded Rear cloth',
        'Indigo Caravanserai | Expanded Rear indigo',
        'Indigo Caravanserai | Expanded Rear ivory',
    )
    for obj in scene.objects:
        if obj.type != 'MESH' or not obj.name.startswith(fabric_prefixes):
            continue
        for idx, slot in enumerate(obj.material_slots):
            if not slot.material:
                continue
            key = slot.material.name.split(' | ')[-1]
            if key in mat_map and mat_map[key]:
                obj.data.materials[idx] = mat_map[key]


def _reproject_all_mirage_uvs(scene, uv_name):
    """Apply crisp world-space box projection to architecture & props."""
    skip_prefixes = (
        'Sunlit dust',
        'Indigo Caravanserai | Wind |',
        'Indigo Caravanserai | Hanging kilim',
        'Indigo Caravanserai | Potter glazed amphora',
        'Indigo Caravanserai | Oasis agave',
        'Indigo Caravanserai | Date palm ringed bole',
        'ENV_',
    )
    for obj in scene.objects:
        if obj.type != 'MESH' or obj.name.startswith(skip_prefixes):
            continue
        _box_project_uv(obj, tile_size=1.9, uv_name=uv_name)


def remaster_mirage(scene, tex_dir):
    """Full pipeline: upgrade Mirage PBR textures + sculpt details + expand buildings + people."""
    import mirage_pbr
    uv_name = _uv_name(scene)

    remaster_prefixes = (
        'Indigo Caravanserai | Masonry ',
        'Indigo Caravanserai | Expanded ',
        'Indigo Caravanserai | Living props ',
    )
    for obj in list(scene.objects):
        if obj.type == 'MESH' and obj.name.startswith(remaster_prefixes):
            me = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if me and me.users == 0:
                bpy.data.meshes.remove(me)

    mirage_pbr.upgrade_mirage_materials(scene, tex_dir)
    _reassign_canvas_materials(scene)
    _add_wing_masonry_and_eaves(scene, uv_name)
    _expand_flanking_architecture(scene, uv_name)
    _add_market_and_craft_props(scene, uv_name)
    _reproject_all_mirage_uvs(scene, uv_name)
