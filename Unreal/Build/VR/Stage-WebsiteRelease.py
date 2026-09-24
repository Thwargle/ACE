"""Copy verified public downloads into the local Thwargle website; never upload.

Example: python Stage-WebsiteRelease.py Releases/2026.09.19-v50
The website remains plain static HTML. Its page generator consumes release.json.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('release',type=Path)
parser.add_argument('--site',type=Path,default=Path('C:/dev/Thwargle.com'))
parser.add_argument('--replace-unpublished',action='store_true',help='Refresh this local staging copy before its first upload; never use for an already distributed release.')
args=parser.parse_args()
root=Path(__file__).resolve().parents[3]
release=args.release.resolve()
site=args.site.resolve()
meta=json.loads((release/'release-manifest.json').read_text(encoding='utf-8-sig'))
version=meta['questVersionCode']
assert (site/'_tools/build-ac-pages.py').is_file(), 'Website page source is missing'
def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest().upper()
win=meta['windowsInstaller']
quest=next(x for x in meta['archives'] if x['name'].startswith('AC-VR-Quest-'))
portable=next(x for x in meta['archives'] if x['name'].startswith('AC-Unreal-and-AC-VR-'))
items={}
for key,entry in [('windows',win),('quest',quest),('portable',portable),('questApk',meta['questApk'])]:
    name=entry.get('file',entry.get('name'))
    assert Path(name).name==name, 'Public filename must not contain directories'
    src=release/name
    assert src.stat().st_size==entry['bytes'] and sha(src)==entry['sha256'], f'Checksum mismatch: {name}'
    items[key]={'file':name,'bytes':entry['bytes'],'sha256':entry['sha256']}
out=site/f'downloads/ac/v{version}'
out.mkdir(parents=True,exist_ok=True)
for entry in items.values():
    target=out/entry['file']
    if target.exists() and sha(target)!=entry['sha256'] and not args.replace_unpublished:
        raise SystemExit(f'Refusing to overwrite a different published artifact: {target}')
    if not target.exists() or (args.replace_unpublished and sha(target)!=entry['sha256']):
        shutil.copy2(release/entry['file'],target)
    assert sha(target)==entry['sha256']
for name in ['SHA256SUMS.txt','RELEASE-NOTES.md']:
    shutil.copy2(release/name,out/name)
assets=site/'assets/ac'
assets.mkdir(parents=True,exist_ok=True)
shutil.copy2(root/'Unreal/Build/Branding/AC-Icon.png',assets/'ac-icon.png')
shutil.copy2(root/'Unreal/Build/Windows/Application.ico',assets/'ac-icon.ico')
public={'version':version,'date':meta['createdUtc'][:10],'windowsVersion':meta['windowsVersion'],
        'questVersion':meta['questVersion'],'windowsSigned':False,'gameDataIncluded':False,**items}
public['updates']={'schema':1,'windows':items['windows'],'quest':items['questApk']}
(assets/'release.json').write_text(json.dumps(public,indent=2)+'\n',encoding='utf-8')
subprocess.run([sys.executable,str(site/'_tools/build-ac-pages.py')],check=True)
print(f'Staged public website downloads in {out}. No remote upload performed.')
