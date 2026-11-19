"""Tests for the pure parts of tools/systest.py."""

from __future__ import annotations

import pytest
import systest


@pytest.mark.parametrize(
    ("args", "text"),
    [
        (["-warp", "1", "1"], "-warp 1 1"),
        (["-file", "my file.wad"], '-file "my file.wad"'),
        (["", "x"], '"" x'),
    ],
)
def test_load_options_text(args: list[str], text: str) -> None:
    assert systest.load_options_text(args) == text


def test_load_options_rejects_quotes() -> None:
    with pytest.raises(systest.SystestError):
        systest.load_options_text(['say "hi"'])


def test_entry_regex_parses_efibootmgr_lines() -> None:
    m = systest.ENTRY_RE.match("Boot0006* linux\tHD(1,GPT,...)/\\EFI\\linux\\grubx64.efi\t")
    assert m and m.group(1) == "0006" and m.group(2) == "linux"
    m = systest.ENTRY_RE.match("Boot0002* NVMe SSD \tPciRoot(0x0)\t")
    assert m and m.group(2).strip() == "NVMe SSD"
