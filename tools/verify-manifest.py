#!/usr/bin/env python3
"""Download and verify every published Hub package (Python 3, standard library)."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import struct
import urllib.request
import zipfile

FOLDERS = {'vst3': 'VST3', 'au': 'AU', 'clap': 'CLAP'}

def verify(manifest, cache, required):
    assert manifest['schema'] == 1, 'Unsupported manifest schema'
    seen = set()
    for plugin in manifest['plugins']:
        pid, version = plugin['id'], plugin['version']
        assert pid not in seen, f'Duplicate plugin: {pid}'
        seen.add(pid)
        downloads = plugin.get('downloads', {})
        assert downloads, f'{pid}: no downloads'
        for platform in required:
            assert platform in downloads, f'{pid}: missing {platform} download'
        for platform, info in downloads.items():
            label = f'{pid} {version} {platform}'
            filename = f'{pid}-{version}-{platform}.zip'
            path = cache / filename
            if not path.exists():
                with urllib.request.urlopen(info['url'], timeout=90) as response:
                    path.write_bytes(response.read())
            assert path.stat().st_size == info['size'], f'{label}: wrong size'
            assert hashlib.sha256(path.read_bytes()).hexdigest() == info['sha256'], f'{label}: checksum mismatch'
            formats = info.get('formats', list(plugin['bundles']))
            assert formats and len(set(formats)) == len(formats), f'{label}: invalid formats'
            with zipfile.ZipFile(path) as archive:
                assert archive.testzip() is None, f'{label}: corrupt ZIP'
                names = set(archive.namelist())
                for name in names:
                    assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts, f'{label}: unsafe path'
                for fmt in formats:
                    assert fmt in FOLDERS and fmt in plugin['bundles'], f'{label}: unknown format {fmt}'
                    assert not (platform == 'win' and fmt == 'au'), f'{label}: AU on Windows'
                    bundle = f"{FOLDERS[fmt]}/{plugin['bundles'][fmt]}"
                    assert any(n == bundle or n.startswith(bundle + '/') for n in names), f'{label}: missing {bundle}'
                    if platform == 'mac':
                        plist = plistlib.loads(archive.read(bundle + '/Contents/Info.plist'))
                        assert plist['CFBundleShortVersionString'] == version, f'{label}: stale bundle version'
                        binary = bundle + '/Contents/MacOS/' + plist['CFBundleExecutable']
                        assert binary in names and archive.getinfo(binary).file_size > 0, f'{label}: missing executable'
                        assert (archive.getinfo(binary).external_attr >> 16) & 0o111, f'{label}: executable bit missing'
                    if platform == 'win':
                        binary = bundle if fmt == 'clap' else bundle + '/Contents/x86_64-win/' + plugin['bundles'][fmt]
                        data = archive.read(binary)
                        assert data[:2] == b'MZ', f'{label}: missing Windows PE binary'
                        offset = struct.unpack_from('<I', data, 0x3c)[0]
                        assert data[offset:offset + 4] == b'PE\0\0' and struct.unpack_from('<H', data, offset + 4)[0] == 0x8664, f'{label}: Windows binary is not x64'
                    if platform == 'win' and fmt == 'vst3':
                        metadata = archive.read(bundle + '/Contents/Resources/moduleinfo.json').decode('utf-8-sig')
                        match = re.search(r'"Version"\s*:\s*"([^"]+)"', metadata)
                        assert match and match.group(1) == version, f'{label}: stale Windows bundle version'
                if pid == 'grille':
                    assert 'CREDITS.md' in names and any(n.startswith('licenses/') for n in names), f'{label}: missing IR attribution'
            print(f'OK {label}: {", ".join(formats)}, {info["size"]} bytes', flush=True)
    print(f'Verified {len(seen)} plugins.', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', nargs='?', default=str(Path(__file__).resolve().parents[1] / 'manifest.json'))
    parser.add_argument('--cache', type=Path, default=Path('dist/verified'))
    parser.add_argument('--require-platform', action='append', default=[])
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    source = args.manifest
    if source.startswith(('https://', 'http://')):
        with urllib.request.urlopen(source, timeout=30) as response:
            manifest = json.load(response)
    else:
        manifest = json.loads(Path(source).read_text())
    verify(manifest, args.cache, args.require_platform)

if __name__ == '__main__':
    main()
