#include "psp_game.h"
#include "psp_input.h"

int psp_game_init(void)
{
    psp_input_init();
    return 0;
}

void psp_game_update(void)
{
    psp_input_update();
}

void psp_game_render(void)
{
    /*
     * Die eigentliche PSP-Grafikausgabe
     * wird in einem späteren Schritt ergänzt.
     */
}

void psp_game_shutdown(void)
{
    psp_input_shutdown();
}
