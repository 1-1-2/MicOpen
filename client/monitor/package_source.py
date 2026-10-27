"""Collect only Git-visible source files; never package dependencies or credentials."""
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
files = subprocess.check_output(
    ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT
).decode().split('\0')
with tarfile.open(ROOT / 'client/build/monitor-source.tar.gz', 'w:gz') as out:
    for name in sorted(set(files)):
        if not name:
            continue
        path = ROOT / name
        if path.is_file() and not path.is_symlink():
            out.add(path, arcname=name, recursive=False)
