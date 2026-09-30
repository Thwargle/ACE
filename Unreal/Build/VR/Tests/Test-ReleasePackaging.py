"""Small isolated fixtures for release assembly/staging; no real builds required.

Run: python Unreal/Build/VR/Tests/Test-ReleasePackaging.py
"""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True


def load_script(name):
    path = Path(__file__).resolve().parents[1] / f'{name}.py'
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load_script('Build-Release')
stage = load_script('Stage-WebsiteRelease')


def put(root, name, value=b'fixture'):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value.encode('utf-8') if isinstance(value, str) else value)
    return path


def elf(version, *, encoding='utf-16-le', machine=62):
    header = bytearray(64)
    header[:7] = b'\x7fELF\x02\x01\x01'
    header[16:18] = (3).to_bytes(2, 'little')
    header[18:20] = machine.to_bytes(2, 'little')
    return bytes(header) + (version + '\0').encode(encoding)


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ac-release-tests-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.site = Path(self.temp.name) / 'site'
        self.linux = self.root / 'Unreal/Saved/LinuxArchive/Linux'

    def fixture(self, version=88, with_linux=True):
        display = f'2026.09.30.{version}'
        self.args = argparse.Namespace(quest_version=version, windows_version=display,
                                       linux_version=display if with_linux else None, installer_revision=6)
        put(self.root, 'Unreal/Plugins/ACEClient/Source/ACEClient/Public/ACEClientBuild.h',
            f'inline constexpr int32 ReleaseNumber = {version};\nconst auto Version = "{display}";')
        apk = put(self.root, 'Quest/Packaged/Android_ASTC/ACUnreal-arm64.apk', b'fixture APK')
        manifest = {'versionCode': version, 'installerRevision': 6, 'version': display,
                    'displayName': 'AC:VR', 'apkSha256': build.sha(apk)}
        quest_zip = put(self.root, f'Quest/Share/AC-VR-Quest-v{version}-installer-r6.zip')
        with zipfile.ZipFile(quest_zip, 'w') as archive:
            for name in ('Install-Quest.ps1', 'Install-Quest.cmd', 'Update-Quest.cmd', 'Repair-Quest.cmd',
                         'Quest-DataTransfer.ps1', 'Quest-ProfileMigration.ps1', 'README.txt',
                         'RELEASE-NOTES.md', 'LICENSE', 'START-HERE.html', 'AC-Icon.png'):
                archive.writestr(name, b'fixture')
            archive.writestr('manifest.json', json.dumps(manifest))
            archive.writestr('AC-VR-arm64.apk', apk.read_bytes())
        put(self.root, 'Unreal/Saved/VRWindowsArchive/Windows/ACUnreal/Binaries/Win64/ACUnreal.exe',
            b'MZ' + (display + '\0').encode('utf-16-le'))
        put(self.root, 'Unreal/Saved/VRWindowsArchive/Windows/Update-Client.ps1')
        put(self.root, 'Unreal/Saved/VRWindowsArchive/Windows/ACUnreal/Content/Paks/game.pak')
        put(self.root, 'Unreal/Build/VR/README-WINDOWS.txt')
        put(self.root, 'Unreal/Build/VR/README-LINUX.txt', 'Linux launch and manual update instructions')
        put(self.root, 'Quest/Sharing/RELEASE-NOTES.md', 'Fixture release notes')
        put(self.root, 'LICENSE', 'Fixture license')
        put(self.root, 'Unreal/Build/Branding/AC-Icon.png')
        put(self.root, 'Unreal/Build/Windows/Application.ico')
        put(self.site, '_tools/build-ac-pages.py', '# Test site generator\n')
        if with_linux:
            put(self.linux, build.LINUX_EXE, elf(display))
            put(self.linux, 'AC-Unreal.sh', '#!/bin/sh\nexec ./ACUnreal/Binaries/Linux/ACUnreal -nohmd "$@"\n')
            put(self.linux, 'ACUnreal.sh', '#!/bin/sh\n# UAT bootstrap\n')
            put(self.linux, 'Engine/Binaries/Linux/CrashReportClient', elf('helper'))
            put(self.linux, 'Engine/Binaries/ThirdParty/Linux/libfixture.so', elf('library'))
            put(self.linux, 'ACUnreal/Content/Paks/game.pak')
            put(self.linux, 'ACUnreal/Content/Paks/game.utoc')
            put(self.linux, 'ACUnreal/Content/Paks/game.ucas')
        return self.args

    def assemble(self):
        with contextlib.redirect_stdout(io.StringIO()):
            release = build.build_release(self.root, self.args)
        # Build-WindowsInstaller.ps1 normally appends this after release assembly.
        installer = put(release, f'AC-Unreal-Setup-v{self.args.quest_version}.exe', b'fixture installer')
        meta = json.loads((release / 'release-manifest.json').read_text(encoding='utf-8'))
        meta['windowsInstaller'] = build.artifact(installer)
        self.write_meta(release, meta)
        return release, meta

    def write_meta(self, release, meta):
        (release / 'release-manifest.json').write_text(json.dumps(meta), encoding='utf-8')

    def publish_local(self, release, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return stage.stage_release(self.root, release, self.site, **kwargs)

    def test_linux_tar_preserves_launch_modes_and_excludes_private_build_files(self):
        self.fixture()
        excluded = (
            'ACUnreal/Saved/Config/Linux/Game.ini', 'ACUnreal/Source/private.cpp',
            'ACUnreal/Intermediate/object.o', 'ACUnreal/Logs/client.log',
            'ACUnreal/DATs/client_portal.dat', 'ACUnreal/Content/client_portal.DAT',
            'ACUnreal/Binaries/Linux/ACUnreal.debug', 'ACUnreal/Binaries/Linux/ACUnreal.sym',
            'ACUnreal/Binaries/Linux/ACUnreal.psym',
            'ACUnreal/Binaries/Linux/game.pdb', 'ACUnreal/Binaries/Win64/game.exe',
            'Engine/DerivedDataCache/cache', 'Engine/Source/private.h',
            'Engine/Binaries/Linux/Manifest_UFSFiles.txt', 'Engine/Binaries/Linux/helper.dll',
            'Manifest_NonUFSFiles.txt', 'Private.txt',
        )
        for name in excluded:
            put(self.linux, name)
        release, meta = self.assemble()
        archive_path = release / meta['linux']['file']
        self.assertEqual(meta['linuxVersion'], self.args.windows_version)
        self.assertEqual(meta['linux'], build.artifact(archive_path))
        self.assertEqual(meta['linuxExeSha256'], build.sha(self.linux / build.LINUX_EXE))
        self.assertIn(archive_path.name, (release / 'SHA256SUMS.txt').read_text())
        with tarfile.open(archive_path, 'r:gz') as archive:
            prefix = 'AC-Unreal-Linux-v88/'
            members = {member.name.removeprefix(prefix): member for member in archive.getmembers() if member.isfile()}
            for name in excluded:
                self.assertNotIn(name, members)
            for name in ('AC-Unreal.sh', 'ACUnreal.sh', build.LINUX_EXE.as_posix(),
                         'Engine/Binaries/Linux/CrashReportClient', 'Engine/Binaries/ThirdParty/Linux/libfixture.so'):
                self.assertEqual(members[name].mode, 0o755, name)
            for name in ('README-LINUX.txt', 'RELEASE-NOTES.md', 'LICENSE', 'ACUnreal/Content/Paks/game.pak'):
                self.assertEqual(members[name].mode, 0o644, name)
            self.assertEqual(archive.extractfile(prefix + 'LICENSE').read(), b'Fixture license')

    def test_v88_stages_linux_and_preserves_windows_quest_update_protocol(self):
        self.fixture()
        release, meta = self.assemble()
        public = self.publish_local(release)
        self.assertEqual(public['linux'], meta['linux'])
        self.assertEqual(public['linuxVersion'], '2026.09.30.88')
        self.assertEqual(public['questVersion'], '2026.09.30.quest.88')
        self.assertEqual(public['questInstalledVersion'], '2026.09.30.88')
        self.assertEqual(public['updates'], {'schema': 1, 'windows': meta['windowsInstaller'],
                                            'quest': meta['questApk'], 'linux': meta['linux']})
        target = self.site / 'downloads/ac/v88' / meta['linux']['file']
        self.assertEqual(build.sha(target), meta['linux']['sha256'])

    def test_v87_without_linux_still_stages(self):
        self.fixture(version=87, with_linux=False)
        release, _ = self.assemble()
        public = self.publish_local(release)
        self.assertNotIn('linux', public)
        self.assertNotIn('linuxVersion', public)
        self.assertEqual(set(public['updates']), {'schema', 'windows', 'quest'})

    def test_release_88_cannot_omit_linux(self):
        self.fixture(with_linux=False)
        with self.assertRaisesRegex(ValueError, 'require a native Linux build'):
            build.build_release(self.root, self.args)
        self.assertFalse((self.root / 'Releases').exists())

    def test_linux_missing_stale_wrong_architecture_or_unreal_string_encoding_rejected(self):
        self.fixture()
        exe = self.linux / build.LINUX_EXE
        for value in (elf('2026.09.30.87'), elf('2026.09.30.880'), elf(self.args.linux_version, machine=183),
                      elf(self.args.linux_version, encoding='utf-8'), elf(self.args.linux_version, encoding='utf-32-le'), b'MZ'):
            with self.subTest(binary=value):
                exe.write_bytes(value)
                with self.assertRaises(ValueError):
                    build.build_release(self.root, self.args)
                self.assertFalse((self.root / 'Releases').exists())
        exe.unlink()
        with self.assertRaisesRegex(ValueError, 'Package Linux first'):
            build.build_release(self.root, self.args)

    def test_linux_cooked_content_and_launcher_required(self):
        self.fixture()
        (self.linux / 'ACUnreal/Content/Paks/game.pak').unlink()
        with self.assertRaisesRegex(ValueError, 'cooked content is missing'):
            build.build_release(self.root, self.args)
        put(self.linux, 'ACUnreal/Content/Paks/game.pak')
        (self.linux / 'AC-Unreal.sh').unlink()
        with self.assertRaisesRegex(ValueError, 'launcher is missing'):
            build.build_release(self.root, self.args)
        self.assertFalse((self.root / 'Releases').exists())

    def test_mismatched_linux_version_rejected_by_build_and_stage(self):
        self.fixture()
        self.args.linux_version = '2026.09.30.87'
        with self.assertRaisesRegex(ValueError, 'must display the same version'):
            build.build_release(self.root, self.args)
        self.args.linux_version = self.args.windows_version
        release, meta = self.assemble()
        meta['linuxVersion'] = '2026.09.30.87'
        self.write_meta(release, meta)
        with self.assertRaisesRegex(ValueError, 'must display the same version'):
            self.publish_local(release)
        self.assertFalse((self.site / 'downloads').exists())

    def test_v88_cannot_stage_without_linux_metadata(self):
        self.fixture()
        release, meta = self.assemble()
        del meta['linux']
        del meta['linuxVersion']
        self.write_meta(release, meta)
        with self.assertRaisesRegex(ValueError, 'require the native Linux archive'):
            self.publish_local(release)
        self.assertFalse((self.site / 'downloads').exists())

    def test_corrupt_linux_archive_fails_before_staging(self):
        self.fixture()
        release, meta = self.assemble()
        (release / meta['linux']['file']).write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            self.publish_local(release)
        self.assertFalse((self.site / 'downloads').exists())

    def test_linux_cannot_use_windows_fallback(self):
        self.fixture()
        release, meta = self.assemble()
        meta['linux'] = meta['windowsInstaller']
        self.write_meta(release, meta)
        with self.assertRaisesRegex(ValueError, 'native versioned tar.gz archive'):
            self.publish_local(release)
        self.assertFalse((self.site / 'downloads').exists())

    def test_existing_linux_artifact_is_immutable_unless_explicitly_unpublished(self):
        self.fixture()
        release, meta = self.assemble()
        target = put(self.site, 'downloads/ac/v88/' + meta['linux']['file'], b'previously distributed')
        with self.assertRaisesRegex(ValueError, 'different published artifact'):
            self.publish_local(release)
        self.assertEqual(target.read_bytes(), b'previously distributed')
        self.assertFalse((target.parent / meta['windowsInstaller']['file']).exists())
        self.assertFalse((self.site / 'assets/ac/release.json').exists())
        self.publish_local(release, replace_unpublished=True)
        self.assertEqual(build.sha(target), meta['linux']['sha256'])
        # An identical local restage remains allowed without the override.
        self.publish_local(release)


if __name__ == '__main__':
    unittest.main(verbosity=2)
