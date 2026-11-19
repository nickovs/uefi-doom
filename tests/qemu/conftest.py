"""QEMU + OVMF fixtures for system tests (marked `qemu`).

Each test stages files in an ESP directory, which start() packs into a raw
FAT32 image with mtools (QEMU's vvfat loses guest writes). QEMU runs headless
via tools/run_qemu.sh. Our DEBUGCON=1 builds log to app.log (port 0x403).
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path

import pytest
from qemu.qmp.legacy import QEMUMonitorProtocol

ROOT = Path(__file__).resolve().parents[2]
EFI_DIR = ROOT / "build" / "x64-devloop-debugcon"
WAD_DIR = ROOT / "tests" / "data"
# Distribution OVMF builds (e.g. Debian/Ubuntu) omit the built-in Shell; their
# standalone Shell package fills in. QEMU's own EDK2 build (Homebrew) has one.
UEFI_SHELL = os.environ.get("UEFI_SHELL") or next(
    (p for p in ["/usr/share/efi-shell-x64/shellx64.efi"] if os.path.exists(p)), None
)


class Qemu:
    """One headless QEMU instance booting an ESP directory."""

    def __init__(self, workdir: Path) -> None:
        self.workdir = workdir
        self.esp = workdir / "esp"
        self.image = workdir / "esp.img"
        self.logdir = workdir / "logs"
        self.proc: subprocess.Popen[bytes] | None = None
        # Unix socket paths are limited to ~104 bytes; pytest's tmp_path is too long.
        self._sockdir = tempfile.mkdtemp(prefix="udq")
        self.qmp_path = os.path.join(self._sockdir, "qmp.sock")
        self._qmp: QEMUMonitorProtocol | None = None
        (self.esp / "EFI" / "BOOT").mkdir(parents=True)
        (self.esp / "EFI" / "DOOM").mkdir(parents=True)
        self.logdir.mkdir()

    def install(self, efi_name: str, as_boot: bool = True, build_dir: Path = EFI_DIR) -> None:
        """Copy a built EFI app into \\EFI\\DOOM (and as BOOTX64.EFI if as_boot)."""
        src = build_dir / efi_name
        shutil.copy(src, self.esp / "EFI" / "DOOM" / efi_name)
        if as_boot:
            shutil.copy(src, self.esp / "EFI" / "BOOT" / "BOOTX64.EFI")

    def wad(self, name: str = "freedoom1.wad", subdir: str = "DOOM") -> None:
        """Copy a fetched WAD into \\EFI\\<subdir>; skip the test if it is missing."""
        src = WAD_DIR / name
        if not src.exists():
            pytest.skip(f"{name} not fetched (run tools/fetch_freedoom.py)")
        shutil.copy(src, self.esp / "EFI" / subdir / name)

    def startup_nsh(self, *lines: str) -> None:
        """Boot a UEFI Shell running these commands: a standalone Shell as
        BOOTX64.EFI if one is installed, else OVMF's built-in Shell."""
        boot = self.esp / "EFI" / "BOOT" / "BOOTX64.EFI"
        boot.unlink(missing_ok=True)
        if UEFI_SHELL:
            shutil.copy(UEFI_SHELL, boot)
        (self.esp / "startup.nsh").write_text("\r\n".join(["fs0:", *lines]) + "\r\n")

    def esp_file(self, rel: str) -> str:
        """Read a file from the FAT image ("" if absent). Safe while QEMU runs."""
        p = subprocess.run(
            ["mtype", "-i", str(self.image), "::/" + rel], capture_output=True, check=False
        )
        return p.stdout.decode(errors="replace") if p.returncode == 0 else ""

    def _make_image(self) -> None:
        content = sum(p.stat().st_size for p in self.esp.rglob("*") if p.is_file())
        mib = max(64, content * 5 // 4 // (1 << 20) + 40)  # FAT32 needs >= ~33 MiB
        subprocess.run(
            ["dd", "if=/dev/zero", f"of={self.image}", "bs=1048576", f"count={mib}"],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["mformat", "-i", str(self.image), "-F", "-v", "ESP", "::"],
            check=True,
            capture_output=True,
        )
        entries = [str(p) for p in self.esp.iterdir()]
        subprocess.run(
            ["mcopy", "-i", str(self.image), "-s", *entries, "::/"], check=True, capture_output=True
        )

    def put_file(self, rel: str, text: str) -> None:
        """Write a file into the FAT image (between boots, e.g. a new startup.nsh)."""
        tmp = self.workdir / "put.tmp"
        tmp.write_text(text)
        subprocess.run(
            ["mcopy", "-o", "-i", str(self.image), str(tmp), "::/" + rel],
            check=True,
            capture_output=True,
        )

    def esp_files(self, rel: str = "") -> list[str]:
        """Names in a directory of the FAT image."""
        p = subprocess.run(
            ["mdir", "-b", "-i", str(self.image), "::/" + rel], capture_output=True, check=False
        )
        return [line.rsplit("/", 1)[-1] for line in p.stdout.decode().split()]

    def start(self, *extra: str, readonly: bool = False) -> None:
        """Build the FAT image from the staged ESP and boot it."""
        self._make_image()
        self._launch(extra, readonly)

    def restart(self, *extra: str, readonly: bool = False) -> None:
        """Power-cycle on the same disk image (fresh NVRAM and logs)."""
        self.stop()
        for f in self.logdir.glob("*.log"):
            f.unlink()  # or wait_for could match the previous boot's log
        self._launch(extra, readonly)

    def _launch(self, extra: tuple[str, ...], readonly: bool) -> None:
        os.makedirs(self._sockdir, exist_ok=True)
        env = dict(os.environ, QEMU_DISPLAY="none", QEMU_LOGDIR=str(self.logdir))
        if readonly:
            env["QEMU_READONLY"] = "1"
        self.proc = subprocess.Popen(
            [
                str(ROOT / "tools" / "run_qemu.sh"),
                str(self.image),
                "-qmp",
                f"unix:{self.qmp_path},server=on,wait=off",
                *extra,
            ],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )

    def log(self, name: str = "app.log") -> str:
        p = self.logdir / name
        return p.read_text(errors="replace") if p.exists() else ""

    def wait_for(self, text: str, name: str = "app.log", timeout: float = 120) -> str:
        """Wait until `text` appears in log `name`; return the log."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            log = self.log(name)
            if text in log:
                return log
            if self.proc and self.proc.poll() is not None:
                err = self.proc.stderr.read().decode() if self.proc.stderr else ""
                pytest.fail(f"QEMU exited early ({self.proc.returncode}): {err}")
            time.sleep(0.5)
        tail = self.log(name)[-2000:]
        pytest.fail(f"timed out waiting for {text!r} in {name}; log tail:\n{tail}")

    def qmp(self, cmd: str, **args: object) -> object:
        """Run a QMP command (connecting on first use) and return its result."""
        if self._qmp is None:
            deadline = time.monotonic() + 10
            while not os.path.exists(self.qmp_path):
                assert time.monotonic() < deadline, "QMP socket never appeared"
                time.sleep(0.1)
            self._qmp = QEMUMonitorProtocol(self.qmp_path)
            self._qmp.connect()
        return self._qmp.command(cmd, **args)

    def keys(self, *names: str, hold_ms: int = 100) -> None:
        """Press and release keys together (QKeyCode names: "ret", "esc", "y", "up")."""
        self.qmp(
            "send-key",
            keys=[{"type": "qcode", "data": n} for n in names],
            **{"hold-time": hold_ms},
        )

    def hold(self, name: str, down: bool, device: str | None = None) -> None:
        """Key down or up only, optionally to one QEMU device id (e.g. a usb-kbd)."""
        extra = {"device": device} if device else {}
        self.qmp(
            "input-send-event",
            events=[
                {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": name}}}
            ],
            **extra,
        )

    def mouse_move(self, dx: int, dy: int = 0, device: str | None = None) -> None:
        """Relative mouse motion, optionally to one QEMU device id."""
        extra = {"device": device} if device else {}
        self.qmp(
            "input-send-event",
            events=[
                {"type": "rel", "data": {"axis": "x", "value": dx}},
                {"type": "rel", "data": {"axis": "y", "value": dy}},
            ],
            **extra,
        )

    def screendump(self, name: str = "screen.ppm") -> Path:
        """Save the display as a PPM in the log directory."""
        path = self.logdir / name
        self.qmp("screendump", filename=str(path))
        return path

    def stop(self) -> None:
        if self._qmp is not None:
            self._qmp.close()
            self._qmp = None
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        shutil.rmtree(self._sockdir, ignore_errors=True)


@pytest.fixture(scope="session")
def efi_build() -> Path:
    subprocess.run(["make", "-s", "test-efi"], cwd=ROOT, check=True)
    return EFI_DIR


@pytest.fixture(scope="session")
def release_build() -> Path:
    """The release profile, with the debugcon log so tests can watch it."""
    subprocess.run(["make", "-s", "PROFILE=release", "DEBUGCON=1", "doom"], cwd=ROOT, check=True)
    return ROOT / "build" / "x64-release-debugcon"


@pytest.fixture
def qemu(tmp_path: Path, efi_build: Path) -> Iterator[Qemu]:
    q = Qemu(tmp_path)
    yield q
    q.stop()
