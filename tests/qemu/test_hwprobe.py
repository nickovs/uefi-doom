"""HWPROBE in batch mode under QEMU: every section runs, the JSON report parses,
devices are taken over and handed back, and the Shell still works afterwards."""

from __future__ import annotations

import json

import pytest

pytestmark = pytest.mark.qemu


def test_hwprobe_batch_report(qemu) -> None:
    qemu.install("HWPROBE.EFI", as_boot=False)
    qemu.startup_nsh(r"\EFI\DOOM\HWPROBE.EFI -batch", "echo TEST-DONE")
    qemu.start("-device", "qemu-xhci", "-device", "usb-kbd")
    log = qemu.wait_for("hwprobe: done", timeout=300)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)

    report = json.loads(qemu.esp_file("EFI/DOOM/HWPROBE.JSON"))
    assert set(report) >= {"system", "acpi", "clock", "video", "input", "speaker"}
    assert report["clock"]["tsc_hz"] > 0
    idle = report["clock"]["idle"]
    assert idle["set_1ms"] and 0 < idle["granularity_us"] <= 2500
    assert idle["wait_20ms_late_max_us"] < 2000  # idle_until must not oversleep
    assert report["acpi"]["tables"], "no ACPI tables dumped"
    assert report["video"]["timings"], "no video timings"
    assert report["video"]["memtype"]["effective"] != "?"

    i8042 = report["input"]["i8042"]
    assert i8042["present"] and i8042["takeover"]
    assert i8042["restored_handles"] == i8042["firmware_handles"]
    assert i8042["firmware_handles_after"] == i8042["firmware_handles"]

    usb = report["input"]["usb_hid"]
    assert any(d["kind"] == "keyboard" and d["takeover"] for d in usb)
    assert "returned to firmware: EFI_SUCCESS" in log
    assert "0 live heap blocks" in qemu.wait_for("exit: reason 0")


def test_hwprobe_avx_and_wc_experiments(qemu) -> None:
    qemu.install("HWPROBE.EFI", as_boot=False)
    qemu.startup_nsh(r"\EFI\DOOM\HWPROBE.EFI -only video -avx -wc -patwc", "echo TEST-DONE")
    qemu.start("-cpu", "max")
    log = qemu.wait_for("hwprobe: done", timeout=300)
    qemu.wait_for("TEST-DONE", name="serial.log", timeout=60)
    video = json.loads(qemu.esp_file("EFI/DOOM/HWPROBE.JSON"))["video"]

    avx = video["avx_experiment"]
    if avx["state"] != "unavailable":
        assert avx["enabled"]
        assert any(t["test"].endswith("_avx") for t in video["timings"])
    if avx["state"] == "enabled":
        assert "cpu: AVX state restored" in log
    assert "wc_experiment" in video
    if video["wc_experiment"].get("set_status") == "EFI_SUCCESS":
        assert "attributes restored (EFI_SUCCESS)" in log

    patwc = video["patwc_experiment"]
    types = [ln for ln in log.splitlines() if ln.startswith("framebuffer memory type")]
    if patwc["enabled"]:
        assert "effective WC" in types[1], types
        assert "pat_wc: page tables and PAT restored" in log
        assert types[-1] == types[0], "memory type not restored"
    else:
        assert patwc["reason"]

    port = video["port_present"]
    assert port["median_us"] > 0
    assert "port present path" in log
    assert log.count("pat_wc: page tables and PAT restored") == (
        2 if port["write_combining"] else 1
    )
