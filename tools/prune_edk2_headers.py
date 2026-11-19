#!/usr/bin/env python3
"""Prune third_party/edk2/MdePkg/Include to the headers the build needs.

Kept: every EDK2 header our sources include (from the compiler's dependency
output) plus the headers listed in third_party/edk2/KEEP.txt and everything
those include. Everything else is deleted.

To add a header later: list it in KEEP.txt, then run with --source pointing at
a full checkout of MdePkg/Include at the commit named in UPSTREAM.md; the
script copies the needed files back in before pruning.

    tools/prune_edk2_headers.py [--source /path/to/edk2/MdePkg/Include] [--dry-run]
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "third_party" / "edk2" / "MdePkg" / "Include"
KEEP = ROOT / "third_party" / "edk2" / "KEEP.txt"


def keep_list() -> list[str]:
    lines = KEEP.read_text().splitlines()
    return [x.strip() for x in lines if x.strip() and not x.lstrip().startswith("#")]


def deps_for(includes: list[str], inc_root: Path) -> set[Path]:
    """Headers (under inc_root) pulled in by a TU that includes `includes`."""
    src = "".join(f"#include <{h}>\n" for h in includes)
    with tempfile.TemporaryDirectory() as td:
        tu = Path(td) / "keep.c"
        tu.write_text("#undef NULL\n#include <Uefi.h>\n" + src)
        cmd = [
            "clang", "-target", "x86_64-unknown-windows", "-ffreestanding", "-fshort-wchar",
            "-nostdlibinc", "-isystem", str(ROOT / "include" / "libc"),
            f"-I{inc_root}", f"-I{inc_root / 'X64'}", "-M", str(tu),
        ]  # fmt: skip
        out = subprocess.run(cmd, capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(f"dependency scan failed:\n{out.stderr}")
    return {p.resolve() for p in _paths(out.stdout) if _under(p, inc_root)}


def build_deps() -> set[Path]:
    """EDK2 headers named in .d files from previous builds."""
    found: set[Path] = set()
    for d in (ROOT / "build").rglob("*.d"):
        for p in _paths(d.read_text()):
            if _under(p, INC):
                found.add(p.resolve())
    return found


def _paths(make_deps: str) -> list[Path]:
    words = make_deps.replace("\\\n", " ").split()
    return [Path(w) if Path(w).is_absolute() else ROOT / w for w in words if w.endswith(".h")]


def _under(p: Path, root: Path) -> bool:
    try:
        (p if p.is_absolute() else ROOT / p).resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", type=Path, help="full MdePkg/Include to restore files from")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if not any((ROOT / "build").rglob("*.d")):
        sys.exit("no dependency files: run `make hwprobe test-efi` first")

    if a.source:
        need = deps_for(keep_list(), a.source)
        for p in need:
            rel = p.relative_to(a.source.resolve())
            dst = INC / rel
            if not dst.exists() and not a.dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dst)

    keep = build_deps() | deps_for(keep_list(), INC)
    removed = 0
    for f in sorted(INC.rglob("*")):
        if f.is_file() and f.resolve() not in keep:
            removed += 1
            if not a.dry_run:
                f.unlink()
    if not a.dry_run:
        for d in sorted(INC.rglob("*"), reverse=True):
            if d.is_dir() and not any(d.iterdir()):
                d.rmdir()
    print(f"kept {len(keep)} headers, {'would remove' if a.dry_run else 'removed'} {removed}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
