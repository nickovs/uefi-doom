"""Tests for tools/dlog.py against the format in include/efi/dlog_core.h."""

from __future__ import annotations

import struct

import dlog
import pytest

ATTRS = struct.pack("<I", 0x6)  # BOOTSERVICE_ACCESS | RUNTIME_ACCESS


def info_bytes(**kw: object) -> bytes:
    f = {
        "magic": b"DLOG", "version": 1, "reason": 0, "status": 1, "count": 0, "chunk": 1024,
        "stored": 0, "dropped": 0, "head": 0, "run_us": 2_500_000, "build": b"abc-devloop",
    }  # fmt: skip
    f.update(kw)
    return struct.pack(
        "<4sHHQIIQQQQ64s",
        f["magic"], f["version"], f["reason"], f["status"], f["count"], f["chunk"],
        f["stored"], f["dropped"], f["head"], f["run_us"], f["build"],
    )  # fmt: skip


def var(name: str, data: bytes) -> tuple[str, bytes]:
    return f"{name}-{dlog.GUID}", ATTRS + data


def test_info_size_matches_c_layout() -> None:
    assert dlog.INFO_SIZE == 120


def test_roundtrip_two_chunks() -> None:
    files = dict(
        [
            var("DoomLogInfo", info_bytes(count=2, chunk=4, stored=6)),
            var("DoomLog000", b"hell"),
            var("DoomLog001", b"o\n"),
            ("Boot0000-8be4df61-93ca-11d0-aa0d-00e098032b8c", b"\0\0\0\0x"),  # ignored
        ]
    )
    info, text = dlog.decode_files(files)
    assert text == "hello\n"
    assert info.exit_reason_name == "quit"
    assert info.exit_status_name == "EFI_WARN_UNKNOWN_GLYPH"
    assert info.build_id == "abc-devloop"


def test_dropped_marker_inserted_after_head() -> None:
    files = dict(
        [
            var("DoomLogInfo", info_bytes(count=1, chunk=64, stored=8, dropped=99, head=4)),
            var("DoomLog000", b"HEADtail"),
        ]
    )
    _, text = dlog.decode_files(files)
    assert text == "HEAD\n[... 99 bytes dropped ...]\ntail"


@pytest.mark.parametrize(
    ("files", "msg"),
    [
        ({}, "no DoomLogInfo"),
        (dict([var("DoomLogInfo", info_bytes(magic=b"XXXX"))]), "bad magic"),
        (dict([var("DoomLogInfo", info_bytes(version=9))]), "unsupported version"),
        (dict([var("DoomLogInfo", b"short")]), "expected 120"),
        (dict([var("DoomLogInfo", info_bytes(count=2, stored=4))]), "missing chunks"),
        (
            dict([var("DoomLogInfo", info_bytes(count=1, stored=9)), var("DoomLog000", b"x")]),
            "header says 9",
        ),
        (dict([var("DoomLogInfo", info_bytes(stored=1, head=5))]), "exceeds"),
    ],
)
def test_malformed(files: dict[str, bytes], msg: str) -> None:
    with pytest.raises(dlog.DlogError, match=msg):
        dlog.decode_files(files)


def test_short_efivarfs_file() -> None:
    with pytest.raises(dlog.DlogError, match="too short"):
        dlog.strip_efivarfs(b"\0\0")


@pytest.mark.parametrize(
    ("status", "name"),
    [
        (0, "EFI_SUCCESS"),
        (1, "EFI_WARN_UNKNOWN_GLYPH"),
        ((1 << 63) | 1, "EFI_LOAD_ERROR"),
        ((1 << 63) | 21, "EFI_ABORTED"),
        ((1 << 63) | 77, "ERROR(77)"),
        (42, "WARN(42)"),
    ],
)
def test_status_names(status: int, name: str) -> None:
    assert dlog.status_name(status) == name


def test_report_format() -> None:
    info = dlog.parse_info(info_bytes(status=1, run_us=1_500_000))
    report = dlog.format_report(info, "line\n")
    assert "status EFI_WARN_UNKNOWN_GLYPH (0x1)" in report
    assert "1.500 s" in report
    assert report.endswith("line\n")
