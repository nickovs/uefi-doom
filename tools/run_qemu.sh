#!/bin/sh
# Boot an ESP in QEMU + OVMF.
#   tools/run_qemu.sh <esp-dir|esp-image> [extra qemu args...]
# A directory is exposed as QEMU's virtual FAT (vvfat): fine for booting, but
# vvfat loses guest writes, so tests that write files use a raw FAT image.
# Environment: OVMF_CODE / OVMF_VARS override firmware paths; QEMU_DISPLAY
# (default "cocoa" on macOS, else "gtk"; use "none" for headless);
# QEMU_READONLY=1 attaches the disk read-only (as virtio-blk: QEMU's IDE/AHCI
# disks cannot be read-only);
# QEMU_LOGDIR (default build/qemu) receives app.log (our DEBUGCON=1 log, port
# 0x403), ovmf.log (firmware debug output, port 0x402) and serial.log.
set -eu

esp=${1:?usage: run_qemu.sh <esp-dir|esp-image> [qemu args...]}
shift
if [ -d "$esp" ]; then
    disk="format=raw,file=fat:rw:$esp"
elif [ -f "$esp" ]; then
    disk="format=raw,file=$esp"
else
    echo "run_qemu.sh: no such directory or image: $esp" >&2
    exit 2
fi

# Firmware: QEMU's bundled EDK2 (Homebrew, Fedora, Arch), else the distro's
# OVMF package (Debian/Ubuntu ovmf, Fedora edk2-ovmf, Arch edk2-ovmf).
share=$(dirname "$(command -v qemu-system-x86_64)")/../share/qemu
code=$share/edk2-x86_64-code.fd
vars_template=$share/edk2-i386-vars.fd
firmware() { # CODE VARS: use this pair if both exist
    [ -f "$1" ] && [ -f "$2" ] && code=$1 && vars_template=$2
}
[ -f "$code" ] ||
    firmware /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_VARS_4M.fd ||
    firmware /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_VARS.fd ||
    firmware /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_VARS.fd ||
    firmware /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_VARS.4m.fd ||
    true # the check below reports the default paths
code=${OVMF_CODE:-$code}
vars_template=${OVMF_VARS:-$vars_template}
if [ "${QEMU_READONLY:-0}" = 1 ]; then
    drive="-drive if=none,id=esp,readonly=on,$disk -device virtio-blk-pci,drive=esp"
else
    drive="-drive $disk"
fi

for f in "$code" "$vars_template"; do
    [ -f "$f" ] || { echo "run_qemu.sh: firmware not found: $f" >&2; exit 2; }
done

logdir=${QEMU_LOGDIR:-build/qemu}
mkdir -p "$logdir"
vars="$logdir/ovmf-vars.fd"
cp "$vars_template" "$vars"   # fresh NVRAM each run

case $(uname -s) in Darwin) default_display=cocoa ;; *) default_display=gtk ;; esac

exec qemu-system-x86_64 \
    -machine q35 -m 512 \
    -drive if=pflash,format=raw,readonly=on,file="$code" \
    -drive if=pflash,format=raw,file="$vars" \
    $drive \
    -chardev file,id=ovmflog,path="$logdir/ovmf.log" \
    -device isa-debugcon,iobase=0x402,chardev=ovmflog \
    -chardev file,id=applog,path="$logdir/app.log" \
    -device isa-debugcon,iobase=0x403,chardev=applog \
    -serial file:"$logdir/serial.log" \
    -display "${QEMU_DISPLAY:-$default_display}" \
    -net none \
    "$@"
