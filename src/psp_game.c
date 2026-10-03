#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

#include "psp_game.h"
#include "psp_input.h"

#define MENU_COUNT 4

static int game_initialized = 0;
static int selected_menu = 0;
static int button_lock = 0;
static int current_screen = 0;

static const char *menu_items[MENU_COUNT] =
{
    "STORY MODE",
    "FREE OPEN WORLD",
    "MULTIPLAYER (LOCAL)",
    "SYSTEM SETTINGS"
};

static void draw_main_menu(void)
{
    int i;

    pspDebugScreenClear();

    pspDebugScreenSetBackColor(0x101018);
    pspDebugScreenSetTextColor(0xFFFFFFFF);

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
        pspDebugScreenSetXY(4, 5 + (i * 2));

        if (i == selected_menu)
        {
            pspDebugScreenSetTextColor(0xFFFFFFFF);

            pspDebugScreenPrintf(
                "> %s <",
                menu_items[i]
            );
        }
        else
        {
            pspDebugScreenSetTextColor(0xFFAAAAAA);

            pspDebugScreenPrintf(
                "  %s",
                menu_items[i]
            );
        }
    }

    /*
     * Bedienung
     */
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(3, 15);
    pspDebugScreenPrintf("UP / DOWN = AUSWAHL");

    pspDebugScreenSetXY(3, 17);
    pspDebugScreenPrintf("X = AUSWAHL");

    pspDebugScreenSetXY(3, 19);
    pspDebugScreenPrintf("GINSENG STRIP GTA");
}

static void draw_submenu(void)
{
    const char *title = "MENU";

    if (current_screen == 1)
        title = "STORY MODE";

    if (current_screen == 2)
        title = "FREE OPEN WORLD";

    if (current_screen == 3)
        title = "MULTIPLAYER (LOCAL)";

    if (current_screen == 4)
        title = "SYSTEM SETTINGS";

    pspDebugScreenClear();

    pspDebugScreenSetBackColor(0x101018);
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(2, 1);
    pspDebugScreenPrintf(
        "GINSENG STRIP GTA"
    );

    pspDebugScreenSetXY(2, 2);
    pspDebugScreenPrintf(
        "------------------------------"
    );

    pspDebugScreenSetXY(4, 5);
    pspDebugScreenPrintf(
        "%s",
        title
    );

    pspDebugScreenSetXY(4, 8);

    if (current_screen == 1)
    {
        pspDebugScreenPrintf(
            "Story Mode wird vorbereitet..."
        );
    }
    else if (current_screen == 2)
    {
        pspDebugScreenPrintf(
            "Free Open World wird vorbereitet..."
        );
    }
    else if (current_screen == 3)
    {
        pspDebugScreenPrintf(
            "Lokaler Multiplayer wird vorbereitet..."
        );
    }
    else if (current_screen == 4)
    {
        pspDebugScreenPrintf(
            "System Settings"
        );

        pspDebugScreenSetXY(6, 10);
        pspDebugScreenPrintf(
            "Language"
        );

        pspDebugScreenSetXY(6, 12);
        pspDebugScreenPrintf(
            "Username"
        );

        pspDebugScreenSetXY(6, 14);
        pspDebugScreenPrintf(
            "Display"
        );

        pspDebugScreenSetXY(6, 16);
        pspDebugScreenPrintf(
            "Sound"
        );
    }

    pspDebugScreenSetXY(4, 21);

    pspDebugScreenPrintf(
        "O = ZURUECK"
    );
}

int psp_game_init(void)
{
    psp_input_init();

    /*
     * Der PSP-Debug-Bildschirm hat bei unserem
     * vorherigen Test bereits korrekt funktioniert.
     * Deshalb benutzen wir ihn als stabile Basis
     * fuer das Menue.
     */
    pspDebugScreenInit();

    pspDebugScreenSetBackColor(0x101018);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();

    game_initialized = 1;
    selected_menu = 0;
    button_lock = 0;
    current_screen = 0;

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
     * Tastensperre gegen mehrfaches Ausloesen.
     */
    if (button_lock > 0)
    {
        button_lock--;
        return;
    }

    /*
     * Im Hauptmenue
     */
    if (current_screen == 0)
    {
        if (input->up)
        {
            selected_menu--;

            if (selected_menu < 0)
            {
                selected_menu = MENU_COUNT - 1;
            }

            button_lock = 8;
            return;
        }

        if (input->down)
        {
            selected_menu++;

            if (selected_menu >= MENU_COUNT)
            {
                selected_menu = 0;
            }

            button_lock = 8;
            return;
        }

        if (input->cross)
        {
            /*
             * Menuepunkt auswaehlen.
             */
            current_screen = selected_menu + 1;

            button_lock = 12;
            return;
        }
    }
    else
    {
        /*
         * O bringt zurueck zum Hauptmenue.
         */
        if (input->circle)
        {
            current_screen = 0;
            button_lock = 12;
            return;
        }
    }
}

void psp_game_render(void)
{
    if (!game_initialized)
    {
        return;
    }

    if (current_screen == 0)
    {
        draw_main_menu();
    }
    else
    {
        draw_submenu();
    }

    /*
     * Auf den naechsten Bildaufbau warten.
     */
    sceDisplayWaitVblankStart();
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();

    game_initialized = 0;
    selected_menu = 0;
    button_lock = 0;
    current_screen = 0;
}
