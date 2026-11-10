"""Bake world-aligned map surrounds from authored void seeds and ground materials.

Run with Python, Pillow, NumPy and SciPy. Original TMJ and map images are inputs.
The PNG masks and generated-image materials beside this script are editable sources.
"""
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).parent
OUTPUT = ROOT / 'build-map-perimeters/baked'
EVIDENCE = ROOT / 'build-evidence/map-perimeters'
PAD = 256
TILE = 256
THEMES = {
    'ancient_ruins': {'seeds': [(7, 392), (426, 106), (353, 341), (295, 599),
                                (547, 423), (714, 573), (679, 719)],
                      'soil': (94, 90, 77), 'seed': 714},
    'dust_2': {'seeds': [], 'soil': (182, 157, 83), 'seed': 208},
    'mirage': {'seeds': [(184, 1093)], 'soil': (137, 127, 107), 'seed': 319},
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ground_tile(name, settings, original):
    image = Image.open(SOURCE / 'materials' / (name + '.png')).convert('RGB')
    pixels = np.asarray(image.resize((TILE, TILE), Image.Resampling.LANCZOS), dtype=np.float32)
    mean = pixels.mean(axis=(0, 1))
    pixels = np.array(settings['soil']) + (pixels - mean) * .65
    if name == 'dust_2':
        # Quilt varied source-sand patches on a torus, retaining the map's grain.
        rng = np.random.default_rng(settings['seed'])
        accumulated = np.zeros((TILE, TILE, 3), dtype=np.float32)
        weights = np.zeros((TILE, TILE), dtype=np.float32)
        window = np.sin(np.pi*(np.arange(24)+.5)/24)**2
        weight = window[:, None]*window[None, :]
        for y in range(0, TILE, 16):
            for x in range(0, TILE, 16):
                source_x = int(rng.integers(0, original.shape[1]-24))
                patch = np.rot90(original[:24, source_x:source_x+24], int(rng.integers(4)))
                iy, ix = np.ix_((np.arange(24)+y) % TILE, (np.arange(24)+x) % TILE)
                accumulated[iy, ix] += patch*weight[:, :, None]
                weights[iy, ix] += weight
        variation = ndimage.gaussian_filter(pixels.mean(axis=2), 9)
        pixels = accumulated/weights[:, :, None] + (variation-variation.mean())[:, :, None]*.2
    # Match opposite borders with a smooth, narrow correction on both sides.
    for axis in (0, 1):
        pixels = np.swapaxes(pixels, 0, axis)
        difference = (pixels[-1] - pixels[0]) / 2
        for offset in range(24):
            weight = (1 - offset / 24) ** 2
            pixels[offset] += difference * weight
            pixels[-1 - offset] -= difference * weight
        pixels = np.swapaxes(pixels, 0, axis)
    return np.clip(np.rint(pixels), 0, 255).astype(np.uint8)


def void_mask(name, original, settings):
    """Only deliberately selected dark components are eligible for replacement."""
    labels, _ = ndimage.label(original.max(axis=2) < 20)
    mask = np.zeros(original.shape[:2], dtype=bool)
    if name == 'dust_2':
        yy, xx = np.indices(mask.shape)
        mask = np.minimum.reduce([xx, yy, mask.shape[1]-1-xx, mask.shape[0]-1-yy]) < 20
    for x, y in settings['seeds']:
        label = labels[y, x]
        assert label != 0, (name, 'void seed no longer matches the source', x, y)
        mask |= labels == label
    path = SOURCE / (name + '_void.png')
    if not path.exists():
        Image.fromarray(mask.astype(np.uint8) * 255).save(path)
    else:
        edited = np.asarray(Image.open(path).convert('L'))
        assert edited.shape == mask.shape, (name, 'mask dimensions')
        mask = edited > 127
    if name == 'dust_2':
        assert np.all(original[mask, 0] > original[mask, 2]+30), 'Sand mask covers non-sand artwork'
    else:
        assert not np.any(mask & (original.max(axis=2) >= 20)), (name, 'mask covers map artwork')
    return mask


def draw_decorations(name, size, allowed, distance, scale, rng, original):
    """Sparse map-scale rubble, wall-foot vegetation and closed background structures."""
    layer = Image.new('RGBA', size)
    draw = ImageDraw.Draw(layer)
    height, width = allowed.shape
    for _ in range(int(width * height / (4000 * scale * scale))):
        x = int(rng.integers(12, width - 12))
        y = int(rng.integers(12, height - 12))
        if not allowed[y, x] or distance[y, x] < 7 * scale:
            continue
        # Retain negative space in the far field and keep the canvas edge tileable.
        edge = min(x, y, width - 1 - x, height - 1 - y)
        if edge < 48 * scale or (distance[y, x] > 80 * scale and rng.random() < .8):
            continue
        radius = int(rng.integers(2, 5) * scale)
        draw.ellipse((x-radius, y, x+radius+2, y+radius+2), fill=(38, 32, 22, 45))
        color = (118, 113, 94, 180) if name == 'ancient_ruins' else (153, 133, 95, 150)
        draw.polygon([(x-radius, y), (x, y-radius), (x+radius, y-1),
                      (x+radius-1, y+radius), (x-radius+1, y+radius)], fill=color)
        draw.line((x-radius, y, x, y-radius, x+radius, y-1), fill=(185, 169, 135, 90), width=1)
        if name != 'dust_2' and rng.random() < .42:
            for _ in range(4):
                dx, dy = rng.integers(-5, 6, size=2) * scale
                draw.line((x, y, x+int(dx), y+int(dy)),
                          fill=(70, 82, 44, 145), width=max(1, int(scale)))

    # Purposefully placed silhouettes, wholly outside the map image and its border.
    placements = [(PAD-60*scale, PAD+180*scale),
                  (PAD+260*scale, PAD-70*scale),
                  (width-PAD+60*scale, height//2),
                  (width//2, height-PAD+60*scale)]
    for cx, cy in placements:
        x, y = int(cx), int(cy)
        rw, rh = int(45*scale), int(28*scale)
        if min(x-rw, y-rh) < 28 or x+rw >= width-28 or y+rh >= height-28:
            continue
        if not allowed[y-rh-8:y+rh+9, x-rw-8:x+rw+9].all():
            continue
        if name == 'ancient_ruins':
            for side in (-1, 1):
                start = x-rw+(0 if side < 0 else 18)
                end = x+rw-(12 if side < 0 else 4)
                draw.rectangle((start+2, y+side*rh, end+3, y+side*rh+6), fill=(25, 28, 20, 65))
                draw.rectangle((start, y+side*rh-3, end, y+side*rh+3),
                               fill=(104, 102, 85, 255), outline=(67, 67, 55, 255))
                for offset in range(start, end, max(8, int(12*scale))):
                    draw.line((offset, y+side*rh-3, offset, y+side*rh+3), fill=(61, 63, 51, 255))
            draw.rectangle((x-rw, y-rh, x-rw+6, y+rh-14), fill=(98, 99, 80, 255))
        elif name == 'mirage':
            draw.rectangle((x-rw+5, y-rh+7, x+rw+8, y+rh+8), fill=(37, 31, 23, 70))
            draw.rectangle((x-rw, y-rh, x+rw, y+rh), fill=(130, 121, 101, 255),
                           outline=(91, 87, 73, 255), width=max(2, int(3*scale)))
            draw.rectangle((x-rw+5, y-rh+5, x+rw-5, y+rh-5), fill=(128, 98, 75, 255))
            for row in range(y-rh+6, y+rh-4, max(3, int(4*scale))):
                draw.line((x-rw+5, row, x+rw-5, row), fill=(151, 120, 91, 255), width=max(1, int(scale)))
            # Reuse the map's own terracotta texels on the background roof fragments.
            tile = Image.fromarray(original[600:632, 760:792]).convert('RGBA')
            roof = Image.new('RGBA', (2*rw-10, 2*rh-10))
            for ty in range(0, roof.height, tile.height):
                for tx in range(0, roof.width, tile.width):
                    roof.paste(tile, (tx, ty))
            layer.alpha_composite(roof, (x-rw+5, y-rh+5))
        else:
            # Wind-worn rock clusters keep desert outskirts visually open.
            for dx, dy in [(-12, 3), (3, -2), (13, 7)]:
                a, b = x+int(dx*scale), y+int(dy*scale)
                r = int(6*scale)
                draw.ellipse((a-r, b-r//2, a+r, b+r//2), fill=(155, 133, 72, 200))
    pixels = np.array(layer)
    pixels[:, :, 3] = np.where(allowed, pixels[:, :, 3], 0)
    return Image.fromarray(pixels)


def bake(name, settings):
    document = json.loads((ROOT / 'assets/cs2d/maps' / (name + '.tmj')).read_text(encoding='utf-8'))
    image_layer = next(layer for layer in document['layers'] if layer['type'] == 'imagelayer')
    image_path = (ROOT / 'assets/cs2d/maps' / image_layer['image']).resolve()
    original = np.asarray(Image.open(image_path).convert('RGB'))
    height, width = original.shape[:2]
    scale = width / image_layer['imagewidth']
    assert scale == height / image_layer['imageheight']
    tile = ground_tile(name, settings, original)
    Image.fromarray(tile).save(OUTPUT / (name + '_ground.png'))
    mask = void_mask(name, original, settings)
    full_h, full_w = height + 2*PAD, width + 2*PAD
    yy, xx = np.indices((full_h, full_w))
    ground = tile[(yy-PAD) % TILE, (xx-PAD) % TILE].copy()
    allowed = np.ones((full_h, full_w), dtype=bool)
    allowed[PAD:PAD+height, PAD:PAD+width] = mask
    distance = ndimage.distance_transform_edt(allowed)

    # Occlusion is strongest at wall feet, with a restrained far-field contrast.
    shade = 1 - .22*np.exp(-distance/(7*scale))
    shade *= 1 - .08*np.clip(distance/(110*scale), 0, 1)
    if name == 'dust_2':
        # The source rectangle ends in continuous sand, with no wall to cast a shadow.
        shade = np.ones_like(shade)
    border = np.minimum.reduce([xx, yy, full_w-1-xx, full_h-1-yy])
    border_mix = np.clip(border/(40*scale), 0, 1)
    shade = 1 + (shade-1)*border_mix
    ground = np.rint(ground*shade[:, :, None]).astype(np.uint8)

    scene = Image.fromarray(ground).convert('RGBA')
    scene.alpha_composite(draw_decorations(name, scene.size, allowed,
                                           distance, scale, np.random.default_rng(settings['seed']), original))
    pixels = np.array(scene.convert('RGB'))
    center = pixels[PAD:PAD+height, PAD:PAD+width]
    if name == 'dust_2':
        iy, ix = np.indices((height, width))
        inset = np.minimum.reduce([ix, iy, width-1-ix, height-1-iy])
        # Feather only the authored twenty-pixel sand margin, outside all walls.
        blend = np.clip((inset-2)/17, 0, 1)
        blend = blend*blend*(3-2*blend)
        center[:] = np.rint(original*blend[:, :, None] + center*(1-blend[:, :, None]))
    center[~mask] = original[~mask]
    assert np.array_equal(center[~mask], original[~mask]), 'Protected source pixels changed'
    assert np.array_equal(pixels[0], tile[(-PAD) % TILE, (np.arange(full_w)-PAD) % TILE])
    output = OUTPUT / (name + '_scene.png')
    Image.fromarray(pixels).save(output, compress_level=6)
    preview = Image.fromarray(pixels)
    preview.thumbnail((1200, 900), Image.Resampling.LANCZOS)
    preview.save(EVIDENCE / (name + '-overview.jpg'), quality=94)
    return dict(theme=name, source_sha256=digest(image_path), mask_sha256=digest(SOURCE/(name+'_void.png')),
                scene_sha256=digest(output), scene_size=[full_w, full_h], native_padding=PAD,
                world_padding=PAD/scale, modified_source_pixels=int(mask.sum()),
                protected_source_pixels=int((~mask).sum()), protected_pixels_identical=True)


if __name__ == '__main__':
    OUTPUT.mkdir(parents=True, exist_ok=True)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    results = [bake(name, settings) for name, settings in THEMES.items()]
    (EVIDENCE/'bake-audit.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('MAP PERIMETER BAKE PASS', flush=True)
