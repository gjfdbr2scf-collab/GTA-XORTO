#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

#define BUF_WIDTH 512
#define TEX_WIDTH 512
#define TEX_HEIGHT 512

#define MENU_COUNT 4

/* Das von bin2o erzeugte Bild */
extern unsigned char MainMenu_start[];

/* Menüauswahl */
static int game_initialized = 0;
static int selected_menu = 0;
static int button_lock = 0;
static int current_screen = 0;

/* GU Display List */
static unsigned int __attribute__((aligned(16))) list[262144];

/*
 * Die vier Bereiche entsprechen den vier Optionen
 * im fertigen MainMenu-Bild.
 *
 * X/Y = linke obere Ecke
 * W/H = Größe des anklickbaren Bereichs
 */
typedef struct
{
    int x;
    int y;
    int w;
    int h;
} MenuArea;

static const MenuArea main_menu_areas[MENU_COUNT] =
{
    { 35, 155, 185, 25 },   /* STORY MODE */
    { 35, 183, 235, 25 },   /* FREE OPEN WORLD */
    { 35, 211, 260, 25 },   /* MULTIPLAYER LOCAL */
    { 35, 239, 235, 25 }    /* SYSTEM SETTINGS */
};


/* -------------------------------------------------- */
/* Hintergrundbild                                    */
/* -------------------------------------------------- */

static void draw_background(void)
{
    sceGuEnable(GU_TEXTURE_2D);

    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        0
    );

    /*
     * Das RAW-Bild ist als 512x512-Texture gespeichert.
     * Sichtbar benutzen wir davon 480x272 Pixel.
     */
    sceGuTexImage(
        0,
        TEX_WIDTH,
        TEX_HEIGHT,
        TEX_WIDTH,
        MainMenu_start
    );

    sceGuTexFunc(
        GU_TFX_REPLACE,
        GU_TCC_RGB
    );

    sceGuTexFilter(
        GU_NEAREST,
        GU_NEAREST
    );

    sceGuDisable(GU_DEPTH_TEST);

    sceGuStart(GU_DIRECT, list);

    typedef struct
    {
        float u, v;
        float x, y, z;
    } Vertex;

    Vertex __attribute__((aligned(16))) vertices[2];

    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[0].z = 0.0f;

    vertices[1].u = 480.0f;
    vertices[1].v = 272.0f;
    vertices[1].x = 480.0f;
    vertices[1].y = 272.0f;
    vertices[1].z = 0.0f;

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_32BITF |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        2,
        0,
        vertices
    );

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}


/* -------------------------------------------------- */
/* Auswahl-Overlay                                    */
/* -------------------------------------------------- */

static void draw_selection_overlay(void)
{
    int x;
    int y;
    int w;
    int h;

    if (selected_menu < 0 || selected_menu >= MENU_COUNT)
        return;

    x = main_menu_areas[selected_menu].x;
    y = main_menu_areas[selected_menu].y;
    w = main_menu_areas[selected_menu].w;
    h = main_menu_areas[selected_menu].h;

    /*
     * Dezenter Rahmen über dem vorhandenen Menütext.
     * Das eigentliche Bild bleibt darunter unverändert.
     */

    sceGuDisable(GU_TEXTURE_2D);

    sceGuStart(GU_DIRECT, list);

    sceGuColor(0xFFFFFFFF);

    typedef struct
    {
        float x, y, z;
    } LineVertex;

    LineVertex __attribute__((aligned(16))) v[5];

    v[0].x = (float)x;
    v[0].y = (float)y;
    v[0].z = 0.0f;

    v[1].x = (float)(x + w);
    v[1].y = (float)y;
    v[1].z = 0.0f;

    v[2].x = (float)(x + w);
    v[2].y = (float)(y + h);
    v[2].z = 0.0f;

    v[3].x = (float)x;
    v[3].y = (float)(y + h);
    v[3].z = 0.0f;

    v[4] = v[0];

    sceGuDrawArray(
        GU_LINE_STRIP,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        5,
        0,
        v
    );

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}


/* -------------------------------------------------- */
/* Main Menu                                          */
/* -------------------------------------------------- */

static void draw_main_menu(void)
{
    sceGuStart(GU_DIRECT, list);

    sceGuClearColor(0x00000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    draw_background();
    draw_selection_overlay();
}


/* -------------------------------------------------- */
/* Submenüs                                            */
/* -------------------------------------------------- */

static void draw_submenu(void)
{
    /*
     * Vorläufig bleiben die Unterseiten einfach.
     * Die eigentlichen Bilder können danach ebenfalls
     * als Hintergrund verwendet werden.
     */

    sceGuStart(GU_DIRECT, list);

    sceGuClearColor(0x101018);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}


/* -------------------------------------------------- */
/* Initialisierung                                     */
/* -------------------------------------------------- */

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


/* -------------------------------------------------- */
/* Eingabe                                            */
/* -------------------------------------------------- */

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

    /*
     * Nur Steuerkreuz.
     */

    if (current_screen == 0)
    {
        if (input->up)
        {
            selected_menu--;

            if (selected_menu < 0)
                selected_menu = MENU_COUNT - 1;

            button_lock = 8;
            return;
        }

        if (input->down)
        {
            selected_menu++;

            if (selected_menu >= MENU_COUNT)
                selected_menu = 0;

            button_lock = 8;
            return;
        }

        if (input->cross)
        {
            current_screen = selected_menu + 1;
            button_lock = 12;
            return;
        }
    }
    else
    {
        if (input->circle)
        {
            current_screen = 0;
            button_lock = 12;
            return;
        }
    }
}


/* -------------------------------------------------- */
/* Rendering                                           */
/* -------------------------------------------------- */

void psp_game_render(void)
{
    if (!game_initialized)
        return;

    if (current_screen == 0)
        draw_main_menu();
    else
        draw_submenu();

    sceGuSwapBuffers();

    sceDisplayWaitVblankStart();
}


/* -------------------------------------------------- */
/* Beenden                                             */
/* -------------------------------------------------- */

void psp_game_shutdown(void)
{
    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    psp_input_shutdown();

    game_initialized = 0;
    selected_menu = 0;
    button_lock = 0;
    current_screen = 0;
}
