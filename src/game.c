#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>

#include "psp_game.h"
#include "psp_input.h"

#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  272

#define BUFFER_WIDTH   512
#define TEXTURE_WIDTH  512
#define TEXTURE_HEIGHT 512

#define MENU_COUNT 4

/*
 * Dieses Symbol kommt aus:
 *
 * bin2o -i assets/MainMenu.raw mainmenu.o MainMenu
 *
 * Deshalb muss die Makefile weiterhin genau diese
 * Asset-Regel enthalten.
 */
extern unsigned char MainMenu_start[];

/*
 * Hauptmenü:
 *
 * 0 = STORY MODE
 * 1 = FREE OPEN WORLD
 * 2 = MULTIPLAYER (LOCAL)
 * 3 = SYSTEM SETTINGS
 */
static int selected_menu = 0;
static int current_screen = 0;
static int button_lock = 0;
static int game_initialized = 0;

/*
 * GU Display List
 */
static unsigned int __attribute__((aligned(16)))
    display_list[262144];


/*
 * Auswahlpositionen passend zu deinem
 * 480x272 MainMenu-Bild.
 */
typedef struct
{
    int x;
    int y;
    int width;
    int height;
} MenuArea;

static const MenuArea menu_area[MENU_COUNT] =
{
    { 20, 125, 180, 28 },  /* STORY MODE */
    { 20, 153, 235, 28 },  /* FREE OPEN WORLD */
    { 20, 181, 270, 28 },  /* MULTIPLAYER */
    { 20, 209, 220, 28 }   /* SYSTEM SETTINGS */
};


/*
 * Einfache Vertex-Struktur für 2D.
 */
typedef struct
{
    float u;
    float v;
    float x;
    float y;
    float z;
} TextureVertex;


/*
 * PSP-Grafik initialisieren.
 */
static void init_graphics(void)
{
    sceGuInit();

    sceGuStart(
        GU_DIRECT,
        display_list
    );

    /*
     * 16-Bit-RGB565-Framebuffer
     */
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
    sceGuDisable(GU_CULL_FACE);

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuDisplay(GU_TRUE);
}


/*
 * Das 512x512-RGB565-Bild zeichnen.
 *
 * Das Bild selbst enthält nur 480x272 sichtbare
 * Nutzdaten; deshalb verwenden wir auch nur
 * U=0..480 und V=0..272.
 */
static void draw_main_menu_background(void)
{
    TextureVertex __attribute__((aligned(16))) vertices[2];

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

    /*
     * Die Grafik liegt direkt im EBOOT.
     * Vor der GPU-Nutzung Cache zurückschreiben.
     */
    sceKernelDcacheWritebackAll();

    sceGuEnable(GU_TEXTURE_2D);

    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        GU_FALSE
    );

    sceGuTexImage(
        0,
        TEXTURE_WIDTH,
        TEXTURE_HEIGHT,
        TEXTURE_WIDTH,
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

    sceGuColor(0xFFFFFFFF);

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_32BITF |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        2,
        NULL,
        vertices
    );

    sceGuDisable(GU_TEXTURE_2D);
}


/*
 * Auswahlmarkierung zeichnen.
 *
 * Wir legen keinen großen Balken über das Bild,
 * sondern einen dezenten hellen Rahmen links
 * neben bzw. über dem aktuell ausgewählten Punkt.
 */
static void draw_menu_selection(void)
{
    int x1;
    int y1;
    int x2;
    int y2;

    float thickness = 2.0f;

    typedef struct
    {
        float x;
        float y;
        float z;
    } ColorVertex;

    ColorVertex __attribute__((aligned(16))) v[8];

    if (selected_menu < 0 ||
        selected_menu >= MENU_COUNT)
    {
        return;
    }

    x1 = menu_area[selected_menu].x;
    y1 = menu_area[selected_menu].y;
    x2 = x1 + menu_area[selected_menu].width;
    y2 = y1 + menu_area[selected_menu].height;

    sceGuDisable(GU_TEXTURE_2D);

    /*
     * Weiße Auswahlfarbe.
     */
    sceGuColor(0xFFFFFFFF);

    /*
     * Obere Linie
     */
    v[0].x = (float)x1;
    v[0].y = (float)y1;
    v[0].z = 0.0f;

    v[1].x = (float)x2;
    v[1].y = (float)y1;
    v[1].z = 0.0f;

    /*
     * Untere Linie
     */
    v[2].x = (float)x1;
    v[2].y = (float)y2;
    v[2].z = 0.0f;

    v[3].x = (float)x2;
    v[3].y = (float)y2;
    v[3].z = 0.0f;

    /*
     * Linke Linie
     */
    v[4].x = (float)x1;
    v[4].y = (float)y1;
    v[4].z = 0.0f;

    v[5].x = (float)x1;
    v[5].y = (float)y2;
    v[5].z = 0.0f;

    /*
     * Rechte Linie
     */
    v[6].x = (float)x2;
    v[6].y = (float)y1;
    v[6].z = 0.0f;

    v[7].x = (float)x2;
    v[7].y = (float)y2;
    v[7].z = 0.0f;

    /*
     * Linien zeichnen.
     */
    sceGuDrawArray(
        GU_LINES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        8,
        NULL,
        v
    );

    /*
     * Kleine zusätzliche Markierung links.
     */
    sceGuColor(0xFFFFFFFF);

    {
        ColorVertex arrow[3];

        arrow[0].x = (float)(x1 - 10);
        arrow[0].y = (float)(y1 + 6);
        arrow[0].z = 0.0f;

        arrow[1].x = (float)(x1 - 2);
        arrow[1].y = (float)(y1 + 14);
        arrow[1].z = 0.0f;

        arrow[2].x = (float)(x1 - 10);
        arrow[2].y = (float)(y1 + 22);
        arrow[2].z = 0.0f;

        sceGuDrawArray(
            GU_LINE_STRIP,
            GU_VERTEX_32BITF |
            GU_TRANSFORM_2D,
            3,
            NULL,
            arrow
        );
    }

    (void)thickness;

    sceGuColor(0xFFFFFFFF);
}


/*
 * Hauptmenü rendern.
 */
static void render_main_menu(void)
{
    draw_main_menu_background();
    draw_menu_selection();
}


/*
 * Vorläufige Unterseite.
 *
 * Die echten Bilder für System Settings,
 * Language Selection usw. bauen wir anschließend
 * auf dieselbe Weise ein.
 */
static void render_submenu(void)
{
    sceGuDisable(GU_TEXTURE_2D);

    sceGuColor(0x202020FF);

    typedef struct
    {
        float x;
        float y;
        float z;
    } Vertex;

    Vertex __attribute__((aligned(16))) v[2];

    v[0].x = 0.0f;
    v[0].y = 0.0f;
    v[0].z = 0.0f;

    v[1].x = 480.0f;
    v[1].y = 272.0f;
    v[1].z = 0.0f;

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );
}


/*
 * Spiel initialisieren.
 */
int psp_game_init(void)
{
    psp_input_init();

    init_graphics();

    selected_menu = 0;
    current_screen = 0;
    button_lock = 0;
    game_initialized = 1;

    return 0;
}


/*
 * Eingaben verarbeiten.
 *
 * Nur Steuerkreuz + X/O.
 */
void psp_game_update(void)
{
    const PSPInputState *input;

    if (!game_initialized)
    {
        return;
    }

    psp_input_update();

    input = psp_input_get_state();

    if (button_lock > 0)
    {
        button_lock--;
        return;
    }

    /*
     * Hauptmenü
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

        /*
         * X öffnet den gewählten Menüpunkt.
         */
        if (input->cross)
        {
            current_screen = selected_menu + 1;
            button_lock = 12;
            return;
        }
    }
    else
    {
        /*
         * O zurück zum Hauptmenü.
         */
        if (input->circle)
        {
            current_screen = 0;
            button_lock = 12;
            return;
        }
    }
}


/*
 * Bildschirm zeichnen.
 */
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
     * Bildschirm löschen.
     */
    sceGuClearColor(0x00000000);

    sceGuClear(
        GU_COLOR_BUFFER_BIT
    );

    if (current_screen == 0)
    {
        render_main_menu();
    }
    else
    {
        render_submenu();
    }

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuSwapBuffers();
}


/*
 * Beenden.
 */
void psp_game_shutdown(void)
{
    if (!game_initialized)
    {
        return;
    }

    sceGuDisplay(GU_FALSE);

    sceGuTerm();

    psp_input_shutdown();

    game_initialized = 0;
    selected_menu = 0;
    current_screen = 0;
    button_lock = 0;
}
