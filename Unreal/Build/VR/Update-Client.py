#!/usr/bin/env python3
"""Install a verified Linux release in place. Only the requesting client is stopped.

Uses Python 3 standard library; no administrator access or package manager.
User Saved folders, DATs, plugins and files absent from the archive are retained.
"""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import subprocess
import tarfile
import tempfile
import time

EXE = Path('ACUnreal/Binaries/Linux/ACUnreal')
ROOTS = {'ACUnreal', 'Engine', 'AC-Unreal.sh', 'ACUnreal.sh', 'NOTICES.txt',
         'README-LINUX.txt', 'RELEASE-NOTES.md', 'LICENSE', 'Update-Client.py'}
PROTECTED = {'saved', 'dats', 'logs', 'source', 'intermediate', 'deriveddatacache'}


class RollbackError(RuntimeError):
    pass


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def check_target(root, rel):
    current = root
    for part in rel.parts:
        current /= part
        require(not current.is_symlink(), 'Update target contains a symbolic link: ' + str(rel))
    return current


def validate_root(root):
    require(root.is_dir() and root != Path('/'), 'Invalid installation folder')
    require((root / EXE).is_file() and (root / 'AC-Unreal.sh').is_file(), 'Installation is missing its client or launcher')
    check_target(root, EXE)
    check_target(root, Path('AC-Unreal.sh'))


def stage_archive(archive, sha256, release, root, stage):
    require(re.fullmatch(r'[0-9a-fA-F]{64}', sha256), 'Invalid expected SHA-256')
    require(digest(archive) == sha256.lower(), 'Archive checksum mismatch; nothing was installed')
    prefix = 'AC-Unreal-Linux-v' + str(release)
    files, names, total = [], set(), 0
    with tarfile.open(archive, 'r:gz') as package:
        for member in package:
            path = PurePosixPath(member.name)
            require(not path.is_absolute() and '..' not in path.parts and '\\' not in member.name,
                    'Unsafe archive path')
            require(path.parts and path.parts[0] == prefix, 'Incorrect release archive root')
            require(member.isdir() or member.isfile(), 'Links and special files are not supported in updates')
            if len(path.parts) == 1:
                require(member.isdir(), 'Invalid archive root')
                continue
            rel = Path(*path.parts[1:])
            require(rel.parts[0] in ROOTS and not any(p.lower() in PROTECTED for p in rel.parts),
                    'Archive attempts to replace user data or an unknown root')
            require(rel.suffix.lower() != '.dat', 'Updates cannot replace game DATs')
            check_target(root, rel)
            if member.isdir():
                continue
            require(rel not in names and 0 <= member.size <= 2 * 1024**3, 'Duplicate or oversized archive member')
            names.add(rel)
            files.append((member, rel))
            total += member.size
            require(len(files) <= 100000 and total <= 12 * 1024**3, 'Archive exceeds update limits')
        require(EXE in names and Path('AC-Unreal.sh') in names, 'Archive has no Linux client or launcher')
        require(shutil.disk_usage(stage).free > total + 100 * 1024**2, 'Not enough space to unpack the update')
        for member, rel in files:
            target = stage / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            with package.extractfile(member) as source, target.open('xb') as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)
            # Discard special permission bits. Preserve executable scripts/libraries.
            target.chmod(0o755 if member.mode & 0o111 else 0o644)
    with (stage / EXE).open('rb') as stream:
        header = stream.read(64)
    require(len(header) == 64 and header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00',
            'Update executable is not Linux x86_64')
    (stage / EXE).chmod(0o755)
    (stage / 'AC-Unreal.sh').chmod(0o755)
    return names


def process_identity(pid):
    try:
        stat = Path('/proc', str(pid), 'stat').read_text().rsplit(')', 1)[1].split()
        if stat[0] == 'Z':
            return None
        return os.readlink('/proc/' + str(pid) + '/exe'), stat[19]
    except (OSError, IndexError):
        return None


def wait_for_client(pid, identity, timeout=60):
    if not identity:
        return
    deadline = time.monotonic() + timeout
    while process_identity(pid) == identity and time.monotonic() < deadline:
        time.sleep(.2)
    if process_identity(pid) != identity:
        return
    # Shutdown was explicitly requested. Never kill by process name or a
    # recycled PID; the executable AND /proc start time must still match.
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 10
    while process_identity(pid) == identity and time.monotonic() < deadline:
        time.sleep(.2)
    if process_identity(pid) == identity:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + 5
        while process_identity(pid) == identity and time.monotonic() < deadline:
            time.sleep(.1)
    require(process_identity(pid) != identity, 'The requesting client could not be stopped')


def install_files(root, stage, names, backup):
    changes = []
    # Retire obsolete cooked pak files so old assets cannot shadow new ones.
    # Never sweep user plugins/configuration or the rest of the install tree.
    old_paks = root / 'ACUnreal/Content/Paks'
    obsolete = {p.relative_to(root) for p in old_paks.rglob('*') if p.is_file()
                and p.suffix.lower() in {'.pak', '.utoc', '.ucas'} and p.relative_to(root) not in names}
    try:
        for rel in sorted(names | obsolete):
            target = check_target(root, rel)
            require(not target.exists() or target.is_file(), 'Update destination is not a file: ' + str(rel))
            saved = backup / rel
            had_old = target.exists()
            if had_old:
                saved.parent.mkdir(parents=True, exist_ok=True)
                os.replace(target, saved)
            changes.append((rel, had_old))
            if rel in names:
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(stage / rel, target)
    except BaseException:
        failures = []
        for rel, had_old in reversed(changes):
            try:
                target = root / rel
                if had_old:
                    os.replace(backup / rel, target)
                elif target.exists():
                    target.unlink()
            except OSError as error:
                failures.append(str(error))
        if failures:
            raise RollbackError('Could not restore all previous files. Recovery copy retained at ' + str(backup))
        raise


def run(args):
    root = args.root.resolve()
    validate_root(root)
    identity = process_identity(args.pid)
    require(not identity or identity[0] == str(root / EXE), 'Requesting process is not this installation')
    # A writable install is required; the updater never elevates privileges.
    with (root / '.ac-update.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        recovery_required = False
        temporary = Path(tempfile.mkdtemp(prefix='.ac-update-', dir=root))
        try:
            try:
                stage = Path(temporary) / 'new'; stage.mkdir()
                backup = Path(temporary) / 'previous'; backup.mkdir()
                names = stage_archive(args.archive, args.sha256, args.release, root, stage)
                wait_for_client(args.pid, identity)
                for proc in Path('/proc').iterdir():
                    if proc.name.isdigit():
                        other = process_identity(int(proc.name))
                        require(not other or other[0] != str(root / EXE),
                                'Another client uses this installation. Close it before updating.')
                install_files(root, stage, names, backup)
            except RollbackError:
                recovery_required = True
                raise
            args.result.write_text('Updated successfully.\n', encoding='utf-8')
            # Cleanup failure must not turn a successful install into a failure
            # or prevent restart. Only remove this verified, installed payload.
            try:
                args.archive.unlink()
            except OSError:
                pass
        except BaseException as error:
            args.result.write_text('Linux update failed: ' + str(error) + '\n', encoding='utf-8')
            raise
        finally:
            if not recovery_required:
                shutil.rmtree(temporary, ignore_errors=True)
            wait_for_client(args.pid, identity)
            # If validation failed after the game closed, reopen the unchanged
            # client. Otherwise never launch a duplicate of the requesting PID.
            if not recovery_required and (process_identity(args.pid) != identity or identity is None):
                subprocess.Popen(['/bin/sh', str(root / 'AC-Unreal.sh')], cwd=root,
                                 start_new_session=True, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--release', type=int, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--result', type=Path, required=True)
    args = parser.parse_args()
    require(0 < args.release < 100000 and args.pid > 1, 'Invalid update request')
    try:
        run(args)
    except Exception as error:
        args.result.write_text('Linux update failed: ' + str(error) + '\n', encoding='utf-8')
        raise SystemExit(1)


if __name__ == '__main__':
    main()
