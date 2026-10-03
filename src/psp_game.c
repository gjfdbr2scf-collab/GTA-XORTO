#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

#include "psp_game.h"
#include "psp_input.h"

static int game_initialized = 0;

int psp_game_init(void)
{
    /* PSP-Eingabe initialisieren */
    psp_input_init();

    /* Debug-Bildschirm initialisieren */
    pspDebugScreenInit();

    /* Hintergrund und Text vorbereiten */
    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();

    /* Sichtbarer Test auf dem Bildschirm */
    pspDebugScreenPrintf("GINSENG STRIP\n");
    pspDebugScreenPrintf("------------------------------\n\n");
    pspDebugScreenPrintf("PSP-Start erfolgreich.\n\n");
    pspDebugScreenPrintf("Grafikausgabe funktioniert.\n");
    pspDebugScreenPrintf("Die EBOOT.PBP wird ausgefuehrt.\n\n");
    pspDebugScreenPrintf("Naechster Schritt:\n");
    pspDebugScreenPrintf("Hauptmenue und echte Grafik.\n");

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

    /* Auf den naechsten Bildaufbau warten */
    sceDisplayWaitVblankStart();
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();
    game_initialized = 0;
}
