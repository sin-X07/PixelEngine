"""High-resolution tileable PBR texture synthesis (BaseColor + ORM + OpenGL Normal)
for PixelEngine's Ancient Ruins (Observatory of Rain) scene.

Reuses seamless noise/masonry/normal generators from `mirage_pbr.py` and upgrades
all `Observatory of Rain | *` stone, basalt, moss, carved relief, jade, gold, clay,
bark, and foliage materials to crisp 1024/512 PBR texture sets with normal & cavity AO.
"""
from pathlib import Path
import bpy
import numpy as np

from mirage_pbr import (
    _tile_noise,
    _tile_fbm,
    _tile_voronoi_stones,
    _brick_grid,
    _height_to_normal_ao,
    _apply_maps_to_material,
)


def _limestone_ashlar_maps(n=1024):
    """Weathered Mayan rainforest carved ashlar blocks: organic warped mortar, mossy joints & rain streaks."""
    face, mortar, per_brick, edge = _brick_grid(
        n, cols=5, rows=8, mortar_w=0.058, seed=101, stagger=0.5, bevel_w=0.055
    )
    # Organic ancient stonework erosion along block joints (break ruler-straight grid feel)
    warp_f = _tile_fbm(n, [(6, 0.55), (16, 0.30), (34, 0.15)], seed=108)
    mortar = np.clip(mortar + (0.5 - edge) * (warp_f - 0.45) * 1.4, 0.0, 1.0)
    macro = _tile_fbm(n, [(4, 0.50), (10, 0.30), (22, 0.20)], seed=102)
    micro = _tile_fbm(n, [(32, 0.45), (64, 0.35), (128, 0.20)], seed=103)
    pit_edge, _ = _tile_voronoi_stones(n, 24, 24, jitter=0.42, seed=104)
    pitting = np.clip((0.12 - pit_edge) * 8.0, 0.0, 1.0) * (macro > 0.42)

    # Vertical tropical rain runoff & humidity dark streaks
    rain_col = _tile_noise(n, 20, 105)
    rain_streak = np.clip(
        _tile_fbm(n, [(12, 0.5), (28, 0.35), (56, 0.15)], seed=106) * 0.55
        + rain_col * 0.45,
        0.0,
        1.0,
    )
    # Damp mossy colonization inside joints, ledges and weathered patches
    moss_noise = _tile_fbm(n, [(5, 0.45), (13, 0.35), (30, 0.20)], seed=107)
    moss_mask = np.clip(
        mortar * 0.80 + (1.0 - np.clip(edge * 3.8, 0.0, 1.0)) * 0.42 + (moss_noise - 0.52) * 1.6,
        0.0,
        1.0,
    ) * (moss_noise > 0.34)

    block_dome = np.sin(np.clip(edge, 0.0, 1.0) * (np.pi * 0.5)) ** 0.55
    height = (
        0.58 * face * (0.86 + 0.14 * block_dome)
        + 0.14 * micro
        + 0.10 * macro
        - 0.24 * mortar
        - 0.10 * pitting
        + 0.04 * moss_mask * micro
    )
    height = np.clip((height - height.min()) / max(1e-6, height.max() - height.min()), 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=6.0, ao_strength=0.62)

    # Color: cool rainforest limestone with subtle green/slate tint + emerald moss crevices
    stone_l = np.array([0.33, 0.36, 0.31], dtype=np.float32)
    stone_d = np.array([0.21, 0.24, 0.20], dtype=np.float32)
    mortar_c = np.array([0.11, 0.13, 0.10], dtype=np.float32)
    moss_c = np.array([0.13, 0.24, 0.10], dtype=np.float32)

    t = np.clip(0.5 + 0.24 * per_brick + 0.26 * (macro - 0.5) + 0.16 * (micro - 0.5), 0.0, 1.0)
    base = stone_d[None, None, :] * (1.0 - t[..., None]) + stone_l[None, None, :] * t[..., None]
    base = base * (1.0 - mortar[..., None] * 0.82) + mortar_c[None, None, :] * (mortar[..., None] * 0.82)
    base = base * (1.0 - moss_mask[..., None] * 0.58) + moss_c[None, None, :] * (moss_mask[..., None] * 0.58)
    # Darken damp rain channels & pitted stone pores
    wetness = np.clip((rain_streak - 0.45) * 1.4, 0.0, 0.55)
    base *= (1.0 - 0.22 * wetness[..., None] - 0.16 * pitting[..., None])
    base *= (0.72 + 0.28 * ao[..., None])

    # Wet rainforest stone: slicker on rained streaks & raised rock polish, rougher in moss/mortar
    rough = np.clip(
        0.58 + 0.24 * mortar + 0.20 * moss_mask + 0.12 * pitting - 0.26 * wetness - 0.10 * (micro - 0.5),
        0.24,
        0.95,
    )
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.92), 'orm': orm, 'normal': normal, 'normal_scale': 1.25, 'metallic': 0.0}


def _basalt_maps(n=1024):
    """Dark volcanic carved basalt: dense granular grain, chiseled grooves, slick wet highlights."""
    edge, tone = _tile_voronoi_stones(n, 7, 7, jitter=0.30, seed=201)
    mortar = np.clip(1.0 - edge / 0.065, 0.0, 1.0)
    micro = _tile_fbm(n, [(36, 0.45), (72, 0.35), (144, 0.20)], seed=202)
    vein = _tile_fbm(n, [(6, 0.5), (16, 0.35), (30, 0.15)], seed=203)
    speckle = _tile_noise(n, 160, 204)

    height = np.clip(0.64 + 0.15 * micro + 0.10 * vein - 0.28 * mortar + 0.05 * speckle, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=5.4, ao_strength=0.58)

    col_d = np.array([0.075, 0.088, 0.095], dtype=np.float32)
    col_l = np.array([0.165, 0.185, 0.192], dtype=np.float32)
    t = np.clip(0.5 + 0.22 * tone + 0.28 * (vein - 0.5) + 0.14 * (micro - 0.5), 0.0, 1.0)
    base = col_d[None, None, :] * (1.0 - t[..., None]) + col_l[None, None, :] * t[..., None]
    base *= (1.0 - 0.42 * mortar[..., None]) * (0.70 + 0.30 * ao[..., None])

    wet_patch = np.clip((vein - 0.42) * 1.5, 0.0, 0.55)
    rough = np.clip(0.46 + 0.28 * mortar - 0.22 * wet_patch + 0.10 * (micro - 0.5), 0.18, 0.90)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.85), 'orm': orm, 'normal': normal, 'normal_scale': 1.15, 'metallic': 0.0}


def _carved_relief_stone_maps(n=1024):
    """Mayan stepped fret / glyph carved relief cornice & frieze blocks."""
    # Stepped geometric fret grid (4x4 tiles per repeat)
    gy, gx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    u = (gx * 4.0) % 1.0
    v = (gy * 4.0) % 1.0
    # Outer sunken frame + inner nested spiral/diamond relief boss
    border = (np.minimum(np.minimum(u, 1.0 - u), np.minimum(v, 1.0 - v)) > 0.10).astype(np.float32)
    inner_ring = (
        (np.abs(u - 0.5) < 0.30) & (np.abs(v - 0.5) < 0.30) &
        ((np.abs(u - 0.5) > 0.20) | (np.abs(v - 0.5) > 0.20))
    ).astype(np.float32)
    center_glyph = ((np.abs(u - 0.5) + np.abs(v - 0.5)) < 0.16).astype(np.float32)

    relief = np.clip(0.32 + 0.28 * border + 0.22 * inner_ring + 0.24 * center_glyph, 0.0, 1.0)
    micro = _tile_fbm(n, [(24, 0.4), (56, 0.35), (120, 0.25)], seed=301)
    weath = _tile_fbm(n, [(6, 0.55), (14, 0.45)], seed=302)
    moss = np.clip((1.0 - border) * 0.65 + (weath - 0.58) * 1.5, 0.0, 1.0) * (weath > 0.40)

    height = np.clip(relief * 0.72 + 0.16 * micro + 0.08 * weath, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=6.8, ao_strength=0.66)

    c_high = np.array([0.36, 0.38, 0.32], dtype=np.float32)
    c_low = np.array([0.16, 0.18, 0.15], dtype=np.float32)
    c_moss = np.array([0.11, 0.22, 0.09], dtype=np.float32)

    base = c_low[None, None, :] * (1.0 - relief[..., None]) + c_high[None, None, :] * relief[..., None]
    base *= (0.85 + 0.25 * micro[..., None])
    base = base * (1.0 - moss[..., None] * 0.55) + c_moss[None, None, :] * (moss[..., None] * 0.55)
    base *= (0.68 + 0.32 * ao[..., None])

    rough = np.clip(0.56 + 0.25 * moss - 0.16 * center_glyph + 0.10 * (micro - 0.5), 0.22, 0.94)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.90), 'orm': orm, 'normal': normal, 'normal_scale': 1.35, 'metallic': 0.0}


def _lichen_moss_maps(n=1024):
    """Lush, spongey jungle moss & lichen mat over damp humus & rock pockets."""
    macro = _tile_fbm(n, [(5, 0.45), (12, 0.35), (28, 0.20)], seed=401)
    tuft_edge, tuft_tone = _tile_voronoi_stones(n, 26, 26, jitter=0.45, seed=402)
    micro = _tile_fbm(n, [(48, 0.45), (96, 0.35), (180, 0.20)], seed=403)

    clumps = np.clip(tuft_edge * 2.4, 0.0, 1.0)
    height = np.clip(0.35 + 0.30 * macro + 0.20 * clumps + 0.18 * micro, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=5.8, ao_strength=0.60)

    c_dark = np.array([0.075, 0.145, 0.055], dtype=np.float32)
    c_mid = np.array([0.145, 0.265, 0.088], dtype=np.float32)
    c_tip = np.array([0.235, 0.350, 0.125], dtype=np.float32)

    t = np.clip(0.5 * macro + 0.35 * clumps + 0.15 * tuft_tone + 0.15 * (micro - 0.5), 0.0, 1.0)
    base = np.where(
        t[..., None] < 0.5,
        c_dark[None, None, :] * (1.0 - t[..., None] * 2.0) + c_mid[None, None, :] * (t[..., None] * 2.0),
        c_mid[None, None, :] * (1.0 - (t[..., None] - 0.5) * 2.0) + c_tip[None, None, :] * ((t[..., None] - 0.5) * 2.0),
    )
    base *= (0.72 + 0.28 * ao[..., None])
    rough = np.clip(0.84 - 0.18 * clumps + 0.08 * (micro - 0.5), 0.58, 0.96)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.90), 'orm': orm, 'normal': normal, 'normal_scale': 1.25, 'metallic': 0.0}


def _courtyard_floor_maps(n=1024):
    """High-res Mayan courtyard limestone pavers with fine-scale blocks, moss seams & rain puddles."""
    face, mortar, per_brick, edge = _brick_grid(
        n, cols=18, rows=22, mortar_w=0.052, seed=501, stagger=0.5, bevel_w=0.045
    )
    macro = _tile_fbm(n, [(4, 0.50), (10, 0.30), (22, 0.20)], seed=502)
    micro = _tile_fbm(n, [(32, 0.45), (70, 0.35), (140, 0.20)], seed=503)
    moss_seed = _tile_fbm(n, [(8, 0.5), (20, 0.5)], seed=504)
    moss = np.clip(mortar * 0.9 + (moss_seed - 0.62) * 1.4, 0.0, 1.0) * (moss_seed > 0.44)
    puddle = np.clip((0.46 - macro) * 3.8, 0.0, 1.0) * face

    height = np.clip(
        0.58 * face + 0.14 * micro + 0.08 * macro - 0.24 * mortar - 0.05 * puddle,
        0.0,
        1.0,
    )
    normal, ao = _height_to_normal_ao(height, strength=5.4, ao_strength=0.58)
    # Flatten normals inside standing rainwater puddles
    flat_n = np.array([0.5, 0.5, 1.0], dtype=np.float32)[None, None, :]
    normal = normal * (1.0 - puddle[..., None] * 0.85) + flat_n * (puddle[..., None] * 0.85)

    paver_d = np.array([0.20, 0.23, 0.19], dtype=np.float32)
    paver_l = np.array([0.33, 0.36, 0.30], dtype=np.float32)
    moss_c = np.array([0.11, 0.21, 0.085], dtype=np.float32)

    t = np.clip(0.5 + 0.22 * per_brick + 0.25 * (macro - 0.5) + 0.12 * (micro - 0.5), 0.0, 1.0)
    base = paver_d[None, None, :] * (1.0 - t[..., None]) + paver_l[None, None, :] * t[..., None]
    base = base * (1.0 - mortar[..., None] * 0.68) + np.array([0.08, 0.10, 0.07])[None, None, :] * (mortar[..., None] * 0.68)
    base = base * (1.0 - moss[..., None] * 0.62) + moss_c[None, None, :] * (moss[..., None] * 0.62)
    # Puddles darken stone underneath
    base *= (1.0 - 0.28 * puddle[..., None])
    base *= (0.76 + 0.24 * ao[..., None])

    rough = np.clip(0.54 + 0.28 * mortar + 0.18 * moss - 0.44 * puddle + 0.08 * (micro - 0.5), 0.08, 0.94)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.90), 'orm': orm, 'normal': normal, 'normal_scale': 1.2, 'metallic': 0.0}


def _edge_trim_maps(n=512):
    """Chiseled ashlar cornice & lintel edge stone with carved chamfers & crisp wet bevels."""
    face, mortar, per_brick, _ = _brick_grid(
        n, cols=4, rows=4, mortar_w=0.04, seed=601, stagger=0.0, bevel_w=0.06
    )
    micro = _tile_fbm(n, [(24, 0.45), (56, 0.35), (110, 0.20)], seed=602)
    macro = _tile_fbm(n, [(6, 0.6), (14, 0.4)], seed=603)
    height = np.clip(0.62 * face + 0.16 * micro + 0.08 * macro - 0.22 * mortar, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=5.0, ao_strength=0.55)

    c_l = np.array([0.39, 0.41, 0.35], dtype=np.float32)
    c_d = np.array([0.25, 0.27, 0.23], dtype=np.float32)
    t = np.clip(0.5 + 0.18 * per_brick + 0.24 * (macro - 0.5) + 0.12 * (micro - 0.5), 0.0, 1.0)
    base = (c_d[None, None, :] * (1.0 - t[..., None]) + c_l[None, None, :] * t[..., None])
    base *= (1.0 - 0.45 * mortar[..., None]) * (0.76 + 0.24 * ao[..., None])
    rough = np.clip(0.48 + 0.26 * mortar - 0.14 * (macro - 0.5), 0.22, 0.90)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.92), 'orm': orm, 'normal': normal, 'normal_scale': 1.1, 'metallic': 0.0}


def _jade_maps(n=512):
    """Deep imperial Mesoamerican carved jade with cloudy mineral veins."""
    vein = _tile_fbm(n, [(4, 0.45), (10, 0.35), (24, 0.20)], seed=701)
    swirl = _tile_fbm(n, [(8, 0.5), (20, 0.5)], seed=702)
    micro = _tile_noise(n, 48, 703)
    height = np.clip(0.55 + 0.16 * vein + 0.10 * swirl + 0.06 * micro, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=2.2, ao_strength=0.28)

    j_deep = np.array([0.055, 0.235, 0.165], dtype=np.float32)
    j_rich = np.array([0.115, 0.440, 0.310], dtype=np.float32)
    j_milk = np.array([0.260, 0.560, 0.420], dtype=np.float32)
    t = np.clip(0.45 * vein + 0.45 * swirl + 0.10 * micro, 0.0, 1.0)
    base = np.where(
        t[..., None] < 0.65,
        j_deep[None, None, :] * (1.0 - t[..., None] / 0.65) + j_rich[None, None, :] * (t[..., None] / 0.65),
        j_rich[None, None, :] * (1.0 - (t[..., None] - 0.65) / 0.35) + j_milk[None, None, :] * ((t[..., None] - 0.65) / 0.35),
    )
    rough = np.clip(0.18 + 0.14 * (1.0 - t) + 0.06 * micro, 0.12, 0.42)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.90), 'orm': orm, 'normal': normal, 'normal_scale': 0.55, 'metallic': 0.0}


def _gold_maps(n=512):
    """Weathered Mesoamerican hammered relic gold with oxidized patina in hammered dimples."""
    dimple, d_tone = _tile_voronoi_stones(n, 16, 16, jitter=0.38, seed=801)
    micro = _tile_fbm(n, [(24, 0.5), (56, 0.5)], seed=802)
    patina = np.clip((_tile_fbm(n, [(6, 0.5), (14, 0.5)], seed=803) - 0.56) * 2.2, 0.0, 1.0)

    height = np.clip(0.52 + 0.22 * np.sqrt(np.clip(dimple, 0.0, 1.0)) + 0.10 * micro - 0.10 * patina, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=3.0, ao_strength=0.40)

    c_gold = np.array([0.84, 0.65, 0.23], dtype=np.float32)
    c_patina = np.array([0.28, 0.24, 0.12], dtype=np.float32)
    base = c_gold[None, None, :] * (0.88 + 0.12 * d_tone[..., None]) * (1.0 - patina[..., None] * 0.65)
    base += c_patina[None, None, :] * (patina[..., None] * 0.65)
    base *= (0.78 + 0.22 * ao[..., None])

    metal = np.clip(0.95 - 0.48 * patina, 0.35, 0.98)
    rough = np.clip(0.26 + 0.38 * patina + 0.08 * (1.0 - dimple), 0.18, 0.72)
    orm = np.stack([ao, rough, metal], axis=-1)
    return {'base': np.clip(base, 0.03, 0.96), 'orm': orm, 'normal': normal, 'normal_scale': 0.80, 'metallic': 1.0}


def _bark_maps(n=512):
    """Deeply furrowed wet rainforest Ceiba bark with green moss streaks."""
    ridge = np.abs(np.sin(_tile_noise(n, 12, 901) * 5.0 + np.linspace(0, 14 * np.pi, n, dtype=np.float32)[None, :]))
    fibers = _tile_fbm(n, [(12, 0.45), (32, 0.35), (64, 0.20)], seed=902)
    moss = np.clip((_tile_fbm(n, [(6, 0.55), (14, 0.45)], seed=903) - 0.50) * 1.8, 0.0, 1.0)

    height = np.clip(0.45 * ridge + 0.35 * fibers + 0.12 * moss, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=6.2, ao_strength=0.60)

    b_d = np.array([0.14, 0.11, 0.085], dtype=np.float32)
    b_l = np.array([0.27, 0.23, 0.18], dtype=np.float32)
    m_c = np.array([0.11, 0.21, 0.08], dtype=np.float32)

    t = np.clip(0.55 * ridge + 0.45 * fibers, 0.0, 1.0)
    base = b_d[None, None, :] * (1.0 - t[..., None]) + b_l[None, None, :] * t[..., None]
    base = base * (1.0 - moss[..., None] * 0.58) + m_c[None, None, :] * (moss[..., None] * 0.58)
    base *= (0.72 + 0.28 * ao[..., None])
    rough = np.clip(0.76 + 0.14 * (1.0 - ridge) - 0.12 * (fibers - 0.5), 0.50, 0.95)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.85), 'orm': orm, 'normal': normal, 'normal_scale': 1.25, 'metallic': 0.0}


def _foliage_maps(n=512, variant='leaf'):
    """Wet tropical foliage (broad leaf, fern, canopy) with central midrib, lateral veins & wet sheen."""
    gy, gx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    vein_wave = np.abs(np.sin((gx - 0.5) * 26.0 + np.abs(gy - 0.5) * 14.0))
    vein = np.exp(-vein_wave * 4.5)
    cell = _tile_fbm(n, [(12, 0.45), (30, 0.35), (64, 0.20)], seed=1001 if variant == 'leaf' else 1002)

    height = np.clip(0.52 + 0.24 * vein + 0.16 * cell, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=3.4, ao_strength=0.35)

    if variant == 'fern':
        c_d = np.array([0.075, 0.185, 0.065], dtype=np.float32)
        c_l = np.array([0.165, 0.335, 0.115], dtype=np.float32)
    elif variant == 'canopy':
        c_d = np.array([0.055, 0.145, 0.055], dtype=np.float32)
        c_l = np.array([0.125, 0.265, 0.095], dtype=np.float32)
    else:
        c_d = np.array([0.070, 0.195, 0.068], dtype=np.float32)
        c_l = np.array([0.155, 0.355, 0.120], dtype=np.float32)

    t = np.clip(0.45 * cell + 0.35 * vein + 0.15, 0.0, 1.0)
    base = (c_d[None, None, :] * (1.0 - t[..., None]) + c_l[None, None, :] * t[..., None]) * (0.82 + 0.18 * ao[..., None])
    # Rain-wet waxy cuticle
    rough = np.clip(0.32 + 0.18 * (1.0 - vein) + 0.10 * (cell - 0.5), 0.18, 0.62)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.88), 'orm': orm, 'normal': normal, 'normal_scale': 0.85, 'metallic': 0.0}


def _clay_maps(n=512):
    """Fired Mayan terracotta pots & weathered ceramic reliefs."""
    gy = np.arange(n, dtype=np.float32)[:, None] / n
    lathe = 0.5 + 0.5 * np.sin(gy * 2.0 * np.pi * 14.0)
    grit = _tile_fbm(n, [(20, 0.45), (48, 0.35), (96, 0.20)], seed=1101)
    stain = _tile_fbm(n, [(5, 0.55), (12, 0.45)], seed=1102)
    height = np.clip(0.55 + 0.12 * lathe + 0.16 * grit + 0.08 * stain, 0.0, 1.0)
    normal, ao = _height_to_normal_ao(height, strength=3.4, ao_strength=0.40)

    c_d = np.array([0.28, 0.15, 0.10], dtype=np.float32)
    c_l = np.array([0.48, 0.27, 0.17], dtype=np.float32)
    t = np.clip(0.45 * stain + 0.35 * grit + 0.15 * lathe, 0.0, 1.0)
    base = (c_d[None, None, :] * (1.0 - t[..., None]) + c_l[None, None, :] * t[..., None]) * (0.78 + 0.22 * ao[..., None])
    rough = np.clip(0.66 + 0.16 * (1.0 - stain) + 0.08 * (grit - 0.5), 0.42, 0.90)
    orm = np.stack([ao, rough, np.zeros_like(ao)], axis=-1)
    return {'base': np.clip(base, 0.02, 0.90), 'orm': orm, 'normal': normal, 'normal_scale': 0.85, 'metallic': 0.0}


def ensure_material(scene, short_key, color_rgba=(0.3, 0.33, 0.29, 1.0), rough=0.65, metal=0.0):
    prefix = scene.get('material_prefix', 'Observatory of Rain | ')
    name = prefix + short_key
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf:
        bsdf.inputs['Base Color'].default_value = color_rgba
        bsdf.inputs['Roughness'].default_value = rough
        bsdf.inputs['Metallic'].default_value = metal
    return mat


def _to_rgba(arr3):
    if arr3.shape[-1] == 4:
        return np.ascontiguousarray(arr3, dtype=np.float32)
    alpha = np.ones((*arr3.shape[:2], 1), dtype=np.float32)
    return np.ascontiguousarray(np.concatenate([arr3.astype(np.float32), alpha], axis=-1), dtype=np.float32)


def upgrade_ruins_materials(scene, tex_dir):
    """Build or upgrade all Ancient Ruins PBR materials with high-res maps."""
    tex_path = Path(tex_dir)
    tex_path.mkdir(parents=True, exist_ok=True)
    prefix = scene.get('material_prefix', 'Observatory of Rain | ')

    builders = {
        'rock': lambda: _limestone_ashlar_maps(1024),
        'basalt': lambda: _basalt_maps(1024),
        'carved_stone': lambda: _carved_relief_stone_maps(1024),
        'lichen': lambda: _lichen_moss_maps(1024),
        'floor': lambda: _courtyard_floor_maps(1024),
        'edge': lambda: _edge_trim_maps(512),
        'jade': lambda: _jade_maps(512),
        'gold': lambda: _gold_maps(512),
        'bark': lambda: _bark_maps(512),
        'clay': lambda: _clay_maps(512),
        'leaf': lambda: _foliage_maps(512, 'leaf'),
        'fern': lambda: _foliage_maps(512, 'fern'),
        'canopy': lambda: _foliage_maps(512, 'canopy'),
    }

    cached_maps = {}
    for short_key, fn in builders.items():
        mat = ensure_material(scene, short_key)
        maps = fn()
        for k in ('base', 'orm', 'normal'):
            maps[k] = _to_rgba(maps[k])
        _apply_maps_to_material(mat, maps, tex_path)
        cached_maps[short_key] = maps
        print(f'PBR upgraded: {prefix}{short_key}', flush=True)

    # Reuse matching crisp PBR node-links on Backdrop materials without bloating GLB with duplicate images
    backdrop_alias = {
        'Backdrop wall': 'rock',
        'Backdrop roof': 'basalt',
        'Backdrop trim': 'carved_stone',
        'Backdrop wood': 'bark',
    }
    for b_key, src_key in backdrop_alias.items():
        b_mat = bpy.data.materials.get(prefix + b_key)
        s_mat = bpy.data.materials.get(prefix + src_key)
        if b_mat and s_mat and b_mat.node_tree and s_mat.node_tree:
            _copy_tex_tree(s_mat, b_mat)
            print(f'PBR aliased: {prefix}{b_key} -> {src_key}', flush=True)


def _copy_tex_tree(src_mat, dst_mat):
    """Clone Base/ORM/Normal image links from src_mat onto dst_mat sharing packed image data."""
    dst = dst_mat.node_tree
    bsdf = next(n for n in dst.nodes if n.type == 'BSDF_PRINCIPLED')
    for node in list(dst.nodes):
        if node != bsdf and node.type != 'OUTPUT_MATERIAL':
            dst.nodes.remove(node)

    src_nodes = src_mat.node_tree.nodes
    src_bsdf = next(n for n in src_nodes if n.type == 'BSDF_PRINCIPLED')
    tex_by_use = {}
    for n in src_nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            if 'Base' in n.image.name:
                tex_by_use['base'] = n.image
            elif 'ORM' in n.image.name:
                tex_by_use['orm'] = n.image
            elif 'NRM' in n.image.name:
                tex_by_use['nrm'] = n.image
    if 'base' in tex_by_use:
        tb = dst.nodes.new('ShaderNodeTexImage')
        tb.image = tex_by_use['base']
        dst.links.new(tb.outputs['Color'], bsdf.inputs['Base Color'])
    if 'orm' in tex_by_use:
        to = dst.nodes.new('ShaderNodeTexImage')
        to.image = tex_by_use['orm']
        sp = dst.nodes.new('ShaderNodeSeparateColor')
        dst.links.new(to.outputs['Color'], sp.inputs['Color'])
        dst.links.new(sp.outputs['Green'], bsdf.inputs['Roughness'])
        dst.links.new(sp.outputs['Blue'], bsdf.inputs['Metallic'])
    if 'nrm' in tex_by_use:
        tn = dst.nodes.new('ShaderNodeTexImage')
        tn.image = tex_by_use['nrm']
        nm = dst.nodes.new('ShaderNodeNormalMap')
        src_nm = next((n for n in src_nodes if n.type == 'NORMAL_MAP'), None)
        if src_nm:
            nm.inputs['Strength'].default_value = src_nm.inputs['Strength'].default_value
        dst.links.new(tn.outputs['Color'], nm.inputs['Color'])
        dst.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    bsdf.inputs['Metallic'].default_value = src_bsdf.inputs['Metallic'].default_value
