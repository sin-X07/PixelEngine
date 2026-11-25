"""Build editable menu environments from the three in-game map art directions.

Run with Blender --background --python-exit-code 1 --python this_file.
The default build replaces only the dust_2 menu bundle. Pass -- --all to
regenerate all scenes, -- --themes ancient_ruins mirage for the courtyards,
or -- --output-dir <path> to stage a review build.
"""
import argparse
import shutil
import uuid
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AUTHORING = Path(__file__).resolve().parent
BUILD = ROOT / 'build-menu-scenes' / uuid.uuid4().hex
OUTPUT = BUILD / 'assets'
SOURCE = BUILD / 'sources'
OUTPUT.mkdir(parents=True, exist_ok=True)
SOURCE.mkdir(parents=True, exist_ok=True)
# Salt Exchange remains the default for existing callers. A named build can
# regenerate either new courtyard without rewriting the other menu assets.
parser = argparse.ArgumentParser()
group = parser.add_mutually_exclusive_group()
names = ('ancient_ruins', 'dust_2', 'mirage')
group.add_argument('--all', action='store_true')
group.add_argument('--themes', nargs='+', choices=names)
parser.add_argument('--output-dir', type=Path)
options = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
themes = names if options.all else tuple(dict.fromkeys(options.themes or ('dust_2',)))
sys.path.insert(0, str(AUTHORING))
# Builders evaluate geometry through midpoly.finish before authoring UVs and exporting.
# Keep a single deterministic build entry for live Blender and background regeneration.
def build_theme(name, blend, glb):
    if name == 'dust_2':
        import salt_market
        salt_market.build(blend, glb)
    else:
        import courtyards
        courtyards.build(name, blend, glb)

for name in themes:
    blend, glb = SOURCE/(name+'.blend'), OUTPUT/(name+'.glb')
    build_theme(name, blend, glb)


# Validate the requested bundle before publishing.
destination = options.output_dir.resolve() if options.output_dir else None
targets = [(SOURCE/(name+'.blend'), (destination or AUTHORING)/(name+'.blend'))
           for name in themes]
targets += [(OUTPUT/(name+'.glb'), (destination or ROOT/'assets/models/menu/scenes')/(name+'.glb'))
            for name in themes]
previous = BUILD/'previous'
previous.mkdir()
existing = {}
for index,(source,target) in enumerate(targets):
    assert source.is_file() and source.stat().st_size > 1000
    if source.suffix == '.glb':
        data = source.read_bytes()
        document = json.loads(data[20:20+struct.unpack_from('<I',data,12)[0]])
        animations = document.get('animations',[])
        assert len(animations) == 1 and len(animations[0]['channels']) == 18, 'All foliage must share one wind clip'
    if target.exists():
        backup = previous/str(index)
        shutil.copyfile(target,backup)
        existing[target] = backup
try:
    for source,target in targets:
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
except Exception:
    for source,target in targets:
        if target in existing:
            shutil.copyfile(existing[target],target)
        elif target.exists():
            target.unlink()
    raise
print('MENU ENVIRONMENT BUNDLE PUBLISHED',flush=True)
