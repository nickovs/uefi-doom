# Architecture

UEFI Doom is a native x86_64 UEFI application (`DOOM.EFI`) that runs Doom
straight from the firmware: no operating system. A second application,
`HWPROBE.EFI`, uses the same platform code to report how a machine behaves.
The reasons behind the main choices are in [design-decisions.md](design-decisions.md);
planned work is in [backlog.md](backlog.md).

## Layers

```
 ┌──────────────────────────────────────────────────────────────┐
 │ doomgeneric engine (third_party/doomgeneric, lightly patched)│
 └───────────────┬──────────────────────────────┬───────────────┘
                 │ DG_* platform hooks          │ C library calls
 ┌───────────────▼──────────────┐   ┌───────────▼───────────────┐
 │ port/   doomgeneric glue     │   │ libc/   thin C library    │
 └──┬────────────┬──────────────┘   └───────────┬───────────────┘
 ┌──▼───┐  ┌─────▼──────┐                       │
 │video/│  │ input/     │                       │
 └──┬───┘  └─────┬──────┘                       │
 ┌──▼────────────▼──────────────────────────────▼──────────────┐
 │ efi/   platform services: lifecycle, memory, log, files,    │
 │        clock, idle, ACPI, CPU features                      │
 └──────────────────────────────┬──────────────────────────────┘
                                │ EDK2 MdePkg headers (types and protocols only)
                         UEFI firmware
```

`hwprobe/` is a second application on top of `efi/`, `video/` and `input/`; it
does not link the engine.

### Dependency rules

1. The engine and `libc/` never include UEFI headers.
2. `efi/` knows nothing about Doom. `video/` and `input/` see Doom only through
   neutral types (an 8-bit indexed frame and palette; key events carrying
   doomgeneric key codes). Only `port/` includes engine headers.
3. Pure logic is kept apart from firmware access. Each hardware module has a
   `*_core.c` (parsing, decoding, tables, maths; no UEFI calls, no port I/O)
   that is compiled for the host and unit-tested, and a driver `*.c` that does
   the I/O and calls the core.
4. Every firmware resource is registered for release at the moment it is
   acquired ([Lifecycle](#lifecycle)).

## Repository layout

| Path | Contents |
|---|---|
| `src/efi/` | Entry and exit, cleanup registry, allocator, logging and the DoomLog variables, files, clock, idle, ACPI, CPU features (AVX), PAT write-combining |
| `src/libc/` | The C library subset the engine and printf need |
| `src/video/` | GOP mode choice, palette LUT, scaler, framebuffer and Blt presentation |
| `src/input/` | i8042 keyboard and aux port, USB HID boot devices, firmware pointers, ConIn fallback, keymaps, source merging |
| `src/port/` | doomgeneric `DG_*` hooks, in-memory WAD loader and validation |
| `src/hwprobe/` | `HWPROBE.EFI` |
| `include/` | One header per module; `include/libc/` shadows the system C headers |
| `third_party/` | doomgeneric (patched, see its `UPSTREAM.md`), an EDK2 `MdePkg` header subset, eyalroz/printf |
| `tests/unit/` | Host C unit tests for every pure core |
| `tests/qemu/` | System tests: QEMU + OVMF, driven over QMP |
| `tests/tools/` | Tests for the Python tools |
| `tools/` | Freedoom fetcher, QEMU launcher, real-hardware test driver, DoomLog parser, EDK2 header pruner |

## Lifecycle

`efi_main` (`src/efi/main.c`) stores the system table pointers, sets the
watchdog for the build profile, starts logging, opens the image's volume,
builds `argv` from `LoadOptions`, calibrates the clock, and then runs the
engine (`doomgeneric_Create`, then `doomgeneric_Tick` forever) under a
`__builtin_setjmp` exit point.

Every way out (`I_Quit`, `I_Error`, the emergency key chord, a failed start-up)
calls `exit()`, which jumps back to that point. There is exactly one teardown
path:

- **Cleanup registry:** `efi_cleanup_push(fn, ctx)` records a release action on
  a fixed LIFO stack whenever a resource is taken: a GOP mode change, a device
  takeover, the firmware timer period, the PAT and page tables, AVX state, an
  open file, a page allocation. Teardown runs them in reverse, logging and
  continuing past any failure; it never calls `exit()`.
- **Memory:** the allocator tracks every block, and frees whatever the engine
  left allocated after everything else has been released, because UEFI does
  not reclaim an application's memory.
- **Fatal errors:** `I_Error` shows its message in text mode and waits for a key
  for up to 60 s (so unattended boots carry on), then exits.

### Build profiles

| | `devloop` (default) | `release` |
|---|---|---|
| Boot-manager watchdog | Left armed (`-watchdog N` re-arms it) | Disabled |
| Exit status | Always `1`, a warning, so the boot manager moves on to the next boot entry | `EFI_SUCCESS` on quit, `EFI_LOAD_ERROR` on a fatal error |
| Log in volatile UEFI variables | Yes | No |

Test builds (`DEBUGCON=1`) add a log sink on QEMU's debug console and the
`-testlog` option. They go to a separate output directory.

## Platform services (`efi/`)

### Logging

`log_msg(level, ...)` feeds several sinks; `printf` and `stdout`/`stderr`
route here too.

| Sink | When |
|---|---|
| ConOut | Before graphics start and after they stop; never during play |
| RAM ring (256 KB) | Always |
| `<APP>.LOG` next to the binary | At exit; also every N seconds with `-logsync N`, so the log survives a hang |
| QEMU debugcon (port 0x403) | Test builds only |
| Volatile UEFI variables | `devloop` builds, at exit |

**DoomLog variables:** project GUID `89c5fe58-f223-4f55-a8dc-6e4cfee8bf6a`,
boot-service and runtime access, not non-volatile (no flash wear; cleared by
any reset). `DoomLog000`, `DoomLog001`, ... hold the log text in chunks sized
from `QueryVariableInfo`. `DoomLogInfo` is written last and holds the magic,
format version, exit status and reason, build ID, run time, and chunk and
truncation counts. A long log keeps its first 16 KB and its most recent part.
`tools/dlog.py` decodes them from Linux's `efivarfs`.

### Memory

`malloc` and friends sit on `AllocatePool` with a 32-byte tracking header
(16-byte aligned, with a magic value checked on `free`). Large buffers (the WAD)
use page allocations registered for cleanup. The engine's zone is 32 MiB.

### Files

All of the game's files live in the directory `DOOM.EFI` was loaded from:
WADs, `default.cfg`, `doomgenericdoom.cfg`, `doomsavN.dsg`, `DOOM.LOG`. Paths
are translated (UTF-8 to UCS-2, `/` to `\`, `.` is the image directory,
escaping the volume is rejected). `rename` is `SetInfo` within one directory.
Firmware statuses map to `errno` values (`EFI_WRITE_PROTECTED` → `EACCES`,
`EFI_VOLUME_FULL` → `ENOSPC`, ...).

WADs are read whole into memory at start-up, their header and directory are
validated, and the engine uses them through doomgeneric's `mapped` WAD class,
so play never touches the disk.

### Arguments

`LoadOptions` is used only if it is well-formed UCS-2 text. A boot entry's
options get a program name prepended; the Shell's already have one. The
tokeniser handles quotes, and the engine's `@file` response files work.

### Clock and idle

- `clock_us()` reads the invariant TSC, scaled by a fixed-point multiplier. The
  frequency comes from CPUID leaf 0x15 when it reports one, cross-checked
  against the ACPI PM timer; otherwise the TSC is calibrated against the PM
  timer, then the HPET, then `Stall`. Without an invariant TSC the HPET or PM
  timer is read directly.
- `idle_init()` asks the Timer architectural protocol for a 1 ms tick (and
  restores the original at exit). `idle_until(deadline)` halts one timer tick at
  a time with `WaitForEvent`, re-checks the clock after each wake-up, and spins
  the final tick.
- `DG_SleepMs` sleeps until the next game tic, so the CPU halts for most of
  each 28.6 ms tic. The engine renders at most one frame per tic (35 fps).

### ACPI

The RSDP comes from the configuration table; every table's length and checksum
is checked. Provides table lookup, FADT fields (8042 flag, PM timer), the HPET
base, and an EISA-ID byte scan of the DSDT and SSDTs (a hint only; AML is not
interpreted).

## C library (`libc/`)

Just what the engine and printf use: string and ctype functions, `atoi`,
`atof`, `strtol`; `printf` family over the vendored `vsnprintf`; `sscanf`
(`%d %i %u %x %o %s %c %[set] %n`, widths, `h`/`l`); a buffered `FILE` over
`efi/fs` (`fopen` modes `r`, `w`, `r+`, with `b`), line-based `fscanf`;
`remove`, `rename`, `mkdir`; `exit`; `errno`. `system` and `getenv` are stubs.
Pure functions are built with a `shim_` prefix for host tests against the host
libc.

## Video (`video/`)

```c
bool video_init(const video_config *cfg);
void video_present(const uint8_t *frame, const uint8_t rgb[256][3], bool palette_changed);
```

- **Mode:** the current GOP mode (normally the panel's native mode, so there is
  no slow mode set), unless `-mode WxH` is given or the current mode is smaller
  than 640×480.
- **Scale:** the largest whole factor `k` (capped by `-scale`) with
  `320k × 240k` fitting the screen: 4:3 aspect correction stretches 200 lines
  to 240 by repeating rows in an even pattern. The image is centred and
  letterboxed.
- **Palette:** a 256-entry LUT in the mode's pixel format, rebuilt only when the
  palette changes.
- **Presentation:** each output row is built once in a cached line buffer and
  copied to the framebuffer with explicit 128-bit (SSE) or 256-bit (AVX) stores,
  once per repeated row; the framebuffer is never read. `PixelBltOnly` modes, or
  `-blt`, use a back buffer and one `GOP->Blt`.
- **Speed:** at start-up the framebuffer is mapped write-combining through the
  PAT and page tables, and AVX state is enabled if the firmware left it off.
  Both are restored at exit. If either cannot be done, presentation falls back
  to uncached wide stores.

## Input (`input/`)

```c
void input_init(input_mode mode, mouse_mode mice);
void input_poll(void);
bool input_next_key(key_event *ev);
bool input_mouse(int *buttons, int *dx, int *dy);
```

`DG_GetKey` polls once per tic and drains the key queue; `DG_GetMouse` (a
local engine hook) returns the motion and buttons gathered since the last tic.

| Source | Device | Mechanism |
|---|---|---|
| i8042 keyboard | Most laptops' internal keyboard; PS/2 keyboards | Firmware PS/2 driver disconnected; ports 0x60/0x64 polled per tic; translated set 1 decoded |
| i8042 aux port | PS/2 mice, touchpads wired to the controller | Started with `D4 F6`, `F2`, `F4`; polled with the keyboard (aux IRQ off); stopped and the command byte restored at exit |
| USB HID boot keyboards and mice | External USB devices | Opened `BY_DRIVER \| EXCLUSIVE`; boot protocol; asynchronous interrupt transfers into a per-device ring; reports diffed into key-downs and key-ups |
| `SIMPLE_POINTER` / `ABSOLUTE_POINTER` | Whatever pointer the firmware still drives | Polled; the console's merged pointers when present |
| ConIn | Fallback when no keyboard can be owned | Key-ups synthesised from the repeat pattern |

- **Merging:** key state is kept per source and merged with reference counts,
  so typematic repeats are dropped and a key held on two keyboards is released
  only when both let go. Mouse motion is summed and buttons OR-ed.
- **Hand-back:** every takeover is undone at exit: firmware drivers are
  reconnected and USB transfers cancelled.
- **Unplugging:** the application installs a minimal Driver Binding protocol
  (its `Supported` and `Start` always refuse) so the USB bus driver can ask it
  to `Stop` a removed device; that device's keys are then released.
- **Emergency exit:** Ctrl+Alt+Backspace from any raw source exits at once with
  full teardown (taking the keyboard also removes the firmware's own
  Ctrl+Alt+Del).
- **Options:** `-input auto|raw|conin`, `-mouse auto|firmware|off`; `-wasd`
  binds W/A/S/D; `novert` (default 1) ignores vertical mouse motion.
- **Keymaps:** set-1 scancodes, HID usages and EFI scan codes map to doomgeneric
  keys. US layout only. Ctrl, Space, `,` and `.` send doomgeneric's fire, use
  and strafe codes.

## Engine glue (`port/`)

`src/port/dg_uefi.c` implements the `DG_*` hooks (init, frame, ticks, sleep,
keys, mouse, fatal screen) over the modules above, and parses the port's own
options (`-scale`, `-mode`, `-blt`, `-input`, `-mouse`, `-wasd`, `-testlog`).
The engine patches are listed in `third_party/doomgeneric/UPSTREAM.md`; all
are under `#ifdef UEFI_DOOM`.

## HWPROBE

`HWPROBE.EFI` reports, without running the game: firmware and CPU identity;
every ACPI table (dumped to `ACPI\`); clock sources and how well they agree;
`Stall`, timer-event, idle, `GetTime` and ConOut costs; GOP modes, the
framebuffer's effective memory type, and write, copy, Blt and present timings
with a suggested scale; keyboard and pointer paths, including takeover and
hand-back; and the PC speaker. By default it runs unattended; `-interactive`
adds prompted capture tests, a test pattern and tones. Output goes to
`HWPROBE.LOG` and `HWPROBE.JSON` next to the binary. Every experiment is
registered for cleanup and undone.

## Build and test

- **Toolchain:** clang targeting `x86_64-unknown-windows` plus `lld-link`
  produce PE32+ directly (`-ffreestanding -mno-red-zone -fshort-wchar
  -mno-stack-arg-probe -fno-stack-protector -fno-strict-aliasing`). One
  Makefile builds both applications, the host unit tests and the release zip.
- **Unit tests:** one C program per pure core, built for the host with ASan and
  UBSan, wrapped by pytest.
- **System tests:** pytest boots OVMF in QEMU from a fresh FAT32 image per test,
  sends keys and mouse motion over QMP (PS/2 and USB devices), and asserts on the
  debugcon log and screendumps: boot and letterbox, input with true release on
  each source, unplugging, save and load across power cycles, config
  persistence, read-only disks, fatal errors, timedemo, alternative CPUs, RAM
  sizes and video paths, and the release build's exit.
- **Real hardware:** `tools/systest.py` drives a Linux machine over ssh: it
  copies the binary to the ESP, creates its own boot entry with the run's
  arguments (never changing `BootOrder`), sets `BootNext`, reboots, waits for
  Linux, and prints the log the `devloop` build left in the DoomLog variables.
- **Lint:** ruff for Python, clang-format for C; tox and pre-commit run them.
