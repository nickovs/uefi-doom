#!/usr/bin/env python3
"""Deploy, boot and collect logs from a real-hardware test machine.

Runs DOOM.EFI or HWPROBE.EFI on a Linux machine via a one-shot BootNext boot,
then reads back the volatile DoomLog UEFI variables once the boot manager has
fallen through to Linux. See docs/architecture.md#build-and-test.

Requires ssh key login and passwordless sudo on the target. Only boot entries
labelled DOOM-TEST / HWPROBE-TEST and files under \\EFI\\DOOM are ever touched;
BootOrder is never modified.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dlog

ESP_DIR = "EFI/DOOM"
APPS = {
    "hwprobe": ("HWPROBE.EFI", "HWPROBE-TEST"),
    "doom": ("DOOM.EFI", "DOOM-TEST"),
}
ENTRY_RE = re.compile(r"^Boot([0-9A-Fa-f]{4})\*?\s+(.*?)\t")


class SystestError(RuntimeError):
    """A remote step failed; the message says which and why."""


@dataclass
class Remote:
    """Thin ssh wrapper around one target host."""

    host: str

    def _ssh(self, timeout: int) -> list[str]:
        return ["ssh", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={timeout}", self.host]

    def run(self, cmd: str, check: bool = True, timeout: int = 10) -> str:
        """Run `cmd` via the remote shell and return stdout."""
        try:
            p = subprocess.run(
                [*self._ssh(timeout), cmd],
                capture_output=True,
                text=True,
                timeout=timeout + 120,
            )
        except subprocess.TimeoutExpired as e:
            raise SystestError(f"{self.host}: timed out running: {cmd}") from e
        if check and p.returncode != 0:
            raise SystestError(
                f"{self.host}: command failed ({p.returncode}): {cmd}\n{p.stderr.strip()}"
            )
        return p.stdout

    def reachable(self, timeout: int = 5) -> bool:
        try:
            p = subprocess.run(
                [*self._ssh(timeout), "true"], capture_output=True, timeout=timeout + 10
            )
        except subprocess.TimeoutExpired:
            return False
        return p.returncode == 0

    def put(self, local: Path, remote_path: str) -> None:
        p = subprocess.run(
            ["scp", "-q", "-o", "BatchMode=yes", str(local), f"{self.host}:{remote_path}"],
            capture_output=True,
            text=True,
        )
        if p.returncode != 0:
            raise SystestError(f"scp {local} failed: {p.stderr.strip()}")


def esp_mount(r: Remote) -> str:
    out = r.run("findmnt -no TARGET,FSTYPE /boot/efi", check=False).split()
    if len(out) != 2 or out[1] != "vfat":
        raise SystestError("ESP not mounted at /boot/efi as vfat")
    return out[0]


def boot_entries(r: Remote) -> dict[str, str]:
    """Map boot entry label -> 4-digit hex number."""
    entries: dict[str, str] = {}
    for line in r.run("efibootmgr").splitlines():
        if m := ENTRY_RE.match(line + "\t"):
            entries.setdefault(m.group(2).strip(), m.group(1).upper())
    return entries


def esp_disk_part(r: Remote, esp: str) -> tuple[str, str]:
    src = r.run(f"findmnt -no SOURCE {shlex.quote(esp)}").strip()
    disk = r.run(f"lsblk -no PKNAME {shlex.quote(src)}").strip()
    part = r.run(f"cat /sys/class/block/{shlex.quote(Path(src).name)}/partition").strip()
    if not disk or not part.isdigit():
        raise SystestError(f"cannot work out disk/partition for {src}")
    return f"/dev/{disk}", part


def load_options_text(args: list[str]) -> str:
    """Join args the way src/efi/argv_core.c splits them (double quotes group)."""
    for a in args:
        if '"' in a:
            raise SystestError(f"argument may not contain a double quote: {a!r}")
    return " ".join(f'"{a}"' if (not a or any(c.isspace() for c in a)) else a for a in args)


def ensure_entry(r: Remote, app: str, args: list[str]) -> str:
    """(Re)create our boot entry for `app` with `args`; returns its number."""
    exe, label = APPS[app]
    existing = boot_entries(r).get(label)
    if existing:
        r.run(f"sudo efibootmgr -q -b {existing} -B")
    esp = esp_mount(r)
    disk, part = esp_disk_part(r, esp)
    cmd = [
        "sudo", "efibootmgr", "-q", "--create-only", "--disk", disk, "--part", part,
        "--label", label, "--loader", f"\\EFI\\DOOM\\{exe}",
    ]  # fmt: skip
    if args:
        # --unicode is a flag; the optional data is the trailing non-option
        # arguments, so "--" stops args like "-loopcheck" parsing as "-l oopcheck".
        cmd += ["--unicode", "--", load_options_text(args)]
    r.run(shlex.join(cmd))
    num = boot_entries(r).get(label)
    if not num:
        raise SystestError(f"boot entry {label} not found after creating it")
    verify_entry(r, num, exe)
    return num


def verify_entry(r: Remote, num: str, exe: str) -> None:
    """Refuse to boot an entry whose device path does not end in our loader."""
    line = next((x for x in r.run("efibootmgr -v").splitlines() if x.startswith(f"Boot{num}")), "")
    want = f"/\\EFI\\DOOM\\{exe}"
    if want.lower() not in line.lower():
        raise SystestError(f"Boot{num} does not point at {want}: {line.strip()}")


def cmd_setup(r: Remote, _: argparse.Namespace) -> None:
    esp = esp_mount(r)
    r.run(f"sudo mkdir -p {shlex.quote(esp)}/{ESP_DIR}")
    r.run("sudo -n efibootmgr >/dev/null")
    print(f"{r.host}: ESP at {esp}, {ESP_DIR} ready, passwordless sudo OK")


def cmd_deploy(r: Remote, a: argparse.Namespace) -> None:
    exe, _ = APPS[a.app]
    local = Path(a.build_dir) / exe
    if not local.is_file():
        raise SystestError(f"{local} not found; run make first")
    esp = esp_mount(r)
    tmp = f"/tmp/udoom-{os.getpid()}-{exe}"
    dest = f"{esp}/{ESP_DIR}/{exe}"
    r.put(local, tmp)
    r.run(f"sudo install -m 0644 {shlex.quote(tmp)} {shlex.quote(dest)} && rm -f {tmp}")
    want = hashlib.sha256(local.read_bytes()).hexdigest()
    got = r.run(f"sudo sha256sum {shlex.quote(dest)}").split()[0]
    if got != want:
        raise SystestError(f"hash mismatch after copy: {got} != {want}")
    print(f"deployed {local} -> {dest} ({want[:12]})")


def fetch_log_files(r: Remote) -> dict[str, bytes]:
    script = (
        f"cd /sys/firmware/efi/efivars && for f in DoomLog*-{dlog.GUID}; do "
        '[ -e "$f" ] && printf "%s %s\\n" "$f" "$(base64 -w0 "$f")"; done; true'
    )
    out = r.run(f"sudo sh -c {shlex.quote(script)}")
    files = {}
    for line in out.splitlines():
        name, _, b64 = line.partition(" ")
        if name and b64:
            files[name] = base64.b64decode(b64)
    return files


def cmd_log(r: Remote, a: argparse.Namespace) -> int:
    files = fetch_log_files(r)
    if a.raw:
        out = Path(a.raw)
        out.mkdir(parents=True, exist_ok=True)
        for name, raw in files.items():
            (out / name).write_bytes(raw)
    try:
        info, text = dlog.decode_files(files)
    except dlog.DlogError as e:
        print(f"{r.host}: {e}", file=sys.stderr)
        return 1
    sys.stdout.write(dlog.format_report(info, text))
    return 0


def boot_id(r: Remote) -> str:
    return r.run("cat /proc/sys/kernel/random/boot_id").strip()


def cmd_run(r: Remote, a: argparse.Namespace) -> int:
    if not a.no_deploy:
        cmd_deploy(r, a)
    num = ensure_entry(r, a.app, a.args)
    before = boot_id(r)
    r.run(f"sudo efibootmgr -q -n {num}")
    if a.no_reboot:
        print(
            f"BootNext={num} ({APPS[a.app][1]} {' '.join(a.args)}) set on {r.host}; "
            "it runs at the next reboot. Afterwards: systest.py log"
        )
        return 0
    print(f"BootNext={num} ({APPS[a.app][1]} {' '.join(a.args)}); rebooting {r.host}")
    r.run("sudo systemctl reboot", check=False)
    deadline = time.monotonic() + a.wait_timeout
    time.sleep(10)
    while time.monotonic() < deadline:
        if r.reachable():
            try:
                if boot_id(r) != before:
                    break
            except SystestError:
                pass
        time.sleep(5)
    else:
        raise SystestError(f"{r.host} did not come back within {a.wait_timeout} s")
    print(f"{r.host} is back up")
    return cmd_log(r, a)


def cmd_status(r: Remote, _: argparse.Namespace) -> None:
    print(r.run("efibootmgr | grep -E '^(BootCurrent|BootNext|BootOrder|Boot[0-9A-F]{4})'"))
    esp = esp_mount(r)
    print(r.run(f"cd {shlex.quote(esp)}/{ESP_DIR} 2>/dev/null && sha256sum *.EFI || true"))
    print("log variable:", "present" if fetch_log_files(r) else "absent")


def cmd_remove(r: Remote, _: argparse.Namespace) -> None:
    entries = boot_entries(r)
    for _exe, label in APPS.values():
        if label in entries:
            r.run(f"sudo efibootmgr -q -b {entries[label]} -B")
            print(f"removed boot entry {label}")
    esp = esp_mount(r)
    r.run(f"sudo rm -rf {shlex.quote(esp)}/{ESP_DIR}")
    print(f"removed {esp}/{ESP_DIR}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--host",
        default=os.environ.get("SYSTEST_HOST"),
        help="target machine (default: $SYSTEST_HOST)",
    )
    ap.add_argument("--build-dir", default="build/x64-devloop")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("setup", help="prepare the ESP directory, check sudo")
    for name in ("deploy", "run"):
        p = sub.add_parser(name)
        p.add_argument("--app", choices=APPS, default="hwprobe")
        if name == "run":
            p.add_argument("--no-deploy", action="store_true")
            p.add_argument(
                "--no-reboot",
                action="store_true",
                help="deploy and set BootNext, then stop (reboot by hand later)",
            )
            p.add_argument("--wait-timeout", type=int, default=900)
            p.add_argument("--raw", help="also save raw variable files here")
            p.add_argument("args", nargs=argparse.REMAINDER, help="-- app arguments")
    p = sub.add_parser("log", help="fetch and print the DoomLog variables")
    p.add_argument("--raw", help="also save raw variable files here")
    sub.add_parser("status")
    sub.add_parser("remove", help="delete our boot entries and \\EFI\\DOOM")
    a = ap.parse_args(argv)
    if not a.host:
        ap.error("no target: pass --host or set SYSTEST_HOST")
    if getattr(a, "args", None) and a.args[0] == "--":
        a.args = a.args[1:]
    r = Remote(a.host)
    handlers = {
        "setup": cmd_setup,
        "deploy": cmd_deploy,
        "run": cmd_run,
        "log": cmd_log,
        "status": cmd_status,
        "remove": cmd_remove,
    }
    try:
        return handlers[a.cmd](r, a) or 0
    except SystestError as e:
        print(f"systest: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
