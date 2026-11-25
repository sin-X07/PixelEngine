"""High-resolution tileable PBR texture synthesis (BaseColor + ORM + OpenGL Normal)
for PixelEngine's Mirage (Indigo Caravanserai) scene.

Follows `blender-pbr-materials` and `blender-texturing` conventions:
  - BaseColor -> sRGB Image Texture -> Principled BSDF Base Color
  - ORM packed Non-Color Image -> Separate Color -> G: Roughness, B: Metallic
    (occlusionTexture is automatically exported by glTF or carried via R channel)
  - Normal -> Non-Color OpenGL Tangent-Space Normal Map -> ShaderNodeNormalMap -> Normal
  - No procedural shader-only nodes left unbaked: everything is exported natively to GLB.
"""
import math
from pathlib import Path
import bpy
import numpy as np


def _tile_noise(n, freq, seed):
    """Perfectly tileable 2D value noise via bicubic-like smoothstep grid interpolation."""
    rng = np.random.default_rng(seed)
    grid = rng.random((freq, freq), dtype=np.float32)
    coords = np.arange(n, dtype=np.float32) * (freq / n)
    i0 = np.floor(coords).astype(np.int32) % freq
    i1 = (i0 + 1) % freq
    f = coords - np.floor(coords)
    s = f * f * (3.0 - 2.0 * f)
    sy = s[:, None]
    sx = s[None, :]
    g00 = grid[i0[:, None], i0[None, :]]
    g10 = grid[i0[:, None], i1[None, :]]
    g01 = grid[i1[:, None], i0[None, :]]
    g11 = grid[i1[:, None], i1[None, :]]
    return (g00 * (1.0 - sx) + g10 * sx) * (1.0 - sy) + (g01 * (1.0 - sx) + g11 * sx) * sy


def _tile_fbm(n, freqs_weights, seed):
    acc = np.zeros((n, n), dtype=np.float32)
    wsum = 0.0
    for idx, (freq, w) in enumerate(freqs_weights):
        acc += _tile_noise(n, max(2, int(freq)), seed + idx * 97) * w
        wsum += w
    return acc / max(1e-6, wsum)


def _tile_voronoi_stones(n, cells_x, cells_y, jitter=0.36, seed=10):
    """Tileable irregular stone/cobble cells with distance-to-border mask and per-cell hash."""
    rng = np.random.default_rng(seed)
    px = rng.uniform(-jitter, jitter, (cells_y, cells_x)).astype(np.float32)
    py = rng.uniform(-jitter, jitter, (cells_y, cells_x)).astype(np.float32)
    cell_tone = rng.uniform(-1.0, 1.0, (cells_y, cells_x)).astype(np.float32)

    gy = (np.arange(n, dtype=np.float32) / n) * cells_y
    gx = (np.arange(n, dtype=np.float32) / n) * cells_x
    iy = np.floor(gy).astype(np.int32)
    ix = np.floor(gx).astype(np.int32)

    d1 = np.full((n, n), 1e6, dtype=np.float32)
    d2 = np.full((n, n), 1e6, dtype=np.float32)
    tone = np.zeros((n, n), dtype=np.float32)

    for dy in (-1, 0, 1):
        cy = (iy + dy) % cells_y
        fy = iy[:, None] + dy + 0.5
        for dx in (-1, 0, 1):
            cx = (ix + dx) % cells_x
            fx = ix[None, :] + dx + 0.5
            pts_y = fy + py[cy[:, None], cx[None, :]]
            pts_x = fx + px[cy[:, None], cx[None, :]]
            dist = np.hypot(gx[None, :] - pts_x, gy[:, None] - pts_y)
            mask1 = dist < d1
            d2 = np.where(mask1, d1, np.minimum(d2, dist))
            d1 = np.where(mask1, dist, d1)
            tone = np.where(mask1, cell_tone[cy[:, None], cx[None, :]], tone)
    edge = np.clip(d2 - d1, 0.0, 1.0)
    return edge, tone


def _height_to_normal_ao(height, strength=4.5, ao_strength=0.45):
    """Convert normalized heightfield [0..1] into OpenGL tangent-space Normal (Y+) and micro-AO."""
    # np.roll(+1/-1) centered differences along tileable boundaries
    dh_dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * strength
    dh_dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * strength
    # Image row 0 in Blender pixels array is bottom (v=0), row increasing upward (+v):
    # OpenGL normal: R = -dh/dx, G = -dh/dy (wait: standard height->normal for bottom-up buffer:
    # surface slope vector dP/du = (1, 0, dh/du), dP/dv = (0, 1, dh/dv) => N = (-dh/du, -dh/dv, 1))
    nx = -dh_dx
    ny = -dh_dy
    nz = np.ones_like(height)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz + 1e-8)
    normal = np.stack([nx * inv_len * 0.5 + 0.5, ny * inv_len * 0.5 + 0.5, nz * inv_len * 0.5 + 0.5], axis=-1)

    # Multi-radius horizon/cavity AO directly from heightfield
    avg = np.zeros_like(height)
    for r in (2, 5, 10):
        avg += 0.25 * (
            np.roll(height, r, axis=0) + np.roll(height, -r, axis=0) +
            np.roll(height, r, axis=1) + np.roll(height, -r, axis=1)
        )
    avg /= 3.0
    cavity = np.clip((avg - height) * 4.2, 0.0, 0.85)
    ao = np.clip(1.0 - cavity * ao_strength, 0.38, 1.0)
    return normal, ao


def _brick_grid(n, cols, rows, mortar_w, seed, stagger=0.5, bevel_w=0.045):
    """Generate realistic handmade adobe/ashlar brick wall heightfield, mortar mask, and per-brick tone."""
    y, x = np.mgrid[0:n, 0:n].astype(np.float32) / n
    warp_x = (_tile_fbm(n, ((8, 1.0), (24, 0.45)), seed + 1) - 0.5) * 0.018
    warp_y = (_tile_fbm(n, ((8, 1.0), (24, 0.45)), seed + 2) - 0.5) * 0.014

    row_f = (y + warp_y) * rows
    row_i = np.floor(row_f).astype(np.int32) % rows
    col_f = (x + warp_x) * cols + (row_i % 2) * stagger
    col_i = np.floor(col_f).astype(np.int32) % cols

    u_cell = col_f - np.floor(col_f)
    v_cell = row_f - np.floor(row_f)

    dx = np.minimum(u_cell, 1.0 - u_cell)
    dy = np.minimum(v_cell, 1.0 - v_cell) * (cols / max(1.0, rows))
    edge_dist = np.minimum(dx, dy)

    rng = np.random.default_rng(seed + 17)
    brick_hash = rng.uniform(-1.0, 1.0, (rows, cols)).astype(np.float32)
    per_brick = brick_hash[row_i, col_i]

    mortar = 1.0 - np.clip((edge_dist - mortar_w) / max(1e-4, bevel_w), 0.0, 1.0)
    brick_face = 1.0 - mortar
    return brick_face, mortar, per_brick, edge_dist


def _synth_pbr_maps(key, n=1024, seed=42):
    """Return dict(base=HWC float32 sRGB, orm=HWC float32 linear, normal=HWC float32 linear, normal_scale=float)."""
    micro = _tile_noise(n, min(256, n // 4), seed + 301)
    fine = _tile_fbm(n, ((16, 0.5), (48, 0.35), (128, 0.15)), seed + 101)
    broad = _tile_fbm(n, ((4, 0.55), (10, 0.30), (24, 0.15)), seed + 201)
    y, x = np.mgrid[0:n, 0:n].astype(np.float32) / n

    metallic_val = 0.0
    normal_strength = 4.2
    normal_scale = 1.0

    if key in ('rose', 'stucco', 'Backdrop wall'):
        # Sun-baked mudbrick + flaking lime/rose plaster with crisp brick joints peeking through
        brick_face, mortar, per_brick, _ = _brick_grid(n, cols=5, rows=10, mortar_w=0.022, seed=seed, bevel_w=0.042)
        brick_tex = (brick_face * (0.68 + 0.14 * per_brick + 0.18 * fine)) - mortar * 0.28

        # Eroded lime plaster patches leaving authentic exposed adobe masonry patches
        plaster_noise = broad * 0.62 + fine * 0.28 + micro * 0.10
        peel_mask = np.clip((plaster_noise - 0.58) * 7.5, 0.0, 1.0)  # 1 = exposed brick, 0 = stucco coat
        stucco_h = 0.72 + 0.14 * fine + 0.08 * micro - mortar * 0.045 * (1.0 - peel_mask)
        brick_h = 0.34 + brick_tex * 0.42
        height = stucco_h * (1.0 - peel_mask) + brick_h * peel_mask

        # Subtle vertical rain-dust streaks and salt efflorescence near bottom seams
        streak = _tile_fbm(n, ((16, 0.7), (48, 0.3)), seed + 55)
        streak = 0.94 + 0.09 * streak

        if key == 'rose':
            plaster_col = np.array([0.81, 0.49, 0.38], dtype=np.float32)
            under_brick = np.array([0.64, 0.38, 0.27], dtype=np.float32)
            mortar_col = np.array([0.74, 0.65, 0.53], dtype=np.float32)
        elif key == 'stucco':
            plaster_col = np.array([0.83, 0.66, 0.44], dtype=np.float32)
            under_brick = np.array([0.68, 0.50, 0.33], dtype=np.float32)
            mortar_col = np.array([0.77, 0.69, 0.55], dtype=np.float32)
        else:
            plaster_col = np.array([0.80, 0.65, 0.47], dtype=np.float32)
            under_brick = np.array([0.66, 0.50, 0.34], dtype=np.float32)
            mortar_col = np.array([0.73, 0.64, 0.51], dtype=np.float32)

        stucco_rgb = plaster_col * (0.86 + 0.18 * broad + 0.10 * fine + 0.07 * micro)[:, :, None] * streak[:, :, None]
        brick_rgb = under_brick * (0.78 + 0.16 * per_brick + 0.22 * fine + 0.10 * micro)[:, :, None]
        brick_rgb = brick_rgb * (1.0 - mortar[:, :, None]) + mortar_col[None, None, :] * (0.86 + 0.18 * fine)[:, :, None] * mortar[:, :, None]
        rgb = stucco_rgb * (1.0 - peel_mask[:, :, None]) + brick_rgb * peel_mask[:, :, None]
        rough = np.clip(0.84 + 0.11 * (peel_mask * 0.5 + mortar * 0.4 + (micro - 0.5) * 0.45), 0.55, 0.98)
        normal_strength = 5.4
        normal_scale = 0.92

    elif key in ('ivory', 'Backdrop trim', 'Backdrop stone'):
        # Dressed limestone blocks with chisel grooves, ashlar joints & pitted grain
        brick_face, mortar, per_brick, _ = _brick_grid(n, cols=3, rows=6, mortar_w=0.018, seed=seed, bevel_w=0.032)
        chisel = 0.5 + 0.5 * np.sin((x * 48.0 + y * 14.0 + fine * 3.5) * math.tau)
        pitting = np.clip((0.32 - fine) * 3.5, 0.0, 1.0) * 0.25
        height = brick_face * (0.72 + 0.14 * broad + 0.08 * micro + 0.05 * chisel - pitting) + mortar * 0.18

        base_stone = np.array([0.89, 0.84, 0.72], dtype=np.float32)
        rgb = base_stone * (0.84 + 0.09 * per_brick + 0.13 * broad + 0.08 * fine + 0.05 * micro - 0.14 * pitting)[:, :, None]
        rgb *= (1.0 - 0.34 * mortar)[:, :, None]
        # Warm sand settled into stone seams
        rgb += np.array([0.10, 0.08, 0.05], dtype=np.float32) * (mortar * (0.5 + 0.5 * fine))[:, :, None]
        rough = np.clip(0.76 + 0.18 * mortar + 0.12 * (micro - 0.5) + 0.10 * pitting, 0.48, 0.97)
        normal_strength = 4.6
        normal_scale = 0.88

    elif key in ('tile', 'indigo', 'Backdrop roof', 'water'):
        if key == 'water':
            rip1 = np.sin((x * 6.0 + y * 2.0 + fine * 1.4) * math.tau) * 0.5 + 0.5
            rip2 = np.cos((x * 3.0 - y * 7.0 + broad * 1.8) * math.tau) * 0.5 + 0.5
            height = 0.5 + 0.25 * (rip1 * rip2)
            rgb = np.array([0.10, 0.42, 0.52], dtype=np.float32) * (0.88 + 0.22 * rip1)[:, :, None]
            rough = np.clip(0.08 + 0.08 * fine, 0.04, 0.22)
            normal_strength = 2.2
            normal_scale = 0.55
        elif key == 'tile' or key == 'Backdrop roof':
            # Hand-glazed Moroccan/Islamic geometric zellige tilework with crisp grout seams & crackle sheen
            bf, mortar, per_t, _ = _brick_grid(n, cols=8, rows=8, mortar_w=0.032, seed=seed, stagger=0.0, bevel_w=0.04)
            # Subtle 45-degree sub-diamond grout accent
            diag = np.minimum(np.abs(((x * 8.0 + y * 8.0) % 1.0) - 0.5), np.abs(((x * 8.0 - y * 8.0) % 1.0) - 0.5))
            sub_grout = 1.0 - np.clip((diag - 0.02) * 38.0, 0.0, 1.0)
            grout = np.maximum(mortar, sub_grout * 0.45)
            orange_peel = _tile_fbm(n, ((32, 0.6), (96, 0.4)), seed + 12)
            height = (1.0 - grout) * (0.76 + 0.06 * orange_peel + 0.07 * per_t) + grout * 0.14
            base_tile = np.array([0.14, 0.52, 0.62], dtype=np.float32) if key == 'tile' else np.array([0.17, 0.46, 0.58], dtype=np.float32)
            rgb = base_tile * (0.85 + 0.14 * per_t + 0.10 * broad + 0.06 * micro)[:, :, None]
            rgb = rgb * (1.0 - grout[:, :, None]) + np.array([0.74, 0.71, 0.64], dtype=np.float32) * (0.88 + 0.12 * micro)[:, :, None] * grout[:, :, None]
            rough = np.clip(0.22 * (1.0 - grout) + 0.88 * grout + 0.06 * (orange_peel - 0.5), 0.14, 0.94)
            normal_strength = 4.8
            normal_scale = 0.88
        else:  # indigo (painted cedar doors / deep cobalt glazed trim)
            grain = 0.5 + 0.5 * np.sin((x * 26.0 + 3.5 * broad + 1.2 * fine) * math.tau)
            plank_seam = 1.0 - np.clip(np.abs(((x * 6.0) % 1.0) - 0.5) * 42.0, 0.0, 1.0)
            height = 0.68 + 0.14 * grain + 0.08 * fine - 0.42 * plank_seam
            base_ind = np.array([0.12, 0.30, 0.56], dtype=np.float32)
            wear = np.clip((broad + fine * 0.4 - 0.72) * 3.0, 0.0, 0.45)
            rgb = base_ind * (0.82 + 0.16 * grain + 0.14 * fine)[:, :, None] * (1.0 - 0.35 * plank_seam)[:, :, None]
            rgb = rgb * (1.0 - wear[:, :, None]) + np.array([0.38, 0.30, 0.22], dtype=np.float32) * wear[:, :, None]
            rough = np.clip(0.46 + 0.25 * plank_seam + 0.20 * wear + 0.10 * (micro - 0.5), 0.28, 0.92)
            normal_strength = 4.2
            normal_scale = 0.82

    elif key in ('bark', 'Backdrop wood', 'Backdrop recess'):
        # Aged desert cedar timber: longitudinal grain fibers, wood plank joints & knots
        plank_seam = 1.0 - np.clip(np.abs(((x * 5.0 + 0.015 * broad) % 1.0) - 0.5) * 38.0, 0.0, 1.0)
        fibers = 0.5 + 0.5 * np.sin((x * 46.0 + 6.5 * broad + 2.2 * fine + 0.25 * np.sin(y * 12.0)) * math.tau)
        ring = 0.5 + 0.5 * np.sin((x * 16.0 + 9.0 * broad) * math.tau)
        height = 0.65 + 0.16 * fibers + 0.10 * ring + 0.06 * micro - 0.48 * plank_seam
        base_wood = np.array([0.38, 0.24, 0.15], dtype=np.float32)
        if key == 'Backdrop recess':
            base_wood *= 0.65
        dust = np.clip((broad - 0.64) * 2.2, 0.0, 0.32)[:, :, None]
        rgb = base_wood * (0.72 + 0.22 * fibers + 0.14 * ring + 0.08 * micro)[:, :, None] * (1.0 - 0.45 * plank_seam)[:, :, None]
        rgb = rgb * (1.0 - dust) + np.array([0.68, 0.56, 0.42], dtype=np.float32) * dust
        rough = np.clip(0.76 + 0.12 * (1.0 - ring) + 0.10 * (micro - 0.5), 0.54, 0.96)
        normal_strength = 5.0
        normal_scale = 0.90

    elif key == 'clay':
        # Hand-thrown terracotta pottery: subtle wheel throwing rings & mineral grog grit
        wheel = 0.5 + 0.5 * np.sin((y * 28.0 + 0.6 * fine) * math.tau)
        height = 0.60 + 0.14 * wheel + 0.12 * micro + 0.08 * broad
        base_clay = np.array([0.71, 0.36, 0.22], dtype=np.float32)
        scorch = np.clip((broad - 0.58) * 2.0, 0.0, 0.35)[:, :, None]
        rgb = base_clay * (0.82 + 0.14 * broad + 0.08 * wheel + 0.08 * micro)[:, :, None]
        rgb *= (1.0 - 0.22 * scorch)
        rough = np.clip(0.68 + 0.16 * fine + 0.10 * (micro - 0.5), 0.44, 0.92)
        normal_strength = 3.2
        normal_scale = 0.72

    elif key in ('cloth', 'Backdrop awning', 'sail_indigo', 'sail_ivory', 'robe_gold', 'robe_rose'):
        # Fine woven desert linen / wool weave (tight subtle grain, no coarse grid)
        warp = 0.5 + 0.5 * np.cos(x * 192.0 * math.tau)
        weft = 0.5 + 0.5 * np.cos(y * 192.0 * math.tau)
        weave = 0.5 * warp + 0.5 * weft
        height = 0.55 + 0.14 * weave + 0.12 * broad + 0.05 * micro
        color_map = {
            'cloth': np.array([0.64, 0.22, 0.19], dtype=np.float32),
            'Backdrop awning': np.array([0.74, 0.56, 0.38], dtype=np.float32),
            'sail_indigo': np.array([0.13, 0.33, 0.60], dtype=np.float32),
            'sail_ivory': np.array([0.87, 0.82, 0.71], dtype=np.float32),
            'robe_gold': np.array([0.78, 0.60, 0.25], dtype=np.float32),
            'robe_rose': np.array([0.70, 0.34, 0.26], dtype=np.float32),
        }
        base_col = color_map[key]
        sun_fade = np.clip((broad - 0.52) * 1.4, 0.0, 0.22)[:, :, None]
        rgb = base_col * (0.87 + 0.09 * weave + 0.09 * fine)[:, :, None]
        rgb = rgb * (1.0 - sun_fade) + np.array([0.84, 0.76, 0.64], dtype=np.float32) * sun_fade
        rough = np.clip(0.88 + 0.07 * (micro - 0.5), 0.76, 0.98)
        normal_strength = 1.6
        normal_scale = 0.45

    elif key == 'skin':
        height = 0.55 + 0.06 * fine + 0.04 * micro
        rgb = np.array([0.64, 0.41, 0.28], dtype=np.float32) * (0.90 + 0.12 * broad + 0.05 * micro)[:, :, None]
        rough = np.clip(0.68 + 0.08 * (micro - 0.5), 0.54, 0.84)
        normal_strength = 1.6
        normal_scale = 0.40

    elif key in ('brass', 'gold'):
        # Hammered artisan brass / copper-gold with patina inside dimples
        cell_edge, cell_tone = _tile_voronoi_stones(n, 22, 22, jitter=0.32, seed=seed + 19)
        hammer = np.clip(cell_edge * 1.8, 0.0, 1.0)
        height = 0.60 + 0.26 * hammer + 0.06 * fine
        metallic_val = 0.92
        base_m = np.array([0.78, 0.60, 0.30], dtype=np.float32) if key == 'brass' else np.array([0.85, 0.67, 0.32], dtype=np.float32)
        patina = np.clip((0.55 - broad) * 2.2, 0.0, 0.35)[:, :, None]
        rgb = base_m * (0.86 + 0.14 * hammer + 0.08 * cell_tone)[:, :, None]
        rgb = rgb * (1.0 - patina) + np.array([0.36, 0.38, 0.24], dtype=np.float32) * patina
        rough = np.clip(0.28 + 0.22 * (1.0 - hammer) + 0.25 * patina[:, :, 0] + 0.08 * (micro - 0.5), 0.18, 0.78)
        normal_strength = 3.2
        normal_scale = 0.70

    elif key == 'leaf':
        # Palm & agave botanical veining + fibrous leaf surface
        vein = 0.5 + 0.5 * np.sin((x * 54.0 + 0.4 * fine) * math.tau)
        height = 0.55 + 0.22 * vein + 0.12 * fine + 0.08 * broad
        base_g = np.array([0.27, 0.43, 0.22], dtype=np.float32)
        tip_sun = np.clip((y - 0.65) * 2.4 + (broad - 0.5) * 0.6, 0.0, 0.40)[:, :, None]
        rgb = base_g * (0.82 + 0.16 * vein + 0.12 * broad)[:, :, None]
        rgb = rgb * (1.0 - tip_sun) + np.array([0.48, 0.46, 0.24], dtype=np.float32) * tip_sun
        rough = np.clip(0.58 + 0.16 * tip_sun[:, :, 0] + 0.12 * (micro - 0.5), 0.38, 0.86)
        normal_strength = 2.8
        normal_scale = 0.68

    else:  # 'deep' / fallback
        height = 0.5 + 0.12 * fine + 0.08 * micro
        rgb = np.array([0.14, 0.12, 0.11], dtype=np.float32) * (0.85 + 0.20 * fine)[:, :, None]
        rough = np.clip(0.92 + 0.05 * (micro - 0.5), 0.80, 0.99)
        normal_strength = 2.0
        normal_scale = 0.40

    normal, ao = _height_to_normal_ao(height, strength=normal_strength, ao_strength=0.48)
    orm = np.ones((n, n, 4), dtype=np.float32)
    orm[:, :, 0] = ao
    orm[:, :, 1] = rough
    orm[:, :, 2] = np.full((n, n), metallic_val, dtype=np.float32)

    base = np.ones((n, n, 4), dtype=np.float32)
    base[:, :, :3] = np.clip(rgb, 0.015, 0.96)
    n_rgba = np.ones((n, n, 4), dtype=np.float32)
    n_rgba[:, :, :3] = np.clip(normal, 0.0, 1.0)
    return {'base': base, 'orm': orm, 'normal': n_rgba, 'normal_scale': normal_scale, 'metallic': metallic_val}


def _upgrade_floor_pbr(material, tex_dir, size=1536):
    """Upscale and enrich Mirage courtyard floor to 1536x1536 with sharp stone joints & normal relief."""
    tree = material.node_tree
    bsdf = next(n for n in tree.nodes if n.type == 'BSDF_PRINCIPLED')
    old_img = bsdf.inputs['Base Color'].links[0].from_node.image
    ow, oh = old_img.size
    old_px = np.empty((oh, ow, 4), dtype=np.float32)
    old_img.pixels.foreach_get(old_px.ravel())

    # Smooth bilinear upscale of existing painted floor pattern (keeps survey seal + processional bands!)
    yy = (np.arange(size, dtype=np.float32) + 0.5) * (oh / size) - 0.5
    xx = (np.arange(size, dtype=np.float32) + 0.5) * (ow / size) - 0.5
    y0 = np.clip(np.floor(yy).astype(np.int32), 0, oh - 1)
    x0 = np.clip(np.floor(xx).astype(np.int32), 0, ow - 1)
    y1 = np.clip(y0 + 1, 0, oh - 1)
    x1 = np.clip(x0 + 1, 0, ow - 1)
    fy = (yy - np.floor(yy))[:, None, None]
    fx = (xx - np.floor(xx))[None, :, None]
    base_rgb = (
        old_px[y0[:, None], x0[None, :], :3] * (1.0 - fx) * (1.0 - fy) +
        old_px[y0[:, None], x1[None, :], :3] * fx * (1.0 - fy) +
        old_px[y1[:, None], x0[None, :], :3] * (1.0 - fx) * fy +
        old_px[y1[:, None], x1[None, :], :3] * fx * fy
    )

    # Reconstruct exact paving slabs matching courtyards.py coordinate space (x=u*24-12, y=v*24-9)
    v, u = np.mgrid[0:size, 0:size].astype(np.float32) / size
    fleck = _tile_fbm(size, ((18, 0.5), (56, 0.35), (160, 0.15)), seed=777)
    grain = _tile_noise(size, 512, seed=778)
    x = u * 24.0 - 12.0
    y = v * 24.0 - 9.0
    row = np.floor(y / 0.83)
    ix = (x / 0.99 + (row % 2) * 0.5 + 0.020 * (fleck - 0.5)) % 1.0
    iy = (y / 0.83 + 0.016 * (fleck - 0.5)) % 1.0
    gap = np.minimum(np.minimum(ix, 1.0 - ix), np.minimum(iy, 1.0 - iy))

    joint_recess = 1.0 - np.clip((gap - 0.012) / 0.026, 0.0, 1.0)
    # Stone micro-relief + drifted sand dust in mortar crevices
    slab_variation = 0.06 * np.sin(row * 73.0 + np.floor(x / 0.99 + (row % 2) * 0.5) * 19.0)
    height = (1.0 - joint_recess) * (0.72 + 0.10 * fleck + 0.06 * grain + slab_variation) + joint_recess * 0.12

    # Crisp stone grain & subtle bevel highlight into upscaled floor color
    base_rgb = base_rgb * (0.94 + 0.10 * fleck + 0.06 * grain)[:, :, None]
    base_rgb = base_rgb * (1.0 - 0.24 * joint_recess[:, :, None]) + np.array([0.52, 0.45, 0.35], dtype=np.float32) * (0.24 * joint_recess[:, :, None])

    normal, ao = _height_to_normal_ao(height, strength=6.2, ao_strength=0.52)
    rough = np.clip(0.70 + 0.22 * joint_recess + 0.12 * (fleck - 0.5) + 0.08 * (grain - 0.5), 0.44, 0.96)

    base = np.ones((size, size, 4), dtype=np.float32)
    base[:, :, :3] = np.clip(base_rgb, 0.015, 0.96)
    orm = np.ones((size, size, 4), dtype=np.float32)
    orm[:, :, 0] = ao
    orm[:, :, 1] = rough
    orm[:, :, 2] = 0.0
    n_rgba = np.ones((size, size, 4), dtype=np.float32)
    n_rgba[:, :, :3] = np.clip(normal, 0.0, 1.0)
    _apply_maps_to_material(material, {'base': base, 'orm': orm, 'normal': n_rgba, 'normal_scale': 0.92, 'metallic': 0.0}, tex_dir)


def _new_or_replace_image(name, arr, path, colorspace):
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old, do_unlink=True)
    h, w, _ = arr.shape
    is_data = (colorspace != 'sRGB')
    img = bpy.data.images.new(name, width=w, height=h, alpha=False, is_data=is_data)
    img.colorspace_settings.name = colorspace
    img.pixels.foreach_set(arr.ravel())
    img.file_format = 'PNG'
    img.filepath_raw = str(path)
    img.save()
    img.pack()
    return img


def _apply_maps_to_material(material, maps, tex_dir):
    tree = material.node_tree
    bsdf = next(n for n in tree.nodes if n.type == 'BSDF_PRINCIPLED')
    for node in list(tree.nodes):
        if node != bsdf and node.type != 'OUTPUT_MATERIAL':
            tree.nodes.remove(node)

    safe = material.name.replace(' | ', '_').replace(' ', '_')
    img_base = _new_or_replace_image(material.name + ' Base1K', maps['base'], tex_dir / f'{safe}_base.png', 'sRGB')
    img_orm = _new_or_replace_image(material.name + ' ORM1K', maps['orm'], tex_dir / f'{safe}_orm.png', 'Non-Color')
    img_nrm = _new_or_replace_image(material.name + ' NRM1K', maps['normal'], tex_dir / f'{safe}_nrm.png', 'Non-Color')

    t_base = tree.nodes.new('ShaderNodeTexImage')
    t_base.image = img_base
    tree.links.new(t_base.outputs['Color'], bsdf.inputs['Base Color'])

    t_orm = tree.nodes.new('ShaderNodeTexImage')
    t_orm.image = img_orm
    split = tree.nodes.new('ShaderNodeSeparateColor')
    tree.links.new(t_orm.outputs['Color'], split.inputs['Color'])
    tree.links.new(split.outputs['Green'], bsdf.inputs['Roughness'])
    tree.links.new(split.outputs['Blue'], bsdf.inputs['Metallic'])

    t_nrm = tree.nodes.new('ShaderNodeTexImage')
    t_nrm.image = img_nrm
    nrm_node = tree.nodes.new('ShaderNodeNormalMap')
    nrm_node.inputs['Strength'].default_value = maps['normal_scale']
    tree.links.new(t_nrm.outputs['Color'], nrm_node.inputs['Color'])
    tree.links.new(nrm_node.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Metallic'].default_value = maps['metallic']


def upgrade_mirage_materials(scene, tex_dir):
    """Upgrade every Mirage material to 1K/512 tileable BaseColor + ORM + Normal maps."""
    tex_dir = Path(tex_dir)
    tex_dir.mkdir(parents=True, exist_ok=True)
    prefix = scene.get('material_prefix', 'Indigo Caravanserai | ')

    # Ensure accent & dedicated fabric/skin materials exist (avoiding stone textures on sails/clothes)
    for extra_key in (
        'brass', 'gold', 'clay', 'indigo', 'leaf', 'rose', 'stucco', 'ivory', 'tile',
        'bark', 'cloth', 'water', 'sail_indigo', 'sail_ivory', 'robe_gold', 'robe_rose', 'skin'
    ):
        full = prefix + extra_key
        if not bpy.data.materials.get(full):
            m = bpy.data.materials.new(full)
            m.use_nodes = True

    hero_1k_keys = {'rose', 'stucco', 'ivory', 'tile', 'bark', 'clay', 'indigo', 'cloth'}
    for idx, mat in enumerate(list(bpy.data.materials)):
        if mat.name.startswith(('Sunlit dust', 'Dot', 'Wire')):
            continue
        if not mat.node_tree:
            continue
        key = mat.name.split(' | ')[-1]
        if key == 'floor':
            _upgrade_floor_pbr(mat, tex_dir, size=1024)
            continue
        if key == 'Backdrop trim':
            key_lookup = 'ivory'
        elif key == 'Backdrop wood':
            key_lookup = 'bark'
        elif key == 'Backdrop wall':
            key_lookup = 'stucco'
        elif key == 'Backdrop roof':
            key_lookup = 'tile'
        elif key == 'Backdrop awning':
            key_lookup = 'cloth'
        elif key == 'Backdrop recess':
            key_lookup = 'Backdrop recess'
        else:
            key_lookup = key
        res = 1024 if key in hero_1k_keys else 512
        maps = _synth_pbr_maps(key_lookup, n=res, seed=400 + idx * 13)
        _apply_maps_to_material(mat, maps, tex_dir)
