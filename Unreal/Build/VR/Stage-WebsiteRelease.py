"""Copy verified public downloads into the local Thwargle website; never upload.

Example: python Stage-WebsiteRelease.py Releases/2026.09.30-v88
The website remains plain static HTML. Its page generator consumes release.json.
Linux is optional for historical releases through v87 and required from v88.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def stage_release(root, release, site, *, replace_unpublished=False):
    release = release.resolve()
    site = site.resolve()
    meta = json.loads((release / 'release-manifest.json').read_text(encoding='utf-8-sig'))
    version = meta['questVersionCode']
    require(type(version) is int and version > 0, 'Release number must be a positive integer')
    require((site / '_tools/build-ac-pages.py').is_file(), 'Website page source is missing')
    require(version < 88 or 'linux' in meta, 'Release 88 and later require the native Linux archive')
    require(('linux' in meta) == ('linuxVersion' in meta), 'Linux artifact and display version must be supplied together')
    if 'linux' in meta:
        require(meta['linuxVersion'] == meta['windowsVersion'] == meta['questVersion'],
                'Linux, Windows, and Quest must display the same version')
        require(meta['linux']['file'] == f'AC-Unreal-Linux-v{version}.tar.gz',
                'Linux artifact must be the native versioned tar.gz archive')
    win = meta['windowsInstaller']
    quest = next(x for x in meta['archives'] if x['name'].startswith('AC-VR-Quest-'))
    portable = next(x for x in meta['archives'] if x['name'].startswith('AC-Unreal-and-AC-VR-Windows-'))
    entries = [('windows', win), ('quest', quest), ('portable', portable), ('questApk', meta['questApk'])]
    if 'linux' in meta:
        entries.append(('linux', meta['linux']))
    items = {}
    for key, entry in entries:
        name = entry.get('file', entry.get('name'))
        require(isinstance(name, str) and name not in {'', '.', '..'}
                and not any(char in name for char in '/\\:\r\n') and Path(name).name == name,
                'Public filename must not contain directories')
        src = release / name
        require(src.stat().st_size == entry['bytes'] and sha(src) == entry['sha256'], f'Checksum mismatch: {name}')
        items[key] = {'file': name, 'bytes': entry['bytes'], 'sha256': entry['sha256']}
    # Existing Quest updaters validate a .quest.<release> suffix in this wire
    # field; preserve it while keeping the installed/display version unified.
    quest_installed_version = meta['questVersion']
    require(quest_installed_version.endswith(f'.{version}'), 'Quest version does not match the release')
    require(meta['windowsVersion'].endswith(f'.{version}'), 'Windows version does not match the release')
    quest_update_version = (quest_installed_version if quest_installed_version.endswith(f'.quest.{version}')
                            else quest_installed_version.rsplit('.', 1)[0] + f'.quest.{version}')
    public = {'version': version, 'date': meta['createdUtc'][:10], 'windowsVersion': meta['windowsVersion'],
              'questVersion': quest_update_version, 'questInstalledVersion': quest_installed_version,
              'windowsSigned': False, 'gameDataIncluded': False, **items}
    public['updates'] = {'schema': 1, 'windows': items['windows'], 'quest': items['questApk']}
    if 'linux' in items:
        public['linuxVersion'] = meta['linuxVersion']
        # Linux clients use this platform-specific entry for a manual browser
        # download. No installer/helper exists on Linux; never reuse Windows.
        public['updates']['linux'] = items['linux']
    out = site / f'downloads/ac/v{version}'
    copies = [(release / entry['file'], out / entry['file'], entry['sha256']) for entry in items.values()]
    copies += [(release / name, out / name, sha(release / name)) for name in ['SHA256SUMS.txt', 'RELEASE-NOTES.md']]
    # Check every collision before changing the staging directory. In particular,
    # a mismatched Linux artifact must not partially replace another platform.
    for _, target, expected_sha in copies:
        if target.exists() and sha(target) != expected_sha and not replace_unpublished:
            raise ValueError(f'Refusing to overwrite a different published artifact: {target}')
    out.mkdir(parents=True, exist_ok=True)
    for source, target, expected_sha in copies:
        if not target.exists() or sha(target) != expected_sha:
            shutil.copy2(source, target)
        require(sha(target) == expected_sha, f'Staged checksum mismatch: {target.name}')
    assets = site / 'assets/ac'
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / 'Unreal/Build/Branding/AC-Icon.png', assets / 'ac-icon.png')
    shutil.copy2(root / 'Unreal/Build/Windows/Application.ico', assets / 'ac-icon.ico')
    (assets / 'release.json').write_text(json.dumps(public, indent=2) + '\n', encoding='utf-8')
    subprocess.run([sys.executable, str(site / '_tools/build-ac-pages.py')], check=True)
    print(f'Staged public website downloads in {out}. No remote upload performed.')
    return public


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    parser.add_argument('--site', type=Path, default=Path('C:/dev/Thwargle.com'))
    parser.add_argument('--replace-unpublished', action='store_true',
                        help='Refresh this local staging copy before its first upload; never use for an already distributed release.')
    args = parser.parse_args()
    try:
        stage_release(Path(__file__).resolve().parents[3], args.release, args.site,
                      replace_unpublished=args.replace_unpublished)
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from error


if __name__ == '__main__':
    main()
