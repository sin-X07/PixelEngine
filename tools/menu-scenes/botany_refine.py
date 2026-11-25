"""High-detail realistic botanical refinement for PixelEngine menu scenes.

Replaces coarse trunks, palm fronds, ferns, bromeliads and vines in
ancient_ruins, dust_2 and mirage while preserving:
  - exact material slots & PBR texture links,
  - UV layer naming conventions,
  - the 18 'Wind | ...' animated nodes and their Menu_Environment NLA tracks,
  - arena clearance zones validated by validate_scenes.py.
"""
import math
import random
import bpy
from mathutils import Vector

TAU = math.tau


def _get_material(scene, short_key):
    prefix = scene.get('material_prefix', '')
    m = bpy.data.materials.get(prefix + short_key)
    if m:
        return m
    # Fallback search by suffix
    for mat in bpy.data.materials:
        if mat.name.endswith(' | ' + short_key) or mat.name == short_key:
            return mat
    return None


def _uv_layer_name(scene):
    for obj in scene.objects:
        if obj.type == 'MESH' and obj.data.uv_layers:
            return obj.data.uv_layers[0].name
    return 'Surface'


def _populate_mesh_data(mesh_data, verts, faces, uvs, mat, uv_name, sharp_deg=48.0):
    mesh_data.clear_geometry()
    mesh_data.from_pydata([tuple(v) for v in verts], [], faces)
    mesh_data.materials.clear()
    if mat:
        mesh_data.materials.append(mat)
    uv_layer = mesh_data.uv_layers.new(name=uv_name)
    for poly in mesh_data.polygons:
        poly.use_smooth = True
        for li in poly.loop_indices:
            vi = mesh_data.loops[li].vertex_index
            uv_layer.data[li].uv = uvs[vi]
    mesh_data.update()
    if hasattr(mesh_data, 'shade_smooth'):
        mesh_data.shade_smooth()
    if hasattr(mesh_data, 'set_sharp_from_angle'):
        mesh_data.set_sharp_from_angle(angle=math.radians(sharp_deg))


def _create_botany_object(scene, name, verts, faces, uvs, mat_key, uv_name,
                          collection=None, sharp_deg=48.0):
    data = bpy.data.meshes.new(name)
    _populate_mesh_data(data, verts, faces, uvs, _get_material(scene, mat_key), uv_name, sharp_deg)
    obj = bpy.data.objects.new(name, data)
    target_col = collection or scene.collection
    target_col.objects.link(obj)
    return obj


def _orthonormal_frame(tangent, prev_normal=None):
    t = tangent.normalized()
    if prev_normal is not None:
        n = prev_normal - t * prev_normal.dot(t)
        if n.length > 1e-5:
            n.normalize()
            b = t.cross(n).normalized()
            return n, b
    up = Vector((0.0, 0.0, 1.0)) if abs(t.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    n = up.cross(t).normalized()
    b = t.cross(n).normalized()
    return n, b


def _append_tube(verts, faces, uvs, points, radii, sides=10, flute_amp=0.0,
                 flute_freq=5, twist=0.0, u_scale=1.0, cap_ends=True):
    """Sweep an organic fluted/twisted tube along 3D polyline `points`."""
    if len(points) < 2:
        return
    base_idx = len(verts)
    n_pts = len(points)
    prev_n = None
    cum_len = 0.0
    lengths = [0.0]
    for i in range(1, n_pts):
        cum_len += (points[i] - points[i - 1]).length
        lengths.append(cum_len)

    for i in range(n_pts):
        if i == 0:
            tangent = points[1] - points[0]
        elif i == n_pts - 1:
            tangent = points[-1] - points[-2]
        else:
            tangent = points[i + 1] - points[i - 1]
        if tangent.length < 1e-6:
            tangent = Vector((0.0, 0.0, 1.0))
        n_vec, b_vec = _orthonormal_frame(tangent, prev_n)
        prev_n = n_vec
        t_frac = i / max(1, n_pts - 1)
        r_base = radii[i]
        v_coord = lengths[i] * 0.65
        for s in range(sides):
            ang = TAU * s / sides + twist * t_frac
            flute = 1.0 + flute_amp * math.sin(flute_freq * ang + t_frac * 3.0)
            r = max(0.001, r_base * flute)
            offset = n_vec * (math.cos(ang) * r) + b_vec * (math.sin(ang) * r)
            verts.append(points[i] + offset)
            uvs.append(((s / sides) * u_scale, v_coord))

    for i in range(n_pts - 1):
        row0 = base_idx + i * sides
        row1 = base_idx + (i + 1) * sides
        for s in range(sides):
            s_next = (s + 1) % sides
            faces.append((row0 + s, row0 + s_next, row1 + s_next, row1 + s))

    if cap_ends and sides >= 3:
        faces.append(tuple(base_idx + s for s in reversed(range(sides))))
        top_row = base_idx + (n_pts - 1) * sides
        faces.append(tuple(top_row + s for s in range(sides)))


def _append_curved_leaf(verts, faces, uvs, origin, forward, side_axis,
                        length, width, arch=0.18, tip_drop=0.28, cup=0.22,
                        segs=6, petiole=0.0):
    """Append a multi-segment curved & V-cupped realistic leaf blade."""
    fwd = forward.normalized()
    side = side_axis - fwd * side_axis.dot(fwd)
    if side.length < 1e-6:
        side = Vector((-fwd.y, fwd.x, 0.0))
    side.normalize()
    up = fwd.cross(side).normalized()
    if up.z < 0.0:
        up = -up

    base_idx = len(verts)
    cols = 5  # -1, -0.5, 0 (midrib), +0.5, +1
    start = Vector(origin) + fwd * petiole

    if petiole > 0.002:
        # Slim petiole stem quad from origin to blade base
        pw = min(width * 0.09, 0.012)
        p0 = Vector(origin)
        p1 = start
        verts.extend([p0 - side * pw, p0 + side * pw, p1 + side * pw * 0.8, p1 - side * pw * 0.8])
        uvs.extend([(0.45, 0.0), (0.55, 0.0), (0.55, 0.08), (0.45, 0.08)])
        faces.append((base_idx, base_idx + 1, base_idx + 2, base_idx + 3))
        base_idx = len(verts)

    for i in range(segs + 1):
        t = i / segs
        profile = math.sin(math.pi * (0.04 + 0.92 * t)) ** 0.72
        if i == segs:
            profile = 0.0
        half_w = 0.5 * width * profile
        z_curve = length * (arch * math.sin(math.pi * t) - tip_drop * (t * t))
        spine = start + fwd * (length * t) + up * z_curve
        for c_idx, frac in enumerate((-1.0, -0.5, 0.0, 0.5, 1.0)):
            # Midrib is slightly recessed (V-shaped fold); outer margins curl gently down at tips
            fold = abs(frac) * half_w * cup * (1.0 - 0.45 * t)
            tip_curl = -0.04 * length * (t * t) * (abs(frac) ** 1.5)
            pt = spine + side * (frac * half_w) + up * (fold + tip_curl)
            verts.append(pt)
            uvs.append((0.5 + 0.48 * frac, t))

    for i in range(segs):
        r0 = base_idx + i * cols
        r1 = base_idx + (i + 1) * cols
        for c in range(cols - 1):
            faces.append((r0 + c, r0 + c + 1, r1 + c + 1, r1 + c))


def _append_heart_leaf(verts, faces, uvs, origin, forward, side_axis,
                       length, width, curl=0.22):
    """Append a realistic rounded cordate vine leaf with auricle lobes, petiole & drip tip."""
    fwd = forward.normalized()
    side = (side_axis - fwd * side_axis.dot(fwd))
    if side.length < 1e-6:
        side = Vector((-fwd.y, fwd.x, 0.0))
    side.normalize()
    up = fwd.cross(side).normalized()
    if up.z < 0.0:
        up = -up

    # Petiole
    p0 = Vector(origin)
    p1 = p0 + fwd * (length * 0.24) + up * (length * 0.04)
    pw = width * 0.045
    bi = len(verts)
    verts.extend([p0 - side * pw, p0 + side * pw, p1 + side * pw * 0.7, p1 - side * pw * 0.7])
    uvs.extend([(0.46, 0.0), (0.54, 0.0), (0.54, 0.1), (0.46, 0.1)])
    faces.append((bi, bi + 1, bi + 2, bi + 3))

    # Rounded cordate blade: 9 longitudinal stations x 7 across
    segs = 8
    cols = 7
    bi = len(verts)
    for i in range(segs + 1):
        t = i / segs
        # Plump heart silhouette: quick rounded shoulder at t=0.10..0.32, smooth S-taper to acuminate drip-tip
        if i == segs:
            w_env = 0.0
        else:
            shoulder = math.sin(math.pi * min(1.0, (t + 0.06) * 0.94)) ** 0.55
            tip_taper = (1.0 - t) ** 0.65
            w_env = shoulder * (0.45 + 0.62 * tip_taper)
        half_w = 0.5 * width * w_env
        # Base basal lobes curve backward behind the petiole insertion
        lobe_pull = -0.22 * length * math.sin(math.pi * min(1.0, t * 2.2)) * (1.0 - t)
        arch = length * (0.16 * math.sin(math.pi * t) - curl * (t ** 2.3))
        for frac in (-1.0, -0.66, -0.33, 0.0, 0.33, 0.66, 1.0):
            # Center midrib starts at p1 (frac=0), lobes flare backwards near frac=+/-0.66
            lobe_w = math.sin(min(1.0, abs(frac)) * math.pi * 0.75)
            along = length * t + lobe_pull * lobe_w
            # Gentle concave cupping along veins + subtle edge undulation
            vein_cup = (abs(frac) ** 1.2) * half_w * 0.22 * (1.0 - 0.4 * t)
            margin_wave = 0.008 * length * math.sin(t * 10.0) * (abs(frac) ** 2)
            pt = p1 + fwd * along + side * (frac * half_w) + up * (arch + vein_cup + margin_wave)
            verts.append(pt)
            uvs.append((0.5 + 0.48 * frac, t))

    for i in range(segs):
        r0 = bi + i * cols
        r1 = bi + (i + 1) * cols
        for c in range(cols - 1):
            faces.append((r0 + c, r0 + c + 1, r1 + c + 1, r1 + c))


def _catmull_rom(ctrl, samples_per_seg=4):
    if len(ctrl) < 2:
        return list(ctrl)
    pts = [ctrl[0]] + list(ctrl) + [ctrl[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for s in range(samples_per_seg):
            t = s / samples_per_seg
            t2, t3 = t * t, t * t * t
            pos = 0.5 * (
                (2.0 * p1)
                + (-p0 + p2) * t
                + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
            )
            out.append(pos)
    out.append(ctrl[-1])
    return out


def _remove_objects_by_prefixes(scene, prefixes):
    removed = 0
    for obj in list(scene.objects):
        if obj.type == 'MESH' and obj.name.startswith(prefixes):
            mesh_data = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if mesh_data and mesh_data.users == 0:
                bpy.data.meshes.remove(mesh_data)
            removed += 1
    return removed


# ---------------------------------------------------------------------------
# 1. ANCIENT RUINS (Observatory of Rain): Ceiba trees, buttress roots, vines,
#    ferns, wind-animated bromeliads, and distant rainforest canopy
# ---------------------------------------------------------------------------

def _build_bromeliad_blade_data(mesh_data, angle, length, width, drop, mat, uv_name):
    """Build a multi-segment channeled & recurved bromeliad blade in local space."""
    forward = Vector((math.cos(angle), math.sin(angle), 0.0))
    side = Vector((-forward.y, forward.x, 0.0))
    up = Vector((0.0, 0.0, 1.0))

    verts, faces, uvs = [], [], []
    segs = 14
    cols = 5
    for i in range(segs + 1):
        t = i / segs
        # Broad sheathing base -> gentle waist -> wide arching mid -> acute recurved tip
        w_env = (0.42 + 0.58 * math.sin(math.pi * min(1.0, t * 0.92 + 0.08))) * (1.0 - t ** 3.2)
        if i == segs:
            w_env = 0.0
        half_w = 0.5 * width * w_env
        r_dist = length * (t - 0.10 * (t ** 2.5))
        z_h = length * (0.42 * math.sin(math.pi * (t ** 0.78)) - drop * (t ** 2.15))
        spine = forward * r_dist + up * z_h
        for frac in (-1.0, -0.5, 0.0, 0.5, 1.0):
            # Deep U-channel trough capable of holding rainwater
            trough = (frac * frac) * half_w * 0.52 * (1.0 - 0.35 * t)
            margin_wave = 0.006 * math.sin(t * 14.0) * abs(frac)
            pt = spine + side * (frac * half_w) + up * (trough + margin_wave)
            verts.append(pt)
            uvs.append((0.5 + 0.45 * frac, t))
    for i in range(segs):
        r0 = i * cols
        r1 = (i + 1) * cols
        for c in range(cols - 1):
            faces.append((r0 + c, r0 + c + 1, r1 + c + 1, r1 + c))
    _populate_mesh_data(mesh_data, verts, faces, uvs, mat, uv_name, sharp_deg=55.0)


def _build_fern_frond(verts, faces, uvs, origin, angle, radius, arch_h, pairs=14, seed=0):
    rng = random.Random(seed)
    fwd = Vector((math.cos(angle), math.sin(angle), 0.0))
    side = Vector((-fwd.y, fwd.x, 0.0))
    up = Vector((0.0, 0.0, 1.0))

    # Curved rachis stem
    stem_pts, stem_r = [], []
    for j in range(pairs + 2):
        t = j / (pairs + 1)
        z = radius * (arch_h * math.sin(math.pi * t * 0.85) - 0.24 * (t ** 2.3))
        p = Vector(origin) + fwd * (radius * t) + up * z + side * (0.025 * radius * math.sin(t * 4.0 + seed))
        stem_pts.append(p)
        stem_r.append(max(0.0025, 0.014 * radius * (1.0 - 0.82 * t)))
    _append_tube(verts, faces, uvs, stem_pts, stem_r, sides=5, cap_ends=False)

    # Alternating curved pinnae (leaflets) along rachis
    for j in range(1, pairs + 1):
        t = j / (pairs + 1)
        p_base = stem_pts[j]
        tang = (stem_pts[j + 1] - stem_pts[max(0, j - 1)]).normalized()
        pinna_len = radius * 0.30 * (math.sin(math.pi * (0.08 + 0.88 * t)) ** 0.72)
        pinna_w = pinna_len * 0.30
        for sign in (-1.0, 1.0):
            # Subtle natural stagger & forward sweep
            dir_p = (side * sign * 0.90 + tang * 0.42 + up * (0.18 - 0.22 * t)).normalized()
            _append_curved_leaf(
                verts, faces, uvs,
                origin=p_base + tang * (0.015 * sign * radius),
                forward=dir_p,
                side_axis=tang,
                length=pinna_len * (0.92 + 0.16 * rng.random()),
                width=pinna_w,
                arch=0.14,
                tip_drop=0.22,
                cup=0.20,
                segs=3,
            )


def _build_vine_cluster(scene, uv_name, x, y, z_top, drop_len, seed, wall_normal_y=-1.0):
    """Build a twisting multi-strand rainforest vine + tendrils + realistic heart leaves."""
    rng = random.Random(seed)
    trunk_v, trunk_f, trunk_uv = [], [], []
    leaf_v, leaf_f, leaf_uv = [], [], []

    strand_count = 3 if seed % 2 == 0 else 2
    for s in range(strand_count):
        ctrl = []
        steps = 8
        x_off = (s - 0.5 * (strand_count - 1)) * 0.11
        s_len = drop_len * (1.0 if s == 0 else (0.52 + 0.42 * rng.random()))
        for k in range(steps + 1):
            t = k / steps
            sway_x = 0.16 * math.sin(5.6 * t + seed * 1.3 + s * 2.2) + 0.04 * math.cos(11.0 * t + s)
            sway_y = wall_normal_y * (0.04 + 0.12 * math.sin(math.pi * t * 0.92) + 0.035 * math.sin(7.0 * t + s))
            ctrl.append(Vector((x + x_off + sway_x, y + sway_y, z_top - t * s_len)))
        pts = _catmull_rom(ctrl, 3)
        r_base = 0.020 if s == 0 else 0.011
        radii = [r_base * (1.0 - 0.72 * (i / max(1, len(pts) - 1))) for i in range(len(pts))]
        _append_tube(trunk_v, trunk_f, trunk_uv, pts, radii, sides=7, flute_amp=0.14, flute_freq=4, twist=4.0)

        # Forked clinging tendrils & side shoots
        for t_ratio in (0.38, 0.72):
            if s != 0 and t_ratio > 0.5:
                continue
            t_idx = int(len(pts) * t_ratio)
            if 2 < t_idx < len(pts) - 2:
                t_start = pts[t_idx]
                t_dir = 1.0 if (t_idx + s) % 2 == 0 else -1.0
                t_ctrl = [
                    t_start + Vector((t_dir * 0.055 * k + 0.02 * math.sin(k * 1.8),
                                      wall_normal_y * (0.025 * k),
                                      -0.065 * k))
                    for k in range(6)
                ]
                t_pts = _catmull_rom(t_ctrl, 2)
                t_r = [0.0065 * (1.0 - 0.75 * (i / max(1, len(t_pts) - 1))) for i in range(len(t_pts))]
                _append_tube(trunk_v, trunk_f, trunk_uv, t_pts, t_r, sides=5)
                # Young small leaf at tendril tip
                tip_p = t_pts[-2]
                _append_heart_leaf(
                    leaf_v, leaf_f, leaf_uv,
                    tip_p,
                    Vector((t_dir * 0.65, wall_normal_y * 0.60, -0.45)).normalized(),
                    Vector((0.2, wall_normal_y * 0.2, 0.9)),
                    0.095 + 0.03 * rng.random(), 0.085 + 0.03 * rng.random(), curl=0.22
                )

        # Varied heart leaves spaced naturally along the vine stem
        leaf_step = 3 if s == 0 else 4
        for idx in range(2, len(pts) - 1, leaf_step):
            t_stem = idx / max(1, len(pts) - 1)
            p = pts[idx]
            tang = (pts[min(len(pts) - 1, idx + 1)] - pts[max(0, idx - 1)]).normalized()
            sign = 1.0 if ((idx + s) % 2 == 0) else -1.0
            out_dir = Vector((
                sign * (0.55 + 0.40 * rng.random()),
                wall_normal_y * (0.52 + 0.38 * rng.random()),
                -0.18 - 0.42 * rng.random()
            )).normalized()
            side_ax = tang.cross(out_dir) + Vector((0.0, 0.0, rng.uniform(-0.35, 0.35)))
            if side_ax.length < 1e-4:
                side_ax = Vector((1.0, 0.0, 0.0))
            size_scale = 1.0 - 0.38 * t_stem
            l_len = (0.11 + 0.075 * rng.random()) * size_scale
            l_w = l_len * (0.86 + 0.14 * rng.random())
            _append_heart_leaf(leaf_v, leaf_f, leaf_uv, p, out_dir, side_ax, l_len, l_w, curl=0.18 + 0.14 * rng.random())

    _create_botany_object(scene, 'Observatory of Rain | Climbing vine', trunk_v, trunk_f, trunk_uv, 'bark', uv_name, sharp_deg=55.0)
    _create_botany_object(scene, 'Observatory of Rain | Heart leaf', leaf_v, leaf_f, leaf_uv, 'leaf', uv_name, sharp_deg=55.0)


def _build_ceiba_tree(scene, uv_name, x, y, h, seed):
    rng = random.Random(seed)
    bark_v, bark_f, bark_uv = [], [], []
    base = Vector((x, y, -0.04))

    # Multi-station organically curved fluted trunk
    ctrl = [
        base,
        Vector((x - 0.12 + 0.08 * math.sin(seed), y + 0.06, h * 0.25)),
        Vector((x - 0.22, y + 0.04, h * 0.56)),
        Vector((x + 0.10, y - 0.02, h * 0.80)),
        Vector((x + 0.36, y, h)),
    ]
    trunk_pts = _catmull_rom(ctrl, 4)
    trunk_r = [0.50 * (1.0 - 0.68 * ((i / (len(trunk_pts) - 1)) ** 0.85)) for i in range(len(trunk_pts))]
    _append_tube(bark_v, bark_f, bark_uv, trunk_pts, trunk_r, sides=14, flute_amp=0.14, flute_freq=6, twist=1.2)

    # 6 curved flaring buttress root fins hugging the trunk and sweeping onto the ground
    for i in range(6):
        ang = i * TAU / 6.0 + 0.23 + 0.12 * math.sin(seed + i)
        rad_dir = Vector((math.cos(ang), math.sin(ang), 0.0))
        tan_dir = Vector((-rad_dir.y, rad_dir.x, 0.0))
        reach = 1.25 + 0.25 * rng.random()
        fin_h = 1.55 + 0.35 * rng.random()
        fin_pts = []
        fin_r = []
        for k in range(7):
            t = k / 6.0
            # Sweep smoothly from ground outer tip inward & upward into trunk
            r_dist = reach * ((1.0 - t) ** 1.45) + 0.12 * t
            z_pos = -0.03 + fin_h * (t ** 1.35)
            wiggle = 0.09 * math.sin(t * math.pi + i)
            fin_pts.append(Vector((x, y, 0.0)) + rad_dir * r_dist + tan_dir * wiggle + Vector((0, 0, z_pos)))
            fin_r.append(0.065 + 0.16 * (1.0 - abs(t - 0.45)))
        _append_tube(bark_v, bark_f, bark_uv, fin_pts, fin_r, sides=8, flute_amp=0.18, flute_freq=3)

    # Forked main boughs & secondary twigs
    twig_tips = []
    for i in range(11):
        a = TAU * i / 11.0 + 0.22 * (rng.random() - 0.5)
        start_idx = max(4, min(len(trunk_pts) - 2, int(len(trunk_pts) * (0.52 + 0.38 * rng.random()))))
        start = trunk_pts[start_idx]
        spread = 1.75 + 0.85 * rng.random()
        end = Vector((x + 0.36, y, h)) + Vector((math.cos(a) * spread, math.sin(a) * spread, 0.85 * rng.random() - 0.30))
        mid = start.lerp(end, 0.48) + Vector((0.12 * math.sin(i), 0.12 * math.cos(i), 0.55))
        bough_pts = _catmull_rom([start, mid, end], 3)
        bough_r = [0.16 * (1.0 - 0.72 * (k / (len(bough_pts) - 1))) for k in range(len(bough_pts))]
        _append_tube(bark_v, bark_f, bark_uv, bough_pts, bough_r, sides=8, flute_amp=0.08, flute_freq=4)
        twig_tips.append((end, a, i))

        # Secondary forked sub-branchlet
        sub_start = bough_pts[len(bough_pts) // 2]
        sub_a = a + (0.48 if i % 2 == 0 else -0.48)
        sub_end = sub_start + Vector((math.cos(sub_a) * 1.15, math.sin(sub_a) * 1.15, 0.45 + 0.3 * rng.random()))
        sub_pts = _catmull_rom([sub_start, sub_start.lerp(sub_end, 0.5) + Vector((0, 0, 0.22)), sub_end], 2)
        sub_r = [0.08 * (1.0 - 0.7 * (k / (len(sub_pts) - 1))) for k in range(len(sub_pts))]
        _append_tube(bark_v, bark_f, bark_uv, sub_pts, sub_r, sides=6)
        twig_tips.append((sub_end, sub_a, i + 11))

    _create_botany_object(scene, 'Observatory of Rain | Ceiba lower trunk', bark_v, bark_f, bark_uv, 'bark', uv_name, sharp_deg=55.0)

    # Palmate / compound leaf clusters across branch tips
    buckets = {'canopy': ([], [], []), 'fern': ([], [], []), 'leaf': ([], [], [])}
    for tip, tip_ang, idx in twig_tips:
        key = ('canopy', 'fern', 'leaf')[idx % 3]
        lv, lf, luv = buckets[key]
        # 6 palmate rosettes per tip, each with 7 curved lanceolate leaflets
        for r_i in range(6):
            rosette_ang = tip_ang + (r_i - 2.5) * 0.58 + 0.28 * rng.random()
            rosette_pos = tip + Vector((math.cos(rosette_ang) * (0.20 + 0.58 * rng.random()),
                                        math.sin(rosette_ang) * (0.20 + 0.58 * rng.random()),
                                        0.32 * (rng.random() - 0.32)))
            if seed == 23 and idx == 1 and r_i >= 4:
                rosette_pos.z += 0.09
            for l_i in range(7):
                la = rosette_ang + (l_i - 3) * 0.42
                fwd = Vector((math.cos(la), math.sin(la), -0.14 + 0.24 * rng.random())).normalized()
                side_ax = Vector((-fwd.y, fwd.x, 0.24 * math.sin(l_i + idx)))
                _append_curved_leaf(
                    lv, lf, luv,
                    origin=rosette_pos,
                    forward=fwd,
                    side_axis=side_ax,
                    length=0.42 + 0.22 * rng.random(),
                    width=0.14 + 0.04 * rng.random(),
                    arch=0.20,
                    tip_drop=0.30,
                    cup=0.24,
                    segs=5,
                    petiole=0.05,
                )
    for key, (lv, lf, luv) in buckets.items():
        if lv:
            _create_botany_object(scene, 'Observatory of Rain | Ceiba leaf cluster', lv, lf, luv, key, uv_name, sharp_deg=55.0)


def refine_ancient_ruins(scene):
    uv_name = _uv_layer_name(scene)

    # 1. Replace wind-animated bromeliads in-place (keeps 18 Menu_Environment animations intact)
    brom_centers = [(-6.0, 4.12, 0.38), (6.35, 4.85, 0.42), (-3.75, 6.15, 1.67)]
    wind_broms = sorted([o for o in scene.objects if o.name.startswith('Observatory of Rain | Wind | bromeliad')],
                        key=lambda o: o.name)
    for idx, obj in enumerate(wind_broms):
        g = idx // 6
        i = idx % 6
        ang = TAU * i / 6.0 + g * 0.42
        length = 1.05 + 0.14 * (i % 3)
        mat_key = 'leaf' if i % 2 == 0 else 'fern'
        _build_bromeliad_blade_data(obj.data, ang, length, 0.22, 0.22, _get_material(scene, mat_key), uv_name)

    # Replace bromeliad inner rosette hearts per cluster (so AABB never spans across the arena)
    _remove_objects_by_prefixes(scene, ('Observatory of Rain | Bromeliad heart',))
    for g, (x, y, z) in enumerate(brom_centers):
        hv, hf, huv = [], [], []
        for ring_i, (cnt, r_len, r_w, z_off) in enumerate(((5, 0.56, 0.14, 0.06), (4, 0.36, 0.10, 0.11))):
            for k in range(cnt):
                a = TAU * k / cnt + ring_i * 0.5 + g * 0.4
                fwd = Vector((math.cos(a), math.sin(a), 0.35 + 0.25 * ring_i)).normalized()
                side = Vector((-fwd.y, fwd.x, 0.0))
                _append_curved_leaf(hv, hf, huv, (x, y, z + z_off), fwd, side, r_len, r_w, arch=0.22, tip_drop=0.05 + 0.1 * ring_i, cup=0.35, segs=5)
        _create_botany_object(scene, 'Observatory of Rain | Bromeliad heart', hv, hf, huv, 'clay', uv_name, sharp_deg=55.0)

    # 2. Replace coarse hanging vines & diamond heart leaves (anchored strictly flush to stone faces)
    _remove_objects_by_prefixes(scene, ('Observatory of Rain | Climbing vine', 'Observatory of Rain | Heart leaf'))
    for i in range(6):
        _build_vine_cluster(scene, uv_name, -9.1 + i * 0.68, 4.75, 2.18 + (i % 2) * 0.23, 1.35 + 0.22 * (i % 3), 2 + i)
    for i in range(5):
        _build_vine_cluster(scene, uv_name, 7.35 + i * 0.51, 6.12, 2.73, 1.45 + 0.20 * (i % 3), 10 + i)
    for i in range(5):
        _build_vine_cluster(scene, uv_name, -9.54 + i * 0.20, 2.23, 3.85 - 0.13 * i, 1.55 + 0.18 * (i % 4), 19 + i)
    # Extra flush vines hugging the left masonry wall & right stone platform edge
    for idx, (vx, vy, vz, vlen) in enumerate((
        (-10.15, 4.75, 2.25, 1.35), (-6.30, 4.75, 2.18, 1.15),
        (7.10, 6.12, 2.73, 1.35), (9.90, 6.12, 2.73, 1.55),
    )):
        _build_vine_cluster(scene, uv_name, vx, vy, vz, vlen, 40 + idx)

    # 3. Replace Ceiba trees (trunks, buttresses, boughs, twigs, leaf clusters)
    _remove_objects_by_prefixes(scene, (
        'Observatory of Rain | Ceiba lower trunk',
        'Observatory of Rain | Ceiba upper trunk',
        'Observatory of Rain | Buttress root',
        'Observatory of Rain | Forked bough',
        'Observatory of Rain | Canopy twig',
        'Observatory of Rain | Ceiba leaf cluster',
    ))
    for x, y, h, seed in ((-9.4, 7.9, 5.7, 11), (-5.3, 10.9, 5.3, 43), (8.4, 9.8, 6.1, 21),
                          (12.3, 12.0, 6.3, 23), (-14.4, 11.0, 6.2, 13)):
        _build_ceiba_tree(scene, uv_name, x, y, h, seed)

    # 4. Replace ferns per cluster so each cluster keeps a tight local bounding box
    _remove_objects_by_prefixes(scene, (
        'Observatory of Rain | Fern with paired leaflets',
        'Observatory of Rain | Watercourse fern',
        'Observatory of Rain | Lip fern',
    ))
    rng = random.Random(934)
    for c_idx, (x, y, z, radius) in enumerate((
            (-8.6, 3.6, 0.06, 1.02), (-6.9, 4.65, 2.01, 0.65),
            (6.72, 5.8, 0.08, 0.90), (9.3, 6.55, 2.6, 0.72), (-3.2, 7.05, 1.4, 0.68))):
        fl_v, fl_f, fl_uv = [], [], []
        ff_v, ff_f, ff_uv = [], [], []
        for i in range(9):
            a = i * TAU / 9.0 + rng.uniform(-0.18, 0.18)
            arch = 0.42 + 0.18 * (i % 2)
            if i % 3 == 0:
                _build_fern_frond(fl_v, fl_f, fl_uv, (x, y, z), a, radius, arch, pairs=14, seed=100 + c_idx * 10 + i)
            else:
                _build_fern_frond(ff_v, ff_f, ff_uv, (x, y, z), a, radius, arch, pairs=14, seed=200 + c_idx * 10 + i)
        if fl_v:
            _create_botany_object(scene, 'Observatory of Rain | Fern with paired leaflets', fl_v, fl_f, fl_uv, 'leaf', uv_name, sharp_deg=55.0)
        if ff_v:
            _create_botany_object(scene, 'Observatory of Rain | Fern with paired leaflets', ff_v, ff_f, ff_uv, 'fern', uv_name, sharp_deg=55.0)

    for c_idx, (cx, cy, h) in enumerate(((7.59, 1.39, 0.91), (9.72, 1.58, 1.30), (10.01, 2.69, 1.63))):
        wf_v, wf_f, wf_uv = [], [], []
        for j in range(5):
            _build_fern_frond(wf_v, wf_f, wf_uv, (cx + 0.18, cy, h + 0.13), 1.0 + j * 1.25, 0.46, 0.45, pairs=10, seed=300 + c_idx * 10 + j)
        _create_botany_object(scene, 'Observatory of Rain | Watercourse fern', wf_v, wf_f, wf_uv, 'fern', uv_name, sharp_deg=55.0)

    for c_idx, (x, y, z) in enumerate(((-9.1, 3.08, 4.24), (9.23, 4.0, 1.02))):
        ll_v, ll_f, ll_uv = [], [], []
        lf_v, lf_f, lf_uv = [], [], []
        for i in range(8):
            target = (ll_v, ll_f, ll_uv) if i % 2 == 0 else (lf_v, lf_f, lf_uv)
            _build_fern_frond(*target, (x, y, z), TAU * i / 8.0, 0.78, 0.34, pairs=12, seed=400 + c_idx * 10 + i)
        _create_botany_object(scene, 'Observatory of Rain | Lip fern', ll_v, ll_f, ll_uv, 'leaf', uv_name, sharp_deg=55.0)
        _create_botany_object(scene, 'Observatory of Rain | Lip fern', lf_v, lf_f, lf_uv, 'fern', uv_name, sharp_deg=55.0)

    # 5. Replace distant rear canopy stems & scallops per tree so AABBs stay local
    _remove_objects_by_prefixes(scene, (
        'Observatory of Rain | Rear canopy stem',
        'Observatory of Rain | Distant canopy scallop',
    ))
    for i in range(25):
        x = -15.0 + i * 1.28
        y = 13.7 + 0.55 * math.sin(i * 2.0)
        z = 3.82 + 0.70 * math.sin(i * 1.2)
        rc_bark_v, rc_bark_f, rc_bark_uv = [], [], []
        rc_leaf_v, rc_leaf_f, rc_leaf_uv = [], [], []
        pts = _catmull_rom([
            Vector((x - 0.18, y, -0.04)),
            Vector((x - 0.08 + 0.06 * math.sin(i), y + 0.04, z * 0.55)),
            Vector((x, y, z + 0.16)),
        ], 3)
        radii = [0.16 * (1.0 - 0.62 * (k / (len(pts) - 1))) for k in range(len(pts))]
        _append_tube(rc_bark_v, rc_bark_f, rc_bark_uv, pts, radii, sides=8, flute_amp=0.10, flute_freq=4)
        crown = Vector((x, y, z + 0.14))
        for j in range(18):
            a = rng.random() * TAU
            r = (rng.random() ** 0.5) * 1.65
            p = crown + Vector((r * math.cos(a), r * math.sin(a), rng.random() * 0.65))
            fwd = Vector((math.cos(a), math.sin(a), -0.10 + 0.18 * rng.random())).normalized()
            side = Vector((-fwd.y, fwd.x, 0.22 * math.sin(j * 2.4 + i)))
            _append_curved_leaf(rc_leaf_v, rc_leaf_f, rc_leaf_uv, p, fwd, side,
                                0.78 + 0.25 * rng.random(), 0.42 + 0.14 * rng.random(),
                                arch=0.18, tip_drop=0.25, cup=0.24, segs=3)
        _create_botany_object(scene, 'Observatory of Rain | Rear canopy stem', rc_bark_v, rc_bark_f, rc_bark_uv, 'bark', uv_name, sharp_deg=55.0)
        _create_botany_object(scene, 'Observatory of Rain | Distant canopy scallop', rc_leaf_v, rc_leaf_f, rc_leaf_uv, 'canopy', uv_name, sharp_deg=55.0)


# ---------------------------------------------------------------------------
# 2. DUST_2 (Salt Exchange): Diamond-bark date palms, feathered 3D fronds,
#    hanging date fruit clusters, and arid desert scrub tufts along edges
# ---------------------------------------------------------------------------

def _build_date_frond_data(mesh_data, angle, mat, uv_name):
    """Build a realistic feathered date palm frond (Phoenix dactylifera) in local space."""
    along = Vector((math.cos(angle), math.sin(angle), 0.0))
    side = Vector((-along.y, along.x, 0.0))
    up = Vector((0.0, 0.0, 1.0))

    verts, faces, uvs = [], [], []
    # Smooth 3-sided arching rachis spine
    rachis_segs = 18
    spine_pts, spine_r = [], []
    for i in range(rachis_segs + 1):
        t = i / rachis_segs
        pt = along * (2.18 * t) + up * (0.62 * math.sin(math.pi * t) - 0.74 * (t * t))
        spine_pts.append(pt)
        spine_r.append(max(0.006, 0.036 * (1.0 - 0.82 * t)))
    _append_tube(verts, faces, uvs, spine_pts, spine_r, sides=5, cap_ends=True)

    # 24 pairs of crisp V-folded lance-shaped leaflets sweeping out & gracefully drooping
    pair_count = 24
    for j in range(2, pair_count + 1):
        t = j / (pair_count + 1)
        idx_f = t * rachis_segs
        i0 = min(rachis_segs - 1, int(idx_f))
        frac = idx_f - i0
        p = spine_pts[i0].lerp(spine_pts[i0 + 1], frac)
        tang = (spine_pts[min(rachis_segs, i0 + 1)] - spine_pts[max(0, i0)]).normalized()
        env = math.sin(math.pi * (0.05 + 0.90 * t)) ** 0.78
        l_len = 0.14 + 0.44 * env
        l_w = 0.036 + 0.030 * env
        for sign in (-1.0, 1.0):
            # Date palm leaflets emerge in alternating V-angles upward, then droop softly
            up_tilt = 0.34 - 0.22 * t + 0.14 * ((j % 2) - 0.5)
            out_dir = (side * sign * 0.78 + tang * 0.58 + up * up_tilt).normalized()
            _append_curved_leaf(
                verts, faces, uvs,
                origin=p + side * (sign * 0.014),
                forward=out_dir,
                side_axis=tang,
                length=l_len,
                width=l_w,
                arch=0.14,
                tip_drop=0.34,
                cup=-0.28,  # characteristic reduplicate V-fold
                segs=4,
            )
    _populate_mesh_data(mesh_data, verts, faces, uvs, mat, uv_name, sharp_deg=50.0)


def _build_date_palm_trunk(scene, uv_name, x, y, h, curve_dx, mat_key, date_mat_key=None):
    bark_v, bark_f, bark_uv = [], [], []
    # Smooth curved core bole
    ctrl = [
        Vector((x, y, -0.02)),
        Vector((x + curve_dx * 0.12, y, h * 0.33)),
        Vector((x + curve_dx * 0.48, y, h * 0.66)),
        Vector((x + curve_dx, y, h)),
    ]
    pts = _catmull_rom(ctrl, 6)
    radii = [0.22 - 0.08 * (i / (len(pts) - 1)) for i in range(len(pts))]
    _append_tube(bark_v, bark_f, bark_uv, pts, radii, sides=12, flute_amp=0.05, flute_freq=8)

    # Diamond-patterned persistent leaf-base scales hugging the trunk
    rings = 34
    for r_i in range(rings):
        t = (r_i + 0.5) / rings
        idx = min(len(pts) - 2, int(t * (len(pts) - 1)))
        center = pts[idx].lerp(pts[idx + 1], t * (len(pts) - 1) - idx)
        rad = 0.225 - 0.075 * t
        scales_per_ring = 7
        offset_ang = (r_i % 2) * (TAU / (2 * scales_per_ring))
        for s in range(scales_per_ring):
            ang = TAU * s / scales_per_ring + offset_ang
            out = Vector((math.cos(ang), math.sin(ang), 0.0))
            tan = Vector((-out.y, out.x, 0.0))
            base_p = center + out * (rad * 0.88)
            tip_p = center + out * (rad * 1.18) + Vector((0, 0, 0.11 + 0.03 * t))
            bi = len(bark_v)
            w = 0.075
            bark_v.extend([
                base_p - tan * w,
                base_p + tan * w,
                tip_p + tan * (w * 0.45),
                tip_p - tan * (w * 0.45),
                base_p + out * 0.02 - Vector((0, 0, 0.06)),
            ])
            bark_uv.extend([(0.2, 0.2), (0.8, 0.2), (0.7, 0.8), (0.3, 0.8), (0.5, 0.0)])
            bark_f.extend([
                (bi, bi + 1, bi + 2, bi + 3),
                (bi, bi + 4, bi + 1),
            ])
    _create_botany_object(scene, 'Palm bark course', bark_v, bark_f, bark_uv, mat_key, uv_name, sharp_deg=45.0)

    # Hanging golden-brown date fruit clusters beneath the crown
    if date_mat_key:
        dv, df, duv = [], [], []
        crown = Vector((x + curve_dx, y, h))
        rng = random.Random(int(abs(x * 100)))
        for c_i in range(2):
            ca = c_i * 2.1 + 0.5
            stalk_start = crown + Vector((math.cos(ca) * 0.12, math.sin(ca) * 0.12, -0.05))
            stalk_mid = stalk_start + Vector((math.cos(ca) * 0.34, math.sin(ca) * 0.34, -0.18))
            stalk_end = stalk_mid + Vector((math.cos(ca) * 0.08, math.sin(ca) * 0.08, -0.45))
            _append_tube(dv, df, duv, _catmull_rom([stalk_start, stalk_mid, stalk_end], 3),
                         [0.022, 0.018, 0.015, 0.012, 0.010, 0.008, 0.006], sides=5)
            for k in range(14):
                drop_p = stalk_mid.lerp(stalk_end, 0.2 + 0.8 * rng.random()) + Vector(
                    ((rng.random() - 0.5) * 0.22, (rng.random() - 0.5) * 0.22, -0.08 * rng.random()))
                _append_tube(dv, df, duv,
                             [drop_p, drop_p + Vector((0, 0, -0.05)), drop_p + Vector((0, 0, -0.10))],
                             [0.014, 0.026, 0.010], sides=6)
        _create_botany_object(scene, 'Date fruit cluster', dv, df, duv, date_mat_key, uv_name, sharp_deg=55.0)


def _add_desert_scrub_tuft(scene, uv_name, x, y, z, scale, mat_leaf, mat_stem, seed):
    """Build a multi-branched desert shrub with dry woody twigs and arched sword-grass tufts."""
    rng = random.Random(seed)
    lv, lf, luv = [], [], []
    sv, sf, suv = [], [], []
    # Forking woody desert twigs
    for b in range(7):
        a = TAU * b / 7.0 + rng.random() * 0.45
        p0 = Vector((x, y, z))
        p1 = p0 + Vector((math.cos(a) * 0.14 * scale, math.sin(a) * 0.14 * scale, (0.18 + 0.08 * rng.random()) * scale))
        p2 = p1 + Vector((math.cos(a + 0.25) * 0.14 * scale, math.sin(a + 0.25) * 0.14 * scale, (0.15 + 0.06 * rng.random()) * scale))
        _append_tube(sv, sf, suv, [p0, p1, p2], [0.020 * scale, 0.012 * scale, 0.005 * scale], sides=5)
        # Small twig fork
        p_sub = p1 + Vector((math.cos(a - 0.5) * 0.11 * scale, math.sin(a - 0.5) * 0.11 * scale, 0.11 * scale))
        _append_tube(sv, sf, suv, [p1, p_sub], [0.009 * scale, 0.004 * scale], sides=4)
    # 28 arching desert grass / spike blades
    for k in range(28):
        a = TAU * k / 28.0 + rng.uniform(-0.18, 0.18)
        pitch = 0.42 + 0.95 * rng.random()
        fwd = Vector((math.cos(a), math.sin(a), pitch)).normalized()
        side = Vector((-fwd.y, fwd.x, 0.0))
        _append_curved_leaf(
            lv, lf, luv,
            origin=(x + 0.05 * math.cos(a) * scale, y + 0.05 * math.sin(a) * scale, z),
            forward=fwd,
            side_axis=side,
            length=(0.40 + 0.28 * rng.random()) * scale,
            width=(0.036 + 0.020 * rng.random()) * scale,
            arch=0.20,
            tip_drop=0.40,
            cup=-0.24,
            segs=5,
        )
    _create_botany_object(scene, 'Desert scrub stem', sv, sf, suv, mat_stem, uv_name, sharp_deg=50.0)
    _create_botany_object(scene, 'Desert scrub grass', lv, lf, luv, mat_leaf, uv_name, sharp_deg=55.0)


def refine_dust_2(scene):
    uv_name = _uv_layer_name(scene)

    # 1. Replace wind-animated date palm fronds in-place (preserves 16 frond + 2 dyer swatch animations)
    fronds = sorted([o for o in scene.objects if o.name.startswith('Wind | date frond')], key=lambda o: o.name)
    for idx, obj in enumerate(fronds):
        palm_x = -11.17 if idx < 8 else 9.27
        j = idx % 8
        ang = j * TAU / 8.0 + palm_x
        mat_key = 'sage' if j % 3 == 0 else 'leaves'
        _build_date_frond_data(obj.data, ang, _get_material(scene, mat_key), uv_name)

    # 2. Replace coarse palm trunks with diamond leaf-base bark + hanging date clusters
    _remove_objects_by_prefixes(scene, ('Palm bark course', 'Date fruit cluster'))
    _build_date_palm_trunk(scene, uv_name, -11.17, 3.35, 6.27, 0.36, 'timber', 'ochre')
    _build_date_palm_trunk(scene, uv_name, 9.27, 3.99, 6.81, 0.36, 'timber', 'ochre')

    # 3. Add visible, grounded arid desert grass/scrub tufts outside protected arena & clear of sacks/crates/car
    _remove_objects_by_prefixes(scene, ('Desert scrub',))
    scrub_spots = (
        (-6.85, 3.72, 0.0, 1.05, 'sage'),   # dye booth corner visible foreground-left
        (-3.95, 3.95, 0.0, 0.92, 'leaves'), # foot of limestone stairs (y>2.65 safe)
        (-2.12, 4.18, 0.0, 0.88, 'sage'),   # between stairs & staved crates (y>2.65 safe)
        (0.08, 5.65, 0.0, 0.95, 'leaves'),  # beside Salt Exchange left portal plinth
        (5.08, 5.58, 0.0, 0.92, 'sage'),    # beside right portal plinth
        (7.72, 2.52, 0.0, 0.96, 'leaves'),  # right-front edge of burlap sacks (clear of x<=6.7 / y<=1.75)
        (8.65, 2.05, 0.0, 1.05, 'sage'),    # beside warehouse door pier (x>6.7 safe)
        (-10.35, 3.55, 0.0, 1.00, 'leaves'),
    )
    for idx, (sx, sy, sz, sc, mkey) in enumerate(scrub_spots):
        _add_desert_scrub_tuft(scene, uv_name, sx, sy, sz, sc, mkey, 'timber', 700 + idx)


# ---------------------------------------------------------------------------
# 3. MIRAGE (Indigo Caravanserai): Ringed fan palms, pleated palm fans,
#    and oasis agave/succulent & courtyard grass tufts
# ---------------------------------------------------------------------------

def _build_fan_palm_data(mesh_data, angle, mat, uv_name):
    """Build a pleated, split-segment fan palm frond (Washingtonia) in local space."""
    along = Vector((math.cos(angle), math.sin(angle), 0.0))
    side = Vector((-along.y, along.x, 0.0))
    up = Vector((0.0, 0.0, 1.0))

    verts, faces, uvs = [], [], []
    # Graceful taller arching petiole stem so fan palm crowns frame both rooftops
    pet_pts, pet_r = [], []
    for i in range(9):
        t = i / 8.0
        pt = along * (1.28 * t) + up * (0.76 * math.sin(math.pi * t * 0.72) + 0.12 * t - 0.10 * (t * t))
        pet_pts.append(pt)
        pet_r.append(max(0.015, 0.036 * (1.0 - 0.50 * t)))
    _append_tube(verts, faces, uvs, pet_pts, pet_r, sides=6, cap_ends=True)

    hastula = pet_pts[-1]
    tang = (pet_pts[-1] - pet_pts[-2]).normalized()
    fan_up = tang.cross(side).normalized()
    if fan_up.z < 0.0:
        fan_up = -fan_up

    # 17 radiating pleated fan segments splitting near tips
    segs = 17
    fan_span = math.radians(150.0)
    for s in range(segs):
        frac = (s / (segs - 1)) * 2.0 - 1.0  # -1..1
        ray_ang = frac * 0.5 * fan_span
        ray_dir = (tang * math.cos(ray_ang) + side * math.sin(ray_ang) + fan_up * (0.14 - 0.24 * abs(frac))).normalized()
        ray_side = fan_up.cross(ray_dir).normalized()
        seg_len = (1.12 - 0.24 * (frac * frac))
        seg_w = 0.095 + 0.025 * (1.0 - abs(frac))
        # Alternate vertical pleating crest/valley across adjacent rays
        pleat = 0.045 if s % 2 == 0 else -0.025
        _append_curved_leaf(
            verts, faces, uvs,
            origin=hastula + fan_up * (pleat * 0.3),
            forward=ray_dir,
            side_axis=ray_side,
            length=seg_len,
            width=seg_w,
            arch=0.14,
            tip_drop=0.38 + 0.16 * abs(frac),
            cup=0.36 if s % 2 == 0 else -0.36,
            segs=5,
        )
    _populate_mesh_data(mesh_data, verts, faces, uvs, mat, uv_name, sharp_deg=45.0)


def _build_fan_palm_trunk(scene, uv_name, x, y, h, mat_key):
    bark_v, bark_f, bark_uv = [], [], []
    ctrl = [
        Vector((x, y, -0.02)),
        Vector((x + 0.05, y + 0.02, h * 0.33)),
        Vector((x + 0.20, y + 0.07, h * 0.66)),
        Vector((x + 0.41, y + 0.13, h)),
    ]
    pts = _catmull_rom(ctrl, 6)
    radii = []
    for i in range(len(pts)):
        t = i / (len(pts) - 1)
        # Characteristic subtle growth-ring swelling along trunk
        ring_swell = 1.0 + 0.065 * math.sin(t * TAU * 18.0)
        radii.append((0.24 - 0.095 * t) * ring_swell)
    _append_tube(bark_v, bark_f, bark_uv, pts, radii, sides=14, flute_amp=0.04, flute_freq=7)

    # Dry fibrous crown petticoat collar under live fronds
    crown = Vector((x + 0.41, y + 0.13, h))
    for k in range(14):
        a = TAU * k / 14.0
        fwd = Vector((math.cos(a), math.sin(a), -0.85)).normalized()
        side = Vector((-fwd.y, fwd.x, 0.0))
        _append_curved_leaf(bark_v, bark_f, bark_uv, crown + Vector((0, 0, -0.08)),
                            fwd, side, 0.65, 0.18, arch=0.08, tip_drop=0.35, cup=0.2, segs=4)
    _create_botany_object(scene, 'Indigo Caravanserai | Date palm ringed bole', bark_v, bark_f, bark_uv, mat_key, uv_name, sharp_deg=48.0)


def _add_oasis_agave(scene, uv_name, x, y, z, scale, mat_leaf, seed, with_reeds=True):
    """Fleshy architectural agave / aloe rosette + optional oasis reeds."""
    rng = random.Random(seed)
    lv, lf, luv = [], [], []
    # 3 concentric tiers of thick succulent leaves
    for tier, (cnt, len_b, w_b, pitch) in enumerate(((10, 0.48, 0.115, 0.34), (7, 0.38, 0.095, 0.72), (5, 0.26, 0.075, 1.12))):
        for k in range(cnt):
            a = TAU * k / cnt + tier * 0.45 + rng.uniform(-0.1, 0.1)
            fwd = Vector((math.cos(a), math.sin(a), pitch)).normalized()
            side = Vector((-fwd.y, fwd.x, 0.0))
            _append_curved_leaf(
                lv, lf, luv,
                origin=(x + 0.03 * math.cos(a) * scale, y + 0.03 * math.sin(a) * scale, z + 0.03 * tier * scale),
                forward=fwd,
                side_axis=side,
                length=len_b * scale,
                width=w_b * scale,
                arch=0.16,
                tip_drop=0.20 - 0.05 * tier,
                cup=0.45,
                segs=5,
            )
    if with_reeds:
        for k in range(11):
            a = TAU * k / 11.0 + rng.uniform(-0.2, 0.2)
            pitch = 0.85 + 0.75 * rng.random()
            fwd = Vector((math.cos(a), math.sin(a), pitch)).normalized()
            side = Vector((-fwd.y, fwd.x, 0.0))
            _append_curved_leaf(
                lv, lf, luv,
                origin=(x + 0.14 * math.cos(a + 0.5) * scale, y + 0.14 * math.sin(a + 0.5) * scale, z),
                forward=fwd,
                side_axis=side,
                length=(0.55 + 0.25 * rng.random()) * scale,
                width=0.032 * scale,
                arch=0.16,
                tip_drop=0.28,
                cup=-0.20,
                segs=5,
            )
    _create_botany_object(scene, 'Indigo Caravanserai | Oasis agave', lv, lf, luv, mat_leaf, uv_name, sharp_deg=52.0)


def refine_mirage(scene):
    uv_name = _uv_layer_name(scene)

    # 1. Replace wind-animated fan palms in-place (preserves 16 palm + 2 kilim animations)
    palms = sorted([o for o in scene.objects if o.name.startswith('Indigo Caravanserai | Wind | fan palm')],
                   key=lambda o: o.name)
    for idx, obj in enumerate(palms):
        palm_x = -10.12 if idx < 8 else 8.04
        j = idx % 8
        ang = TAU * j / 8.0 + palm_x
        _build_fan_palm_data(obj.data, ang, _get_material(scene, 'leaf'), uv_name)

    # 2. Replace coarse ringed trunks with organic ringed & petticoat-crowned trunks
    _remove_objects_by_prefixes(scene, ('Indigo Caravanserai | Date palm ringed bole',))
    _build_fan_palm_trunk(scene, uv_name, -10.12, 9.85, 5.33, 'bark')
    _build_fan_palm_trunk(scene, uv_name, 8.04, 9.43, 5.94, 'bark')

    # 3. Add crisp architectural oasis agave & reeds in clear courtyard corners (zero prop intersection, outside arena)
    _remove_objects_by_prefixes(scene, ('Indigo Caravanserai | Oasis agave', 'Indigo Caravanserai | Oasis grass'))
    spots = (
        (-6.20, 5.15, 0.0, 1.05, True),   # shady corner left of blue-door tower (y>>2.7 safe)
        (-7.05, 2.78, 0.0, 0.94, False),  # clean gap between tea dais (y<=1.82) and amphorae (y>=3.59)
        (1.22, 5.95, 0.0, 0.92, True),    # beside arcade left gallery pier (y>>2.7 safe)
        (5.78, 4.15, 0.0, 0.96, True),    # beside octagonal fountain plinth (y>>2.7 safe)
        (8.92, 2.55, 0.0, 1.02, False),   # courtyard gap between lane parapet (y<=2.24) and oil jugs (y>=4.54)
        (-0.35, 6.55, 0.0, 0.92, True),   # shady inner courtyard wall recess (y>>2.7 safe)
    )
    for idx, (ax, ay, az, sc, reeds) in enumerate(spots):
        _add_oasis_agave(scene, uv_name, ax, ay, az, sc, 'leaf', 900 + idx, with_reeds=reeds)
