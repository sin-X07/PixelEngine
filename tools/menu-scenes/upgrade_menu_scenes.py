"""Apply a scene's dust or background pass to the current authoring and runtime assets.

Blender background invocation:
  blender -b --python-exit-code 1 -P tools/menu-scenes/upgrade_menu_scenes.py -- THEME PASS OUTPUT
OUTPUT is a staging directory. Copy the accepted .blend and .glb into the project after review.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from menu_dust import MOTE_PREFIX


def read_glb(path):
    raw = path.read_bytes()
    json_size = struct.unpack_from('<I', raw, 12)[0]
    return json.loads(raw[20:20+json_size]), bytearray(raw[28+json_size:])


def write_glb(path, document, data):
    data.extend(bytes((-len(data))%4))
    document['buffers'][0]['byteLength'] = len(data)
    header = json.dumps(document, separators=(',',':')).encode()
    header += b' '*((-len(header))%4)
    path.write_bytes(struct.pack('<4sIII4s', b'glTF', 2, 28+len(header)+len(data), len(header), b'JSON')
                     + header + struct.pack('<I4s', len(data), b'BIN\0') + data)


def carry_material_maps(previous, output):
    old, source = read_glb(previous)
    doc, data = read_glb(output)
    translated = {}
    source_materials = {m['name']:m for m in old['materials']}

    def pixels(document, buf, image):
        view = document['bufferViews'][image['bufferView']]
        start = view.get('byteOffset', 0)
        return bytes(buf[start:start+view['byteLength']])

    def translate(index):
        if index in translated:
            return translated[index]
        texture = copy.deepcopy(old['textures'][index])
        image = copy.deepcopy(old['images'][texture['source']])
        blob = pixels(old, source, image)
        for i, candidate in enumerate(doc.setdefault('images', [])):
            if candidate.get('name') == image.get('name') and pixels(doc, data, candidate)==blob:
                texture['source'] = i
                break
        else:
            data.extend(bytes((-len(data))%4))
            image['bufferView'] = len(doc['bufferViews'])
            doc['bufferViews'].append({'buffer':0,'byteOffset':len(data),'byteLength':len(blob)})
            data.extend(blob)
            texture['source'] = len(doc['images'])
            doc['images'].append(image)
        if 'sampler' in texture:
            sampler = old['samplers'][texture['sampler']]
            samplers = doc.setdefault('samplers', [])
            if sampler not in samplers:
                samplers.append(sampler)
            texture['sampler'] = samplers.index(sampler)
        textures = doc.setdefault('textures', [])
        if texture not in textures:
            textures.append(texture)
        translated[index] = textures.index(texture)
        return translated[index]

    for i, current in enumerate(doc['materials']):
        name = current['name']
        if name not in source_materials:
            continue
        old_material = copy.deepcopy(source_materials[name])
        pbr = old_material.get('pbrMetallicRoughness', {})
        for container, keys in ((old_material, ('normalTexture', 'occlusionTexture', 'emissiveTexture')),
                                (pbr, ('baseColorTexture', 'metallicRoughnessTexture'))):
            for key in keys:
                if key in container:
                    container[key]['index'] = translate(container[key]['index'])
        if 'occlusionTexture' in old_material:
            old_base = source_materials[name]['pbrMetallicRoughness']['baseColorTexture']
            image = old['images'][old['textures'][old_base['index']]['source']]
            fallback_name = 'PixelEngine AO fallback: '+image['name']
            for j, texture in enumerate(old['textures']):
                if texture.get('name') == fallback_name:
                    translate(j)
                    break
        doc['materials'][i] = old_material

    # The compatible renderer draws nodes in array order. Blend the far motes
    # over opaque scenery before the closer motes are drawn.
    nodes = doc['nodes']
    assert not doc.get('skins')
    order = sorted(range(len(nodes)), key=lambda i: (
        nodes[i].get('name','').startswith(MOTE_PREFIX),
        nodes[i].get('name','') if nodes[i].get('name','').startswith(MOTE_PREFIX) else i))
    indices = {old_index: i for i, old_index in enumerate(order)}
    doc['nodes'] = [nodes[i] for i in order]
    for node in doc['nodes']:
        if 'children' in node:
            node['children'] = [indices[i] for i in node['children']]
    for scene in doc['scenes']:
        scene['nodes'] = [indices[i] for i in scene['nodes']]
    for clip in doc['animations']:
        for channel in clip['channels']:
            target = channel['target']
            target['node'] = indices[target['node']]
    write_glb(output, doc, data)
    return doc


def verify_glb(doc, expected_motes):
    clip, = doc['animations']
    assert clip['name'] == 'Menu_Environment'
    motion = [c for c in clip['channels']
              if doc['nodes'][c['target']['node']]['name'].startswith(MOTE_PREFIX)]
    assert len(clip['channels']) - len(motion) == 18
    assert len(motion) == expected_motes
    if motion:
        dust_index, = {part['material']
                       for channel in motion
                       for part in doc['meshes'][doc['nodes'][channel['target']['node']]['mesh']]['primitives']}
        dust = doc['materials'][dust_index]
        assert dust['alphaMode'] == 'BLEND' and 'baseColorTexture' in dust['pbrMetallicRoughness']
    for mesh in doc['meshes']:
        for part in mesh['primitives']:
            material = doc['materials'][part['material']]
            for scope in (material, material.get('pbrMetallicRoughness', {})):
                for key, slot in scope.items():
                    if key.endswith('Texture'):
                        assert 'TEXCOORD_'+str(slot.get('texCoord',0)) in part['attributes'], (mesh['name'], material['name'])
    assert all('bufferView' in image for image in doc['images'])


def capture(path):
    scene = bpy.context.scene
    scene.frame_set(0)
    camera = scene.camera
    camera.location = (0,-8,1.05)
    camera.rotation_euler = (Vector((0,0,.95))-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.sensor_fit = 'VERTICAL'
    camera.data.angle_y = 0.977384381
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x, scene.render.resolution_y = 2560, 1600
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def stage_current(theme, output, report):
    scene = bpy.context.scene
    scene.frame_set(0)
    output.mkdir(parents=True,exist_ok=True)
    assets = ROOT/'assets/models/menu/scenes'
    from salt_market import export
    glb = output/(theme+'.glb')
    export(glb, legacy_limits=False)
    added = sum(o.name.startswith(MOTE_PREFIX) for o in scene.objects)
    doc = carry_material_maps(assets/(theme+'.glb'), glb)
    verify_glb(doc, added)
    report.update(
        bytes=glb.stat().st_size, materials=len(doc['materials']), images=len(doc['images']),
        vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count']
                     for m in doc['meshes'] for p in m['primitives']),
        animation_channels=len(doc['animations'][0]['channels']),
    )
    (output/'asset-summary.json').write_text(json.dumps(report, indent=2),encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('theme',choices=('dust_2','mirage','ancient_ruins'))
    parser.add_argument('stage',choices=('before','dust','background'))
    parser.add_argument('output',type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    theme, stage, output = args.theme, args.stage, args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(HERE/(theme+'.blend')))
    if stage == 'before':
        capture(output/'blender.png')
        return
    scene = bpy.context.scene
    scene.frame_set(0)
    if stage == 'dust':
        from menu_dust import bake_dust
        report = bake_dust(theme)
    else:
        from menu_background import detail_background
        report = detail_background(theme)
    stage_current(theme, output, report)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(output/(theme+'.blend')), compress=True)
    print('STAGED', theme, stage, report, flush=True)
    capture(output/'blender.png')


if __name__ == '__main__':
    main()
