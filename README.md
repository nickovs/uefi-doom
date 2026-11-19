# UEFI Doom

A port of Doom (via [doomgeneric](https://github.com/ozkl/doomgeneric)) that
runs as a native x86_64 UEFI application, straight from the firmware with no
operating system.

`DOOM.EFI` plays Freedoom or your own IWAD with true key presses and releases
from the internal (PS/2) keyboard and USB keyboards, turns with a USB or PS/2
mouse, and saves games and settings next to itself. It leaves the machine as
it found it on exit. There is no sound or networking yet.

- [Installing on a PC](docs/INSTALL.md) (end-user guide)
- [Architecture](docs/architecture.md)
- [Design decisions](docs/design-decisions.md)
- [Backlog](docs/backlog.md)

## Requirements

macOS (Apple's clang from Xcode or the Command Line Tools):

```bash
brew install lld clang-format qemu mtools uv
```

Linux (Debian/Ubuntu; install [uv](https://docs.astral.sh/uv/) separately):

```bash
sudo apt install clang lld clang-format make git zip qemu-system-x86 ovmf efi-shell-x64 mtools
```

clang compiles for the `x86_64-unknown-windows` target and `lld-link` links
the PE32+ EFI binaries. QEMU and the OVMF firmware run the system tests
(`tools/run_qemu.sh` finds Homebrew's and the usual distribution firmware
paths, or set `OVMF_CODE` and `OVMF_VARS`); `mtools` builds the FAT disk
images they boot from. Distribution OVMF builds leave out the UEFI Shell that
some tests boot through, so the tests use `efi-shell-x64`'s standalone Shell
when it is installed (or the file named by `UEFI_SHELL`).

## Building

```bash
make                    # build/x64-devloop/DOOM.EFI and HWPROBE.EFI
make doom               # just DOOM.EFI
make PROFILE=release    # build/x64-release/...
make dist               # build/dist/uefi-doom-<version>.zip (release builds + install guide)
make test-efi           # QEMU test builds (debug console logging)
```

| Profile | Watchdog | Exit status | Log to UEFI variables |
|---|---|---|---|
| `devloop` (default) | left armed (`-watchdog N` re-arms) | always 1, so the boot manager moves on to the next boot entry | yes |
| `release` | disabled | `EFI_SUCCESS` on quit, `EFI_LOAD_ERROR` on a fatal error | no |

Both applications write their log to `<APP>.LOG` next to themselves at exit;
`-logsync N` also rewrites it every N seconds so the log survives a hang.

## DOOM.EFI

Put `DOOM.EFI` and an IWAD in the same directory (for example `\EFI\DOOM\` on
the ESP). Without `-iwad` it looks for the usual IWAD names (`doom2.wad`,
`doom.wad`, `doom1.wad`, `freedoom1.wad`, ...) next to itself. All of Doom's
command-line options work, from the UEFI Shell or a boot entry's arguments:

```text
FS0:\EFI\DOOM\> DOOM.EFI -iwad freedoom1.wad -warp 1 1 -skill 3
```

The free [Freedoom](https://freedoom.github.io/) IWADs are used for testing:

```bash
uv run tools/fetch_freedoom.py   # pinned release, SHA-256 checked -> tests/data/
```

### Controls

Doom's defaults: arrows move, Ctrl fires, Space opens doors, Alt strafes, Shift
runs, `,`/`.` strafe, Esc is the menu. A mouse turns and fires; moving it
forward does nothing (`novert 1` in `doomgenericdoom.cfg`; set it to 0 for
vanilla mouse-walking). `-wasd` binds W/A/S/D to move and strafe, for a hand on
the mouse; it is saved with the settings. **Ctrl+Alt+Backspace quits at once**
(the firmware's own Ctrl+Alt+Del is off while the game owns the keyboard).

### Input

The game reads the PS/2 keyboard controller and any USB boot keyboards and mice
directly, all at once, and hands them back to the firmware on exit. Mice and
touchpads on the PS/2 aux port are read directly too; any other pointer the
firmware drives for its own setup screens is read through the firmware. I²C
touchpads that the firmware does not drive are not supported yet.

If the game can own no keyboard it falls back to the firmware console, which
reports presses but not releases (movement then stutters for the length of the
firmware's key-repeat delay).

| Option | Effect |
|---|---|
| `-input auto\|raw\|conin` | Keyboard sources: all raw sources with console fallback (default), raw only, or console only |
| `-mouse auto\|firmware\|off` | Pointer sources: all (default), only the firmware's, or none |
| `-wasd` | Bind W/A/S/D to move and strafe |

### Display

The picture is scaled by the largest whole factor that fits, with 4:3 aspect
correction, centred. While waiting for the next game tic the CPU halts instead
of spinning.

| Option | Effect |
|---|---|
| `-scale N` | Cap the scale factor |
| `-mode WxH` | Use this GOP mode |
| `-blt` | Draw through `GOP->Blt` instead of the framebuffer |

### Files

Savegames (`doomsav0.dsg`, ...) and settings (`default.cfg`,
`doomgenericdoom.cfg`) live next to `DOOM.EFI`. Settings are written on a
normal quit (not after Ctrl+Alt+Backspace or a fatal error). On a read-only disk
the game still runs, and saving shows "SAVE FAILED: DISK IS READ-ONLY". Fatal
errors are shown in text mode until a key is pressed (at most 60 s).

## HWPROBE.EFI

A hardware probe to run on a new machine before the game. It reports firmware
and CPU details, ACPI hints (and dumps every ACPI table to `ACPI\`), clock
sources and their agreement, timer and idle behaviour, `Stall`, `GetTime` and
ConOut costs, GOP modes, the framebuffer's effective memory type and
write/copy/Blt/present timings (with a suggested maximum scale), the keyboard
and pointer paths including takeover and hand-back, and the PC speaker.

| Option | Effect |
|---|---|
| (none) | Unattended: everything that needs no person present |
| `-interactive` | Also prompted tests (each started with ENTER): key-repeat timing, keyboard and pointer capture, an on-screen test pattern, speaker tones. Extends the watchdog to 30 minutes. |
| `-wc` | Try write-combining through the firmware (attributes are restored) |
| `-patwc` | Map the framebuffer write-combining through the PAT and page tables (restored) |
| `-avx` | Enable AVX state if the firmware left it off (restored), and time 256-bit stores |
| `-only SECTION` | One of `system`, `acpi`, `clock`, `video`, `input`, `speaker` |
| `-hang` | Spin forever (to test the watchdog and `-logsync`) |

Output, next to the binary: `HWPROBE.LOG`, `HWPROBE.JSON`, `ACPI\<SIG><n>.BIN`.

## Running in QEMU

```bash
make run                # interactive window: DOOM.EFI plus any WADs in tests/data/
QEMU_DISPLAY=none make DEBUGCON=1 run   # headless; logs in build/qemu/
```

`build/qemu/app.log` holds the application's log (`DEBUGCON=1` builds only);
`build/qemu/ovmf.log` holds the firmware's debug output. `QEMU_READONLY=1`
attaches the disk read-only.

## Testing

```bash
uv sync                          # test tooling (pinned in pyproject.toml)
uv run tools/fetch_freedoom.py   # once: the DOOM.EFI QEMU tests skip without it
uv run pytest                    # host C unit tests, tool tests and QEMU system tests
uv run tox                       # lint (ruff, clang-format) + unit tests
uv run tox -e qemu               # QEMU system tests only
```

Host unit tests are C programs in `tests/unit/` built with ASan and UBSan
(`make unit`). QEMU tests in `tests/qemu/` boot OVMF headless from a fresh FAT32
image per test and drive it over QMP.

### On real hardware

`tools/systest.py` drives a Linux machine over ssh: it copies the binary to the
ESP, creates its own boot entry (never touching `BootOrder`), sets `BootNext`,
reboots, waits for Linux to come back, and prints the log the `devloop` build
left in volatile UEFI variables.

```bash
export SYSTEST_HOST=my-test-machine.local            # or pass --host
uv run tools/systest.py setup                        # once
uv run tools/systest.py run --app hwprobe -- -only clock
uv run tools/systest.py log                          # re-read the last log
uv run tools/systest.py status
uv run tools/systest.py remove                       # delete our entries and \EFI\DOOM
```

The machine needs UEFI boot with Secure Boot disabled, `efibootmgr`, ssh key
login and passwordless sudo, and its firmware must continue to the next boot
entry when an application returns a warning status (EDK2-based firmware does).

## Hardware

Any x86-64 PC that boots with UEFI and Secure Boot off should start the game.
For full play it needs a keyboard the game can own: a PS/2-style internal
keyboard (as on most laptops) or a USB keyboard. Run `HWPROBE.EFI` on a new
machine first and keep its report; see the [install guide](docs/INSTALL.md).

## Licence

GPL-2.0 (see `LICENSE`), as required by the Doom source. Vendored components
keep their own licences: doomgeneric (GPL-2.0), EDK2 headers
(BSD-2-Clause-Patent), printf (MIT). Freedoom is not included;
`fetch_freedoom.py` downloads it.

Author: Nicko van Someren
