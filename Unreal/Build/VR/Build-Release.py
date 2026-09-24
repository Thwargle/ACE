"""Assemble existing Quest/Windows builds into a clean, versioned local release.

Run Build-Windows.ps1, Build-Quest.ps1, tests, and Build-SharePackage.ps1 first.
This script packages existing builds; it does not certify live headset behavior.
"""
from pathlib import Path
from datetime import date, datetime, timezone
import argparse
import hashlib
import json
import shutil
import zipfile

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--quest-version', type=int, required=True)
parser.add_argument('--windows-version', required=True)
parser.add_argument('--installer-revision', type=int, default=6)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
quest = root / 'Quest'
windows = root / 'Unreal/Saved/VRWindowsArchive/Windows'
version = args.quest_version
quest_zip = quest / f'Share/AC-VR-Quest-v{version}-installer-r{args.installer_revision}.zip'
apk = quest / 'Packaged/Android_ASTC/ACUnreal-arm64.apk'
release = root / f'Releases/{date.today():%Y.%m.%d}-v{version}'
if release.exists():
    raise SystemExit(f'Release already exists; preserve it: {release}')
with zipfile.ZipFile(quest_zip) as archive:
    expected = {'AC-VR-arm64.apk', 'Install-Quest.ps1', 'Install-Quest.cmd', 'Update-Quest.cmd',
                'Repair-Quest.cmd', 'Quest-DataTransfer.ps1', 'Quest-ProfileMigration.ps1', 'README.txt', 'RELEASE-NOTES.md', 'LICENSE', 'manifest.json', 'START-HERE.html', 'AC-Icon.png'}
    assert set(archive.namelist()) == expected, 'Unexpected Quest bundle contents'
    assert archive.testzip() is None, 'Quest ZIP checksum failure'
    manifest = json.loads(archive.read('manifest.json').decode('utf-8-sig'))
    assert manifest['versionCode'] == version and manifest['installerRevision'] == args.installer_revision
    assert manifest['displayName'] == 'AC:VR', 'Rebuild the Quest bundle with current branding'
    assert manifest['apkSha256'] == sha(apk)
    assert hashlib.sha256(archive.read('AC-VR-arm64.apk')).hexdigest().upper() == manifest['apkSha256']
exe = windows / 'ACUnreal/Binaries/Win64/ACUnreal.exe'
assert exe.is_file(), 'Package Windows first'
# The source stamp is a consistency check, not a substitute for build validation.
stamp = (root / 'Unreal/Plugins/ACEClient/Source/ACEClient/Public/ACEClientBuild.h').read_text()
assert f'"{args.windows_version}"' in stamp, 'Windows version differs from source stamp'
assert f'ReleaseNumber = {version};' in stamp, 'Updater release number differs from package'
assert (windows/'Update-Client.ps1').is_file(), 'Updater helper missing from Windows package'
release.mkdir(parents=True)
shutil.copy2(quest_zip, release / quest_zip.name)
win_zip = release / f'AC-Unreal-and-AC-VR-Windows-v{version}.zip'
prefix = f'AC-Unreal-and-AC-VR-Windows-v{version}/'
members = []
with zipfile.ZipFile(win_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as archive:
    for path in sorted(windows.rglob('*')):
        if not path.is_file():
            continue
        rel = path.relative_to(windows)
        if any(part.lower() in {'saved', 'intermediate', 'source', 'logs', 'dats'} for part in rel.parts):
            continue
        if path.suffix.lower() in {'.pdb', '.log', '.dat', '.dmp'} or path.name.startswith('Manifest_'):
            continue
        if rel.parts[0] not in {'ACUnreal', 'Engine', 'ACUnreal.exe', 'Launch-VR.ps1', 'Launch-VR.bat', 'AC-Unreal.bat', 'AC-VR.bat', 'Update-Client.ps1', 'NOTICES.txt'}:
            continue
        assert path.name.lower() not in {'config.js', 'log4net.config'}, 'Local server config in runtime'
        archive.write(path, prefix + rel.as_posix())
        members.append(rel.as_posix())
    for source, name in [('Unreal/Build/VR/README-WINDOWS.txt', 'README-WINDOWS.txt'),
                         ('Quest/Sharing/RELEASE-NOTES.md', 'RELEASE-NOTES.md'), ('LICENSE', 'LICENSE')]:
        archive.write(root / source, prefix + name)
with zipfile.ZipFile(win_zip) as archive:
    assert archive.testzip() is None, 'Windows ZIP checksum failure'
shutil.copy2(quest / 'Sharing/RELEASE-NOTES.md', release / 'RELEASE-NOTES.md')
direct_apk = release / f'AC-VR-Quest-v{version}.apk'
shutil.copy2(apk, direct_apk)
archives = [release / quest_zip.name, win_zip]
(release / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in archives + [direct_apk]))
(release / 'release-manifest.json').write_text(json.dumps({
    'desktopProduct': 'AC:Unreal', 'vrProduct': 'AC:VR',
    'createdUtc': datetime.now(timezone.utc).isoformat(), 'questVersion': manifest['version'],
    'questVersionCode': version, 'installerRevision': args.installer_revision,
    'windowsVersion': args.windows_version, 'apkSha256': sha(apk), 'windowsExeSha256': sha(exe),
    'gameDataIncluded': False, 'serverIncluded': False, 'savedSettingsIncluded': False,
    'headsetAcceptance': 'See RELEASE-NOTES.md; packaging alone is not live acceptance.',
    'archives': [{'name': p.name, 'bytes': p.stat().st_size, 'sha256': sha(p)} for p in archives],
    'questApk': {'file': direct_apk.name, 'bytes': direct_apk.stat().st_size, 'sha256': sha(direct_apk)},
    'windowsFiles': members,
}, indent=2) + '\n')
print(json.dumps({'release': str(release), 'archives': [{'name':p.name,'bytes':p.stat().st_size} for p in archives]}, indent=2))
