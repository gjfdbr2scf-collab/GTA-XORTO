#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

static int game_initialized = 0;

int psp_game_init(void)
{
    psp_input_init();

    pspDebugScreenInit();

    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();

    pspDebugScreenPrintf("GINSENG STRIP GTA\n");
    pspDebugScreenPrintf("Aufloesung: %dx%d\n\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    pspDebugScreenPrintf("PSP-Grafiksystem gestartet.\n");
    pspDebugScreenPrintf("Hauptmenue wird vorbereitet.\n");

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

    sceDisplayWaitVblankStart();
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();
    game_initialized = 0;
}
