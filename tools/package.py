"""Build the release zip that Melty installs into the Skyrim folder.

    python tools/package.py <version> --address-library <folder with SKSE/Plugins/versionlib-*.bin>

Layout (archive root = {game}):
    MojaveInSkyrim.asi, MojaveInSkyrim.ini
    Data/SKSE/Plugins/versionlib-<ver>.bin       Address Library by meh321 (shared with other mods)
    Data/SKSE/Plugins/MojaveInSkyrim/            README, LICENSE, THIRD-PARTY-NOTICES
Nothing from Skyrim or Fallout: New Vegas goes in.
"""
import argparse
import hashlib
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Skyrim builds the release supports: the Address Library files it ships. Keep in step with the
# recipe's game version range.
GAME_VERSIONS = ['1-6-1170-0', '1-6-1179-0', '1-7-99-0', '1-7-104-0']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('version')
    ap.add_argument('--address-library', required=True)
    a = ap.parse_args()

    asi = ROOT / 'build' / 'RelWithDebInfo' / 'MojaveInSkyrim.asi'
    files = {
        'MojaveInSkyrim.asi': asi,
        'MojaveInSkyrim.ini': ROOT / 'package' / 'MojaveInSkyrim.ini',
        'Data/SKSE/Plugins/MojaveInSkyrim/README.md': ROOT / 'README.md',
        'Data/SKSE/Plugins/MojaveInSkyrim/LICENSE.txt': ROOT / 'LICENSE',
        'Data/SKSE/Plugins/MojaveInSkyrim/THIRD-PARTY-NOTICES.md': ROOT / 'THIRD-PARTY-NOTICES.md',
    }
    plugins = Path(a.address_library) / 'SKSE' / 'Plugins'
    for v in GAME_VERSIONS:
        files[f'Data/SKSE/Plugins/versionlib-{v}.bin'] = plugins / f'versionlib-{v}.bin'
    missing = [str(p) for p in files.values() if not p.is_file()]
    if missing:
        print('missing:\n  ' + '\n  '.join(missing))
        return 1

    out = ROOT / 'dist' / f'MojaveInSkyrim-{a.version}.zip'
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, src in files.items():
            z.write(src, name)
    data = out.read_bytes()
    print(f'{out.name}  {len(data)} bytes  sha256 {hashlib.sha256(data).hexdigest()}')
    for name, src in files.items():
        print(f'  {src.stat().st_size:>9}  {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
