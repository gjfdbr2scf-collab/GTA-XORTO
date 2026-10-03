#include <stdio.h>
#include "game.h"

int main(void)
{
    printf("Yenzangs Trip - PSP Game\n");

    game_init();

    printf("Game started.\n");

    game_update();
    game_render();

    game_shutdown();

    return 0;
}
