/* doomgeneric platform layer for UEFI: DG_* over the video, clock, idle and
 * input modules, plus the fatal-error screen and the test log.
 * See docs/architecture.md#engine-glue-port. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include "efi/clock.h"
#include "efi/efi_main.h"
#include "efi/idle.h"
#include "efi/log.h"
#include "input/input.h"
#include "input/input_hw.h"
#include "video/video.h"

#include "d_player.h"
#include "doomgeneric.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_controls.h"

const char app_name[] = "DOOM";

#define FATAL_WAIT_MS 60000u /* then return to firmware anyway (unattended boots) */
#define TICRATE 35

static int testlog_every; /* tics; 0 = off (test builds only) */
static int testlog_next;

void DG_Init(void) {
    video_config cfg = {.aspect_correct = true, .allow_wc = true, .allow_avx = true};
    //!
    // @arg <n>
    // Largest integer scale factor to use (default: as large as fits).
    int p = M_CheckParmWithArgs("-scale", 1);
    if (p) {
        int k = atoi(myargv[p + 1]);
        if (k < 1 || k > 32) {
            I_Error("-scale: expected 1 to 32, not '%s'", myargv[p + 1]);
        }
        cfg.max_scale = (unsigned)k;
    }
    //!
    // @arg <WxH>
    // Use this GOP mode if the firmware offers it (default: the current mode).
    p = M_CheckParmWithArgs("-mode", 1);
    if (p) {
        unsigned w, h;
        if (sscanf(myargv[p + 1], "%ux%u", &w, &h) != 2 || !w || !h) {
            I_Error("-mode: expected WIDTHxHEIGHT, not '%s'", myargv[p + 1]);
        }
        cfg.pref_w = w;
        cfg.pref_h = h;
    }
    //!
    // Present through GOP->Blt instead of writing the framebuffer.
    cfg.force_blt = M_ParmExists("-blt");
    if (!video_init(&cfg)) {
        I_Error("No usable graphics mode (see the log)");
    }
    idle_init();

    //!
    // @arg auto|raw|conin
    // Keyboard sources: all raw devices with ConIn as fallback (default),
    // raw devices only, or the firmware console only.
    input_mode mode = INPUT_AUTO;
    p = M_CheckParmWithArgs("-input", 1);
    if (p) {
        const char *m = myargv[p + 1];
        if (strcmp(m, "raw") == 0) {
            mode = INPUT_RAW;
        } else if (strcmp(m, "conin") == 0) {
            mode = INPUT_CONIN;
        } else if (strcmp(m, "auto") != 0) {
            I_Error("-input: expected auto, raw or conin, not '%s'", m);
        }
    }
    //!
    // @arg auto|firmware|off
    // Mice: raw USB and PS/2 mice plus the firmware's pointers (default),
    // the firmware's pointers only, or none.
    mouse_mode mice = MOUSE_AUTO;
    p = M_CheckParmWithArgs("-mouse", 1);
    if (p) {
        const char *m = myargv[p + 1];
        if (strcmp(m, "firmware") == 0) {
            mice = MOUSE_FIRMWARE;
        } else if (strcmp(m, "off") == 0) {
            mice = MOUSE_OFF;
        } else if (strcmp(m, "auto") != 0) {
            I_Error("-mouse: expected auto, firmware or off, not '%s'", m);
        }
    }
    input_init(mode, mice);
#if DEBUGCON
    //!
    // @arg <n>
    // Test builds: log player state every n tics.
    p = M_CheckParmWithArgs("-testlog", 1);
    if (p) {
        testlog_every = atoi(myargv[p + 1]);
    }
#endif
}

static void testlog(void) {
    if (!testlog_every || gametic < testlog_next) {
        return;
    }
    testlog_next = gametic + testlog_every;
    const player_t *pl = &players[consoleplayer];
    if (gamestate == GS_LEVEL && pl->mo) {
        log_msg(LOG_INFO, "testlog tic=%d x=%d y=%d angle=%u health=%d", gametic, pl->mo->x >> 16,
                pl->mo->y >> 16, (unsigned)(pl->mo->angle >> 24), pl->health);
    } else {
        log_msg(LOG_INFO, "testlog tic=%d state=%d", gametic, (int)gamestate);
    }
}

void DG_DrawFrame(void) {
    static uint8_t rgb[256][3];
    bool changed = palette_changed;
    if (changed) {
        for (int i = 0; i < 256; i++) {
            rgb[i][0] = colors[i].r;
            rgb[i][1] = colors[i].g;
            rgb[i][2] = colors[i].b;
        }
        palette_changed = false;
    }
    video_present(DG_ScreenBuffer, (const uint8_t (*)[3])rgb, changed);
    testlog();
}

/* The engine only sleeps to wait for the next tic (I_Sleep(1) in a loop) or for
 * fixed delays, so sleep until the later of the two: the CPU idles in between. */
void DG_SleepMs(uint32_t ms) {
    uint64_t now = clock_us();
    uint64_t tic = (now / 1000) * TICRATE / 1000; /* as I_GetTime computes it */
    uint64_t next_tic_us = ((tic + 1) * 1000 + TICRATE - 1) / TICRATE * 1000;
    uint64_t want = now + (uint64_t)ms * 1000;
    idle_until(want > next_tic_us ? want : next_tic_us);
}

uint32_t DG_GetTicksMs(void) {
    return clock_ms();
}

/* -wasd: bind W/A/S/D to forward, strafe left, back, strafe right. Applied on
 * the first input poll, after the config is loaded, and saved with it on quit,
 * so it needs giving only once. */
static void apply_wasd_once(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;
    //!
    // Bind W/A/S/D to move and strafe (saved in the config).
    if (M_ParmExists("-wasd")) {
        key_up = 'w';
        key_down = 's';
        key_strafeleft = 'a';
        key_straferight = 'd';
        log_msg(LOG_INFO, "controls: WASD movement bound");
    }
}

int DG_GetKey(int *pressed, unsigned char *key) {
    apply_wasd_once();
    key_event ev;
    if (!input_next_key(&ev)) {
        return 0;
    }
    *pressed = ev.pressed;
    *key = ev.key;
    if (testlog_every) {
        log_msg(LOG_INFO, "testlog key %s 0x%02x tic=%d", ev.pressed ? "down" : "up", ev.key,
                gametic);
    }
    return 1;
}

/* Called by the patched I_GetEvent once per tic. */
int DG_GetMouse(int *buttons, int *dx, int *dy) {
    return input_mouse(buttons, dx, dy);
}

void DG_SetWindowTitle(const char *title) {
    log_msg(LOG_INFO, "title: %s", title);
}

/* Called by the patched I_Error (unless -nogui): show the message in text
 * mode and wait for a key, so the user can read it before we exit. */
void DG_FatalScreen(const char *msg) {
    video_shutdown(); /* back to text mode; ConOut logging on again */
    log_msg(LOG_ERROR, "Fatal error: %s", msg);
    log_msg(LOG_ERROR, "Press any key to exit.");
    key_event ev;
    while (input_next_key(&ev)) { /* only a new press dismisses the screen */
    }
    uint16_t scan, ch;
    uint32_t shift;
    while (conin_read(&scan, &ch, &shift)) {
    }
    input_wait_any_key(FATAL_WAIT_MS);
}

int app_main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) {
        doomgeneric_Tick();
    }
}
