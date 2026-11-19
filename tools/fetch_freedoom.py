#!/usr/bin/env python3
"""Download the pinned Freedoom release and extract its IWADs into tests/data/.

The archive's SHA-256 is pinned, so a changed or tampered download is rejected.
Usable as a module (``fetch``) or a command (``tools/fetch_freedoom.py [dest]``).
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "0.13.0"
URL = f"https://github.com/freedoom/freedoom/releases/download/v{VERSION}/freedoom-{VERSION}.zip"
SHA256 = "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"
WADS = ("freedoom1.wad", "freedoom2.wad")
DEFAULT_DEST = Path(__file__).resolve().parent.parent / "tests" / "data"


class FetchError(Exception):
    """The download failed verification or did not contain the expected WADs."""


def sha256_file(path: Path) -> str:
    """Hex SHA-256 of a file."""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def extract_wads(archive: Path, dest: Path, names: tuple[str, ...] = WADS) -> list[Path]:
    """Copy each named WAD (matched by file name, any directory) out of the zip."""
    dest.mkdir(parents=True, exist_ok=True)
    out = []
    with zipfile.ZipFile(archive) as z:
        by_name = {Path(i.filename).name: i for i in z.infolist() if not i.is_dir()}
        for name in names:
            info = by_name.get(name)
            if info is None:
                raise FetchError(f"{name} not found in {archive.name}")
            target = dest / name
            with z.open(info) as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            out.append(target)
    return out


def fetch(dest: Path = DEFAULT_DEST, url: str = URL, sha256: str = SHA256) -> list[Path]:
    """Download (unless all WADs exist), verify and extract. Returns the WAD paths."""
    existing = [dest / n for n in WADS]
    if all(p.exists() for p in existing):
        return existing
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / "freedoom.zip"
        with urllib.request.urlopen(url, timeout=120) as r, archive.open("wb") as f:
            shutil.copyfileobj(r, f)
        got = sha256_file(archive)
        if got != sha256:
            raise FetchError(f"SHA-256 mismatch: expected {sha256}, got {got}")
        return extract_wads(archive, dest)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dest", nargs="?", type=Path, default=DEFAULT_DEST)
    args = ap.parse_args(argv)
    try:
        for p in fetch(args.dest):
            print(p)
    except (FetchError, OSError) as e:
        print(f"fetch_freedoom: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
