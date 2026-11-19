"""DOOM.EFI under QEMU + OVMF with Freedoom: boot to the title loop, ConIn input,
a timedemo, fatal errors, and a clean exit back to the Shell.

By default the game reads the PS/2 keyboard directly (tests/qemu/test_input.py
covers the raw sources); the ConIn test forces `-input conin`. Assertions use
the debugcon log, including `-testlog N` player-state lines, rather than
wall-clock timing."""

from __future__ import annotations

import re
import time

import pytest

pytestmark = pytest.mark.qemu

DOOM = r"\EFI\DOOM\DOOM.EFI"
TESTLOG = re.compile(r"testlog tic=(\d+) x=(-?\d+) y=(-?\d+) angle=(\d+) health=(\d+)")


def positions(log: str) -> list[tuple[int, int, int]]:
    """(tic, x, y) from every in-level testlog line."""
    return [(int(t), int(x), int(y)) for t, x, y, _, _ in TESTLOG.findall(log)]


def read_ppm(path) -> tuple[int, int, bytes]:
    """Width, height and RGB bytes of a binary (P6) PPM from QMP screendump."""
    data = path.read_bytes()
    fields = data.split(maxsplit=4)
    assert fields[0] == b"P6"
    w, h = int(fields[1]), int(fields[2])
    return w, h, fields[4][: w * h * 3]


def start_doom(qemu, *args: str, qemu_args: tuple[str, ...] = ()) -> None:
    qemu.install("DOOM.EFI", as_boot=False)
    qemu.wad()
    qemu.startup_nsh(f"{DOOM} -iwad freedoom1.wad {' '.join(args)}", "echo TEST-DONE")
    qemu.start(*qemu_args)


def test_boot_to_title(qemu) -> None:
    start_doom(qemu, "-testlog 35")
    log = qemu.wait_for("health=", timeout=180)  # attract-mode demo is playing
    assert "wad: freedoom1.wad mapped" in log
    m = re.search(r"video: mode \d+ (\d+)x(\d+) .* scale (\d+) 4:3 at \((\d+),(\d+)\)", log)
    assert m, "video mode not logged"
    w, h, k, x0, y0 = map(int, m.groups())

    sw, sh, rgb = read_ppm(qemu.screendump())
    assert (sw, sh) == (w, h)

    def px(x: int, y: int) -> bytes:
        i = (y * sw + x) * 3
        return rgb[i : i + 3]

    out_w, out_h = 320 * k, 240 * k
    assert px(x0 // 2, sh // 2) == b"\0\0\0", "left letterbox not black"
    assert px(x0 + out_w + (sw - x0 - out_w) // 2, sh // 2) == b"\0\0\0"
    inside = {
        px(x0 + out_w * i // 8, y0 + out_h * j // 8) for i in range(1, 8) for j in range(1, 8)
    }
    assert len(inside) > 8, "game area looks blank"


def test_boot_entry_finds_iwad_next_to_image(qemu) -> None:
    # As \EFI\BOOT\BOOTX64.EFI with no arguments: the IWAD search covers the
    # image directory.
    qemu.install("DOOM.EFI")
    qemu.wad(subdir="BOOT")
    qemu.start()
    log = qemu.wait_for("wad: freedoom1.wad mapped", timeout=180)
    assert "fs: image directory \\EFI\\BOOT" in log


def test_menu_quit_returns_to_shell(qemu) -> None:
    start_doom(qemu, "-testlog 35")
    qemu.wait_for("testlog tic=", timeout=180)
    # Esc opens the main menu on "New Game"; Up wraps to "Quit Game".
    for key in ("esc", "up", "ret", "y"):
        qemu.keys(key)
        time.sleep(1.0)
    log = qemu.wait_for("exit: reason 0", timeout=60)
    assert "live heap blocks before free" in log  # the engine's heap is reclaimed by free-all
    assert "pat_wc: page tables and PAT restored" in log
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    assert "Freedoom" in qemu.esp_file("EFI/DOOM/DOOM.LOG")


def test_conin_movement_and_release(qemu) -> None:
    start_doom(qemu, "-input conin -warp 1 1 -nomonsters -testlog 1")
    qemu.wait_for("health=100", timeout=180)
    time.sleep(1.0)
    before = positions(qemu.log())[-1]

    # PS/2 has no typematic repeat in QEMU, so emulate it: a stream of strokes.
    for _ in range(25):
        qemu.keys("up", hold_ms=20)
        time.sleep(0.06)
    last_stroke_tic = positions(qemu.log())[-1][0]

    # 100 tics later the pulse (<= 150 ms) has released the key and friction has
    # stopped the player (momentum decays for ~60 tics): the last 20 tics are still.
    deadline = time.monotonic() + 60
    while positions(qemu.log())[-1][0] < last_stroke_tic + 100:
        assert time.monotonic() < deadline, "game stopped ticking"
        time.sleep(0.5)
    after = positions(qemu.log())
    moved = [p for p in after if p[0] > before[0] and (p[1], p[2]) != (before[1], before[2])]
    assert moved, "Up arrow did not move the player"
    tail = [p for p in after if p[0] > last_stroke_tic + 80]
    assert len({(x, y) for _, x, y in tail}) == 1, f"still moving after release: {tail}"


def test_timedemo_completes(qemu) -> None:
    start_doom(qemu, "-timedemo demo1 -nogui")
    log = qemu.wait_for("exit: reason", timeout=600)
    assert re.search(r"timed \d+ gametics in \d+ realtics", log), log[-2000:]
    assert "live heap blocks before free" in log  # the engine's heap is reclaimed by free-all
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)


def test_missing_iwad_fatal_screen(qemu) -> None:
    qemu.install("DOOM.EFI", as_boot=False)
    qemu.startup_nsh(f"{DOOM} -iwad nothere.wad", "echo TEST-DONE")
    qemu.start()
    qemu.wait_for("Press any key to exit", name="serial.log", timeout=120)
    assert "IWAD file 'nothere.wad' not found" in qemu.log("serial.log")
    qemu.keys("ret")
    log = qemu.wait_for("exit: reason 1", timeout=60)
    assert "live heap blocks before free" in log  # the engine's heap is reclaimed by free-all
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)


def test_corrupt_pwad_rejected(qemu) -> None:
    # A PWAD whose directory points past the end of the file.
    bad = b"PWAD" + (5).to_bytes(4, "little") + (1000).to_bytes(4, "little")
    (qemu.esp / "EFI" / "DOOM" / "bad.wad").write_bytes(bad)
    start_doom(qemu, "-file bad.wad")
    qemu.wait_for("Press any key to exit", name="serial.log", timeout=120)
    assert "directory (5 entries at 1000) outside the file" in qemu.log("serial.log")
    qemu.keys("ret")
    qemu.wait_for("exit: reason 1", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
