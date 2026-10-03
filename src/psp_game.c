#include <stdio.h>
#include <string.h>

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

#define BUFFER_WIDTH 512
#define TEXTURE_SIZE 512

static unsigned int __attribute__((aligned(16))) display_list[262144];

static unsigned short __attribute__((aligned(16)))
    main_menu_texture[TEXTURE_SIZE * TEXTURE_SIZE];

static int game_initialized = 0;
static int menu_loaded = 0;

typedef struct
{
    float u, v;
    short x, y, z;
} Vertex;

static int load_main_menu(void)
{
    SceUID file;

    file = sceIoOpen(
        "assets/MainMenu.raw",
        PSP_O_RDONLY,
        0777
    );

    if (file < 0)
    {
        return -1;
    }

    int bytes_read = sceIoRead(
        file,
        main_menu_texture,
        sizeof(main_menu_texture)
    );

    sceIoClose(file);

    if (bytes_read != sizeof(main_menu_texture))
    {
        return -1;
    }

    sceKernelDcacheWritebackAll();

    return 0;
}

int psp_game_init(void)
{
    psp_input_init();

    /*
     * Hauptmenü laden.
     * Die Datei ist eine 512x512-RGB565-Textur,
     * deren sichtbarer Bildbereich 480x272 ist.
     */
    if (load_main_menu() == 0)
    {
        menu_loaded = 1;
    }

    /*
     * PSP Graphics Utility initialisieren.
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

    sceGuDepthBuffer(
        (void *)(BUFFER_WIDTH * SCREEN_HEIGHT * 4),
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
    sceGuDisable(GU_LIGHTING);

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuDisplay(GU_TRUE);

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
    {
        return;
    }

    sceGuStart(
        GU_DIRECT,
        display_list
    );

    /*
     * Schwarzer Hintergrund.
     */
    sceGuClearColor(0x00000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    if (menu_loaded)
    {
        Vertex *vertices;

        sceGuEnable(GU_TEXTURE_2D);

        sceGuTexMode(
            GU_PSM_5650,
            0,
            0,
            0
        );

        sceGuTexImage(
            0,
            TEXTURE_SIZE,
            TEXTURE_SIZE,
            TEXTURE_SIZE,
            main_menu_texture
        );

        sceGuTexFunc(
            GU_TFX_REPLACE,
            GU_TCC_RGB
        );

        sceGuTexFilter(
            GU_NEAREST,
            GU_NEAREST
        );

        vertices = (Vertex *)sceGuGetMemory(
            2 * sizeof(Vertex)
        );

        /*
         * Das Bild wird von 0,0 bis 480,272
         * auf den PSP-Bildschirm gezeichnet.
         */
        vertices[0].u = 0.0f;
        vertices[0].v = 0.0f;
        vertices[0].x = 0;
        vertices[0].y = 0;
        vertices[0].z = 0;

        vertices[1].u = 480.0f;
        vertices[1].v = 272.0f;
        vertices[1].x = 480;
        vertices[1].y = 272;
        vertices[1].z = 0;

        sceGuColor(0xFFFFFFFF);

        sceGuDrawArray(
            GU_SPRITES,
            GU_TEXTURE_32BITF |
            GU_VERTEX_16BIT |
            GU_TRANSFORM_2D,
            2,
            0,
            vertices
        );

        sceGuDisable(GU_TEXTURE_2D);
    }

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
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
    menu_loaded = 0;
}
