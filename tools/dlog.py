"""Parse the volatile DoomLog UEFI variables written by devloop builds.

The format is defined by ``include/efi/dlog_core.h``. On Linux each variable is
a file in efivarfs named ``<Name>-<GUID>`` whose content is a 4-byte
little-endian attribute word followed by the variable data.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass

GUID = "89c5fe58-f223-4f55-a8dc-6e4cfee8bf6a"
INFO_NAME = "DoomLogInfo"
CHUNK_RE = re.compile(r"^DoomLog(\d{3})$")
VERSION = 1

_INFO_FMT = "<4sHHQIIQQQQ64s"
INFO_SIZE = struct.calcsize(_INFO_FMT)  # 120, matches the C _Static_assert

EXIT_REASONS = {0: "quit", 1: "fatal", 2: "init-failure", 3: "emergency"}

_ERROR_NAMES = [
    "EFI_SUCCESS", "EFI_LOAD_ERROR", "EFI_INVALID_PARAMETER", "EFI_UNSUPPORTED",
    "EFI_BAD_BUFFER_SIZE", "EFI_BUFFER_TOO_SMALL", "EFI_NOT_READY", "EFI_DEVICE_ERROR",
    "EFI_WRITE_PROTECTED", "EFI_OUT_OF_RESOURCES", "EFI_VOLUME_CORRUPTED", "EFI_VOLUME_FULL",
    "EFI_NO_MEDIA", "EFI_MEDIA_CHANGED", "EFI_NOT_FOUND", "EFI_ACCESS_DENIED",
    "EFI_NO_RESPONSE", "EFI_NO_MAPPING", "EFI_TIMEOUT", "EFI_NOT_STARTED",
    "EFI_ALREADY_STARTED", "EFI_ABORTED",
]  # fmt: skip
_WARN_NAMES = {
    1: "EFI_WARN_UNKNOWN_GLYPH",
    2: "EFI_WARN_DELETE_FAILURE",
    3: "EFI_WARN_WRITE_FAILURE",
    4: "EFI_WARN_BUFFER_TOO_SMALL",
    5: "EFI_WARN_STALE_DATA",
}
_ERROR_BIT = 1 << 63


class DlogError(ValueError):
    """The DoomLog variables are missing, malformed or inconsistent."""


@dataclass(frozen=True)
class DlogInfo:
    """Decoded DoomLogInfo header."""

    exit_reason: int
    exit_status: int
    chunk_count: int
    chunk_size: int
    stored_bytes: int
    dropped_bytes: int
    head_bytes: int
    run_time_us: int
    build_id: str

    @property
    def exit_reason_name(self) -> str:
        return EXIT_REASONS.get(self.exit_reason, f"unknown({self.exit_reason})")

    @property
    def exit_status_name(self) -> str:
        return status_name(self.exit_status)


def status_name(status: int) -> str:
    """Return the UEFI name of an EFI_STATUS value."""
    if status & _ERROR_BIT:
        code = status & ~_ERROR_BIT
        if code < len(_ERROR_NAMES):
            return _ERROR_NAMES[code]
        return f"ERROR({code})"
    if status == 0:
        return "EFI_SUCCESS"
    return _WARN_NAMES.get(status, f"WARN({status})")


def strip_efivarfs(raw: bytes) -> tuple[int, bytes]:
    """Split efivarfs file content into (attributes, data)."""
    if len(raw) < 4:
        raise DlogError(f"efivarfs content too short ({len(raw)} bytes)")
    (attrs,) = struct.unpack_from("<I", raw)
    return attrs, raw[4:]


def parse_info(data: bytes) -> DlogInfo:
    """Decode and validate the DoomLogInfo variable data."""
    if len(data) != INFO_SIZE:
        raise DlogError(f"DoomLogInfo is {len(data)} bytes, expected {INFO_SIZE}")
    (magic, version, reason, status, count, chunk, stored, dropped, head, run_us, build) = (
        struct.unpack(_INFO_FMT, data)
    )
    if magic != b"DLOG":
        raise DlogError(f"bad magic {magic!r}")
    if version != VERSION:
        raise DlogError(f"unsupported version {version}")
    if head > stored:
        raise DlogError(f"head_bytes {head} exceeds stored_bytes {stored}")
    return DlogInfo(
        exit_reason=reason,
        exit_status=status,
        chunk_count=count,
        chunk_size=chunk,
        stored_bytes=stored,
        dropped_bytes=dropped,
        head_bytes=head,
        run_time_us=run_us,
        build_id=build.split(b"\0", 1)[0].decode("utf-8", "replace"),
    )


def assemble(info: DlogInfo, chunks: dict[int, bytes]) -> str:
    """Join chunk data into the log text, marking any dropped region."""
    missing = [i for i in range(info.chunk_count) if i not in chunks]
    if missing:
        raise DlogError(f"missing chunks: {missing}")
    data = b"".join(chunks[i] for i in range(info.chunk_count))
    if len(data) != info.stored_bytes:
        raise DlogError(f"chunks hold {len(data)} bytes, header says {info.stored_bytes}")
    if info.dropped_bytes:
        marker = f"\n[... {info.dropped_bytes} bytes dropped ...]\n".encode()
        data = data[: info.head_bytes] + marker + data[info.head_bytes :]
    return data.decode("utf-8", "replace")


def decode_files(files: dict[str, bytes]) -> tuple[DlogInfo, str]:
    """Decode a mapping of efivarfs file name -> raw content into (info, log)."""
    info: DlogInfo | None = None
    chunks: dict[int, bytes] = {}
    suffix = "-" + GUID
    for fname, raw in files.items():
        if not fname.endswith(suffix):
            continue
        name = fname[: -len(suffix)]
        _, data = strip_efivarfs(raw)
        if name == INFO_NAME:
            info = parse_info(data)
        elif m := CHUNK_RE.match(name):
            chunks[int(m.group(1))] = data
    if info is None:
        raise DlogError("no DoomLogInfo variable (app did not exit cleanly, or the machine reset)")
    return info, assemble(info, chunks)


def format_report(info: DlogInfo, log: str) -> str:
    """Human-readable summary followed by the log text."""
    lines = [
        f"build:   {info.build_id}",
        f"exit:    {info.exit_reason_name}, status {info.exit_status_name} "
        f"(0x{info.exit_status:x})",
        f"runtime: {info.run_time_us / 1e6:.3f} s",
        f"log:     {info.stored_bytes} bytes in {info.chunk_count} chunk(s)"
        + (f", {info.dropped_bytes} dropped" if info.dropped_bytes else ""),
        "-" * 72,
        log.rstrip("\n"),
    ]
    return "\n".join(lines) + "\n"
