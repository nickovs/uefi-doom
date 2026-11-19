"""Firmware and machine variation QEMU can imitate: an AVX-capable CPU, little
RAM, a USB keyboard unplugged mid-game, the Blt present path with a chosen mode
and scale, and the release build's exit contract."""

from __future__ import annotations

import re
import time

import pytest

pytestmark = pytest.mark.qemu

DOOM = r"\EFI\DOOM\DOOM.EFI"


def start(qemu, args: str, *qemu_args: str, build_dir=None) -> None:
    if build_dir:
        qemu.install("DOOM.EFI", as_boot=False, build_dir=build_dir)
    else:
        qemu.install("DOOM.EFI", as_boot=False)
    qemu.wad()
    qemu.startup_nsh(f"{DOOM} -iwad freedoom1.wad {args}", "echo TEST-DONE")
    qemu.start(*qemu_args)


def emergency_exit(qemu) -> str:
    qemu.keys("ctrl", "alt", "backspace")
    log = qemu.wait_for("exit: reason 3", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    return log


def test_avx_cpu_uses_256_bit_stores(qemu) -> None:
    start(qemu, "-testlog 35", "-cpu", "max")
    log = qemu.wait_for("health=", timeout=180)
    assert re.search(r"cpu: AVX state (enabled|already on)", log), log[-3000:]
    assert "avx-256" in log
    log = emergency_exit(qemu)
    assert "AVX state restored" in log or "already on" in log


def test_little_ram_still_plays(qemu) -> None:
    start(qemu, "-testlog 35", "-m", "128")
    log = qemu.wait_for("health=", timeout=180)
    assert "zone memory" in log
    emergency_exit(qemu)


def test_usb_keyboard_unplugged_while_key_held(qemu) -> None:
    start(
        qemu,
        "-warp 1 1 -nomonsters -testlog 1",
        "-vga", "none", "-device", "VGA,id=vga0", "-device", "qemu-xhci",
        "-device", "usb-kbd,id=ukbd,display=vga0",
    )  # fmt: skip
    qemu.wait_for("health=100", timeout=180)
    time.sleep(2)
    qemu.hold("left", True, device="vga0")
    time.sleep(0.5)
    qemu.qmp("device_del", id="ukbd")
    deadline = time.monotonic() + 30
    while "testlog key up 0xac" not in qemu.log():
        assert time.monotonic() < deadline, (
            "held key never released after unplug:\n" + qemu.log()[-3000:]
        )
        time.sleep(0.5)
    log = emergency_exit(qemu)  # the PS/2 keyboard still works
    assert "input: USB keyboard" in log


def test_blt_with_chosen_mode_and_scale(qemu) -> None:
    start(qemu, "-testlog 35 -blt -mode 800x600 -scale 2")
    log = qemu.wait_for("health=", timeout=180)
    assert re.search(r"video: mode \d+ 800x600 .* scale 2 4:3 at \(80,60\) via Blt", log), log[
        -3000:
    ]
    emergency_exit(qemu)


def test_bad_option_is_a_clear_error(qemu) -> None:
    start(qemu, "-scale 0")
    qemu.wait_for("Press any key to exit", name="serial.log", timeout=120)
    assert "-scale: expected 1 to 32" in qemu.log("serial.log")
    qemu.keys("ret")
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)


def test_release_build_quits_with_success(qemu, release_build) -> None:
    start(qemu, "", build_dir=release_build)
    log = qemu.wait_for("title:", timeout=180)
    assert "watchdog: disabled" in log
    time.sleep(2)
    qemu.keys("f10")
    time.sleep(1)
    qemu.keys("y")
    log = qemu.wait_for("exit: reason 0", timeout=60)
    assert "status EFI_SUCCESS" in log
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
