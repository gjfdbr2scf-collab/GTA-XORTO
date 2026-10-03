#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspdebug.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512

#define MENU_COUNT 4

static unsigned int __attribute__((aligned(16))) display_list[262144];

static int game_initialized = 0;
static int selected_menu = 0;
static int button_lock = 0;

static const char *menu_items[MENU_COUNT] =
{
    "STORY MODE",
    "FREE OPEN WORLD",
    "MULTIPLAYER (LOCAL)",
    "SYSTEM SETTINGS"
};

int psp_game_init(void)
{
    psp_input_init();

    pspDebugScreenInit();

    /*
     * PSP-Grafik wieder genauso initialisieren
     * wie beim funktionierenden roten Test.
     */
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
    selected_menu = 0;
    button_lock = 0;

    return 0;
}

void psp_game_update(void)
{
    const PSPInputState *input;

    if (!game_initialized)
    {
        return;
    }

    psp_input_update();

    input = psp_input_get_state();

    if (button_lock > 0)
    {
        button_lock--;
        return;
    }

    if (input->up)
    {
        selected_menu--;

        if (selected_menu < 0)
        {
            selected_menu = MENU_COUNT - 1;
        }

        button_lock = 8;
    }
    else if (input->down)
    {
        selected_menu++;

        if (selected_menu >= MENU_COUNT)
        {
            selected_menu = 0;
        }

        button_lock = 8;
    }

    if (input->cross)
    {
        /*
         * Die eigentlichen Menüseiten kommen
         * im nächsten Schritt.
         */
        button_lock = 12;
    }
}

void psp_game_render(void)
{
    int i;

    if (!game_initialized)
    {
        return;
    }

    /*
     * GU-Framebuffer löschen.
     * Dunkler Hintergrund statt schwarzem
     * "hängenden" Bildschirm.
     */
    sceGuStart(GU_DIRECT, display_list);

    sceGuClearColor(0x202020FF);

    sceGuClear(
        GU_COLOR_BUFFER_BIT
    );

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    /*
     * Debug-Menü auf den Bildschirm schreiben.
     */
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(2, 1);
    pspDebugScreenPrintf(
        "GINSENG STRIP 2007"
    );

    pspDebugScreenSetXY(2, 2);
    pspDebugScreenPrintf(
        "------------------------------"
    );

    for (i = 0; i < MENU_COUNT; i++)
    {
        pspDebugScreenSetXY(
            4,
            5 + (i * 2)
        );

        if (i == selected_menu)
        {
            pspDebugScreenSetTextColor(
                0xFFFFFFFF
            );

            pspDebugScreenPrintf(
                "> %s <",
                menu_items[i]
            );
        }
        else
        {
            pspDebugScreenSetTextColor(
                0xFFAAAAAA
            );

            pspDebugScreenPrintf(
                "  %s",
                menu_items[i]
            );
        }
    }

    pspDebugScreenSetTextColor(
        0xFFFFFFFF
    );

    pspDebugScreenSetXY(3, 16);
    pspDebugScreenPrintf(
        "UP / DOWN = AUSWAHL"
    );

    pspDebugScreenSetXY(3, 18);
    pspDebugScreenPrintf(
        "X = BESTAETIGEN"
    );

    pspDebugScreenSetXY(3, 20);
    pspDebugScreenPrintf(
        "GINSENG STRIP GTA - PSP EDITION"
    );

    sceDisplayWaitVblankStart();

    sceGuSwapBuffers();
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();

    sceGuDisplay(GU_FALSE);

    sceGuTerm();

    game_initialized = 0;
}
