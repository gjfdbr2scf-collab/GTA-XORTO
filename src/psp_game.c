#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512

static unsigned int __attribute__((aligned(16))) display_list[262144];

static int game_initialized = 0;

int psp_game_init(void)
{
    psp_input_init();

    pspDebugScreenInit();

    sceGuInit();

    sceGuStart(GU_DIRECT, display_list);

    sceGuDrawBuffer(
        GU_PSM_5650,
        (void *)0,
        BUFFER_WIDTH
    );

    sceGuDispBuffer(
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (void *)(BUFFER_WIDTH * SCREEN_HEIGHT * 2),
        BUFFER_WIDTH
    );

    sceGuOffset(
        2048 - (SCREEN_WIDTH / 2),
        2048 - (SCREEN_HEIGHT / 2)
    );

    sceGuViewport(
        2048,
        2048,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    sceGuScissor(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuDisplay(GU_TRUE);

    game_initialized = 1;

    return 0;
}

void psp_game_update(void)
{
    psp_input_update();
}

void psp_game_render(void)
{
    if (!game_initialized)
        return;

    sceGuStart(GU_DIRECT, display_list);

    /*
     * Sichtbarer Test-Hintergrund.
     * Damit sehen wir sofort, ob die PSP-Grafikausgabe läuft.
     */
    sceGuClearColor(0x202060FF);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuSwapBuffers();

    /*
     * Zusätzlicher Text-Test.
     */
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(2, 2);

    pspDebugScreenPrintf(
        "GINSENG STRIP GTA"
    );

    pspDebugScreenSetXY(2, 4);

    pspDebugScreenPrintf(
        "PSP-Grafik laeuft."
    );

    pspDebugScreenSetXY(2, 6);

    pspDebugScreenPrintf(
        "Naechster Schritt: Hauptmenue."
    );
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();

    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    game_initialized = 0;
}
