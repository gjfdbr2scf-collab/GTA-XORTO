#include <stdio.h>
#include "game.h"
#include "psp_game.h"

int main(void)
{
    printf("Yenzangs Trip - PSP Game\n");

    game_init();
    psp_game_init();

    printf("Game started.\n");

    game_update();
    psp_game_update();

    game_render();
    psp_game_render();

    psp_game_shutdown();
    game_shutdown();

    return 0;
}
