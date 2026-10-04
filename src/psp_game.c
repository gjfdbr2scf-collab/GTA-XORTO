#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>

#include "psp_game.h"
#include "psp_input.h"

/* Das von bin2o erzeugte Bild */
extern unsigned char MainMenu_start[];

/* Bildschirm */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

/* Spielstatus */
static int game_initialized = 0;
static int selected_menu = 0;
static int button_lock = 0;
static int current_screen = 0;

/* GU Display List */
static unsigned int __attribute__((aligned(16))) list[262144];

/*
 * Die vier Bereiche entsprechen den vier Optionen
 * im fertigen MainMenu-Bild.
 */
typedef struct
{
    int x;
    int y;
    int w;
    int h;
} MenuArea;

#define MENU_COUNT 4

static const MenuArea main_menu_areas[MENU_COUNT] =
{
    { 35, 155, 185, 25 },   /* STORY MODE */
    { 35, 183, 235, 25 },   /* FREE OPEN WORLD */
    { 35, 211, 260, 25 },   /* MULTIPLAYER LOCAL */
    { 35, 239, 235, 25 }    /* SYSTEM SETTINGS */
};


/* =========================================================
 * Initialisierung
 * ========================================================= */
int psp_game_init(void)
{
    psp_input_init();

    sceGuInit();

    sceGuStart(GU_DIRECT, list);

    sceGuDrawBuffer(
        GU_PSM_5650,
        (void *)0,
        BUF_WIDTH
    );

    sceGuDispBuffer(
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (void *)0x88000,
        BUF_WIDTH
    );

    sceGuDepthBuffer(
        (void *)0x110000,
        BUF_WIDTH
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

    sceGuDepthRange(
        0xc350,
        0x2710
    );

    sceGuScissor(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    game_initialized = 1;
    selected_menu = 0;
    button_lock = 0;
    current_screen = 0;

    return 0;
}


/* =========================================================
 * Update
 * ========================================================= */
void psp_game_update(void)
{
    const PSPInputState *input;

    if (!game_initialized)
        return;

    psp_input_update();
    input = psp_input_get_state();

    if (button_lock > 0)
    {
        button_lock--;
        return;
    }

    (void)input;
}


/* =========================================================
 * Render
 * ========================================================= */
void psp_game_render(void)
{
    typedef struct
    {
        unsigned short u;
        unsigned short v;
        unsigned int color;
        short x;
        short y;
        short z;
    } Vertex;

    Vertex *vertices;

    if (!game_initialized)
        return;

    sceGuStart(GU_DIRECT, list);

    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        0
    );

    sceGuTexImage(
        0,
        512,
        512,
        512,
        MainMenu_start
    );

    sceGuTexFunc(
        GU_TFX_REPLACE,
        GU_TCC_RGB
    );

    sceGuTexFilter(
        GU_LINEAR,
        GU_LINEAR
    );

    sceGuEnable(GU_TEXTURE_2D);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    vertices = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));

    vertices[0].u = 0;
    vertices[0].v = 0;
    vertices[0].color = 0xffffffff;
    vertices[0].x = 0;
    vertices[0].y = 0;
    vertices[0].z = 0;

    vertices[1].u = 480;
    vertices[1].v = 272;
    vertices[1].color = 0xffffffff;
    vertices[1].x = 480;
    vertices[1].y = 272;
    vertices[1].z = 0;

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_16BIT |
        GU_COLOR_8888 |
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        2,
        NULL,
        vertices
    );

    sceGuDisable(GU_TEXTURE_2D);

    sceGuFinish();
    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );
}


/* =========================================================
 * Shutdown
 * ========================================================= */
void psp_game_shutdown(void)
{
    if (!game_initialized)
        return;

    sceGuTerm();

    game_initialized = 0;
}


/* =========================================================
 * Wrapper für game.h
 * ========================================================= */
void game_init(void)
{
    psp_game_init();
}

void game_update(void)
{
    psp_game_update();
}

void game_render(void)
{
    psp_game_render();
}

void game_shutdown(void)
{
    psp_game_shutdown();
}
