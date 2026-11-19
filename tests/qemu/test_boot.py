"""Smoke tests: the app boots under OVMF, exits cleanly, and the boot manager
falls through to the next boot option (the devloop exit-status contract)."""

from __future__ import annotations

import time

import pytest

pytestmark = pytest.mark.qemu


def test_hwprobe_boots_and_falls_through(qemu) -> None:
    qemu.install("HWPROBE.EFI")
    qemu.start()
    log = qemu.wait_for("exit: reason 0")
    assert "hwprobe" in log and "starting" in log
    assert "status WARN(1)" in log
    assert "0 live heap blocks" in log
    # Status 1 is not EFI_SUCCESS, so EDK2's BDS moves on instead of showing its menu.
    # Only debug OVMF builds log this (release builds, as distributions ship, are silent).
    if qemu.log("ovmf.log"):
        qemu.wait_for("Booting EFI Internal Shell", name="ovmf.log", timeout=60)


def test_shell_args_and_log_file(qemu) -> None:
    qemu.install("HWPROBE.EFI", as_boot=False)
    qemu.startup_nsh(r"\EFI\DOOM\HWPROBE.EFI -alpha beta", "echo TEST-DONE")
    qemu.start()
    log = qemu.wait_for("exit: reason 0")
    assert "args from Shell" in log
    assert 'argv[2] = "beta"' in log
    # Clean return to the Shell, which then runs the next startup.nsh line.
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    assert "hwprobe" in qemu.esp_file("EFI/DOOM/HWPROBE.LOG")


def test_logsync_survives_hang(qemu) -> None:
    qemu.install("HWPROBE.EFI", as_boot=False)
    qemu.startup_nsh(r"\EFI\DOOM\HWPROBE.EFI -logsync 1 -hang")
    qemu.start()
    qemu.wait_for("spinning until the watchdog fires")
    deadline = time.monotonic() + 30
    while "spinning" not in qemu.esp_file("EFI/DOOM/HWPROBE.LOG"):
        assert time.monotonic() < deadline, "log file not synced during hang"
        time.sleep(0.5)
