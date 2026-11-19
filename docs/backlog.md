# Backlog

What is planned, roughly in priority order. The current structure is in
[architecture.md](architecture.md) and the reasoning behind it in
[design-decisions.md](design-decisions.md).

## Next

### Hardware coverage

- Run `HWPROBE.EFI` and the game on more laptops and desktops from different
  vendors, fix the firmware quirks found (each with a regression test where the
  logic is pure), and publish a supported-hardware table in the README.
- Tune defaults from those reports: ConIn pulse timing, clock fallbacks, the
  scale cap where write-combining is unavailable.
- Check the Windows install steps (`bcdedit`) in [INSTALL.md](INSTALL.md).

### Touchpads and keyboards over I²C

Most current laptops connect the touchpad, and some the keyboard, through
HID over I²C, which few firmwares drive. Supporting it needs:

- a driver for the platform's I²C controller (on Intel and AMD usually a
  Synopsys DesignWare block, so close to generic);
- ACPI work: finding `PNP0C50` devices, their `I2cSerialBus` and interrupt
  resources, and calling `_DSM` for the HID descriptor address, which needs a
  small AML evaluator or a targeted pattern match;
- HID over I²C, plus a HID report-descriptor parser, because these devices do
  not use the fixed boot protocol;
- takeover and hand-back like the other input sources, and tests against an
  emulated device.

## Later

### Sound

doomgeneric keeps Chocolate Doom's `sound_module_t` / `music_module_t`
interfaces; a `src/sound/` module will provide both, and with no device found
the game stays silent as today.

1. **PC speaker:** effects only (`DP*` lumps) through PIT channel 2 and port
   0x61. It is cheap, but on many laptops the beep is inaudible, so it is used
   only when nothing better exists. Its 140 Hz tone changes need a timer event
   faster than the 35 Hz game loop.
2. **PCM in QEMU, mixer and music:** an AC'97 or virtio-sound driver over
   `PCI_IO`, an 8-channel effects mixer (host-tested), and OPL music emulation
   from Chocolate Doom (MUS to OPL). Mixing runs in the main loop every tic into
   a DMA ring sized for about three tics; drivers never block.
3. **Intel HDA:** a generic codec parser driven by the pin configuration the
   firmware sets, with a quirk table for amplifier and GPIO enables. Laptops
   that route audio through a DSP with SoundWire codecs stay out of reach.

### Networking

- Restore Chocolate Doom's multiplayer code and provide its `net_module_t`.
- Transport: raw Ethernet frames through `SIMPLE_NETWORK_PROTOCOL` first
  (available whenever a NIC driver exists; LAN only, broadcast discovery), then
  optionally UDP through the firmware's IP stack.
- Link and DHCP bring-up happen before the game loop with a visible timeout,
  only when a network game is requested; steady-state I/O is polled.

### Smaller items

- Decode raw scancode set 2, for i8042s with translation off (they currently
  fall back to ConIn).
- USB hot-plug: pick up keyboards and mice connected after start-up.
- Keyboard layouts other than US (affects typed text such as save names only).
- Write-combining on machines with 5-level paging, or with the framebuffer in a
  4 KiB-mapped region.
- Ship a removable-media layout (`\EFI\BOOT\BOOTX64.EFI`) in the release zip.
- Continuous integration for the unit and lint suites.

## Not planned

- Architectures other than x86_64.
- Secure Boot signing.
- Gamepads and other non-boot-protocol HID devices (would follow from the
  report-descriptor parser above).
- Apple hardware (SPI keyboards and trackpads).
- Rendering above the original resolution, or other engine enhancements.
