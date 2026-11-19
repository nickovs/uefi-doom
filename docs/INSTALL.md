# Installing UEFI Doom on a PC

UEFI Doom runs straight from the firmware, with no operating system. Installing
it adds one directory and one boot entry; nothing else on the disk is touched,
and your normal operating system keeps booting by default.

## What you need

- An x86-64 PC that boots with UEFI (almost every PC from the last decade).
- **Secure Boot turned off** in the firmware setup: `DOOM.EFI` is not signed.
- An IWAD: your own `doom.wad`, `doom2.wad` or `doom1.wad` (shareware), or the
  free [Freedoom](https://freedoom.github.io/) `freedoom1.wad` / `freedoom2.wad`.
- The release zip (`make dist` builds `build/dist/uefi-doom-<version>.zip`).

## 1. Copy the files to the EFI system partition (ESP)

Copy the zip's `EFI\DOOM` directory, plus your WAD, to `\EFI\DOOM\` on the ESP:

- **Linux:** the ESP is usually mounted at `/boot/efi` (or `/efi`, `/boot`):
  `sudo mkdir -p /boot/efi/EFI/DOOM && sudo cp EFI/DOOM/* doom.wad /boot/efi/EFI/DOOM/`
- **Windows** (administrator prompt): `mountvol S: /S`, then copy into
  `S:\EFI\DOOM\`, then `mountvol S: /D`.

Allow about 30 MB for a WAD; savegames and settings are written next to
`DOOM.EFI` later.

## 2. Add a boot entry

The entry is for the firmware's one-time boot menu; keep your OS first.

- **Linux:**

  ```bash
  sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label "DOOM" --loader '\EFI\DOOM\DOOM.EFI'
  ```

  Use your ESP's disk and partition number (`lsblk`, `findmnt /boot/efi`).
  `efibootmgr` puts the new entry first in `BootOrder`; put your OS back in
  front with `sudo efibootmgr -o XXXX,YYYY,...`. To pass options (see below),
  add them after `--unicode --`, e.g. `--unicode -- "-iwad doom2.wad"`; without
  the `--`, efibootmgr reads options such as `-loadgame` as its own.
- **UEFI Shell:** `bcfg boot add N fs0:\EFI\DOOM\DOOM.EFI "DOOM"`, with N after
  the existing entries (N = 0 would make Doom the default).
- **Windows:** `bcdedit /copy {bootmgr} /d "DOOM"`, then
  `bcdedit /set {new-guid} path \EFI\DOOM\DOOM.EFI`. *(Not yet verified.)*

You can also start `DOOM.EFI` from a UEFI Shell: `fs0:`, `cd \EFI\DOOM`, `DOOM.EFI`.

## 3. Play

Choose "DOOM" in the firmware's one-time boot menu (F12, F9, F8 or Esc at
power-on, depending on the maker).

| Key | Action |
|---|---|
| Arrows | Move and turn |
| Ctrl | Fire |
| Space | Open doors, use switches |
| Alt + arrows, `,` `.` | Strafe |
| Shift | Run |
| Esc | Menu (quit is the last item; F10 quits directly) |
| **Ctrl+Alt+Backspace** | **Quit at once** (use if anything goes wrong) |

A mouse (USB or PS/2) or a supported touchpad turns and fires; moving it
forward does nothing, so walk with the keys. Quitting returns to the firmware, which boots the
next entry or shows its menu.

## Options

Add them in the boot entry or after `DOOM.EFI` in the Shell. All of Doom's own
options work too (`-iwad`, `-file`, `-warp`, `-skill`, `-loadgame`, ...).

| Option | Use |
|---|---|
| `-scale N` | Cap the picture's scale factor (default: as large as fits) |
| `-mode WxH` | Use this screen mode, if the firmware offers it |
| `-blt` | Draw through the firmware's copy routine instead of the framebuffer (try this if the picture is garbled) |
| `-wasd` | Bind W/A/S/D to move and strafe (kept in the saved settings) |
| `-mouse firmware` | Use only the pointer the firmware's setup screens use (try this if the touchpad does not work); `-mouse off` ignores mice |
| `-input conin` | Read keys through the firmware console only (try this if the keyboard does not work; movement then stutters) |
| `-logsync N` | Rewrite `DOOM.LOG` every N seconds, so it survives a hang |

## If something goes wrong

- Everything is logged to `\EFI\DOOM\DOOM.LOG`.
- `HWPROBE.EFI` (in the same zip) tests the machine without running the game:
  start it like `DOOM.EFI` (add `-interactive` to also test the keyboard,
  picture and speaker). It writes `HWPROBE.LOG`, `HWPROBE.JSON` and an `ACPI\`
  directory next to itself. Please send those files with a bug report.
- A machine that hangs can be turned off with a long press on the power button;
  nothing on the disk except `\EFI\DOOM\` is ever written.

## Uninstall

- Delete the boot entry: `sudo efibootmgr -b XXXX -B` (Linux),
  `bcfg boot rm N` (Shell) or `bcdedit /delete {guid}` (Windows).
- Delete `\EFI\DOOM\` from the ESP.
