#include "game.h"

int main(void)
{
    game_init();
    game_update();
    game_render();
    game_shutdown();

    return 0;
}
