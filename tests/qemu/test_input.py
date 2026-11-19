"""Raw input sources under QEMU: the PS/2 keyboard read directly through the
i8042, a USB boot keyboard and mouse taken over from firmware, the emergency
exit chord, and low-power idle.

Turning has no momentum in Doom, so a turn key gives an exact test of key
release: the view must stop rotating within a tic of the logged key-up."""

from __future__ import annotations

import re
import time

import pytest

pytestmark = pytest.mark.qemu

DOOM = r"\EFI\DOOM\DOOM.EFI"
ANGLE = re.compile(r"testlog tic=(\d+) x=-?\d+ y=-?\d+ angle=(\d+) health=\d+")
KEY = re.compile(r"testlog key (down|up) 0x([0-9a-f]{2}) tic=(\d+)")
DK_LEFT = 0xAC
# A usb-kbd bound to a named display receives that display's QMP key events
# (otherwise QEMU routes them to the PS/2 keyboard).
USB_KBD = ("-vga", "none", "-device", "VGA,id=vga0", "-device", "qemu-xhci",
           "-device", "usb-kbd,display=vga0")  # fmt: skip


def start(qemu, *args: str, qemu_args: tuple[str, ...] = ()) -> None:
    qemu.install("DOOM.EFI", as_boot=False)
    qemu.wad()
    qemu.startup_nsh(
        f"{DOOM} -iwad freedoom1.wad -warp 1 1 -nomonsters -testlog 1 {' '.join(args)}",
        "echo TEST-DONE",
    )
    qemu.start(*qemu_args)
    qemu.wait_for("health=100", timeout=180)


def angles(log: str) -> list[tuple[int, int]]:
    return [(int(t), int(a)) for t, a in ANGLE.findall(log)]


def wait_tic(qemu, tic: int, timeout: float = 60) -> str:
    deadline = time.monotonic() + timeout
    while True:
        log = qemu.log()
        a = angles(log)
        if a and a[-1][0] >= tic:
            return log
        assert time.monotonic() < deadline, "game stopped ticking"
        time.sleep(0.3)


def check_turn_and_release(qemu, device: str | None = None) -> str:
    """Hold Left about a second, release, and check rotation stops at once."""
    qemu.hold("left", True, device)
    time.sleep(1.2)
    qemu.hold("left", False, device)
    deadline = time.monotonic() + 30
    while not (
        m := [k for k in KEY.findall(qemu.log()) if int(k[1], 16) == DK_LEFT and k[0] == "up"]
    ):
        assert time.monotonic() < deadline, "no key-up for Left"
        time.sleep(0.3)
    downs = [int(t) for d, k, t in KEY.findall(qemu.log()) if d == "down" and int(k, 16) == DK_LEFT]
    assert len(downs) == 1, "a held key must give one key-down (no typematic repeats)"
    up_tic = int(m[0][2])
    log = wait_tic(qemu, up_tic + 20)
    a = dict(angles(log))
    assert a[up_tic] != a[downs[0]], "Left did not turn the player"
    after = {a[t] for t in a if t > up_tic}
    assert len(after) == 1, f"still turning after key-up at tic {up_tic}: {sorted(a.items())[-25:]}"
    return log


def finish(qemu) -> str:
    """Quit with Ctrl+Alt+Backspace and wait for the Shell."""
    qemu.keys("ctrl", "alt", "backspace")
    log = qemu.wait_for("exit: reason 3", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    return log


def test_ps2_true_release_and_emergency_exit(qemu) -> None:
    start(qemu)
    check_turn_and_release(qemu)
    log = finish(qemu)
    assert "input: i8042 on" in log
    assert "emergency exit" in log
    assert "i8042: reconnect firmware driver: EFI_SUCCESS" in log
    assert re.search(r"key presses i8042=[1-9]\d* usb=0 conin=0", log)


def test_usb_keyboard_true_release(qemu) -> None:
    start(qemu, qemu_args=USB_KBD)
    check_turn_and_release(qemu, device="vga0")
    log = finish(qemu)
    assert "input: USB keyboard" in log
    assert "returned to firmware: EFI_SUCCESS" in log
    assert re.search(r"key presses i8042=\d+ usb=[1-9]", log)


def test_usb_mouse_turns(qemu) -> None:
    start(qemu, qemu_args=("-device", "qemu-xhci", "-device", "usb-mouse"))
    log = qemu.log()
    assert "input: USB mouse" in log
    before = angles(log)[-1]
    for _ in range(20):
        qemu.mouse_move(40)
        time.sleep(0.05)
    log = wait_tic(qemu, before[0] + 35)
    assert angles(log)[-1][1] != before[1], "mouse motion did not turn the player"
    finish(qemu)


def test_idle_uses_fast_timer_and_restores_it(qemu) -> None:
    start(qemu)
    m = re.search(r"idle: firmware timer (\d+) us -> (\S+) us; wake-up granularity (\d+) us",
                  qemu.log())  # fmt: skip
    assert m, "idle not initialised"
    assert int(m.group(3)) <= 2500, "1 ms timer not achieved"
    log = finish(qemu)
    if m.group(2) != "unchanged":
        assert f"idle: timer period restored to {m.group(1)} us: EFI_SUCCESS" in log


STATE = re.compile(r"testlog tic=(\d+) x=(-?\d+) y=(-?\d+) angle=(\d+) health=\d+")


def state(qemu, settle_tics: int = 0) -> tuple[int, int, int, int]:
    """Latest (tic, x, y, angle), optionally after `settle_tics` more tics."""
    start_tic = int(STATE.findall(qemu.log())[-1][0])
    deadline = time.monotonic() + 60
    while (s := STATE.findall(qemu.log())) and int(s[-1][0]) < start_tic + settle_tics:
        assert time.monotonic() < deadline, "game stopped ticking"
        time.sleep(0.3)
    return tuple(map(int, s[-1]))


def test_ps2_mouse_turns_and_ignores_vertical(qemu) -> None:
    # No USB devices: QMP mouse events go to QEMU's PS/2 mouse (i8042 aux port).
    start(qemu)
    assert "PS/2 mouse on" in qemu.log()
    _, x, y, angle = state(qemu, 35)
    for _ in range(15):  # vertical only: novert (default on) ignores it
        qemu.mouse_move(0, -40)
        time.sleep(0.05)
    _, x2, y2, angle2 = state(qemu, 80)  # let any glide stop
    assert (x2, y2, angle2) == (x, y, angle), "vertical mouse motion moved the player"
    for _ in range(15):
        qemu.mouse_move(40, 0)
        time.sleep(0.05)
    assert state(qemu, 35)[3] != angle, "horizontal PS/2 mouse motion did not turn"
    log = finish(qemu)  # the keyboard still works with aux bytes interleaved
    assert "i8042: reconnect firmware driver: EFI_SUCCESS" in log
    assert re.search(r"mouse reports ps2=[1-9]", log)


def test_wasd_moves_and_is_saved(qemu) -> None:
    start(qemu, "-wasd")
    _, x, y, _ = state(qemu, 35)
    qemu.hold("a", True)
    time.sleep(0.8)
    qemu.hold("a", False)
    _, x2, y2, _ = state(qemu, 80)
    assert (x2, y2) != (x, y), "A did not strafe"
    qemu.keys("f10")
    time.sleep(1)
    qemu.keys("y")
    qemu.wait_for("exit: reason 0", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    cfg = qemu.esp_file("EFI/DOOM/default.cfg")
    assert re.search(r"^key_up\s+17\s*$", cfg, re.M), cfg[:600]  # W, as a DOS scancode
    assert re.search(r"^key_strafeleft\s+30\s*$", cfg, re.M)  # A
    assert re.search(r"^novert\s+1\s*$", qemu.esp_file("EFI/DOOM/doomgenericdoom.cfg"), re.M)


def test_firmware_pointers_used_when_asked(qemu) -> None:
    # OVMF has no mouse driver of its own, so only the console's (empty) merged
    # pointers exist: check they are found and read without trouble.
    start(qemu, "-mouse firmware")
    log = qemu.log()
    assert "input: firmware simple pointer (console)" in log
    assert "0 USB mouse/mice, PS/2 mouse off, 2 firmware pointer(s)" in log
    state(qemu, 35)
    finish(qemu)
