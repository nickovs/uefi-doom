"""Run the host-built C unit tests (built by `make unit`) as pytest tests."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = sorted((ROOT / "tests" / "unit").glob("test_*.c"))


@pytest.fixture(scope="session")
def built() -> Path:
    subprocess.run(["make", "-s", "unit"], cwd=ROOT, check=True)
    return ROOT / "build" / "host"


@pytest.mark.parametrize("src", SOURCES, ids=lambda p: p.stem)
def test_c_unit(built: Path, src: Path) -> None:
    exe = built / src.stem
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    assert p.returncode == 0, p.stderr
