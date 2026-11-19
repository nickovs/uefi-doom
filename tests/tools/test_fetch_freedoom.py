"""tools/fetch_freedoom.py: verification and extraction, offline (file:// URLs)."""

from __future__ import annotations

import hashlib
import zipfile
from pathlib import Path

import fetch_freedoom as ff
import pytest


def make_zip(path: Path, members: dict[str, bytes]) -> str:
    with zipfile.ZipFile(path, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_fetch_verifies_and_extracts(tmp_path: Path) -> None:
    archive = tmp_path / "fd.zip"
    sha = make_zip(
        archive,
        {
            "freedoom-x/freedoom1.wad": b"IWAD1",
            "freedoom-x/freedoom2.wad": b"IWAD2",
            "x/README": b"",
        },
    )
    dest = tmp_path / "data"
    got = ff.fetch(dest, archive.as_uri(), sha)
    assert [p.name for p in got] == ["freedoom1.wad", "freedoom2.wad"]
    assert (dest / "freedoom2.wad").read_bytes() == b"IWAD2"
    # Already present: no download (a bad URL would otherwise fail).
    assert ff.fetch(dest, "file:///nonexistent", "0" * 64) == got


def test_fetch_rejects_hash_mismatch(tmp_path: Path) -> None:
    archive = tmp_path / "fd.zip"
    make_zip(archive, {"freedoom1.wad": b"x", "freedoom2.wad": b"y"})
    with pytest.raises(ff.FetchError, match="SHA-256 mismatch"):
        ff.fetch(tmp_path / "data", archive.as_uri(), "0" * 64)
    assert not (tmp_path / "data").exists()


def test_extract_reports_missing_wad(tmp_path: Path) -> None:
    archive = tmp_path / "fd.zip"
    make_zip(archive, {"freedoom1.wad": b"x"})
    with pytest.raises(ff.FetchError, match="freedoom2.wad not found"):
        ff.extract_wads(archive, tmp_path / "out")


def test_main_reports_errors(tmp_path: Path, monkeypatch, capsys) -> None:
    def boom(dest: Path) -> list[Path]:
        raise ff.FetchError("nope")

    monkeypatch.setattr(ff, "fetch", boom)
    assert ff.main([str(tmp_path)]) == 1
    assert "nope" in capsys.readouterr().err
