# UEFI Doom: build for x86_64 UEFI (PE32+) with clang + lld-link, plus host unit tests.
# See docs/architecture.md#build-and-test.

PROFILE  ?= devloop
DEBUGCON ?= 0

ifneq ($(PROFILE),devloop)
ifneq ($(PROFILE),release)
$(error PROFILE must be devloop or release)
endif
endif

BUILD    := build
OUT      := $(BUILD)/x64-$(PROFILE)$(if $(filter 1,$(DEBUGCON)),-debugcon)
HOSTOUT  := $(BUILD)/host
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo nogit)-$(PROFILE)
# Rewritten only when the ID changes, so the files that embed it are rebuilt then.
BUILD_ID_STAMP := $(OUT)/build-id
$(shell mkdir -p $(OUT) && (echo '$(BUILD_ID)' | cmp -s - $(BUILD_ID_STAMP) || \
	echo '$(BUILD_ID)' > $(BUILD_ID_STAMP)))

CC       := clang
EFI_TARGET := -target x86_64-unknown-windows

EFI_CFLAGS := $(EFI_TARGET) -std=c11 -O2 \
	-ffreestanding -fshort-wchar -mno-red-zone -mno-stack-arg-probe \
	-fno-stack-protector -fno-strict-aliasing \
	-nostdlibinc -isystem include/libc \
	-Ithird_party/edk2/MdePkg/Include -Ithird_party/edk2/MdePkg/Include/X64 \
	-Ithird_party/printf/src -Iinclude \
	-DPROFILE_$(shell echo $(PROFILE) | tr a-z A-Z)=1 -DDEBUGCON=$(DEBUGCON) \
	-DBUILD_ID='"$(BUILD_ID)"' \
	-MMD -MP
WARN_OWN  := -Wall -Wextra -Werror -Wno-unused-parameter
WARN_VEND := -w
EFI_LDFLAGS := $(EFI_TARGET) -nostdlib -fuse-ld=lld \
	-Wl,-entry:efi_main -Wl,-subsystem:efi_application
# clang links the PE32+ images with lld-link (Homebrew: lld; Debian/Ubuntu: lld).
NEED_LLD = $(if $(shell command -v lld-link 2>/dev/null),,\
	$(error lld-link not found: install LLVM's lld (brew install lld / apt install lld)))

# Modules shared by every UEFI application.
COMMON_SRC := $(wildcard src/efi/*.c) $(wildcard src/libc/*.c) $(wildcard src/video/*.c) \
	$(wildcard src/input/*.c)
VENDOR_SRC := third_party/printf/src/printf/printf.c
HWPROBE_SRC := $(wildcard src/hwprobe/*.c)
# The engine (vendored doomgeneric, minus its other platforms) and our glue.
ENGINE_SRC := $(wildcard third_party/doomgeneric/src/*.c)
PORT_SRC := $(wildcard src/port/*.c)
DOOM_DEFS := -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DUEFI_DOOM \
	-Ithird_party/doomgeneric/src

obj = $(patsubst %.c,$(OUT)/obj/%.o,$(1))

COMMON_OBJ  := $(call obj,$(COMMON_SRC))
VENDOR_OBJ  := $(call obj,$(VENDOR_SRC))
HWPROBE_OBJ := $(call obj,$(HWPROBE_SRC))
ENGINE_OBJ  := $(call obj,$(ENGINE_SRC))
PORT_OBJ    := $(call obj,$(PORT_SRC))

# The engine is built as a POSIX-ish program: hide clang's Windows-target macros
# (port/ includes engine headers but no UEFI headers, so it gets the same).
$(ENGINE_OBJ): EXTRA_CFLAGS := -std=gnu11 -U_WIN32 -U_WIN64 -U_MSC_VER $(DOOM_DEFS)
$(PORT_OBJ): EXTRA_CFLAGS := -U_WIN32 -U_WIN64 -U_MSC_VER $(DOOM_DEFS)

.PHONY: all doom hwprobe test-efi unit esp run dist format format-check clean
all: doom hwprobe

doom: $(OUT)/DOOM.EFI
hwprobe: $(OUT)/HWPROBE.EFI

$(OUT)/DOOM.EFI: $(COMMON_OBJ) $(VENDOR_OBJ) $(ENGINE_OBJ) $(PORT_OBJ)
	$(NEED_LLD)$(CC) $(EFI_LDFLAGS) $^ -o $@

$(OUT)/HWPROBE.EFI: $(COMMON_OBJ) $(VENDOR_OBJ) $(HWPROBE_OBJ)
	$(NEED_LLD)$(CC) $(EFI_LDFLAGS) $^ -o $@

$(call obj,src/efi/main.c src/efi/log.c src/hwprobe/hwprobe.c): $(BUILD_ID_STAMP)

$(OUT)/obj/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(EFI_CFLAGS) $(EXTRA_CFLAGS) $(WARN_OWN) -c $< -o $@

$(OUT)/obj/third_party/%.o: third_party/%.c
	@mkdir -p $(@D)
	$(CC) $(EFI_CFLAGS) $(EXTRA_CFLAGS) $(WARN_VEND) -c $< -o $@

# Test build: same sources with the QEMU debugcon log sink enabled.
test-efi:
	$(MAKE) DEBUGCON=1 doom hwprobe

# --- Host unit tests ---------------------------------------------------------
# Each tests/unit/test_X.c links against the pure *_core.c files it names in
# UNIT_SRC_test_X.
HOST_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc/libc -Itests/unit \
	-DLIBC_HOST_TEST -D_DEFAULT_SOURCE \
	-fsanitize=address,undefined -fno-omit-frame-pointer

UNIT_SRC_test_ring  := src/efi/ring_core.c
UNIT_SRC_test_dlog  := src/efi/dlog_core.c
UNIT_SRC_test_argv  := src/efi/argv_core.c
UNIT_SRC_test_path  := src/efi/path_core.c
UNIT_SRC_test_acpi  := src/efi/acpi_core.c
UNIT_SRC_test_video := src/video/video_core.c
UNIT_SRC_test_memtype := src/efi/memtype_core.c
UNIT_SRC_test_conin := src/input/conin_core.c
UNIT_SRC_test_i8042 := src/input/i8042_core.c src/input/keymap_core.c
UNIT_SRC_test_hid   := src/input/hid_core.c src/input/keymap_core.c
UNIT_SRC_test_pointer := src/input/pointer_core.c
UNIT_SRC_test_wad   := src/port/wad_check.c
UNIT_SRC_test_libc  := src/libc/ctype.c src/libc/str.c src/libc/scan_core.c
UNIT_SRC_test_file  := src/libc/file.c src/libc/scan_core.c src/libc/ctype.c tests/unit/fake_fs.c

UNIT_TESTS := $(patsubst tests/unit/%.c,$(HOSTOUT)/%,$(wildcard tests/unit/test_*.c))

unit: $(UNIT_TESTS)

.SECONDEXPANSION:
$(HOSTOUT)/test_%: tests/unit/test_%.c $$(UNIT_SRC_test_$$*) tests/unit/check.h \
	$(wildcard src/libc/*.h)
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) $< $(UNIT_SRC_test_$*) -o $@

# --- QEMU --------------------------------------------------------------------
ESP := $(BUILD)/esp
esp: doom hwprobe
	@mkdir -p $(ESP)/EFI/BOOT $(ESP)/EFI/DOOM
	cp $(OUT)/HWPROBE.EFI $(ESP)/EFI/DOOM/HWPROBE.EFI
	cp $(OUT)/DOOM.EFI $(ESP)/EFI/DOOM/DOOM.EFI
	cp $(OUT)/DOOM.EFI $(ESP)/EFI/BOOT/BOOTX64.EFI
	cp $(wildcard tests/data/*.wad) $(ESP)/EFI/BOOT/ 2>/dev/null || true

run: esp
	tools/run_qemu.sh $(ESP)

# --- Formatting ----------------------------------------------------------------
FORMAT_FILES := $(shell find src include tests/unit -name '*.[ch]' 2>/dev/null)
format:
	clang-format -i $(FORMAT_FILES)
format-check:
	clang-format --dry-run -Werror $(FORMAT_FILES)

# --- Release package ------------------------------------------------------------
# Release builds of both apps plus the install guide; no WADs.
VERSION := $(shell git describe --always --dirty 2>/dev/null || echo nogit)
DIST    := $(BUILD)/dist
DIST_NAME := uefi-doom-$(VERSION)
dist:
	$(MAKE) PROFILE=release doom hwprobe
	rm -rf $(DIST)/stage
	mkdir -p $(DIST)/stage/$(DIST_NAME)/EFI/DOOM
	cp $(BUILD)/x64-release/DOOM.EFI $(BUILD)/x64-release/HWPROBE.EFI $(DIST)/stage/$(DIST_NAME)/EFI/DOOM/
	cp docs/INSTALL.md README.md LICENSE $(DIST)/stage/$(DIST_NAME)/
	rm -f $(DIST)/$(DIST_NAME).zip
	cd $(DIST)/stage && zip -qr ../$(DIST_NAME).zip $(DIST_NAME)
	@echo $(DIST)/$(DIST_NAME).zip

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
