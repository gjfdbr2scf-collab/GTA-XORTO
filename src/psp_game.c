#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

#include "psp_game.h"
#include "psp_input.h"

#define MENU_COUNT 4

static int game_initialized = 0;
static int selected_menu = 0;
static int button_lock = 0;

static const char *menu_items[MENU_COUNT] =
{
    "STORY MODE",
    "FREE OPEN WORLD",
    "MULTIPLAYER",
    "SYSTEM SETTINGS"
};

int psp_game_init(void)
{
    psp_input_init();

    pspDebugScreenInit();

    pspDebugScreenSetBackColor(0x101018);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();

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

    /*
     * Kleine Sperre gegen zu schnelles
     * mehrfaches Umschalten.
     */
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

    /*
     * X waehlt den aktuellen Menuepunkt aus.
     * Die eigentlichen Spielbereiche werden
     * in den naechsten Schritten eingebaut.
     */
    if (input->cross)
    {
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

    pspDebugScreenClear();

    /*
     * Titel
     */
    pspDebugScreenSetXY(2, 1);
    pspDebugScreenPrintf("GINSENG STRIP GTA");

    pspDebugScreenSetXY(2, 2);
    pspDebugScreenPrintf("------------------------------");

    /*
     * Hauptmenue
     */
    for (i = 0; i < MENU_COUNT; i++)
    {
        pspDebugScreenSetXY(5, 5 + (i * 2));

        if (i == selected_menu)
        {
            pspDebugScreenSetTextColor(0xFFFFFFFF);
            pspDebugScreenPrintf("> %s <", menu_items[i]);
        }
        else
        {
            pspDebugScreenSetTextColor(0xFFAAAAAA);
            pspDebugScreenPrintf("  %s", menu_items[i]);
        }
    }

    /*
     * Bedienhinweise
     */
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(3, 16);
    pspDebugScreenPrintf("UP/DOWN  = AUSWAHL");

    pspDebugScreenSetXY(3, 17);
    pspDebugScreenPrintf("X        = AUSWAHL BESTAETIGEN");

    pspDebugScreenSetXY(3, 19);
    pspDebugScreenPrintf("GINSENG STRIP GTA - PSP EDITION");

    sceDisplayWaitVblankStart();
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();

    game_initialized = 0;
}
