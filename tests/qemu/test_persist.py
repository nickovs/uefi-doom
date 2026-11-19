"""Persistence under QEMU: a savegame survives a power cycle and loads back to
the same place, the config file is read at startup and written on quit, and a
read-only disk disables saving without stopping the game."""

from __future__ import annotations

import re
import time

import pytest

pytestmark = pytest.mark.qemu

DOOM = r"\EFI\DOOM\DOOM.EFI"
STATE = re.compile(r"testlog tic=(\d+) x=(-?\d+) y=(-?\d+) angle=(\d+) health=\d+")


def nsh(args: str) -> str:
    return f"fs0:\r\n{DOOM} -iwad freedoom1.wad {args}\r\necho TEST-DONE\r\n"


def boot(qemu, args: str, readonly: bool = False) -> None:
    qemu.install("DOOM.EFI", as_boot=False)
    qemu.wad()
    qemu.startup_nsh(f"{DOOM} -iwad freedoom1.wad {args}", "echo TEST-DONE")
    qemu.start(readonly=readonly)
    qemu.wait_for("health=100", timeout=180)
    settle(qemu)  # past the level-start screen wipe, which runs no tics


def states(log: str) -> list[tuple[int, int, int, int]]:
    return [tuple(map(int, m)) for m in STATE.findall(log)]


def press(qemu, *keys: str, gap: float = 0.6) -> None:
    for k in keys:
        qemu.keys(k)
        time.sleep(gap)


def settle(qemu, tics: int = 35) -> tuple[int, int, int, int]:
    """Last state once `tics` more tics have passed."""
    start = states(qemu.log())[-1][0]
    deadline = time.monotonic() + 60
    while (s := states(qemu.log()))[-1][0] < start + tics:
        assert time.monotonic() < deadline, "game stopped ticking"
        time.sleep(0.3)
    return s[-1]


def save_slot0(qemu) -> None:
    # Esc opens the menu on New Game; Down x3 is Save Game; slot 0; name "t".
    press(qemu, "esc", "down", "down", "down", "ret", "ret", "t", "ret")


def quit_via_menu(qemu) -> str:
    press(qemu, "f10", "y")  # quick quit: the main menu remembers its last item
    log = qemu.wait_for("exit: reason 0", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    return log


def test_save_survives_power_cycle_and_loads(qemu) -> None:
    boot(qemu, "-warp 1 1 -nomonsters -testlog 1")
    qemu.hold("left", True)
    time.sleep(0.8)
    qemu.hold("left", False)
    _, x, y, angle = settle(qemu)
    save_slot0(qemu)
    assert settle(qemu)[1:] == (x, y, angle), "player moved while saving"
    quit_via_menu(qemu)
    assert "doomsav0.dsg" in qemu.esp_files("EFI/DOOM")
    assert "temp.dsg" not in qemu.esp_files("EFI/DOOM")

    qemu.put_file("startup.nsh", nsh("-loadgame 0 -nomonsters -testlog 1"))
    qemu.restart()
    qemu.wait_for("health=100", timeout=180)
    loaded = settle(qemu)  # the first lines predate the load taking effect
    assert loaded[1:] == (x, y, angle), "loaded game is not where it was saved"


def moves(qemu, key: str) -> bool:
    """Hold a key for a second: did the player's position change?"""
    before = settle(qemu, 80)  # let earlier momentum (~60 tics) die away
    qemu.hold(key, True)
    time.sleep(1.0)
    qemu.hold(key, False)
    return settle(qemu)[1:3] != before[1:3]


def test_config_is_loaded_saved_and_reloaded(qemu) -> None:
    # A vanilla-style config: DOS scancodes, W (17) forward, ',' (51) strafe left.
    qemu.install("DOOM.EFI", as_boot=False)
    (qemu.esp / "EFI" / "DOOM" / "default.cfg").write_text("key_up 17\nkey_strafeleft 51\n")
    boot(qemu, "-warp 1 1 -nomonsters -testlog 1")
    assert moves(qemu, "w"), "W did not move the player: config not loaded"
    assert moves(qemu, "comma"), "',' did not strafe: vanilla binding not mapped"
    quit_via_menu(qemu)
    cfg = qemu.esp_file("EFI/DOOM/default.cfg")
    assert re.search(r"^key_up\s+17\s*$", cfg, re.M), cfg[:500]
    assert re.search(r"^mouse_sensitivity\s+\d+", cfg, re.M), "config not written in full"
    # doomgeneric's action keys have no scancode and are saved raw (160-163):
    # they must survive the next load, or fire/use/strafe stop working.
    assert re.search(r"^key_fire\s+163\s*$", cfg, re.M), cfg[:500]

    qemu.put_file("startup.nsh", nsh("-warp 1 1 -nomonsters -testlog 1"))
    qemu.restart()
    qemu.wait_for("health=100", timeout=180)
    settle(qemu)
    assert moves(qemu, "comma"), "',' stopped strafing after the config was rewritten"
    assert moves(qemu, "w")


def test_read_only_disk_disables_saving(qemu) -> None:
    boot(qemu, "-warp 1 1 -nomonsters -testlog 1", readonly=True)
    save_slot0(qemu)
    log = qemu.wait_for("G_DoSaveGame: cannot write", timeout=60)
    assert "I_Error" not in log
    settle(qemu)  # still running
    qemu.keys("ctrl", "alt", "backspace")
    qemu.wait_for("exit: reason 3", timeout=60)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
