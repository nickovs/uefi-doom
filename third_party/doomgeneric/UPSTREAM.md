# doomgeneric (ozkl/doomgeneric)

- Upstream: https://github.com/ozkl/doomgeneric
- Commit: `e257a09527062bfb986b6e5e0780839b6cbd5b71` (2026-10-11)
- Licence: GPL-2.0 (`LICENSE`).
- Contents: `src/` holds the engine sources from upstream `doomgeneric/`: every
  file in upstream's `SRC_DOOM` list except the platform backends
  (`doomgeneric_*.c`), plus all headers. `README.md` is upstream's.

## Build-time configuration (Makefile, not patches)

- `-DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200`: an 8-bit indexed
  320×200 frame; scaling and palette conversion happen in `src/video/`.
- `-U_WIN32 -U_WIN64 -U_MSC_VER`: clang's `x86_64-unknown-windows` target
  defines these, but the engine must take its POSIX paths.
- `-DUEFI_DOOM`: enables the local changes below.

## Local changes

All behaviour changes are inside `#ifdef UEFI_DOOM` (or `#ifndef`), so
`diff -r` against upstream shows exactly these:

| File | Change | Why |
|---|---|---|
| `i_system.c` | `DEFAULT_RAM` 32 MiB (was 6) | RAM is plentiful; big PWADs need a larger zone. `-mb` still overrides. |
| `i_system.c` | `I_Quit` calls `exit(0)` | Upstream returns to the caller (its `exit` is SDL-only); we leave through `efi_exit`. |
| `i_system.c` | `I_Error` calls `DG_FatalScreen(msg)` instead of the zenity/`system()` popup | Shows the message in text mode and waits for a key (skipped with `-nogui`). |
| `w_file.c` | `W_OpenFile` uses `mem_wad_file` (`src/port/w_file_mem.c`) | WADs are read whole into memory, validated, and used zero-copy. |
| `m_config.c` | `GetDefaultConfigDir` returns `""` | Config and savegames live in the image directory. |
| `i_input.c` | `I_GetEvent` keeps draining after a key-up | Upstream stops after the first key-up each tic, delaying other events. |
| `i_input.c` | `I_GetEvent` posts an `ev_mouse` from `DG_GetMouse()`, without vertical motion when `novert` is set | doomgeneric's API has no mouse. |
| `i_video.c` | `I_BindVideoVariables` binds `novert` (default 1) | Upstream lists `novert` in the config but never binds or applies it. |
| `m_config.c` | Config load/save compiled in (upstream has them under `ORIGCODE` only) | Settings persist in `default.cfg` and `doomgenericdoom.cfg`. |
| `m_config.c` | Key bindings 128–255 load as raw Doom keys; vanilla bindings to Ctrl, Space, `,` and `.` load as `KEY_FIRE`, `KEY_USE`, `KEY_STRAFE_L/R` | doomgeneric's action keys have no scancode, so they are saved raw, and upstream's loader turned them into 0 (fire, use and strafe lost after the first quit). The port's keyboard sends the action codes for those keys. |
| `g_game.c` | `G_DoSaveGame` reports a failed open, write, close or rename as an in-game message (`G_SaveFailed`) and keeps the old save | Upstream calls `I_Error`, and its recovery file goes to `/tmp`. |

