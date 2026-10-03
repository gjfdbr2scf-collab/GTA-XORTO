#include <pspmoduleinfo.h>
#include <pspthreadman.h>
#include <pspkernel.h>

#include "psp_game.h"

PSP_MODULE_INFO("Yenzangs Trip", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (psp_game_init() != 0)
    {
        sceKernelExitGame();
        return -1;
    }

    while (1)
    {
        psp_game_update();
        psp_game_render();

        /* ungefähr 60 Bilder pro Sekunde */
        sceKernelDelayThread(16666);
    }

    return 0;
}
