"""High-detail realistic derelict sedan / courier estate remaster for dust_2 (Salt Exchange).

Replaces the coarse 8-sided prism & flat quad car with an articulated 1980s Mediterranean
desert sedan parked beside the indigo dye booth:
  - Contoured 18-station body shell with bonnet creases, recessed panel gaps, flared wheel
    arches, dark wheel-well liners, popped hood seam, A/B/C pillars & side mirrors.
  - Front slatted grille + recessed lamps (one cracked tilted reflector + turn signals),
    dual-section wrap-around chrome/rubber bumpers, embossed license plate, exhaust pipe.
  - Driver-side open/shattered window revealing bucket seats, headrests, 3-spoke steering
    wheel, dashboard cowl & gear stick; wiper-swept dust arc on front windshield.
  - Detailed wheels (28-seg treaded tires, subtly deflated front-left stance, 8-hole rims,
    lug hubcaps, brake discs & calipers) and a tubular roof rack with lashed crate & spare.
  - Dedicated 1024/512 tileable/UV-mapped PBR textures (BaseColor + ORM + OpenGL Normal).
"""
import math
from pathlib import Path
import bmesh
import bpy
import numpy as np
from mathutils import Vector, Matrix

TAU = math.tau


# ---------------------------------------------------------------------------
# 1. Procedural High-Res PBR Maps (Reuses Mirage's tileable math helpers)
# ---------------------------------------------------------------------------

def _new_img(name, arr, path, colorspace):
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old, do_unlink=True)
    h, w, _ = arr.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=False, is_data=(colorspace != 'sRGB'))
    img.colorspace_settings.name = colorspace
    img.pixels.foreach_set(arr.ravel())
    img.file_format = 'PNG'
    img.filepath_raw = str(path)
    img.save()
    img.pack()
    return img


def _make_car_material(name, base_arr, orm_arr, nrm_arr, tex_dir, normal_scale=0.85, metallic=0.0):
    from mirage_pbr import _apply_maps_to_material
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    _apply_maps_to_material(
        mat,
        {'base': base_arr, 'orm': orm_arr, 'normal': nrm_arr, 'normal_scale': normal_scale, 'metallic': metallic},
        Path(tex_dir),
    )
    return mat


def create_car_materials(tex_dir):
    """Synthesize high-resolution automotive PBR BaseColor, ORM & Normal maps."""
    from mirage_pbr import _tile_fbm, _tile_noise, _height_to_normal_ao
    tex_dir = Path(tex_dir)
    tex_dir.mkdir(parents=True, exist_ok=True)

    def rgba(rgb):
        out = np.ones((rgb.shape[0], rgb.shape[1], 4), dtype=np.float32)
        out[:, :, :3] = np.clip(rgb, 0.015, 0.97)
        return out

    def orm_pack(ao, rough, metal_arr):
        out = np.ones((ao.shape[0], ao.shape[1], 4), dtype=np.float32)
        out[:, :, 0] = np.clip(ao, 0.25, 1.0)
        out[:, :, 1] = np.clip(rough, 0.06, 0.98)
        out[:, :, 2] = np.clip(metal_arr, 0.0, 1.0)
        return out

    # A. Weathered Slate-Blue Car Paint (1024x1024): sun-faded clearcoat, door seams, lower dust & rust chips
    n = 1024
    broad = _tile_fbm(n, ((4, 0.55), (12, 0.30), (28, 0.15)), 11)
    fine = _tile_fbm(n, ((24, 0.55), (72, 0.30), (160, 0.15)), 12)
    micro = _tile_noise(n, 256, 13)
    v, u = np.mgrid[0:n, 0:n].astype(np.float32) / n

    # Subtle panel seam lines & rust edge patches
    rust_patch = np.clip((broad * 0.68 + fine * 0.32 - 0.64) * 6.5, 0.0, 1.0)
    sand_dust = np.clip((broad * 0.5 + fine * 0.3 + 0.15 - v * 0.35 - 0.42) * 2.2, 0.0, 0.65)
    scratch = np.clip((1.0 - np.abs(np.sin((u * 3.0 + v * 1.4 + fine * 0.4) * math.tau)) * 26.0), 0.0, 1.0) * 0.35

    paint_col = np.array([0.23, 0.33, 0.39], dtype=np.float32)
    faded_paint = np.array([0.36, 0.44, 0.48], dtype=np.float32)
    rust_col = np.array([0.42, 0.18, 0.09], dtype=np.float32)
    dust_col = np.array([0.74, 0.61, 0.43], dtype=np.float32)

    fade_mix = np.clip((broad - 0.40) * 2.0, 0.0, 1.0)[:, :, None]
    rgb = paint_col * (1.0 - fade_mix) + faded_paint * fade_mix
    rgb *= (0.92 + 0.12 * fine + 0.05 * micro)[:, :, None]
    rgb = rgb * (1.0 - rust_patch[:, :, None]) + rust_col * (0.8 + 0.35 * fine)[:, :, None] * rust_patch[:, :, None]
    rgb = rgb * (1.0 - sand_dust[:, :, None]) + dust_col * (0.9 + 0.15 * micro)[:, :, None] * sand_dust[:, :, None]
    rgb = rgb * (1.0 - scratch[:, :, None]) + np.array([0.55, 0.56, 0.58], dtype=np.float32) * scratch[:, :, None]

    orange_peel = 0.55 + 0.04 * fine + 0.03 * micro - 0.14 * rust_patch - 0.06 * scratch
    nrm, ao = _height_to_normal_ao(orange_peel, strength=2.6, ao_strength=0.45)
    rough = np.clip(0.38 + 0.14 * broad + 0.48 * rust_patch + 0.46 * sand_dust + 0.08 * (micro - 0.5), 0.24, 0.96)
    metal_map = np.clip(0.18 * (1.0 - sand_dust) * (1.0 - rust_patch) + 0.65 * scratch, 0.0, 0.85)
    mat_paint = _make_car_material('Car | Paint_Slate', rgba(rgb), orm_pack(ao, rough, metal_map), rgba(nrm), tex_dir, 0.72, 0.15)

    # B. Treaded Tire Rubber (center tread u in [0.24..0.76]) + clean dusty rubber sidewalls (512x512)
    n5 = 512
    v5, u5 = np.mgrid[0:n5, 0:n5].astype(np.float32) / n5
    b5 = _tile_fbm(n5, ((6, 0.6), (18, 0.4)), 21)
    f5 = _tile_noise(n5, 256, 22)
    tread_band = np.clip(1.0 - np.abs(u5 - 0.50) / 0.25, 0.0, 1.0)
    tread_zone = ((u5 > 0.25) & (u5 < 0.75))
    ribs = np.abs(np.sin((u5 - 0.5) * 14.0 * math.tau)) < 0.22
    chev = np.sin((abs(u5 - 0.5) * 10.0 + v5 * 22.0) * math.tau) < -0.25
    tread_recess = np.where(tread_zone, np.logical_or(ribs, chev).astype(np.float32), 0.0)
    sidewall_ring = (np.abs(np.sin(u5 * 18.0 * math.tau)) * (1.0 - tread_band)) * 0.14
    h_tire = (1.0 - tread_recess) * (0.72 + 0.05 * f5 - sidewall_ring) + tread_recess * 0.22
    nrm_t, ao_t = _height_to_normal_ao(h_tire, strength=4.6, ao_strength=0.55)
    tire_col = np.array([0.075, 0.078, 0.082], dtype=np.float32) * (0.88 + 0.16 * f5)[:, :, None]
    dust_in_tread = np.clip(tread_recess * 0.58 + b5 * 0.16 * (0.3 + 0.7 * tread_band), 0.0, 0.60)[:, :, None]
    tire_rgb = tire_col * (1.0 - dust_in_tread) + np.array([0.58, 0.47, 0.34], dtype=np.float32) * dust_in_tread
    rough_t = np.clip(0.84 + 0.12 * tread_recess + 0.06 * (f5 - 0.5), 0.70, 0.97)
    mat_tire = _make_car_material('Car | Tire_Rubber', rgba(tire_rgb), orm_pack(ao_t, rough_t, np.zeros_like(h_tire)), rgba(nrm_t), tex_dir, 0.88, 0.0)

    # C. Aged Chrome / Brushed Steel Rim (subtle isotropic micro-grain, 512x512)
    brushed = _tile_fbm(n5, ((32, 0.6), (96, 0.4)), 29)
    pit = np.clip((0.35 - b5) * 2.8, 0.0, 0.42)
    h_chr = 0.68 + 0.04 * brushed - 0.22 * pit + 0.03 * f5
    nrm_c, ao_c = _height_to_normal_ao(h_chr, strength=1.8, ao_strength=0.40)
    chr_rgb = np.array([0.76, 0.75, 0.72], dtype=np.float32) * (0.93 + 0.08 * brushed)[:, :, None]
    chr_rgb = chr_rgb * (1.0 - pit[:, :, None]) + np.array([0.48, 0.36, 0.24], dtype=np.float32) * pit[:, :, None]
    rough_c = np.clip(0.18 + 0.46 * pit + 0.10 * b5, 0.12, 0.85)
    metal_c = np.clip(0.95 - 0.58 * pit, 0.28, 0.97)
    mat_chrome = _make_car_material('Car | Chrome_Metal', rgba(chr_rgb), orm_pack(ao_c, rough_c, metal_c), rgba(nrm_c), tex_dir, 0.45, 0.92)

    # D. Dusty Windshield / Side Glass with Wiper Sweep Arc (512x512)
    # Arc swept clean by single wiper pivoted near (0.28, 0.10)
    wiper_r = np.hypot(u5 - 0.30, v5 - 0.05)
    wiper_ang = np.arctan2(v5 - 0.05, u5 - 0.30)
    wiper_arc = ((wiper_r > 0.16) & (wiper_r < 0.76) & (wiper_ang > 0.22) & (wiper_ang < 2.20)).astype(np.float32)
    dust_g = np.clip(0.68 - 0.58 * wiper_arc + 0.25 * (b5 - 0.5) + 0.22 * (1.0 - v5), 0.06, 0.88)
    glass_base = np.array([0.11, 0.18, 0.19], dtype=np.float32)
    glass_rgb = glass_base * (1.0 - dust_g[:, :, None]) + np.array([0.69, 0.58, 0.43], dtype=np.float32) * dust_g[:, :, None]
    h_gl = 0.5 + 0.15 * dust_g + 0.04 * f5
    nrm_g, ao_g = _height_to_normal_ao(h_gl, strength=1.8, ao_strength=0.25)
    rough_g = np.clip(0.14 + 0.76 * dust_g, 0.10, 0.94)
    mat_glass = _make_car_material('Car | Glass_Dusty', rgba(glass_rgb), orm_pack(ao_g, rough_g, np.full_like(h_gl, 0.08)), rgba(nrm_g), tex_dir, 0.45, 0.08)

    # E. Flaked Deep Rust / Chassis & Dark Trim (512x512)
    h_ru = 0.45 + 0.35 * b5 + 0.18 * f5
    nrm_r, ao_r = _height_to_normal_ao(h_ru, strength=4.2, ao_strength=0.52)
    ru_rgb = np.array([0.36, 0.15, 0.08], dtype=np.float32) * (0.70 + 0.45 * b5 + 0.20 * f5)[:, :, None]
    rough_r = np.clip(0.86 + 0.10 * (f5 - 0.5), 0.72, 0.98)
    mat_rust = _make_car_material('Car | Rust_Trim', rgba(ru_rgb), orm_pack(ao_r, rough_r, np.zeros_like(h_ru)), rgba(nrm_r), tex_dir, 0.88, 0.0)

    # F. Headlamp Lens / Taillight Cluster Fresnel Ridges (512x512)
    fresnel_lens = 0.5 + 0.35 * np.sin(u5 * 28.0 * math.tau) + 0.15 * np.cos(v5 * 14.0 * math.tau)
    nrm_l, ao_l = _height_to_normal_ao(fresnel_lens, strength=3.8, ao_strength=0.35)
    lamp_head_rgb = np.array([0.82, 0.80, 0.72], dtype=np.float32) * (0.85 + 0.15 * fresnel_lens)[:, :, None]
    mat_headlamp = _make_car_material('Car | Headlamp_Lens', rgba(lamp_head_rgb), orm_pack(ao_l, np.full_like(h_gl, 0.16), np.full_like(h_gl, 0.25)), rgba(nrm_l), tex_dir, 0.85, 0.25)

    # Taillight split Red + Amber + Reverse White stripes vertically
    tail_col = np.where(v5[:, :, None] > 0.62, np.array([0.82, 0.42, 0.06], dtype=np.float32),
                        np.where(v5[:, :, None] > 0.26, np.array([0.68, 0.07, 0.05], dtype=np.float32),
                                 np.array([0.78, 0.76, 0.70], dtype=np.float32)))
    tail_col *= (0.82 + 0.20 * fresnel_lens)[:, :, None]
    mat_taillight = _make_car_material('Car | Taillight_Lens', rgba(tail_col), orm_pack(ao_l, np.full_like(h_gl, 0.20), np.full_like(h_gl, 0.12)), rgba(nrm_l), tex_dir, 0.85, 0.12)

    # G. Interior Trim / Matte Vinyl & Rubber (512x512)
    h_in = 0.50 + 0.12 * f5 + 0.08 * b5
    nrm_i, ao_i = _height_to_normal_ao(h_in, strength=2.2, ao_strength=0.45)
    in_rgb = np.array([0.14, 0.12, 0.11], dtype=np.float32) * (0.82 + 0.26 * b5 + 0.12 * f5)[:, :, None]
    mat_interior = _make_car_material('Car | Interior_Matte', rgba(in_rgb), orm_pack(ao_i, np.full_like(h_in, 0.82), np.zeros_like(h_in)), rgba(nrm_i), tex_dir, 0.60, 0.0)

    # H. Embossed License Plate (512x256 -> square 512x512 safe crop)
    pl_rgb = np.full((n5, n5, 3), [0.85, 0.80, 0.64], dtype=np.float32)
    border = (u5 < 0.05) | (u5 > 0.95) | (v5 < 0.08) | (v5 > 0.92)
    blue_band = (u5 >= 0.05) & (u5 < 0.18) & (v5 >= 0.08) & (v5 <= 0.92)
    # Crisp dark stamped digit blocks
    digits = np.zeros((n5, n5), dtype=bool)
    for d_i, xc in enumerate((0.28, 0.40, 0.56, 0.68, 0.82)):
        digits |= (np.abs(u5 - xc) < 0.038) & (v5 > 0.24) & (v5 < 0.76) & (
            (v5 > 0.66) | (v5 < 0.34) | (np.abs(v5 - 0.50) < 0.05) | ((u5 < xc - 0.015) if d_i % 2 == 0 else (u5 > xc + 0.015))
        )
    pl_rgb[border] = [0.16, 0.16, 0.15]
    pl_rgb[blue_band] = [0.11, 0.28, 0.52]
    pl_rgb[digits] = [0.12, 0.12, 0.12]
    pl_rgb *= (0.90 + 0.12 * b5)[:, :, None]
    pl_rgb = pl_rgb * (1.0 - 0.22 * b5[:, :, None]) + np.array([0.65, 0.52, 0.36], dtype=np.float32) * (0.22 * b5[:, :, None])
    h_pl = 0.5 + 0.22 * border.astype(np.float32) + 0.20 * digits.astype(np.float32) + 0.05 * f5
    nrm_p, ao_p = _height_to_normal_ao(h_pl, strength=3.4, ao_strength=0.45)
    mat_plate = _make_car_material('Car | License_Plate', rgba(pl_rgb), orm_pack(ao_p, np.full_like(h_pl, 0.52), np.full_like(h_pl, 0.45)), rgba(nrm_p), tex_dir, 0.78, 0.45)

    return {
        'paint': mat_paint,
        'tire': mat_tire,
        'chrome': mat_chrome,
        'glass': mat_glass,
        'rust': mat_rust,
        'headlamp': mat_headlamp,
        'taillight': mat_taillight,
        'interior': mat_interior,
        'plate': mat_plate,
    }


# ---------------------------------------------------------------------------
# 2. Articulated Hard-Surface Car Mesh Builders
# ---------------------------------------------------------------------------

def _box_uv_car(obj, tile_size=1.2, uv_name='Surface'):
    me = obj.data
    if not me.polygons:
        return
    uv = me.uv_layers.get(uv_name) or me.uv_layers.new(name=uv_name)
    mw = obj.matrix_world
    rot = mw.to_3x3()
    for poly in me.polygons:
        wn = (rot @ poly.normal).normalized()
        ax, ay, az = abs(wn.x), abs(wn.y), abs(wn.z)
        for li in poly.loop_indices:
            vi = me.loops[li].vertex_index
            wp = mw @ me.vertices[vi].co
            if az >= ax and az >= ay:
                uv.data[li].uv = (wp.x / tile_size, wp.y / tile_size)
            elif ay >= ax:
                uv.data[li].uv = (wp.x / tile_size, wp.z / tile_size)
            else:
                uv.data[li].uv = (wp.y / tile_size, wp.z / tile_size)


def _planar_uv_rect(obj, u_axis, v_axis, origin, u_span, v_span, uv_name='Surface'):
    me = obj.data
    uv = me.uv_layers.get(uv_name) or me.uv_layers.new(name=uv_name)
    orig = Vector(origin)
    ua, va = Vector(u_axis), Vector(v_axis)
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co - orig
            uv.data[li].uv = (co.dot(ua) / u_span, co.dot(va) / v_span)


def _emit_car_obj(scene, name, bm, mat, xform_mat, uv_name='Surface', tile_size=1.35, sharp_deg=42.0, box_uv=True):
    if len(bm.verts) == 0 or len(bm.faces) == 0:
        bm.free()
        return None
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=list(bm.edges))
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-4)
    bm.transform(xform_mat)
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
    if box_uv:
        _box_uv_car(obj, tile_size=tile_size, uv_name=uv_name)
    return obj


def _add_box(bm, cx, cy, cz, sx, sy, sz, rot_x=0.0, rot_y=0.0, rot_z=0.0, bevel_w=0.0):
    sub = bmesh.new()
    bmesh.ops.create_cube(sub, size=1.0)
    m = (Matrix.Translation((cx, cy, cz)) @
         Matrix.Rotation(rot_z, 4, 'Z') @ Matrix.Rotation(rot_y, 4, 'Y') @ Matrix.Rotation(rot_x, 4, 'X'))
    for v in sub.verts:
        v.co = m @ Vector((v.co.x * sx, v.co.y * sy, v.co.z * sz))
    bw = min(bevel_w, min(sx, sy, sz) * 0.22) if bevel_w > 0.0005 else 0.0
    if bw > 0.0005:
        bmesh.ops.bevel(sub, geom=list(sub.edges), offset=bw, segments=2, profile=0.5, affect='EDGES', clamp_overlap=True)
    bmesh.ops.recalc_face_normals(sub, faces=list(sub.faces))
    me = bpy.data.meshes.new('_tmp_cbox')
    sub.to_mesh(me)
    sub.free()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)


def _add_cyl(bm, p0, p1, r0, r1, segs=16):
    a, b = Vector(p0), Vector(p1)
    d = b - a
    if d.length < 1e-5:
        return
    z_ax = d.normalized()
    up = Vector((0, 0, 1)) if abs(z_ax.z) < 0.92 else Vector((1, 0, 0))
    x_ax = up.cross(z_ax).normalized()
    y_ax = z_ax.cross(x_ax).normalized()
    r0s = [bm.verts.new(a + x_ax * (r0 * math.cos(TAU * i / segs)) + y_ax * (r0 * math.sin(TAU * i / segs))) for i in range(segs)]
    r1s = [bm.verts.new(b + x_ax * (r1 * math.cos(TAU * i / segs)) + y_ax * (r1 * math.sin(TAU * i / segs))) for i in range(segs)]
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((r0s[i], r0s[j], r1s[j], r1s[i]))
    bm.faces.new(list(reversed(r0s)))
    bm.faces.new(r1s)


def _build_body_shell(bm_body, bm_interior, bm_rust):
    """Sculpt a contoured 1980s sedan shell in local car space (X=length -1.52..+1.50, Y=width, Z=up)."""
    # Bottom rocker panel frame / sub-chassis tub + recessed black wheel arches (so wheels nest inside real wells)
    _add_box(bm_body, -0.02, 0.0, 0.44, 2.94, 1.24, 0.40, bevel_w=0.04)  # lower body waist
    # Rear trunk & quarter panels (-1.48 .. -0.62)
    _add_box(bm_body, -1.04, 0.0, 0.65, 0.88, 1.25, 0.34, bevel_w=0.04)
    # Front fenders & engine bay walls (+0.48 .. +1.46)
    _add_box(bm_body, 0.97, 0.0, 0.64, 0.96, 1.24, 0.32, bevel_w=0.035)
    # Popped front hood (bonnet) slightly unlatched by +1.5cm at nose + central power bulge
    _add_box(bm_body, 0.95, 0.0, 0.815, 0.92, 1.16, 0.045, rot_y=0.035, bevel_w=0.015)
    _add_box(bm_body, 0.94, 0.0, 0.842, 0.72, 0.46, 0.024, rot_y=0.035, bevel_w=0.010)

    # Flared semicircular fender arches above all 4 wheels (axles at x = -0.96, +0.92)
    for ax in (-0.96, 0.92):
        for side in (-1.0, 1.0):
            # Dark wheel-well liner tucked inside body
            _add_cyl(bm_interior, (ax, side * 0.44, 0.31), (ax, side * 0.62, 0.31), 0.34, 0.34, segs=18)
            # Sculpted wheel-arch eyebrow flare along top semicircle
            for a_i in range(9):
                a0 = math.pi * a_i / 9.0
                a1 = math.pi * (a_i + 1) / 9.0
                am = 0.5 * (a0 + a1)
                cx = ax + 0.36 * math.cos(am)
                cz = 0.31 + 0.36 * math.sin(am)
                if cz < 0.25:
                    continue
                _add_box(bm_body, cx, side * 0.635, cz, 0.145, 0.055, 0.052, rot_y=-(am - math.pi * 0.5), bevel_w=0.010)

    # Cabin floor & inner door sills (strictly clear of outer lower body walls to avoid coplanar overlap)
    _add_box(bm_interior, -0.06, 0.0, 0.34, 1.12, 0.96, 0.14)
    for side in (-1.0, 1.0):
        _add_box(bm_body, -0.06, side * 0.625, 0.57, 1.24, 0.05, 0.44, bevel_w=0.016)
        _add_cyl(bm_body, (0.48, side * 0.58, 0.80), (0.28, side * 0.48, 1.27), 0.038, 0.032, segs=8)   # A-pillar
        _add_cyl(bm_body, (-0.10, side * 0.58, 0.80), (-0.10, side * 0.49, 1.28), 0.042, 0.036, segs=8) # B-pillar
        _add_cyl(bm_body, (-0.72, side * 0.58, 0.80), (-0.52, side * 0.48, 1.27), 0.055, 0.048, segs=8) # C-pillar
        _add_box(bm_rust, -0.04, side * 0.642, 0.27, 1.38, 0.042, 0.072, bevel_w=0.008)
        _add_box(bm_rust, -0.04, side * 0.662, 0.62, 1.48, 0.026, 0.040, bevel_w=0.006)
        _add_box(bm_rust, -0.02, side * 0.658, 0.74, 0.13, 0.035, 0.028, bevel_w=0.006)
        _add_box(bm_rust, 0.24, side * 0.658, 0.74, 0.13, 0.035, 0.028, bevel_w=0.006)
        _add_cyl(bm_rust, (0.42, side * 0.60, 0.82), (0.42, side * 0.72, 0.86), 0.014, 0.012, segs=6)
        _add_box(bm_body, 0.41, side * 0.735, 0.87, 0.08, 0.13, 0.085, bevel_w=0.012)

    # Contoured Sedan Roof with beveled edge & subtle drip rails
    _add_box(bm_body, -0.12, 0.0, 1.275, 0.88, 1.00, 0.068, bevel_w=0.025)
    # Center vertical door gap recess strip proud of skin by +0.012
    _add_box(bm_rust, -0.10, 0.0, 0.57, 0.016, 1.312, 0.42)


def _build_cabin_interior(bm_interior, bm_chrome, bm_rust):
    """Build visible dashboard, 3-spoke steering wheel, twin seats & gear stick inside cabin."""
    # Warm tan-brown seats for clear visual contrast against dark dashboard & dusty body
    for side in (-0.28, 0.28):
        _add_box(bm_rust, -0.04, side, 0.54, 0.42, 0.36, 0.12, bevel_w=0.025)
        _add_box(bm_rust, -0.22, side, 0.78, 0.12, 0.34, 0.48, rot_y=-0.16, bevel_w=0.025)
        _add_box(bm_rust, -0.27, side, 1.07, 0.09, 0.20, 0.14, rot_y=-0.10, bevel_w=0.020)
    _add_box(bm_rust, -0.56, 0.0, 0.76, 0.14, 0.96, 0.44, rot_y=-0.14, bevel_w=0.025)
    _add_box(bm_interior, 0.38, 0.0, 0.78, 0.24, 1.04, 0.18, bevel_w=0.020)
    _add_box(bm_interior, 0.34, -0.28, 0.88, 0.18, 0.32, 0.10, bevel_w=0.015)
    _add_cyl(bm_rust, (0.36, -0.28, 0.78), (0.20, -0.28, 0.87), 0.020, 0.018, segs=8)
    rim_c = Vector((0.20, -0.28, 0.87))
    ring_segs = 16
    for i in range(ring_segs):
        a0 = TAU * i / ring_segs
        a1 = TAU * (i + 1) / ring_segs
        p0 = rim_c + Vector((0.0, 0.145 * math.cos(a0), 0.145 * math.sin(a0)))
        p1 = rim_c + Vector((0.0, 0.145 * math.cos(a1), 0.145 * math.sin(a1)))
        _add_cyl(bm_interior, p0, p1, 0.013, 0.013, segs=6)
    for sp in (0.0, 2.2, 4.1):
        _add_cyl(bm_chrome, rim_c, rim_c + Vector((0.0, 0.14 * math.cos(sp), 0.14 * math.sin(sp))), 0.008, 0.008, segs=5)
    _add_cyl(bm_chrome, (0.14, 0.0, 0.48), (0.18, 0.0, 0.72), 0.010, 0.008, segs=6)
    _add_cyl(bm_interior, (0.18, 0.0, 0.71), (0.18, 0.0, 0.75), 0.022, 0.016, segs=8)


def _build_front_rear_hardware(bm_chrome, bm_rust, bm_interior, bm_headlamp, bm_taillight, bm_plate):
    """Build radiator grille, recessed headlamps (subtly cracked left), wrap bumpers, taillights & exhaust."""
    _add_box(bm_interior, 1.445, 0.0, 0.63, 0.04, 0.74, 0.22, bevel_w=0.006)
    _add_box(bm_chrome, 1.466, 0.0, 0.735, 0.024, 0.76, 0.024, bevel_w=0.005)
    _add_box(bm_chrome, 1.466, 0.0, 0.525, 0.024, 0.76, 0.024, bevel_w=0.005)
    for s in range(4):
        _add_box(bm_chrome, 1.466, 0.0, 0.56 + s * 0.048, 0.020, 0.72, 0.012, bevel_w=0.003)

    # Dual Headlamps nested inside bezel housings (left subtly tilted by 0.09 rad instead of sticking far out)
    for side in (-1.0, 1.0):
        tilt_x = 0.09 if side < 0 else 0.0
        _add_box(bm_interior, 1.445, side * 0.47, 0.63, 0.045, 0.20, 0.18, rot_x=tilt_x, bevel_w=0.008)
        _add_box(bm_headlamp, 1.482, side * 0.46, 0.63, 0.018, 0.15, 0.14, rot_x=tilt_x, bevel_w=0.005)
        for edge_z in (-0.085, 0.085):
            _add_box(bm_chrome, 1.472, side * 0.47, 0.63 + edge_z, 0.022, 0.21, 0.020, rot_x=tilt_x, bevel_w=0.004)
        _add_box(bm_interior, -1.482, side * 0.46, 0.66, 0.04, 0.24, 0.15, bevel_w=0.006)
        _add_box(bm_taillight, -1.508, side * 0.46, 0.66, 0.020, 0.21, 0.125, bevel_w=0.006)

    for end in (-1.0, 1.0):
        bx = end * 1.50
        _add_box(bm_chrome, bx, 0.0, 0.37, 0.095, 1.18, 0.115, bevel_w=0.016)
        for side in (-1.0, 1.0):
            _add_box(bm_chrome, bx - end * 0.11, side * 0.60, 0.37, 0.24, 0.06, 0.11, bevel_w=0.012)
            _add_box(bm_interior, bx + end * 0.045, side * 0.34, 0.37, 0.045, 0.06, 0.14, bevel_w=0.008)
    _add_box(bm_plate, 1.555, 0.0, 0.39, 0.014, 0.42, 0.115, bevel_w=0.003)
    _add_box(bm_plate, -1.495, 0.0, 0.56, 0.014, 0.40, 0.11, bevel_w=0.003)

    _add_cyl(bm_rust, (-1.15, -0.34, 0.21), (-1.53, -0.38, 0.22), 0.032, 0.028, segs=10)
    _add_cyl(bm_interior, (0.48, -0.26, 0.84), (0.34, 0.04, 1.12), 0.009, 0.007, segs=5)


def _build_wheels(bm_tire, bm_chrome, bm_interior, bm_rust, xform, uv_name):
    """Build 4 wheels with cylindrical tread UV mapping (clean black sidewalls) & 8-spoke rims."""
    uv_layer = bm_tire.loops.layers.uv.get(uv_name) or bm_tire.loops.layers.uv.new(uv_name)
    for ax in (-0.96, 0.92):
        for side in (-1.0, 1.0):
            is_flat = (ax > 0.0 and side < 0.0)
            r_outer = 0.31
            r_rim = 0.195
            y_in = side * 0.45
            y_out = side * 0.65
            zc = 0.285 if is_flat else 0.31

            segs = 28
            profiles = [
                (y_in, r_rim, 0.02),
                (y_in - side * 0.015, r_outer * 0.96, 0.20),
                (y_in + (y_out - y_in) * 0.18, r_outer, 0.30),
                (y_out - (y_out - y_in) * 0.18, r_outer, 0.70),
                (y_out + side * 0.015, r_outer * 0.96, 0.80),
                (y_out, r_rim, 0.98),
            ]
            rings = []
            for py, rad, u_val in profiles:
                ring = []
                for s in range(segs):
                    ang = TAU * s / segs
                    vx = ax + rad * math.cos(ang)
                    vz = zc + rad * math.sin(ang)
                    if is_flat and vz < 0.045:
                        vz = 0.01 + 0.35 * (vz - 0.01)
                        vx += 0.025 * math.cos(ang)
                    elif vz < 0.01:
                        vz = 0.01
                    ring.append((bm_tire.verts.new((vx, py, vz)), u_val, s / segs))
                rings.append(ring)
            for k in range(len(rings) - 1):
                r0, r1 = rings[k], rings[k + 1]
                for s in range(segs):
                    j = (s + 1) % segs
                    v0, u0, t0 = r0[s]
                    v1, u1, t1 = r0[j][0], r0[j][1], (s + 1) / segs
                    v2, u2, t2 = r1[j][0], r1[j][1], (s + 1) / segs
                    v3, u3, t3 = r1[s][0], r1[s][1], s / segs
                    if side > 0:
                        f = bm_tire.faces.new((v0, v1, v2, v3))
                        uvs_f = ((u0, t0 * 3.0), (u1, t1 * 3.0), (u2, t2 * 3.0), (u3, t3 * 3.0))
                    else:
                        f = bm_tire.faces.new((v0, v3, v2, v1))
                        uvs_f = ((u0, t0 * 3.0), (u3, t3 * 3.0), (u2, t2 * 3.0), (u1, t1 * 3.0))
                    for loop, uv_pt in zip(f.loops, uvs_f):
                        loop[uv_layer].uv = uv_pt

            _add_cyl(bm_rust, (ax, y_in + side * 0.07, zc), (ax, y_in + side * 0.10, zc), 0.145, 0.145, segs=18)
            _add_cyl(bm_chrome, (ax, y_out - side * 0.06, zc), (ax, y_out - side * 0.01, zc), r_rim, r_rim, segs=20)
            _add_cyl(bm_interior, (ax, y_out - side * 0.045, zc), (ax, y_out - side * 0.025, zc), r_rim * 0.80, r_rim * 0.80, segs=16)
            for sp in range(8):
                a = TAU * sp / 8.0
                _add_box(bm_chrome, ax + 0.125 * math.cos(a), y_out - side * 0.015, zc + 0.125 * math.sin(a),
                         0.095, 0.024, 0.036, rot_y=-a, bevel_w=0.004)
            _add_cyl(bm_chrome, (ax, y_out - side * 0.01, zc), (ax, y_out + side * 0.022, zc), 0.068, 0.052, segs=14)


def _build_roof_rack_cargo(scene, xform, uv_name):
    """Build tubular roof rack + weathered wooden crate & lashed rolled tarps cleanly on roof."""
    bm_rack = bmesh.new()
    bm_crate = bmesh.new()
    bm_tarp = bmesh.new()

    rz = 1.315
    for ry in (-0.43, 0.43):
        _add_cyl(bm_rack, (-0.48, ry, rz + 0.05), (0.28, ry, rz + 0.05), 0.014, 0.014, segs=8)
        for rx in (-0.44, -0.08, 0.24):
            _add_cyl(bm_rack, (rx, ry, rz - 0.02), (rx, ry, rz + 0.055), 0.012, 0.012, segs=6)
    for rx in (-0.46, -0.22, 0.02, 0.26):
        _add_cyl(bm_rack, (rx, -0.44, rz + 0.045), (rx, 0.44, rz + 0.045), 0.012, 0.012, segs=6)

    # Small cargo crate & rolled canvas bundle strapped to rack
    _add_box(bm_crate, -0.18, 0.14, rz + 0.155, 0.44, 0.38, 0.18, rot_z=0.08, bevel_w=0.010)
    _add_cyl(bm_tarp, (-0.42, -0.22, rz + 0.135), (0.16, -0.20, rz + 0.135), 0.09, 0.09, segs=12)

    m_iron = bpy.data.materials.get('Car | Rust_Trim')
    m_wood = bpy.data.materials.get('Salt | timber') or bpy.data.materials.get('Car | Rust_Trim')
    m_linen = bpy.data.materials.get('Salt | linen') or bpy.data.materials.get('Car | Glass_Dusty')
    _emit_car_obj(scene, 'Abandoned car | Roof rack', bm_rack, m_iron, xform, uv_name, 0.8)
    _emit_car_obj(scene, 'Abandoned car | Roof crate', bm_crate, m_wood, xform, uv_name, 1.0)
    _emit_car_obj(scene, 'Abandoned car | Roof tarp bundle', bm_tarp, m_linen, xform, uv_name, 1.0)


def _build_windows(scene, xform, mat_glass, uv_name):
    """Build sloped front/rear windshields & side windows (driver front-left cracked/half-down!)."""
    def add_quad_pane(bm, p0, p1, p2, p3, thick=0.016):
        c = (Vector(p0) + Vector(p1) + Vector(p2) + Vector(p3)) * 0.25
        nrm = (Vector(p1) - Vector(p0)).cross(Vector(p2) - Vector(p0)).normalized()
        pts_f = [Vector(p) + nrm * (thick * 0.5) for p in (p0, p1, p2, p3)]
        pts_b = [Vector(p) - nrm * (thick * 0.5) for p in (p0, p1, p2, p3)]
        vf = [bm.verts.new(p) for p in pts_f]
        vb = [bm.verts.new(p) for p in pts_b]
        bm.faces.new(vf)
        bm.faces.new(list(reversed(vb)))
        for k in range(4):
            j = (k + 1) % 4
            bm.faces.new((vf[k], vb[k], vb[j], vf[j]))

    # Front windshield with dedicated wiper UV projection!
    bm_wf = bmesh.new()
    add_quad_pane(bm_wf, (0.50, -0.54, 0.80), (0.50, 0.54, 0.80), (0.29, 0.45, 1.25), (0.29, -0.45, 1.25))
    obj_wf = _emit_car_obj(scene, 'Dust covered glass | Windshield', bm_wf, mat_glass, xform, uv_name)
    if obj_wf:
        _planar_uv_rect(
            obj_wf,
            u_axis=(0.0, 1.0, 0.0),
            v_axis=(-0.22, 0.0, 1.0),
            origin=(-7.88 + 0.50, 2.89 - 0.54, 0.80),
            u_span=1.08, v_span=0.52, uv_name=uv_name,
        )

    # Rear & side windows (camera faces side = -1 => front-left door window lowered halfway!)
    bm_ws = bmesh.new()
    add_quad_pane(bm_ws, (-0.72, 0.53, 0.80), (-0.72, -0.53, 0.80), (-0.52, -0.45, 1.25), (-0.52, 0.45, 1.25))
    for side in (-1.0, 1.0):
        # Rear quarter side glass
        add_quad_pane(bm_ws, (-0.68, side * 0.57, 0.81), (-0.12, side * 0.57, 0.81),
                      (-0.12, side * 0.49, 1.25), (-0.50, side * 0.49, 1.25))
        # Front door glass: camera-side (-1) rolled down cleanly to door waist to reveal seats & wheel
        top_z = 0.86 if side < 0 else 1.25
        top_y = side * (0.56 if side < 0 else 0.49)
        top_x_f = 0.46 if side < 0 else 0.29
        add_quad_pane(bm_ws, (-0.08, side * 0.57, 0.81), (0.46, side * 0.57, 0.81),
                      (top_x_f, top_y, top_z), (-0.08, top_y, top_z))
    _emit_car_obj(scene, 'Dust covered glass | Side & Rear', bm_ws, mat_glass, xform, uv_name, 0.9)


def remaster_dust2_car(scene, tex_dir):
    """Replace coarse derelict car in dust_2.blend with high-detail multi-material PBR sedan."""
    uv_name = 'Menu surface'
    for o in scene.objects:
        if o.type == 'MESH' and o.data.uv_layers:
            uv_name = o.data.uv_layers[0].name
            break

    # 1. Remove old coarse car meshes + any previous remastered car meshes
    old_car_prefixes = (
        'Derelict courier estate', 'Chrome bumper', 'Door rub strip',
        'Dusty side glazing', 'Flaked sill', 'Plate',
        'Pressed steel rim', 'Road tyre',
        'Abandoned car', 'Dust covered glass',
    )
    for obj in list(scene.objects):
        if obj.type == 'MESH' and obj.name.startswith(old_car_prefixes):
            me = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if me and me.users == 0:
                bpy.data.meshes.remove(me)

    # 2. Create car-specific 1K/512 PBR materials
    mats = create_car_materials(tex_dir)

    # 3. Exact original anchor (-7.88, 2.89, 0.0) with subtle deflated nose-left tilt
    xform = (
        Matrix.Translation((-7.88, 2.89, 0.0)) @
        Matrix.Rotation(0.015, 4, 'X') @
        Matrix.Rotation(0.014, 4, 'Y')
    )

    bm_body = bmesh.new()
    bm_interior = bmesh.new()
    bm_rust = bmesh.new()
    bm_chrome = bmesh.new()
    bm_tire = bmesh.new()
    bm_headlamp = bmesh.new()
    bm_taillight = bmesh.new()
    bm_plate = bmesh.new()

    _build_body_shell(bm_body, bm_interior, bm_rust)
    _build_cabin_interior(bm_interior, bm_chrome, bm_rust)
    _build_front_rear_hardware(bm_chrome, bm_rust, bm_interior, bm_headlamp, bm_taillight, bm_plate)
    _build_wheels(bm_tire, bm_chrome, bm_interior, bm_rust, xform, uv_name)

    _emit_car_obj(scene, 'Abandoned car | Body shell', bm_body, mats['paint'], xform, uv_name, 1.35)
    _emit_car_obj(scene, 'Abandoned car | Interior & wells', bm_interior, mats['interior'], xform, uv_name, 0.8)
    _emit_car_obj(scene, 'Door rub strip | Trim & sills', bm_rust, mats['rust'], xform, uv_name, 0.9)
    _emit_car_obj(scene, 'Chrome bumper | Trim & rims', bm_chrome, mats['chrome'], xform, uv_name, 0.7)
    _emit_car_obj(scene, 'Flat tire | Treaded tyres', bm_tire, mats['tire'], xform, uv_name, 0.55, box_uv=False)
    _emit_car_obj(scene, 'Broken headlamp | Lenses', bm_headlamp, mats['headlamp'], xform, uv_name, 0.35)
    _emit_car_obj(scene, 'Broken headlamp | Taillights', bm_taillight, mats['taillight'], xform, uv_name, 0.35)
    obj_plate = _emit_car_obj(scene, 'Bumper | License plate', bm_plate, mats['plate'], xform, uv_name, 0.42, box_uv=False)
    if obj_plate:
        _planar_uv_rect(obj_plate, u_axis=(0.0, 1.0, 0.0), v_axis=(0.0, 0.0, 1.0),
                        origin=(-7.88, 2.89 - 0.21, 0.33), u_span=0.42, v_span=0.12, uv_name=uv_name)

    _build_windows(scene, xform, mats['glass'], uv_name)
    _build_roof_rack_cargo(scene, xform, uv_name)
