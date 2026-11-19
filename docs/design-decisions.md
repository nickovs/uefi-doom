# Design decisions

The decisions that shape UEFI Doom, each with its reason. The structure they
produce is described in [architecture.md](architecture.md).

## Platform and toolchain

**1. doomgeneric as the engine, vendored and patched in place.**
It comes from Chocolate Doom, so it is accurate and 64-bit clean, and its
platform layer is a handful of `DG_*` functions. id's original source is not
64-bit clean, and Chocolate Doom depends on SDL throughout. Patches are kept
under `#ifdef UEFI_DOOM` and listed in `third_party/doomgeneric/UPSTREAM.md`.

**2. x86_64 only.** It covers the PCs that matter, and the input, clock and
video work depends on x86 hardware: the i8042, the TSC, the PAT.

**3. GPL-2.0.** Required by the Doom source.

**4. clang and lld-link straight to PE32+, with EDK2 headers only.**
It builds natively on macOS with no ELF-to-PE step and no EDK2 build system.
The vendored `MdePkg` headers give the protocol definitions (including DXE
Services, USB, ACPI and the Timer architectural protocol) without the library.

**5. Our own thin C library.** The engine needs a small set of functions, and
the `FILE` layer has to be ours anyway to load WADs into memory. Existing UEFI
C libraries are heavyweight or poorly maintained. `vsnprintf` comes from the
MIT-licensed eyalroz/printf.

## Lifecycle

**6. One exit path with a cleanup registry.** Firmware does not undo an
application's changes or reclaim its memory, and an application that leaves a
device disconnected or a mode changed breaks the machine until reboot. Every
acquisition registers its own release, and every exit runs the same teardown.

**7. The development build returns status 1 and logs to volatile variables.**
EDK2's boot manager stops at its menu when an application returns
`EFI_SUCCESS`, and many vendor firmwares do the same after an error status.
A warning status makes it continue to the next boot entry without a reset, so
volatile variables survive and the log can be read from the operating system
that boots next. Volatile variables cause no flash wear. The release build
returns `EFI_SUCCESS` and writes no variables.

**8. The development build keeps the boot-manager watchdog armed.** A hang then
resets the machine after five minutes instead of needing a power cycle.
`-logsync` writes the log file periodically so a hang can still be diagnosed.

## Time

**9. Never `GetTime()` for timing.** It typically has one-second resolution and
is slow (it reads the CMOS clock). The clock is the invariant TSC, with its
frequency from CPUID leaf 0x15 or calibrated against the ACPI PM timer. The PM
timer is preferred to the HPET, which some recent Intel platforms leave
unreliable.

**10. Halt between tics, one timer tick at a time.** The game waits for most of
each tic. Busy-waiting runs laptops hot, so the firmware timer is set to 1 ms
and the application sleeps in `WaitForEvent`. One long timer would oversleep
wherever firmware time runs slow, so it sleeps one tick at a time, re-checks the
TSC and spins only the final tick.

**11. One frame per tic.** Doom has no interpolation, so rendering faster than
35 fps would show identical frames.

## Video

**12. The current GOP mode, the largest whole scale, 4:3 aspect correction.**
The current mode is normally the panel's native mode, and changing modes can be
slow and resync the display. Whole-number scaling keeps pixels sharp; the 4:3
correction matches how Doom looked on a CRT. Letterboxing fills the rest.

**13. Write each framebuffer row with explicit wide stores and never read it.**
Firmware often maps the framebuffer uncached, where every bus transaction costs
the same whatever its width. `memcpy` and `rep movsb` can then degrade to one
byte per transaction, over ten times slower than 128-bit stores; 256-bit stores
double the rate again.

**14. Make the framebuffer write-combining through the PAT and page tables.**
The firmware's own route (`SetMemorySpaceAttributes`) is often refused. With
write-combining a full-screen frame takes well under a millisecond instead of a
large part of a tic, so the scale factor stops being a performance question.
The change touches only the framebuffer's pages, uses a free PAT entry, and is
undone at exit. Known limits: only the boot CPU's PAT is changed (other CPUs
are parked in firmware), and it needs 4-level, identity-mapped paging; when
either assumption fails it falls back to uncached wide stores.

**15. Enable AVX state if the firmware left it off.** It doubles uncached write
throughput, and is restored at exit.

**16. Accept tearing.** UEFI has no vertical-sync or page-flip facility.

## Input

**17. Read keyboards directly.** UEFI's console input reports key presses but
never releases, and Doom's movement needs both. Faking releases from the
firmware's key repeat makes movement lag by the repeat delay (around half a
second). So the game takes over the i8042, where most laptops' internal
keyboards appear, and USB boot keyboards, and decodes real make and break
events. ConIn remains the fallback.

**18. Disconnect the firmware's driver before touching a device.** Polling a port
the firmware also polls loses bytes, which means stuck or missed keys. If no
firmware PS/2 driver can be found to disconnect, the i8042 is left alone.

**19. Don't reset or reconfigure the i8042.** On laptops it is emulated by the
embedded controller, and a reset can upset it. The game reads the existing
configuration, polls, and restores the command byte at exit.

**20. All sources at once, merged per key.** A laptop keyboard and an external
keyboard work together; a key is held while any source holds it.

**21. A Driver Binding protocol that refuses everything.** Without one, the USB
bus driver cannot remove a device the application has open, and a key held
when it is unplugged sticks. With it, the bus driver calls our `Stop`.

**22. Ctrl+Alt+Backspace always exits.** Owning the keyboard removes the
firmware's own Ctrl+Alt+Del, so there must be a way out that does not depend on
the game's menus.

**23. The mouse turns; it does not walk (`novert` on).** Doom's default moves the
player on vertical mouse motion, which means repeatedly lifting the mouse.
`-wasd` puts movement under the other hand.

**24. Pointers through every available path.** USB boot mice and the PS/2 aux
port are read directly; any pointer the firmware still drives is read through
`SIMPLE_POINTER` / `ABSOLUTE_POINTER`. I²C-HID touchpads, which most current
laptops use and few firmwares drive, are not yet supported
([backlog](backlog.md)).

## Files

**25. Everything lives next to `DOOM.EFI`.** WADs, settings, saves and the log go
in the image directory. This works the same from a boot entry, the Shell or
removable media, and nothing else on the disk is written.

**26. Read WADs whole into memory.** Firmware file reads are synchronous and slow
on USB storage, and Doom loads some lumps mid-game. Reading the WAD at start-up
removes all in-game disk I/O. The header and directory are validated first,
because the engine trusts them.

**27. Saving failures are messages, not fatal errors.** A read-only or full disk
shows "SAVE FAILED" and play continues; the previous save is kept.

## Testing

**28. Pure cores tested on the host; drivers tested in QEMU.** Decoders, parsers,
scalers and the C library run as host programs under ASan and UBSan. The
drivers run under OVMF in QEMU with input sent over QMP, so key release, device
unplugging, persistence and teardown are checked automatically.

**29. Freedoom for automated tests.** It is free to redistribute. It is downloaded
at a pinned version and checked by SHA-256; no WAD is ever committed.

**30. HWPROBE before the game on a new machine.** Firmware behaviour varies
(framebuffer caching, timer granularity, which drivers exist), and a report from
a probe that changes nothing permanent is the cheapest way to learn it.
