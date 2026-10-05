#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psputility.h>
#include <pspiofilemgr.h>
#include <pspvaudio.h>

#include <math.h>
#include <string.h>

#include "psp_game.h"

/*
 * GINSENG STRIP 2002
 *
 * Target presentation:
 * - 480x272 PSP framebuffer
 * - third-person 3D chase camera for gameplay
 * - peaceful city exploration
 * - classic early-2000s console / PS2-style realistic low-poly look
 *
 * Flow:
 * TITLE -> LANGUAGE -> SAVE -> SAVING -> FINISH SAVE -> MAIN MENU
 *
 * Main Menu:
 * STORY MODE
 * FREE OPEN WORLD
 * MULTIPLAYER (LOCAL)
 * SYSTEM SETTINGS
 *
 * Story:
 * cinematic van intro -> two brothers exit (older + youngest child)
 * -> third cousin opens door
 * -> player takes control -> walk together through Peine -> bridge
 *
 * Music:
 * title/language/save/intro = OFF
 * main menu/playable states = ON
 */

extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

/* ------------------------------------------------------------------------- */
/* Display                                                                   */
/* ------------------------------------------------------------------------- */

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f
#define DEG_TO_RAD 0.017453292519943295f

/* ------------------------------------------------------------------------- */
/* Save                                                                      */
/* ------------------------------------------------------------------------- */

#define SAVE_DIR  "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

typedef struct
{
    unsigned int magic;
    int language;
    char username[32]; /* kept for compatibility */
} SaveData;

/* ------------------------------------------------------------------------- */
/* Game states                                                               */
/* ------------------------------------------------------------------------- */

typedef enum
{
    GAME_STATE_TITLE = 0,
    GAME_STATE_LANGUAGE,
    GAME_STATE_SAVE,
    GAME_STATE_SAVING,
    GAME_STATE_SAVE_FINISHED,
    GAME_STATE_MAIN_MENU,
    GAME_STATE_STORY_INTRO,
    GAME_STATE_STORY,
    GAME_STATE_FREE_WORLD,
    GAME_STATE_MULTIPLAYER,
    GAME_STATE_SETTINGS
} GameState;

/* ------------------------------------------------------------------------- */
/* Global state                                                              */
/* ------------------------------------------------------------------------- */

/* 1 MiB GU command list: the textured city uses substantially more
 * per-frame vertex memory than the original small primitive renderer. */
static unsigned int __attribute__((aligned(64)))
    list[0x100000 / 4];

static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;

static int selected_language = 0;
static int selected_menu = 0;
static int settings_selection = 0;

static unsigned int old_buttons = 0;

static SaveData save_data;

/* ------------------------------------------------------------------------- */
/* Fade / save timing                                                        */
/* ------------------------------------------------------------------------- */

static int transition_active = 0;
static int transition_phase = 0;
static int transition_alpha = 0;
static GameState transition_target = GAME_STATE_TITLE;

static int saving_timer = 0;
static int save_finished_timer = 0;

/* ------------------------------------------------------------------------- */
/* Music                                                                     */
/* ------------------------------------------------------------------------- */

#define MUSIC_RATE    22050
#define MUSIC_SAMPLES 1024
#define MUSIC_VOLUME  0x6000

static volatile int music_running = 0;
static volatile unsigned int music_position = 0;
static SceUID music_thread = -1;

static short __attribute__((aligned(64)))
    music_buffer[MUSIC_SAMPLES];

/* ------------------------------------------------------------------------- */
/* Story cinematic                                                           */
/* ------------------------------------------------------------------------- */

static int intro_frame = 0;

/* 0 = van approaches, 1 = parks, 2 = brothers exit, 3 = walk to door,
 * 4 = cousin opens, 5 = handoff to player */
static int intro_phase = 0;

#define INTRO_PHASE_APPROACH 240
#define INTRO_PHASE_PARK      90
#define INTRO_PHASE_EXIT     150
#define INTRO_PHASE_WALK     180
#define INTRO_PHASE_DOOR     120
#define INTRO_TOTAL (INTRO_PHASE_APPROACH + INTRO_PHASE_PARK + \
                     INTRO_PHASE_EXIT + INTRO_PHASE_WALK + INTRO_PHASE_DOOR)

/* Van */
static float intro_van_x = -42.0f;
static float intro_van_z = 8.0f;
static float intro_van_yaw = PI_F * 0.5f;

/* Brothers in intro */
static float intro_bro1_x = 0.0f;
static float intro_bro1_z = 0.0f;
static float intro_bro2_x = 0.0f;
static float intro_bro2_z = 0.0f;

/* Cousin */
static int cousin_door_open = 0;

/* ------------------------------------------------------------------------- */
/* Player / vehicle                                                          */
/* ------------------------------------------------------------------------- */

static float player_x = 0.0f;
static float player_y = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f;
static float camera_pitch = -0.08f;
static float walk_bob = 0.0f;

static float brother_x = -2.0f;
static float brother_y = 0.0f;
static float brother_z = 4.5f;
static float brother_yaw = 0.0f;

static int in_vehicle = 0;
static int current_vehicle = -1;

/* Local second player */
static float player2_x = 4.0f;
static float player2_z = 4.5f;
static float player2_yaw = PI_F;
static float player2_walk_phase = 0.0f;

static int story_step = 0;

/* ------------------------------------------------------------------------- */
/* Camera                                                                    */
/* ------------------------------------------------------------------------- */

static ScePspFVector3 camera_eye;
static ScePspFVector3 camera_center;
static ScePspFVector3 camera_up;

/* ------------------------------------------------------------------------- */
/* City                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct
{
    float x;
    float z;
    float w;
    float d;
    float h;
    unsigned int wall;
    unsigned int roof;
} Building;

static const Building buildings[] =
{
    { -42.0f, -42.0f, 18.0f, 16.0f, 12.0f, 0xffd7c1a8, 0xff8d5b4e },
    { -15.0f, -44.0f, 20.0f, 18.0f, 16.0f, 0xffbfcfd8, 0xff6c747b },
    {  18.0f, -43.0f, 22.0f, 16.0f, 14.0f, 0xffe2c4a7, 0xff8d6251 },
    {  45.0f, -43.0f, 20.0f, 17.0f, 18.0f, 0xffc4c9b8, 0xff77745f },

    { -44.0f, -12.0f, 16.0f, 18.0f, 10.0f, 0xffd5bb96, 0xff9a604d },
    {  43.0f, -10.0f, 19.0f, 18.0f, 15.0f, 0xffb8c8d3, 0xff6d7680 },

    { -44.0f,  17.0f, 18.0f, 19.0f, 18.0f, 0xffd9b09f, 0xff79584f },
    {  44.0f,  17.0f, 18.0f, 19.0f, 13.0f, 0xffd6cfaa, 0xff7a6749 },

    { -44.0f,  46.0f, 19.0f, 17.0f, 14.0f, 0xffb9cbd7, 0xff6f747d },
    { -15.0f,  44.0f, 20.0f, 18.0f, 18.0f, 0xffe0c5a9, 0xff8d5c4e },
    {  16.0f,  44.0f, 19.0f, 18.0f, 11.0f, 0xffd4d9bd, 0xff6d765e },
    {  45.0f,  44.0f, 18.0f, 18.0f, 16.0f, 0xffd7b7a1, 0xff7e5149 },

    { -75.0f, -72.0f, 20.0f, 18.0f, 10.0f, 0xffceb18d, 0xff8d5e47 },
    { -47.0f, -72.0f, 19.0f, 18.0f, 13.0f, 0xffd4c3ad, 0xff7a6e59 },
    { -18.0f, -72.0f, 22.0f, 19.0f, 16.0f, 0xffbac9d2, 0xff6a7480 },
    {  16.0f, -72.0f, 21.0f, 18.0f, 12.0f, 0xffd7c6aa, 0xff8b614f },
    {  46.0f, -72.0f, 19.0f, 18.0f, 18.0f, 0xffc9d3bd, 0xff707961 },

    { -75.0f,  72.0f, 22.0f, 18.0f, 12.0f, 0xffd7c3a9, 0xff8b6253 },
    { -45.0f,  72.0f, 20.0f, 18.0f, 16.0f, 0xffbacbd3, 0xff6f7880 },
    { -15.0f,  72.0f, 19.0f, 17.0f, 11.0f, 0xffd2d1ba, 0xff7e735a },
    {  15.0f,  72.0f, 22.0f, 18.0f, 17.0f, 0xffe0c0a5, 0xff8a584d },
    {  46.0f,  72.0f, 20.0f, 18.0f, 13.0f, 0xffbdcad8, 0xff6b7482 },

    { -75.0f,  10.0f, 17.0f, 20.0f, 17.0f, 0xffd3baa3, 0xff80584b },
    {  75.0f,  10.0f, 17.0f, 20.0f, 20.0f, 0xffc8d2bc, 0xff647062 },

    { -75.0f,  40.0f, 17.0f, 18.0f, 10.0f, 0xffc6c8cf, 0xff6f6b73 },
    {  75.0f,  42.0f, 17.0f, 18.0f, 15.0f, 0xffdcb693, 0xff855d4f }
};

#define BUILDING_COUNT ((int)(sizeof(buildings) / sizeof(buildings[0])))

typedef struct
{
    float x;
    float z;
    float yaw;
    float speed;
    unsigned int body;
} CityCar;

static CityCar cars[] =
{
    { -24.0f,  0.0f,  0.0f,  0.0f, 0xffc23c35 },
    {  24.0f,  0.0f, PI_F,    0.0f, 0xffd6b640 },
    {  0.0f,  30.0f, PI_F*0.5f, 0.0f, 0xff507fb6 },
    {  0.0f,-30.0f, PI_F*1.5f, 0.0f, 0xffdfe1e2 },

    /* Blue family van used in story intro. */
    { -42.0f,  8.0f, PI_F*0.5f, 0.0f, 0xff2d67b7 }
};

#define CAR_COUNT ((int)(sizeof(cars) / sizeof(cars[0])))
#define STORY_VAN_INDEX 4

typedef struct
{
    float x;
    float z;
    float phase;
    unsigned int shirt;
} Pedestrian;

static Pedestrian pedestrians[] =
{
    { -10.0f,  9.0f, 0.0f, 0xff496f9c },
    {  10.0f, -8.0f, 1.0f, 0xffa36f43 },
    { -9.0f, -9.0f, 2.0f, 0xff6e985b },
    {  11.0f, 11.0f, 3.0f, 0xff9d5d85 },
    {  34.0f, -4.0f, 4.0f, 0xff8a6e50 },
    { -34.0f,  5.0f, 5.0f, 0xff4e8f83 }
};

#define PEDESTRIAN_COUNT ((int)(sizeof(pedestrians) / sizeof(pedestrians[0])))

/* ------------------------------------------------------------------------- */
/* Textured world materials and mesh data                                    */
/* ------------------------------------------------------------------------- */

/*
 * The world renderer is built from textured triangles/quads and low-poly
 * cylinders/spheres. No cube primitive is used for the world renderer.
 */

typedef struct
{
    float x;
    float y;
    float z;
} Vertex3D;

typedef struct
{
    unsigned short u;
    unsigned short v;
    unsigned int color;
    float x;
    float y;
    float z;
} WorldVertex;

#define WORLD_TEX_W 128
#define WORLD_TEX_H 128
#define WORLD_TEX_PIXELS (WORLD_TEX_W * WORLD_TEX_H)

enum
{
    TEX_ASPHALT = 0,
    TEX_SIDEWALK,
    TEX_GRASS,
    TEX_BRICK,
    TEX_PLASTER,
    TEX_ROOF,
    TEX_GLASS,
    TEX_WOOD,
    TEX_METAL,
    TEX_CAR_BODY,
    TEX_MARKING,
    TEX_COUNT
};

static unsigned short __attribute__((aligned(64)))
    world_textures[TEX_COUNT][WORLD_TEX_PIXELS];

static int world_textures_ready = 0;
/* Reuse the GU texture setup while adjacent geometry uses the same material. */
static int world_texture_bound = -1;

static unsigned short rgb565_from_rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return (unsigned short)(
        ((r >> 3) << 11) |
        ((g >> 2) << 5) |
        (b >> 3)
    );
}

/* sceGuColor uses 0xAABBGGRR byte order. */
static unsigned int gu_color_rgba(int r, int g, int b, int a)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    if (a < 0) a = 0;
    if (a > 255) a = 255;

    return ((unsigned int)a << 24) |
           ((unsigned int)b << 16) |
           ((unsigned int)g << 8) |
           (unsigned int)r;
}

/* Convert the project's conventional 0xAARRGGBB constants to PSP GU order. */
static unsigned int gu_color_from_argb(unsigned int argb)
{
    unsigned int a = argb & 0xff000000;
    unsigned int r = (argb >> 16) & 0xff;
    unsigned int g = (argb >> 8) & 0xff;
    unsigned int b = argb & 0xff;

    return a | (b << 16) | (g << 8) | r;
}

static unsigned int shade_gu_color(unsigned int color, int percent)
{
    int r = (int)(color & 0xff);
    int g = (int)((color >> 8) & 0xff);
    int b = (int)((color >> 16) & 0xff);
    unsigned int a = color & 0xff000000;

    r = (r * percent) / 100;
    g = (g * percent) / 100;
    b = (b * percent) / 100;

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    return a | ((unsigned int)b << 16) |
           ((unsigned int)g << 8) | (unsigned int)r;
}

static void make_world_textures(void)
{
    int x;
    int y;

    if (world_textures_ready)
        return;

    /* Fine asphalt aggregate with less obvious repeating noise. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            unsigned int noise =
                (unsigned int)x * 374761393u ^
                (unsigned int)y * 668265263u;
            int n;
            int c;

            noise = (noise ^ (noise >> 13)) * 1274126177u;
            n = (int)((noise >> 27) & 15u);
            c = 34 + n;
            world_textures[TEX_ASPHALT][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(c, c + 1, c + 2);
        }
    }

    /* Concrete paving slabs. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int joint = ((x & 31) == 0 || (y & 31) == 0);
            int c = joint ? 108 : 151 + ((x + y) & 7);
            world_textures[TEX_SIDEWALK][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(c, c, c - 2);
        }
    }

    /* Mottled lawn. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int n = (x * 13 + y * 7 + x * y) & 15;
            world_textures[TEX_GRASS][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(42 + (n >> 2), 96 + n * 3, 45 + (n >> 1));
        }
    }

    /* Brick masonry. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int row = y / 16;
            int offset = (row & 1) ? 12 : 0;
            int mortar = (((x + offset) % 32) == 0) || ((y % 16) == 0);
            int n = (x * 5 + y * 9) & 7;

            world_textures[TEX_BRICK][y * WORLD_TEX_W + x] = mortar
                ? rgb565_from_rgb(124, 103, 91)
                : rgb565_from_rgb(152 + n * 3, 78 + n * 2, 60 + n);
        }
    }

    /* Stucco/plaster. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int n = (x * 11 + y * 19 + x * y) & 9;
            world_textures[TEX_PLASTER][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(188 + n, 172 + n, 146 + n);
        }
    }

    /* Roof shingles. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int shingle = ((x + ((y / 16) & 1) * 10) % 24) < 2;
            int line = (y % 16) == 0;
            int c = (shingle || line) ? 67 : 94 + ((x + y) & 9);
            world_textures[TEX_ROOF][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(c + 20, c + 4, c + 2);
        }
    }

    /* Glass panes. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int frame = ((x % 32) <= 2 || (y % 32) <= 2);
            int c = frame ? 55 : 82 + ((x * 3 + y) & 15);
            world_textures[TEX_GLASS][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(45, c, 95 + ((x + y) & 15));
        }
    }

    /* Wood grain. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int stripe = (x + y * 3) & 31;
            world_textures[TEX_WOOD][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(95 + stripe * 2, 59 + stripe, 37);
        }
    }

    /* Brushed metal. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int c = 104 + ((x * 3 + y * 5) & 15);
            world_textures[TEX_METAL][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(c, c + 5, c + 8);
        }
    }

    /* Neutral paint detail texture, tinted per object by GU color. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int c = 110 + ((x + y) & 15);
            world_textures[TEX_CAR_BODY][y * WORLD_TEX_W + x] =
                rgb565_from_rgb(c + 18, c + 5, c);
        }
    }

    /* Road line texture. */
    for (y = 0; y < WORLD_TEX_H; ++y)
    {
        for (x = 0; x < WORLD_TEX_W; ++x)
        {
            int stripe = ((x > 7 && x < 57) && (y > 24 && y < 40));
            world_textures[TEX_MARKING][y * WORLD_TEX_W + x] = stripe
                ? rgb565_from_rgb(238, 229, 190)
                : rgb565_from_rgb(110, 110, 105);
        }
    }

    /* The GE reads textures through DMA; make the CPU-written texture data
     * visible before the first textured draw. */
    sceKernelDcacheWritebackRange(
        world_textures,
        sizeof(world_textures)
    );

    world_textures_ready = 1;
}

static void bind_world_texture(int texture_id)
{
    if (texture_id < 0 || texture_id >= TEX_COUNT)
        texture_id = TEX_PLASTER;

    if (texture_id == world_texture_bound)
    {
        sceGuEnable(GU_TEXTURE_2D);
        return;
    }

    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        GU_FALSE
    );

    sceGuTexImage(
        0,
        WORLD_TEX_W,
        WORLD_TEX_H,
        WORLD_TEX_W,
        world_textures[texture_id]
    );

    sceGuTexFunc(
        GU_TFX_MODULATE,
        GU_TCC_RGB
    );

    sceGuTexFilter(
        GU_LINEAR,
        GU_LINEAR
    );

    sceGuEnable(
        GU_TEXTURE_2D
    );

    world_texture_bound = texture_id;
}

static void draw_textured_triangles(
    const WorldVertex *vertices,
    int count,
    int texture_id
)
{
    bind_world_texture(texture_id);

    sceGuDrawArray(
        GU_TRIANGLES,
        GU_TEXTURE_16BIT |
        GU_COLOR_8888 |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        count,
        NULL,
        vertices
    );

}

static void draw_textured_quad(
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    float x2, float y2, float z2,
    float x3, float y3, float z3,
    int texture_id,
    unsigned int color
)
{
    WorldVertex *v =
        (WorldVertex *)sceGuGetMemory(
            6 * sizeof(WorldVertex)
        );

    v[0].u = 0;               v[0].v = 0;
    v[0].color = color;       v[0].x = x0; v[0].y = y0; v[0].z = z0;
    v[1].u = WORLD_TEX_W;     v[1].v = 0;
    v[1].color = color;       v[1].x = x1; v[1].y = y1; v[1].z = z1;
    v[2].u = WORLD_TEX_W;     v[2].v = WORLD_TEX_H;
    v[2].color = color;       v[2].x = x2; v[2].y = y2; v[2].z = z2;
    v[3] = v[0];
    v[4] = v[2];
    v[5].u = 0;               v[5].v = WORLD_TEX_H;
    v[5].color = color;       v[5].x = x3; v[5].y = y3; v[5].z = z3;

    draw_textured_triangles(v, 6, texture_id);
}

static void draw_box_textured(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
    int texture_id,
    unsigned int color
)
{
    float x0 = x - sx * 0.5f;
    float x1 = x + sx * 0.5f;
    float y0 = y - sy * 0.5f;
    float y1 = y + sy * 0.5f;
    float z0 = z - sz * 0.5f;
    float z1 = z + sz * 0.5f;

    draw_textured_quad(x0,y0,z0, x1,y0,z0, x1,y1,z0, x0,y1,z0, texture_id,shade_gu_color(color,92));
    draw_textured_quad(x1,y0,z1, x0,y0,z1, x0,y1,z1, x1,y1,z1, texture_id,shade_gu_color(color,82));
    draw_textured_quad(x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1, texture_id,shade_gu_color(color,88));
    draw_textured_quad(x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0, texture_id,shade_gu_color(color,76));
    draw_textured_quad(x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1, texture_id,shade_gu_color(color,112));
    draw_textured_quad(x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0, texture_id,shade_gu_color(color,70));
}

static void draw_sloped_roof(
    float x,
    float y,
    float z,
    float sx,
    float sz,
    int texture_id,
    unsigned int color
)
{
    float x0 = x - sx * 0.5f;
    float x1 = x + sx * 0.5f;
    float z0 = z - sz * 0.5f;
    float z1 = z + sz * 0.5f;
    float ridge = y + 1.55f;
    WorldVertex *v =
        (WorldVertex *)sceGuGetMemory(
            12 * sizeof(WorldVertex)
        );

    v[0].u=0; v[0].v=0; v[0].color=color; v[0].x=x0; v[0].y=y; v[0].z=z0;
    v[1].u=WORLD_TEX_W; v[1].v=0; v[1].color=color; v[1].x=x1; v[1].y=y; v[1].z=z0;
    v[2].u=WORLD_TEX_W/2; v[2].v=WORLD_TEX_H; v[2].color=color; v[2].x=x; v[2].y=ridge; v[2].z=z0;
    v[3].u=0; v[3].v=0; v[3].color=color; v[3].x=x1; v[3].y=y; v[3].z=z1;
    v[4].u=WORLD_TEX_W; v[4].v=0; v[4].color=color; v[4].x=x0; v[4].y=y; v[4].z=z1;
    v[5].u=WORLD_TEX_W/2; v[5].v=WORLD_TEX_H; v[5].color=color; v[5].x=x; v[5].y=ridge; v[5].z=z1;
    v[6].u=0; v[6].v=0; v[6].color=color; v[6].x=x0; v[6].y=y; v[6].z=z1;
    v[7].u=WORLD_TEX_W; v[7].v=0; v[7].color=color; v[7].x=x0; v[7].y=y; v[7].z=z0;
    v[8].u=WORLD_TEX_W/2; v[8].v=WORLD_TEX_H; v[8].color=color; v[8].x=x; v[8].y=ridge; v[8].z=z;
    v[9].u=0; v[9].v=0; v[9].color=color; v[9].x=x1; v[9].y=y; v[9].z=z0;
    v[10].u=WORLD_TEX_W; v[10].v=0; v[10].color=color; v[10].x=x1; v[10].y=y; v[10].z=z1;
    v[11].u=WORLD_TEX_W/2; v[11].v=WORLD_TEX_H; v[11].color=color; v[11].x=x; v[11].y=ridge; v[11].z=z;

    draw_textured_triangles(v, 12, texture_id);
}

static void draw_cylinder_y(
    float x,
    float y,
    float z,
    float radius,
    float height,
    int texture_id,
    unsigned int color
)
{
    const int sides = 8;
    int i;
    WorldVertex *v =
        (WorldVertex *)sceGuGetMemory(
            sides * 6 * sizeof(WorldVertex)
        );

    for (i = 0; i < sides; ++i)
    {
        float a0 = ((float)i / (float)sides) * 2.0f * PI_F;
        float a1 = ((float)(i + 1) / (float)sides) * 2.0f * PI_F;
        float c0 = cosf(a0) * radius;
        float s0 = sinf(a0) * radius;
        float c1 = cosf(a1) * radius;
        float s1 = sinf(a1) * radius;
        int n = i * 6;

        v[n+0].u=0; v[n+0].v=0; v[n+0].color=color; v[n+0].x=x+c0; v[n+0].y=y;          v[n+0].z=z+s0;
        v[n+1].u=WORLD_TEX_W; v[n+1].v=0; v[n+1].color=color; v[n+1].x=x+c1; v[n+1].y=y;          v[n+1].z=z+s1;
        v[n+2].u=WORLD_TEX_W; v[n+2].v=WORLD_TEX_H; v[n+2].color=color; v[n+2].x=x+c1; v[n+2].y=y+height; v[n+2].z=z+s1;
        v[n+3] = v[n+0];
        v[n+4] = v[n+2];
        v[n+5].u=0; v[n+5].v=WORLD_TEX_H; v[n+5].color=color; v[n+5].x=x+c0; v[n+5].y=y+height; v[n+5].z=z+s0;
    }

    draw_textured_triangles(v, sides * 6, texture_id);
}

static void draw_cylinder_z(
    float x,
    float y,
    float z,
    float radius,
    float depth,
    int texture_id,
    unsigned int color
)
{
    const int sides = 8;
    int i;
    WorldVertex *v =
        (WorldVertex *)sceGuGetMemory(
            sides * 6 * sizeof(WorldVertex)
        );

    for (i = 0; i < sides; ++i)
    {
        float a0 = ((float)i / (float)sides) * 2.0f * PI_F;
        float a1 = ((float)(i + 1) / (float)sides) * 2.0f * PI_F;
        float c0 = cosf(a0) * radius;
        float s0 = sinf(a0) * radius;
        float c1 = cosf(a1) * radius;
        float s1 = sinf(a1) * radius;
        float z0 = z - depth * 0.5f;
        float z1 = z + depth * 0.5f;
        int n = i * 6;

        v[n+0].u=0; v[n+0].v=0; v[n+0].color=color; v[n+0].x=x+c0; v[n+0].y=y+s0; v[n+0].z=z0;
        v[n+1].u=WORLD_TEX_W; v[n+1].v=0; v[n+1].color=color; v[n+1].x=x+c1; v[n+1].y=y+s1; v[n+1].z=z0;
        v[n+2].u=WORLD_TEX_W; v[n+2].v=WORLD_TEX_H; v[n+2].color=color; v[n+2].x=x+c1; v[n+2].y=y+s1; v[n+2].z=z1;
        v[n+3] = v[n+0];
        v[n+4] = v[n+2];
        v[n+5].u=0; v[n+5].v=WORLD_TEX_H; v[n+5].color=color; v[n+5].x=x+c0; v[n+5].y=y+s0; v[n+5].z=z1;
    }

    draw_textured_triangles(v, sides * 6, texture_id);
}

static void draw_uv_sphere(
    float x,
    float y,
    float z,
    float radius,
    int texture_id,
    unsigned int color
)
{
    const int rings = 4;
    const int sides = 8;
    int r;

    for (r = 0; r < rings; ++r)
    {
        float p0 = -0.5f * PI_F + ((float)r / (float)rings) * PI_F;
        float p1 = -0.5f * PI_F + ((float)(r + 1) / (float)rings) * PI_F;
        int s;
        WorldVertex *v =
            (WorldVertex *)sceGuGetMemory(
                sides * 6 * sizeof(WorldVertex)
            );

        for (s = 0; s < sides; ++s)
        {
            float a0 = ((float)s / (float)sides) * 2.0f * PI_F;
            float a1 = ((float)(s + 1) / (float)sides) * 2.0f * PI_F;
            float cp0 = cosf(p0);
            float sp0 = sinf(p0);
            float cp1 = cosf(p1);
            float sp1 = sinf(p1);
            int n = s * 6;

            v[n+0].u=0; v[n+0].v=0; v[n+0].color=color;
            v[n+0].x=x+cosf(a0)*cp0*radius; v[n+0].y=y+sp0*radius; v[n+0].z=z+sinf(a0)*cp0*radius;
            v[n+1].u=WORLD_TEX_W; v[n+1].v=0; v[n+1].color=color;
            v[n+1].x=x+cosf(a1)*cp0*radius; v[n+1].y=y+sp0*radius; v[n+1].z=z+sinf(a1)*cp0*radius;
            v[n+2].u=WORLD_TEX_W; v[n+2].v=WORLD_TEX_H; v[n+2].color=color;
            v[n+2].x=x+cosf(a1)*cp1*radius; v[n+2].y=y+sp1*radius; v[n+2].z=z+sinf(a1)*cp1*radius;
            v[n+3]=v[n+0];
            v[n+4]=v[n+2];
            v[n+5].u=0; v[n+5].v=WORLD_TEX_H; v[n+5].color=color;
            v[n+5].x=x+cosf(a0)*cp1*radius; v[n+5].y=y+sp1*radius; v[n+5].z=z+sinf(a0)*cp1*radius;
        }

        draw_textured_triangles(v, sides * 6, texture_id);
    }
}

static void draw_ellipsoid(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
    int texture_id,
    unsigned int color
)
{
    ScePspFVector3 pos;
    ScePspFVector3 scale;

    pos.x = x;
    pos.y = y;
    pos.z = z;
    scale.x = sx;
    scale.y = sy;
    scale.z = sz;

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumScale(&scale);
    draw_uv_sphere(0.0f, 0.0f, 0.0f, 1.0f, texture_id, color);
    sceGumPopMatrix();
}

typedef struct
{
    unsigned int color;
    float x;
    float y;
    float z;
} ShadowVertex;

static void draw_shadow_blob(float x, float z, float sx, float sz)
{
    const int sides = 8;
    int i;
    ShadowVertex *v =
        (ShadowVertex *)sceGuGetMemory(
            sides * 3 * sizeof(ShadowVertex)
        );

    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(
        GU_ADD,
        GU_SRC_ALPHA,
        GU_ONE_MINUS_SRC_ALPHA,
        0,
        0
    );

    for (i = 0; i < sides; ++i)
    {
        float a0 = ((float)i / (float)sides) * 2.0f * PI_F;
        float a1 = ((float)(i + 1) / (float)sides) * 2.0f * PI_F;
        int n = i * 3;

        v[n+0].color=0x60000000; v[n+0].x=x;                  v[n+0].y=0.02f; v[n+0].z=z;
        v[n+1].color=0x60000000; v[n+1].x=x+cosf(a0)*sx;   v[n+1].y=0.02f; v[n+1].z=z+sinf(a0)*sz;
        v[n+2].color=0x60000000; v[n+2].x=x+cosf(a1)*sx;   v[n+2].y=0.02f; v[n+2].z=z+sinf(a1)*sz;
    }

    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        sides * 3,
        NULL,
        v
    );

    sceGuDisable(GU_BLEND);
}

static void draw_window_z(
    float x,
    float y,
    float z,
    float w,
    float h,
    int facing_sign,
    unsigned int glass_color
)
{
    float frame_w = w + 0.12f;
    float frame_h = h + 0.14f;
    unsigned int frame_color = gu_color_rgba(196, 188, 168, 255);

    draw_textured_quad(
        x-frame_w*0.5f,y-0.07f,z,
        x+frame_w*0.5f,y-0.07f,z,
        x+frame_w*0.5f,y+frame_h-0.07f,z,
        x-frame_w*0.5f,y+frame_h-0.07f,z,
        TEX_METAL,
        frame_color
    );
    draw_textured_quad(
        x-w*0.5f,y,z+facing_sign*0.026f,
        x+w*0.5f,y,z+facing_sign*0.026f,
        x+w*0.5f,y+h,z+facing_sign*0.026f,
        x-w*0.5f,y+h,z+facing_sign*0.026f,
        TEX_GLASS,
        glass_color
    );
}

static void draw_window_x(
    float x,
    float y,
    float z,
    float w,
    float h,
    int facing_sign,
    unsigned int glass_color
)
{
    float frame_w = w + 0.12f;
    float frame_h = h + 0.14f;
    unsigned int frame_color = gu_color_rgba(196, 188, 168, 255);

    draw_textured_quad(
        x,y-0.07f,z-frame_w*0.5f,
        x,y-0.07f,z+frame_w*0.5f,
        x,y+frame_h-0.07f,z+frame_w*0.5f,
        x,y+frame_h-0.07f,z-frame_w*0.5f,
        TEX_METAL,
        frame_color
    );
    draw_textured_quad(
        x+facing_sign*0.026f,y,z-w*0.5f,
        x+facing_sign*0.026f,y,z+w*0.5f,
        x+facing_sign*0.026f,y+h,z+w*0.5f,
        x+facing_sign*0.026f,y+h,z-w*0.5f,
        TEX_GLASS,
        glass_color
    );
}

static void draw_building(const Building *b)
{
    int material = (((int)(b->x + b->z)) & 1) ? TEX_BRICK : TEX_PLASTER;
    unsigned int wall_color = gu_color_from_argb(b->wall);
    unsigned int roof_color = gu_color_from_argb(b->roof);
    float front_z = b->z - b->d * 0.5f - 0.045f;
    float back_z = b->z + b->d * 0.5f + 0.045f;
    float window_h = b->h > 14.0f ? 2.2f : 1.8f;
    float window_y = b->h > 15.0f ? 4.2f : 3.6f;
    int rows = b->h > 15.0f ? 2 : 1;
    int cols = b->w > 19.0f ? 3 : 2;
    int side_cols = b->d > 17.0f ? 2 : 1;
    int r;
    int c;

    draw_box_textured(
        b->x,
        b->h * 0.5f,
        b->z,
        b->w,
        b->h,
        b->d,
        material,
        wall_color
    );

    draw_sloped_roof(
        b->x,
        b->h,
        b->z,
        b->w * 1.08f,
        b->d * 1.08f,
        TEX_ROOF,
        roof_color
    );

    for (r = 0; r < rows; ++r)
    {
        for (c = 0; c < cols; ++c)
        {
            float span = b->w * 0.68f;
            float wx =
                b->x - span * 0.5f +
                span * ((float)c + 0.5f) / (float)cols;
            float wy = window_y + (float)r * 4.5f;
            float ww = b->w * 0.14f;
            unsigned int glass_color =
                ((r + c + (int)b->x) % 4 == 0)
                    ? gu_color_rgba(255, 177, 116, 255)
                    : gu_color_rgba(124, 178, 203, 255);

            draw_window_z(wx, wy, front_z, ww, window_h, -1, glass_color);
            draw_window_z(wx, wy, back_z, ww, window_h, 1, glass_color);
        }
    }

    /* Continue the window rhythm around the side walls, not only the front. */
    for (r = 0; r < rows; ++r)
    {
        for (c = 0; c < side_cols; ++c)
        {
            float span = b->d * 0.62f;
            float wz = b->z - span * 0.5f +
                span * ((float)c + 0.5f) / (float)side_cols;
            float wy = window_y + (float)r * 4.5f;
            float ww = b->d * 0.17f;
            float left_x = b->x - b->w * 0.5f - 0.045f;
            float right_x = b->x + b->w * 0.5f + 0.045f;
            unsigned int glass_color =
                ((r + c + (int)b->z) % 4 == 0)
                    ? gu_color_rgba(255, 177, 116, 255)
                    : gu_color_rgba(124, 178, 203, 255);

            draw_window_x(left_x, wy, wz, ww, window_h, -1, glass_color);
            draw_window_x(right_x, wy, wz, ww, window_h, 1, glass_color);
        }
    }

    /* Door, step and small metal rail. */
    draw_textured_quad(
        b->x-b->w*0.10f,0.12f,front_z,
        b->x+b->w*0.10f,0.12f,front_z,
        b->x+b->w*0.10f,b->h*0.38f,front_z,
        b->x-b->w*0.10f,b->h*0.38f,front_z,
        TEX_WOOD,
        gu_color_rgba(151, 94, 54, 255)
    );

    draw_box_textured(
        b->x,
        0.08f,
        front_z - 0.10f,
        b->w * 0.28f,
        0.16f,
        0.38f,
        TEX_METAL,
        0xffffffff
    );

    /* Original striped shop awnings give the low-rise blocks more character. */
    if (b->h <= 14.0f)
    {
        float awning_w = b->w * 0.56f;
        int stripe;

        for (stripe = 0; stripe < 6; ++stripe)
        {
            float x0 = b->x - awning_w * 0.5f +
                awning_w * (float)stripe / 6.0f;
            float x1 = b->x - awning_w * 0.5f +
                awning_w * (float)(stripe + 1) / 6.0f;
            unsigned int stripe_color = (stripe & 1)
                ? gu_color_rgba(239, 214, 174, 255)
                : gu_color_rgba(186, 75, 68, 255);

            draw_textured_quad(
                x0,3.18f,front_z-0.03f,
                x1,3.18f,front_z-0.03f,
                x1,2.90f,front_z-0.64f,
                x0,2.90f,front_z-0.64f,
                TEX_WOOD,
                stripe_color
            );
        }
    }

    if (b->h > 15.0f)
    {
        draw_box_textured(
            b->x,
            b->h * 0.52f,
            front_z - 0.13f,
            b->w * 0.44f,
            0.14f,
            0.34f,
            TEX_METAL,
            0xffffffff
        );
    }
}

static void draw_palm_frond(
    float x,
    float y,
    float z,
    float angle,
    float length,
    float width,
    unsigned int color
)
{
    const int segments = 5;
    int i;
    float dx = cosf(angle);
    float dz = sinf(angle);
    float sx = -dz;
    float sz = dx;
    WorldVertex *v = (WorldVertex *)sceGuGetMemory(
        segments * 6 * sizeof(WorldVertex)
    );

    for (i = 0; i < segments; ++i)
    {
        float t0 = (float)i / (float)segments;
        float t1 = (float)(i + 1) / (float)segments;
        float w0 = width * (1.0f - t0 * 0.82f);
        float w1 = width * (1.0f - t1 * 0.82f);
        float x0 = x + dx * length * t0;
        float z0 = z + dz * length * t0;
        float x1 = x + dx * length * t1;
        float z1 = z + dz * length * t1;
        float y0 = y + sinf(t0 * PI_F) * 0.20f - t0 * t0 * 0.48f;
        float y1 = y + sinf(t1 * PI_F) * 0.20f - t1 * t1 * 0.48f;
        unsigned int c0 = shade_gu_color(color, 100 - i * 3);
        unsigned int c1 = shade_gu_color(color, 94 - i * 3);
        int n = i * 6;

        v[n+0].u=0;             v[n+0].v=0;                v[n+0].color=c0;
        v[n+0].x=x0+sx*w0;      v[n+0].y=y0;               v[n+0].z=z0+sz*w0;
        v[n+1].u=WORLD_TEX_W;   v[n+1].v=0;                v[n+1].color=c0;
        v[n+1].x=x0-sx*w0;      v[n+1].y=y0;               v[n+1].z=z0-sz*w0;
        v[n+2].u=WORLD_TEX_W;   v[n+2].v=WORLD_TEX_H;      v[n+2].color=c1;
        v[n+2].x=x1-sx*w1;      v[n+2].y=y1;               v[n+2].z=z1-sz*w1;
        v[n+3]=v[n+0];
        v[n+4]=v[n+2];
        v[n+5].u=0;             v[n+5].v=WORLD_TEX_H;      v[n+5].color=c1;
        v[n+5].x=x1+sx*w1;      v[n+5].y=y1;               v[n+5].z=z1+sz*w1;
    }

    draw_textured_triangles(v, segments * 6, TEX_GRASS);
}

static void draw_palm_tree(float x, float z, float scale_factor)
{
    const int trunk_segments = 4;
    const int frond_count = 8;
    float trunk_h = 4.6f * scale_factor;
    float lean_x = 0.52f * scale_factor;
    float lean_z = 0.18f * scale_factor;
    float crown_x = x + lean_x;
    float crown_z = z + lean_z;
    int i;

    draw_shadow_blob(x, z, 1.05f * scale_factor, 0.80f * scale_factor);

    for (i = 0; i < trunk_segments; ++i)
    {
        float t = (float)i / (float)trunk_segments;
        float cx = x + lean_x * (t + 0.125f);
        float cz = z + lean_z * (t + 0.125f);
        float radius = 0.22f * scale_factor * (1.0f - t * 0.24f);

        draw_cylinder_y(
            cx,
            trunk_h * t,
            cz,
            radius,
            trunk_h / (float)trunk_segments + 0.04f,
            TEX_WOOD,
            gu_color_rgba(150, 107, 64, 255)
        );
    }

    for (i = 0; i < frond_count; ++i)
    {
        float angle = (2.0f * PI_F * (float)i) / (float)frond_count;
        float length = (2.7f + (float)(i & 1) * 0.35f) * scale_factor;

        draw_palm_frond(
            crown_x,
            trunk_h,
            crown_z,
            angle,
            length,
            0.42f * scale_factor,
            gu_color_rgba(59 + (i & 1) * 8, 113 + (i & 1) * 10, 55, 255)
        );
    }
}

static void draw_human_pose(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt,
    float phase
)
{
    ScePspFVector3 pos;
    float gait = sinf(phase) * 0.30f;
    float arm_gait = -gait * 0.72f;

    shirt = gu_color_from_argb(shirt);

    draw_shadow_blob(x, z, 0.34f, 0.24f);

    pos.x = x;
    pos.y = y;
    pos.z = z;

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);

    /* Legs pivot at the hips so walking has a visible alternating stride. */
    sceGumPushMatrix();
    pos.x = -0.115f;
    pos.y = 0.78f;
    pos.z = 0.0f;
    sceGumTranslate(&pos);
    sceGumRotateX(gait);
    draw_cylinder_y(0.0f, -0.76f, 0.0f, 0.085f, 0.76f, TEX_PLASTER, gu_color_from_argb(0xff283044));
    draw_box_textured(0.0f, -0.72f, -0.075f, 0.17f, 0.12f, 0.28f, TEX_METAL, gu_color_from_argb(0xff202226));
    sceGumPopMatrix();

    sceGumPushMatrix();
    pos.x = 0.115f;
    pos.y = 0.78f;
    pos.z = 0.0f;
    sceGumTranslate(&pos);
    sceGumRotateX(-gait);
    draw_cylinder_y(0.0f, -0.76f, 0.0f, 0.085f, 0.76f, TEX_PLASTER, gu_color_from_argb(0xff283044));
    draw_box_textured(0.0f, -0.72f, -0.075f, 0.17f, 0.12f, 0.28f, TEX_METAL, gu_color_from_argb(0xff202226));
    sceGumPopMatrix();

    /* Rounded clothing forms keep people from reading as stacked boxes. */
    draw_ellipsoid(
        0.0f, 0.82f, 0.0f,
        0.205f, 0.16f, 0.14f,
        TEX_PLASTER, gu_color_from_argb(0xff283044)
    );
    draw_ellipsoid(
        0.0f, 1.17f, 0.0f,
        0.255f, 0.37f, 0.16f,
        TEX_PLASTER, shirt
    );

    /* Sleeves and hands swing opposite the legs. */
    sceGumPushMatrix();
    pos.x = -0.285f;
    pos.y = 1.46f;
    pos.z = 0.0f;
    sceGumTranslate(&pos);
    sceGumRotateX(arm_gait - 0.08f);
    draw_cylinder_y(0.0f, -0.38f, 0.0f, 0.075f, 0.40f, TEX_PLASTER, shirt);
    draw_cylinder_y(0.0f, -0.56f, 0.0f, 0.055f, 0.20f, TEX_PLASTER, gu_color_from_argb(0xffb9825e));
    sceGumPopMatrix();

    sceGumPushMatrix();
    pos.x = 0.285f;
    pos.y = 1.46f;
    pos.z = 0.0f;
    sceGumTranslate(&pos);
    sceGumRotateX(-arm_gait - 0.08f);
    draw_cylinder_y(0.0f, -0.38f, 0.0f, 0.075f, 0.40f, TEX_PLASTER, shirt);
    draw_cylinder_y(0.0f, -0.56f, 0.0f, 0.055f, 0.20f, TEX_PLASTER, gu_color_from_argb(0xffb9825e));
    sceGumPopMatrix();

    draw_cylinder_y(0.0f, 1.51f, 0.0f, 0.075f, 0.12f, TEX_PLASTER, gu_color_from_argb(0xffb9825e));
    draw_uv_sphere(0.0f, 1.67f, 0.0f, 0.19f, TEX_PLASTER, gu_color_from_argb(0xffc99372));
    draw_ellipsoid(0.0f, 1.785f, 0.015f, 0.18f, 0.105f, 0.18f, TEX_WOOD, gu_color_from_argb(0xff38291f));
    draw_uv_sphere(0.0f, 1.65f, -0.17f, 0.045f, TEX_PLASTER, gu_color_from_argb(0xffc99372));

    /* Small facial marks make the head read at the PSP chase-camera scale. */
    draw_uv_sphere(-0.052f, 1.69f, -0.171f, 0.020f, TEX_METAL, gu_color_from_argb(0xff292322));
    draw_uv_sphere( 0.052f, 1.69f, -0.171f, 0.020f, TEX_METAL, gu_color_from_argb(0xff292322));

    sceGumPopMatrix();
}

static void draw_human(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt
)
{
    draw_human_pose(x, y, z, yaw, shirt, 0.0f);
}

static void draw_child(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt
)
{
    ScePspFVector3 pos;

    draw_shadow_blob(x, z, 0.36f, 0.25f);

    shirt = gu_color_from_argb(shirt);

    pos.x = x;
    pos.y = y;
    pos.z = z;

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);

    draw_cylinder_y(-0.14f, 0.0f, 0.0f, 0.12f, 0.66f, TEX_METAL, gu_color_from_argb(0xffd5d7dc));
    draw_cylinder_y( 0.14f, 0.0f, 0.0f, 0.12f, 0.66f, TEX_METAL, gu_color_from_argb(0xffd5d7dc));

    draw_ellipsoid(0.0f, 0.99f, 0.0f, 0.25f, 0.34f, 0.18f, TEX_PLASTER, shirt);
    draw_uv_sphere(0.0f, 1.50f, 0.0f, 0.28f, TEX_PLASTER, gu_color_from_argb(0xffe1b88e));
    draw_ellipsoid(0.0f, 1.70f, 0.0f, 0.28f, 0.12f, 0.28f, TEX_WOOD, gu_color_from_argb(0xff5b4032));

    sceGumPopMatrix();
}

typedef struct
{
    float x;
    float y;
} CarProfilePoint;

static void set_car_vertex(
    WorldVertex *v,
    float u,
    float tv,
    unsigned int color,
    float x,
    float y,
    float z
)
{
    v->u = (unsigned short)u;
    v->v = (unsigned short)tv;
    v->color = color;
    v->x = x;
    v->y = y;
    v->z = z;
}

/* A seven-point extruded body profile gives the car a low-poly silhouette. */
static void draw_car_body_mesh(
    float body_l,
    float body_w,
    float body_h,
    unsigned int color
)
{
    const int point_count = 7;
    float base_y = 0.72f - body_h * 0.5f;
    CarProfilePoint p[7];
    WorldVertex *v;
    int n = 0;
    int side;
    int i;

    p[0].x = -body_l * 0.50f; p[0].y = base_y + body_h * 0.22f;
    p[1].x = -body_l * 0.48f; p[1].y = base_y + body_h * 0.64f;
    p[2].x = -body_l * 0.34f; p[2].y = base_y + body_h * 0.88f;
    p[3].x =  body_l * 0.13f; p[3].y = base_y + body_h * 0.84f;
    p[4].x =  body_l * 0.40f; p[4].y = base_y + body_h * 0.68f;
    p[5].x =  body_l * 0.50f; p[5].y = base_y + body_h * 0.53f;
    p[6].x =  body_l * 0.50f; p[6].y = base_y + body_h * 0.22f;

    v = (WorldVertex *)sceGuGetMemory(
        point_count * 12 * sizeof(WorldVertex)
    );

    for (side = 0; side < 2; ++side)
    {
        float z = side ? body_w * 0.5f : -body_w * 0.5f;
        float cx = 0.0f;
        float cy = base_y + body_h * 0.52f;
        unsigned int face_color = shade_gu_color(color, side ? 94 : 82);

        for (i = 0; i < point_count; ++i)
        {
            int next = (i + 1) % point_count;
            float u0 = ((p[i].x / body_l) + 0.5f) * WORLD_TEX_W;
            float u1 = ((p[next].x / body_l) + 0.5f) * WORLD_TEX_W;
            float v0 = ((p[i].y - base_y) / body_h) * WORLD_TEX_H;
            float v1 = ((p[next].y - base_y) / body_h) * WORLD_TEX_H;

            set_car_vertex(&v[n++], WORLD_TEX_W * 0.5f, WORLD_TEX_H * 0.5f,
                face_color, cx, cy, z);
            set_car_vertex(&v[n++], u0, v0, face_color,
                p[i].x, p[i].y, z);
            set_car_vertex(&v[n++], u1, v1, face_color,
                p[next].x, p[next].y, z);
        }
    }

    /* Join both side profiles to form hood, bumpers, trunk and underside. */
    for (i = 0; i < point_count; ++i)
    {
        int next = (i + 1) % point_count;
        float u0 = ((p[i].x / body_l) + 0.5f) * WORLD_TEX_W;
        float u1 = ((p[next].x / body_l) + 0.5f) * WORLD_TEX_W;
        int percent = (p[i].y + p[next].y) >
                      (2.0f * (base_y + body_h * 0.60f)) ? 108 : 80;
        unsigned int face_color = shade_gu_color(color, percent);
        float z0 = -body_w * 0.5f;
        float z1 =  body_w * 0.5f;

        set_car_vertex(&v[n++], u0, 0, face_color, p[i].x, p[i].y, z0);
        set_car_vertex(&v[n++], u1, 0, face_color, p[next].x, p[next].y, z0);
        set_car_vertex(&v[n++], u1, WORLD_TEX_H, face_color, p[next].x, p[next].y, z1);
        set_car_vertex(&v[n++], u0, 0, face_color, p[i].x, p[i].y, z0);
        set_car_vertex(&v[n++], u1, WORLD_TEX_H, face_color, p[next].x, p[next].y, z1);
        set_car_vertex(&v[n++], u0, WORLD_TEX_H, face_color, p[i].x, p[i].y, z1);
    }

    draw_textured_triangles(v, n, TEX_CAR_BODY);
}

/* Tapered glass cabin with sloped windscreen and rear window. */
static void draw_car_cabin(
    float roof_l,
    float body_w,
    float roof_h,
    unsigned int paint
)
{
    float y0 = -roof_h * 0.5f;
    float y1 =  roof_h * 0.5f;
    float xb = roof_l * 0.50f;
    float xt = roof_l * 0.31f;
    float zb = body_w * 0.43f;
    float zt = body_w * 0.34f;
    float side;

    draw_textured_quad(
        -xt,y1,-zt, xt,y1,-zt, xt,y1,zt, -xt,y1,zt,
        TEX_CAR_BODY,
        shade_gu_color(paint, 112)
    );

    for (side = -1.0f; side <= 1.0f; side += 2.0f)
    {
        float z0 = side * zb;
        float z1 = side * zt;

        draw_textured_quad(
            -xb,y0,z0, xb,y0,z0, xt,y1,z1, -xt,y1,z1,
            TEX_GLASS,
            gu_color_from_argb(0xff9fc8dc)
        );
        draw_textured_quad(
            -0.025f,y0,z0, 0.025f,y0,z0,
             0.025f,y1,z1, -0.025f,y1,z1,
            TEX_CAR_BODY,
            shade_gu_color(paint, 82)
        );
        draw_textured_quad(
            -xb*0.88f,y0,z0, -xb*0.80f,y0,z0,
            -xt*0.88f,y1,z1, -xt*0.98f,y1,z1,
            TEX_CAR_BODY,
            shade_gu_color(paint, 86)
        );
    }

    /* Raked front windshield. */
    draw_textured_quad(
        xb,y0,-zb, xb,y0,zb, xt,y1,zt, xt,y1,-zt,
        TEX_GLASS,
        gu_color_from_argb(0xffbdd6e1)
    );

    /* Rear glass leans toward the trunk. */
    draw_textured_quad(
        -xb,y0,zb, -xb,y0,-zb, -xt,y1,-zt, -xt,y1,zt,
        TEX_GLASS,
        gu_color_from_argb(0xff829fb0)
    );

    /* Painted A-pillars on each side help define the window openings. */
    for (side = -1.0f; side <= 1.0f; side += 2.0f)
    {
        float z0 = side * (zb + 0.012f);
        float z1 = side * (zt + 0.012f);
        float x0 = xb * 0.88f;
        float x1 = xt * 0.90f;

        draw_textured_quad(
            x0 - 0.045f,y0,z0, x0 + 0.045f,y0,z0,
            x1 + 0.035f,y1,z1, x1 - 0.035f,y1,z1,
            TEX_CAR_BODY,
            shade_gu_color(paint, 88)
        );
    }
}

static void draw_fender_arch(
    float center_x,
    float center_y,
    float z,
    float outer_radius,
    float inner_radius,
    unsigned int color
)
{
    const int segments = 10;
    int i;
    WorldVertex *v = (WorldVertex *)sceGuGetMemory(
        segments * 6 * sizeof(WorldVertex)
    );

    for (i = 0; i < segments; ++i)
    {
        float a0 = (float)i * PI_F / (float)segments;
        float a1 = (float)(i + 1) * PI_F / (float)segments;
        float x0o = center_x + cosf(a0) * outer_radius;
        float y0o = center_y + sinf(a0) * outer_radius;
        float x1o = center_x + cosf(a1) * outer_radius;
        float y1o = center_y + sinf(a1) * outer_radius;
        float x0i = center_x + cosf(a0) * inner_radius;
        float y0i = center_y + sinf(a0) * inner_radius;
        float x1i = center_x + cosf(a1) * inner_radius;
        float y1i = center_y + sinf(a1) * inner_radius;
        unsigned int lit = shade_gu_color(color, 104);
        unsigned int shade = shade_gu_color(color, 84);
        int n = i * 6;

        set_car_vertex(&v[n+0], 0, 0, shade, x0o, y0o, z);
        set_car_vertex(&v[n+1], WORLD_TEX_W, 0, shade, x1o, y1o, z);
        set_car_vertex(&v[n+2], WORLD_TEX_W, WORLD_TEX_H, lit, x1i, y1i, z);
        set_car_vertex(&v[n+3], 0, 0, shade, x0o, y0o, z);
        set_car_vertex(&v[n+4], WORLD_TEX_W, WORLD_TEX_H, lit, x1i, y1i, z);
        set_car_vertex(&v[n+5], 0, WORLD_TEX_H, lit, x0i, y0i, z);
    }

    draw_textured_triangles(v, segments * 6, TEX_CAR_BODY);
}

static void draw_car_model(
    float x,
    float z,
    float yaw,
    unsigned int color,
    int van_style
)
{
    ScePspFVector3 pos;
    float body_l = van_style ? 4.5f : 3.6f;
    float body_w = van_style ? 1.75f : 1.60f;
    float body_h = van_style ? 1.0f : 0.72f;
    float roof_l = van_style ? 2.85f : 2.12f;
    float roof_h = van_style ? 0.92f : 0.72f;
    float wheel_x = body_l * 0.31f;
    float wheel_z = body_w * 0.56f;

    color = gu_color_from_argb(color);

    draw_shadow_blob(x, z, body_l * 0.50f, body_w * 0.56f);

    pos.x = x;
    pos.y = 0.0f;
    pos.z = z;

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    /* The long body axis is local X; align it with the car's forward vector. */
    sceGumRotateY(PI_F * 0.5f - yaw);

    draw_car_body_mesh(body_l, body_w, body_h, color);

    /* Fine door shut-lines and handles add scale cues to the body sides. */
    {
        float side_z;
        int side;
        for (side = -1; side <= 1; side += 2)
        {
            side_z = (float)side * (body_w * 0.5f + 0.022f);
            draw_textured_quad(
                -body_l*0.08f,0.54f,side_z,
                -body_l*0.08f+0.035f,0.54f,side_z,
                -body_l*0.08f+0.035f,0.97f,side_z,
                -body_l*0.08f,0.97f,side_z,
                TEX_METAL,
                gu_color_rgba(49, 50, 48, 255)
            );
            draw_textured_quad(
                body_l*0.04f,0.79f,side_z,
                body_l*0.17f,0.79f,side_z,
                body_l*0.17f,0.83f,side_z,
                body_l*0.04f,0.83f,side_z,
                TEX_METAL,
                gu_color_rgba(205, 199, 181, 255)
            );
        }
    }

    sceGumPushMatrix();
    pos.x = 0.0f;
    pos.y = 1.48f;
    pos.z = 0.0f;
    sceGumTranslate(&pos);
    draw_car_cabin(roof_l, body_w, roof_h, color);

    sceGumPopMatrix();

    /* Four wheels and simple bright hub rings. */
    draw_cylinder_z(-wheel_x,0.44f,-wheel_z,0.39f,0.24f,TEX_METAL,gu_color_from_argb(0xff25282c));
    draw_cylinder_z( wheel_x,0.44f,-wheel_z,0.39f,0.24f,TEX_METAL,gu_color_from_argb(0xff25282c));
    draw_cylinder_z(-wheel_x,0.44f, wheel_z,0.39f,0.24f,TEX_METAL,gu_color_from_argb(0xff25282c));
    draw_cylinder_z( wheel_x,0.44f, wheel_z,0.39f,0.24f,TEX_METAL,gu_color_from_argb(0xff25282c));
    draw_cylinder_z(-wheel_x,0.44f,-wheel_z,0.17f,0.25f,TEX_METAL,gu_color_from_argb(0xffaeb5ba));
    draw_cylinder_z( wheel_x,0.44f,-wheel_z,0.17f,0.25f,TEX_METAL,gu_color_from_argb(0xffaeb5ba));
    draw_cylinder_z(-wheel_x,0.44f, wheel_z,0.17f,0.25f,TEX_METAL,gu_color_from_argb(0xffaeb5ba));
    draw_cylinder_z( wheel_x,0.44f, wheel_z,0.17f,0.25f,TEX_METAL,gu_color_from_argb(0xffaeb5ba));

    /* Paint arcs over each tire opening to visually join wheels to the body. */
    draw_fender_arch(-wheel_x,0.44f,-wheel_z-0.14f,0.47f,0.365f,color);
    draw_fender_arch( wheel_x,0.44f,-wheel_z-0.14f,0.47f,0.365f,color);
    draw_fender_arch(-wheel_x,0.44f, wheel_z+0.14f,0.47f,0.365f,color);
    draw_fender_arch( wheel_x,0.44f, wheel_z+0.14f,0.47f,0.365f,color);

    /* Bumpers, grille, headlights and tail lamps. */
    draw_box_textured(
        body_l*0.49f,
        0.56f,
        0.0f,
        0.14f,
        0.20f,
        body_w*0.92f,
        TEX_METAL,
        gu_color_from_argb(0xff8a9299)
    );
    draw_box_textured(
        -body_l*0.49f,
        0.56f,
        0.0f,
        0.14f,
        0.20f,
        body_w*0.92f,
        TEX_METAL,
        gu_color_from_argb(0xff8a9299)
    );
    draw_box_textured(
        body_l*0.485f,
        0.73f,
        0.0f,
        0.045f,
        0.24f,
        body_w*0.28f,
        TEX_METAL,
        gu_color_from_argb(0xff292d30)
    );
    draw_box_textured(body_l*0.47f,0.82f,-body_w*0.31f,0.08f,0.18f,0.28f,TEX_METAL,gu_color_from_argb(0xffffe6aa));
    draw_box_textured(body_l*0.47f,0.82f, body_w*0.31f,0.08f,0.18f,0.28f,TEX_METAL,gu_color_from_argb(0xffffe6aa));
    draw_box_textured(-body_l*0.47f,0.82f,-body_w*0.31f,0.08f,0.18f,0.25f,TEX_METAL,gu_color_from_argb(0xffc6352f));
    draw_box_textured(-body_l*0.47f,0.82f, body_w*0.31f,0.08f,0.18f,0.25f,TEX_METAL,gu_color_from_argb(0xffc6352f));

    sceGumPopMatrix();
}

static void draw_bridge(void)
{
    draw_box_textured(0.0f,1.2f,-68.0f,26.0f,1.0f,8.0f,TEX_METAL,gu_color_from_argb(0xffb8bec5));

    draw_textured_quad(
        -12.0f,1.72f,-71.6f,
         12.0f,1.72f,-71.6f,
         12.0f,1.72f,-64.4f,
        -12.0f,1.72f,-64.4f,
        TEX_ASPHALT,
        0xffffffff
    );

    draw_box_textured(0.0f,2.7f,-72.0f,26.0f,1.2f,0.16f,TEX_METAL,0xffffffff);
    draw_box_textured(0.0f,2.7f,-64.0f,26.0f,1.2f,0.16f,TEX_METAL,0xffffffff);
    draw_box_textured(-8.0f,-1.0f,-68.0f,1.4f,4.4f,1.6f,TEX_METAL,gu_color_from_argb(0xff9da2a7));
    draw_box_textured( 8.0f,-1.0f,-68.0f,1.4f,4.4f,1.6f,TEX_METAL,gu_color_from_argb(0xff9da2a7));
}

static void draw_road_segment(
    float x,
    float z,
    float w,
    float d,
    int horizontal
)
{
    if (horizontal)
    {
        draw_textured_quad(
            x-w*0.5f,-0.05f,z-d*0.5f,
            x+w*0.5f,-0.05f,z-d*0.5f,
            x+w*0.5f,-0.05f,z+d*0.5f,
            x-w*0.5f,-0.05f,z+d*0.5f,
            TEX_ASPHALT,0xffffffff
        );

        draw_textured_quad(
            x-w*0.5f,-0.015f,z-0.035f,
            x+w*0.5f,-0.015f,z-0.035f,
            x+w*0.5f,-0.015f,z+0.035f,
            x-w*0.5f,-0.015f,z+0.035f,
            TEX_MARKING,0xffffffff
        );
    }
    else
    {
        draw_textured_quad(
            x-w*0.5f,-0.05f,z-d*0.5f,
            x+w*0.5f,-0.05f,z-d*0.5f,
            x+w*0.5f,-0.05f,z+d*0.5f,
            x-w*0.5f,-0.05f,z+d*0.5f,
            TEX_ASPHALT,0xffffffff
        );

        draw_textured_quad(
            x-0.035f,-0.015f,z-d*0.5f,
            x+0.035f,-0.015f,z-d*0.5f,
            x+0.035f,-0.015f,z+d*0.5f,
            x-0.035f,-0.015f,z+d*0.5f,
            TEX_MARKING,0xffffffff
        );
    }
}

static void render_city_world(void)
{
    int i;

    /* Fog hides the far edge; don't spend GE time drawing beyond it. */
#define CAMERA_NEAR(xp, zp, limit) \
    (((xp) - camera_eye.x) * ((xp) - camera_eye.x) + \
     ((zp) - camera_eye.z) * ((zp) - camera_eye.z) <= (limit) * (limit))

    make_world_textures();

    /* Continuous ground. */
    draw_textured_quad(
        -100.0f,0.0f,-100.0f,
         100.0f,0.0f,-100.0f,
         100.0f,0.0f, 100.0f,
        -100.0f,0.0f, 100.0f,
        TEX_GRASS,
        0xffffffff
    );

    /* Streets and side streets. */
    draw_road_segment(0.0f,0.0f,190.0f,12.0f,1);
    draw_road_segment(0.0f,0.0f,12.0f,190.0f,0);
    draw_road_segment(0.0f,-50.0f,190.0f,9.0f,1);
    draw_road_segment(0.0f,50.0f,190.0f,9.0f,1);
    draw_road_segment(-50.0f,0.0f,9.0f,190.0f,0);
    draw_road_segment(50.0f,0.0f,9.0f,190.0f,0);

    /* Continuous pavements around the main roads. */
    draw_textured_quad(-95.0f,0.01f,-8.0f, 95.0f,0.01f,-8.0f, 95.0f,0.01f,-6.6f, -95.0f,0.01f,-6.6f,TEX_SIDEWALK,0xffffffff);
    draw_textured_quad(-95.0f,0.01f, 8.0f, 95.0f,0.01f, 8.0f, 95.0f,0.01f, 6.6f, -95.0f,0.01f, 6.6f,TEX_SIDEWALK,0xffffffff);
    draw_textured_quad(-8.0f,0.01f,-95.0f,-6.6f,0.01f,-95.0f,-6.6f,0.01f,95.0f,-8.0f,0.01f,95.0f,TEX_SIDEWALK,0xffffffff);
    draw_textured_quad( 8.0f,0.01f,-95.0f, 6.6f,0.01f,-95.0f, 6.6f,0.01f,95.0f, 8.0f,0.01f,95.0f,TEX_SIDEWALK,0xffffffff);

    /* Waterfront. */
    draw_textured_quad(
        -100.0f,-0.18f,-100.0f,
         100.0f,-0.18f,-100.0f,
         100.0f,-0.18f,-82.0f,
        -100.0f,-0.18f,-82.0f,
        TEX_GLASS,
        gu_color_from_argb(0xff96cbe5)
    );

    draw_textured_quad(
        -100.0f,0.01f,-82.0f,
         100.0f,0.01f,-82.0f,
         100.0f,0.01f,-78.0f,
        -100.0f,0.01f,-78.0f,
        TEX_SIDEWALK,
        gu_color_from_argb(0xffe4d0a7)
    );

    for (i = 0; i < BUILDING_COUNT; ++i)
        if (CAMERA_NEAR(buildings[i].x, buildings[i].z, 165.0f))
            draw_building(&buildings[i]);

    /* Parks and roadside greenery. */
    if (CAMERA_NEAR(-62.0f,-60.0f,135.0f)) draw_palm_tree(-62.0f,-60.0f,1.15f);
    if (CAMERA_NEAR( 28.0f,-60.0f,135.0f)) draw_palm_tree( 28.0f,-60.0f,1.10f);
    if (CAMERA_NEAR(-62.0f, 60.0f,135.0f)) draw_palm_tree(-62.0f, 60.0f,0.95f);
    if (CAMERA_NEAR( 28.0f, 60.0f,135.0f)) draw_palm_tree( 28.0f, 60.0f,0.90f);
    if (CAMERA_NEAR(-66.0f,  8.0f,135.0f)) draw_palm_tree(-66.0f,  8.0f,0.85f);
    if (CAMERA_NEAR( 60.0f, -4.0f,135.0f)) draw_palm_tree( 60.0f, -4.0f,0.95f);

    draw_bridge();

    for (i = 0; i < CAR_COUNT; ++i)
    {
        if (!CAMERA_NEAR(cars[i].x, cars[i].z, 135.0f))
            continue;

        if (i == STORY_VAN_INDEX && state == GAME_STATE_STORY_INTRO)
            continue;

        draw_car_model(
            cars[i].x,
            cars[i].z,
            cars[i].yaw,
            cars[i].body,
            i == STORY_VAN_INDEX
        );
    }

    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
    {
        Pedestrian *p = &pedestrians[i];
        float walk = sinf(p->phase * 1.7f);

        if (!CAMERA_NEAR(p->x, p->z, 110.0f))
            continue;

        draw_human_pose(
            p->x,
            0.0f,
            p->z,
            walk * 0.18f,
            p->shirt,
            p->phase * 16.0f
        );
    }

#undef CAMERA_NEAR
}

/* ------------------------------------------------------------------------- */
/* 2D font                                                                    */
/* ------------------------------------------------------------------------- */

static const unsigned char glyph_A[7] = {14,17,17,31,17,17,17};
static const unsigned char glyph_B[7] = {30,17,17,30,17,17,30};
static const unsigned char glyph_C[7] = {14,17,16,16,16,17,14};
static const unsigned char glyph_D[7] = {30,17,17,17,17,17,30};
static const unsigned char glyph_E[7] = {31,16,16,30,16,16,31};
static const unsigned char glyph_F[7] = {31,16,16,30,16,16,16};
static const unsigned char glyph_G[7] = {14,17,16,23,17,17,14};
static const unsigned char glyph_H[7] = {17,17,17,31,17,17,17};
static const unsigned char glyph_I[7] = {31,4,4,4,4,4,31};
static const unsigned char glyph_L[7] = {16,16,16,16,16,16,31};
static const unsigned char glyph_M[7] = {17,27,21,21,17,17,17};
static const unsigned char glyph_N[7] = {17,25,21,19,17,17,17};
static const unsigned char glyph_O[7] = {14,17,17,17,17,17,14};
static const unsigned char glyph_P[7] = {30,17,17,30,16,16,16};
static const unsigned char glyph_R[7] = {30,17,17,30,20,18,17};
static const unsigned char glyph_S[7] = {15,16,16,14,1,1,30};
static const unsigned char glyph_T[7] = {31,4,4,4,4,4,4};
static const unsigned char glyph_U[7] = {17,17,17,17,17,17,14};
static const unsigned char glyph_V[7] = {17,17,17,17,17,10,4};
static const unsigned char glyph_W[7] = {17,17,17,21,21,21,10};
static const unsigned char glyph_Y[7] = {17,17,10,4,4,4,4};
static const unsigned char glyph_0[7] = {14,17,19,21,25,17,14};
static const unsigned char glyph_1[7] = {4,12,4,4,4,4,14};
static const unsigned char glyph_2[7] = {14,17,1,2,4,8,31};
static const unsigned char glyph_3[7] = {30,1,1,14,1,1,30};
static const unsigned char glyph_4[7] = {2,6,10,18,31,2,2};
static const unsigned char glyph_5[7] = {31,16,16,30,1,1,30};
static const unsigned char glyph_6[7] = {14,16,16,30,17,17,14};
static const unsigned char glyph_7[7] = {31,1,2,4,8,8,8};
static const unsigned char glyph_8[7] = {14,17,17,14,17,17,14};
static const unsigned char glyph_9[7] = {14,17,17,15,1,1,14};

static const unsigned char *get_glyph(char c)
{
    switch (c)
    {
        case 'A': return glyph_A;
        case 'B': return glyph_B;
        case 'C': return glyph_C;
        case 'D': return glyph_D;
        case 'E': return glyph_E;
        case 'F': return glyph_F;
        case 'G': return glyph_G;
        case 'H': return glyph_H;
        case 'I': return glyph_I;
        case 'L': return glyph_L;
        case 'M': return glyph_M;
        case 'N': return glyph_N;
        case 'O': return glyph_O;
        case 'P': return glyph_P;
        case 'R': return glyph_R;
        case 'S': return glyph_S;
        case 'T': return glyph_T;
        case 'U': return glyph_U;
        case 'V': return glyph_V;
        case 'W': return glyph_W;
        case 'Y': return glyph_Y;
        case '0': return glyph_0;
        case '1': return glyph_1;
        case '2': return glyph_2;
        case '3': return glyph_3;
        case '4': return glyph_4;
        case '5': return glyph_5;
        case '6': return glyph_6;
        case '7': return glyph_7;
        case '8': return glyph_8;
        case '9': return glyph_9;
        default:  return NULL;
    }
}

static int text_width(const char *text, int scale)
{
    int width = 0;

    while (*text)
    {
        width += 6 * scale;
        ++text;
    }

    return width > 0 ? width - scale : 0;
}

static void draw_text(
    const char *text,
    int x,
    int y,
    int scale,
    unsigned int color
)
{
    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    const char *p;
    int pixel_count = 0;
    int n = 0;
    int current_x = x;
    Vertex *v;

    for (p = text; *p; ++p)
    {
        const unsigned char *g = get_glyph(*p);
        int row;
        int col;

        if (!g)
            continue;

        for (row = 0; row < 7; ++row)
            for (col = 0; col < 5; ++col)
                if (g[row] & (1 << (4 - col)))
                    ++pixel_count;
    }

    if (pixel_count <= 0)
        return;

    v = (Vertex *)sceGuGetMemory(
        pixel_count * 2 * sizeof(Vertex)
    );

    sceGuColor(color);

    for (p = text; *p; ++p)
    {
        const unsigned char *g = get_glyph(*p);
        int row;
        int col;

        if (!g)
        {
            current_x += 6 * scale;
            continue;
        }

        for (row = 0; row < 7; ++row)
        {
            for (col = 0; col < 5; ++col)
            {
                if (g[row] & (1 << (4 - col)))
                {
                    v[n].x = (short)(current_x + col * scale);
                    v[n].y = (short)(y + row * scale);
                    v[n].z = 0;

                    v[n + 1].x =
                        (short)(current_x + (col + 1) * scale);
                    v[n + 1].y =
                        (short)(y + (row + 1) * scale);
                    v[n + 1].z = 0;

                    n += 2;
                }
            }
        }

        current_x += 6 * scale;
    }

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        n,
        NULL,
        v
    );
}

static void draw_rect_2d(
    int x,
    int y,
    int w,
    int h,
    unsigned int color
)
{
    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    Vertex *v =
        (Vertex *)sceGuGetMemory(
            2 * sizeof(Vertex)
        );

    v[0].x = (short)x;
    v[0].y = (short)y;
    v[0].z = 0;

    v[1].x = (short)(x + w);
    v[1].y = (short)(y + h);
    v[1].z = 0;

    sceGuColor(color);

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );
}


/* ------------------------------------------------------------------------- */
/* Embedded texture rendering                                                */
/* ------------------------------------------------------------------------- */

static void draw_texture(const void *texture)
{
    typedef struct
    {
        unsigned short u;
        unsigned short v;
        unsigned int color;
        short x;
        short y;
        short z;
    } TextureVertex;

    TextureVertex *v =
        (TextureVertex *)sceGuGetMemory(
            2 * sizeof(TextureVertex)
        );

    /*
     * Assets are packed as 512x512 RGB565 textures.
     * Only the 480x272 visible area is drawn.
     */
    v[0].u = 0;
    v[0].v = 0;
    v[0].color = 0xffffffff;
    v[0].x = 0;
    v[0].y = 0;
    v[0].z = 0;

    v[1].u = SCREEN_WIDTH;
    v[1].v = SCREEN_HEIGHT;
    v[1].color = 0xffffffff;
    v[1].x = SCREEN_WIDTH;
    v[1].y = SCREEN_HEIGHT;
    v[1].z = 0;

    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        GU_FALSE
    );

    sceGuTexImage(
        0,
        512,
        512,
        512,
        texture
    );

    sceGuTexFunc(
        GU_TFX_REPLACE,
        GU_TCC_RGB
    );

    sceGuTexFilter(
        GU_NEAREST,
        GU_NEAREST
    );

    sceGuEnable(
        GU_TEXTURE_2D
    );

    sceGuDrawArray(
        GU_SPRITES,
        GU_TEXTURE_16BIT |
        GU_COLOR_8888 |
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );

    sceGuDisable(
        GU_TEXTURE_2D
    );
}


/* ------------------------------------------------------------------------- */
/* Save                                                                       */
/* ------------------------------------------------------------------------- */

static int save_exists(void)
{
    SceUID fd;
    SaveData data;

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_RDONLY,
        0
    );

    if (fd < 0)
        return 0;

    memset(&data, 0, sizeof(data));

    if (sceIoRead(fd, &data, sizeof(data)) == sizeof(data))
    {
        sceIoClose(fd);
        return data.magic == SAVE_MAGIC;
    }

    sceIoClose(fd);
    return 0;
}

static void save_game(void)
{
    SceUID fd;

    sceIoMkdir(
        SAVE_DIR,
        0777
    );

    memset(
        save_data.username,
        0,
        sizeof(save_data.username)
    );

    save_data.magic = SAVE_MAGIC;
    save_data.language = selected_language;

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC,
        0777
    );

    if (fd >= 0)
    {
        sceIoWrite(
            fd,
            &save_data,
            sizeof(save_data)
        );

        sceIoClose(fd);
    }
}

static void load_game(void)
{
    SceUID fd;

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_RDONLY,
        0
    );

    if (fd < 0)
        return;

    if (sceIoRead(fd, &save_data, sizeof(save_data))
        == sizeof(save_data))
    {
        selected_language = save_data.language;
    }

    sceIoClose(fd);
}

/* ------------------------------------------------------------------------- */
/* Music                                                                      */
/* ------------------------------------------------------------------------- */

static int music_thread_func(
    SceSize args,
    void *argp
)
{
    const short *samples =
        (const short *)Music_start;

    unsigned int sample_count =
        (unsigned int)(Music_end - Music_start) /
        sizeof(short);

    int channel;
    unsigned int i;

    (void)args;
    (void)argp;

    if (sample_count == 0)
    {
        music_running = 0;
        return 0;
    }

    channel = sceVaudioChReserve(
        MUSIC_SAMPLES,
        MUSIC_RATE,
        PSP_VAUDIO_FORMAT_MONO
    );

    if (channel < 0)
    {
        music_running = 0;
        return channel;
    }

    while (music_running)
    {
        for (i = 0; i < MUSIC_SAMPLES; ++i)
        {
            if (music_position >= sample_count)
                music_position = 0;

            music_buffer[i] =
                samples[music_position++];
        }

        sceVaudioOutputBlocking(
            MUSIC_VOLUME,
            music_buffer
        );
    }

    sceVaudioChRelease();

    return 0;
}

static void music_start(void)
{
    if (music_running)
        return;

    music_position = 0;
    music_running = 1;

    music_thread = sceKernelCreateThread(
        "ginseng_music",
        music_thread_func,
        0x12,
        0x4000,
        0,
        NULL
    );

    if (music_thread >= 0)
    {
        sceKernelStartThread(
            music_thread,
            0,
            NULL
        );
    }
    else
    {
        music_running = 0;
    }
}

static void music_stop(void)
{
    music_running = 0;

    if (music_thread >= 0)
    {
        sceKernelWaitThreadEnd(
            music_thread,
            NULL
        );

        music_thread = -1;
    }
}

/* ------------------------------------------------------------------------- */
/* Fade                                                                       */
/* ------------------------------------------------------------------------- */

static void start_transition(
    GameState next_state
)
{
    transition_active = 1;
    transition_phase = 0;
    transition_alpha = 0;
    transition_target = next_state;
}

static void update_transition(void)
{
    if (!transition_active)
        return;

    if (transition_phase == 0)
    {
        transition_alpha += 18;

        if (transition_alpha >= 255)
        {
            transition_alpha = 255;

            state = transition_target;

            if (state == GAME_STATE_SAVING)
                saving_timer = 0;

            if (state == GAME_STATE_SAVE_FINISHED)
                save_finished_timer = 0;

            /* Change state while fully black. Start/stop audio only after
             * the fade-in has completed so Press Start itself stays silent. */
            music_stop();

            if (state == GAME_STATE_STORY_INTRO)
            {
                music_stop();
                intro_frame = 0;
                intro_phase = 0;
                intro_van_x = -42.0f;
                intro_van_z = 8.0f;
                cousin_door_open = 0;
                intro_bro1_x = -13.0f;
                intro_bro1_z = 5.2f;
                intro_bro2_x = -13.0f;
                intro_bro2_z = 10.0f;
            }

            transition_phase = 1;
        }
    }
    else
    {
        transition_alpha -= 10;

        if (transition_alpha <= 0)
        {
            transition_alpha = 0;
            transition_active = 0;

            if (state == GAME_STATE_MAIN_MENU ||
                state == GAME_STATE_STORY ||
                state == GAME_STATE_FREE_WORLD ||
                state == GAME_STATE_MULTIPLAYER)
            {
                music_start();
            }
        }
    }
}

static void render_fade(void)
{
    if (!transition_active && transition_alpha == 0)
        return;

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(
        GU_ADD,
        GU_SRC_ALPHA,
        GU_ONE_MINUS_SRC_ALPHA,
        0,
        0
    );

    draw_rect_2d(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        ((unsigned int)transition_alpha << 24)
    );

    sceGuDisable(GU_BLEND);
}

/* ------------------------------------------------------------------------- */
/* Camera                                                                    */
/* ------------------------------------------------------------------------- */

static void set_3d_camera(
    float eye_x,
    float eye_y,
    float eye_z,
    float center_x,
    float center_y,
    float center_z
)
{
    camera_eye.x = eye_x;
    camera_eye.y = eye_y;
    camera_eye.z = eye_z;

    camera_center.x = center_x;
    camera_center.y = center_y;
    camera_center.z = center_z;

    camera_up.x = 0.0f;
    camera_up.y = 1.0f;
    camera_up.z = 0.0f;

    sceGumMatrixMode(
        GU_PROJECTION
    );

    sceGumLoadIdentity();

    sceGumPerspective(
        65.0f,
        (float)SCREEN_WIDTH /
        (float)SCREEN_HEIGHT,
        0.25f,
        350.0f
    );

    sceGumMatrixMode(
        GU_VIEW
    );

    sceGumLoadIdentity();

    sceGumLookAt(
        &camera_eye,
        &camera_center,
        &camera_up
    );

    sceGumMatrixMode(
        GU_MODEL
    );

    sceGumLoadIdentity();
}

static void set_third_person_camera(void)
{
    float fx;
    float fz;
    float eye_x;
    float eye_y;
    float eye_z;

    if (in_vehicle && current_vehicle >= 0)
    {
        CityCar *c = &cars[current_vehicle];

        fx = sinf(c->yaw);
        fz = -cosf(c->yaw);

        set_3d_camera(
            c->x - fx * 7.0f,
            3.35f,
            c->z - fz * 7.0f,
            c->x + fx * 2.5f,
            1.25f,
            c->z + fz * 2.5f
        );
        return;
    }

    fx = sinf(player_yaw);
    fz = -cosf(player_yaw);

    eye_x = player_x - fx * 5.2f;
    eye_y = 2.75f + sinf(walk_bob) * 0.025f;
    eye_z = player_z - fz * 5.2f;

    set_3d_camera(
        eye_x,
        eye_y,
        eye_z,
        player_x + fx * 0.65f,
        1.25f + camera_pitch,
        player_z + fz * 0.65f
    );
}
static void update_camera_turn_input(
    const SceCtrlData *pad
)
{
    if (!pad || in_vehicle)
        return;

    if (pad->Buttons & PSP_CTRL_LTRIGGER)
        player_yaw -= 0.045f;

    if (pad->Buttons & PSP_CTRL_RTRIGGER)
        player_yaw += 0.045f;

    while (player_yaw > PI_F)
        player_yaw -= 2.0f * PI_F;

    while (player_yaw < -PI_F)
        player_yaw += 2.0f * PI_F;
}

/* ------------------------------------------------------------------------- */
/* Story intro camera and animation                                          */
/* ------------------------------------------------------------------------- */

static void update_story_intro(void)
{
    ++intro_frame;

    if (intro_frame < INTRO_PHASE_APPROACH)
        intro_phase = 0;
    else if (intro_frame < INTRO_PHASE_APPROACH + INTRO_PHASE_PARK)
        intro_phase = 1;
    else if (intro_frame < INTRO_PHASE_APPROACH + INTRO_PHASE_PARK + INTRO_PHASE_EXIT)
        intro_phase = 2;
    else if (intro_frame < INTRO_PHASE_APPROACH + INTRO_PHASE_PARK +
                          INTRO_PHASE_EXIT + INTRO_PHASE_WALK)
        intro_phase = 3;
    else
        intro_phase = 4;

    if (intro_frame < INTRO_PHASE_APPROACH)
    {
        float t =
            (float)intro_frame /
            (float)INTRO_PHASE_APPROACH;

        intro_van_x =
            -42.0f +
            t * 29.0f;
    }
    else if (intro_frame <
             INTRO_PHASE_APPROACH +
             INTRO_PHASE_PARK)
    {
        intro_van_x = -13.0f;
    }
    else if (intro_frame <
             INTRO_PHASE_APPROACH +
             INTRO_PHASE_PARK +
             INTRO_PHASE_EXIT)
    {
        int local =
            intro_frame -
            INTRO_PHASE_APPROACH -
            INTRO_PHASE_PARK;

        float t =
            (float)local /
            (float)INTRO_PHASE_EXIT;

        if (t > 1.0f)
            t = 1.0f;

        /*
         * Two brothers emerge from the side doors.
         */
        intro_bro1_x =
            -13.0f +
            t * 2.4f;

        intro_bro1_z =
            5.2f +
            t * 1.3f;

        intro_bro2_x =
            -13.0f -
            t * 1.8f;

        intro_bro2_z =
            10.0f -
            t * 1.2f;
    }
    else if (intro_frame <
             INTRO_PHASE_APPROACH +
             INTRO_PHASE_PARK +
             INTRO_PHASE_EXIT +
             INTRO_PHASE_WALK)
    {
        int local =
            intro_frame -
            INTRO_PHASE_APPROACH -
            INTRO_PHASE_PARK -
            INTRO_PHASE_EXIT;

        float t =
            (float)local /
            (float)INTRO_PHASE_WALK;

        if (t > 1.0f)
            t = 1.0f;

        intro_bro1_x =
            -10.6f -
            t * 2.0f;

        intro_bro1_z =
            6.5f +
            t * 2.6f;

        intro_bro2_x =
            -14.8f +
            t * 4.6f;

        intro_bro2_z =
            8.8f +
            t * 0.4f;
    }
    else if (intro_frame < INTRO_TOTAL)
    {
        cousin_door_open =
            intro_frame >
            INTRO_TOTAL - 70;
    }
    else
    {
        player_x = -10.6f;
        player_z = 9.1f;
        player_yaw = PI_F;

        brother_x = -14.8f;
        brother_z = 10.8f;
        brother_yaw = PI_F;

        story_step = 0;

        start_transition(
            GAME_STATE_STORY
        );
    }
}

/* ------------------------------------------------------------------------- */
/* Story / free movement                                                     */
/* ------------------------------------------------------------------------- */

static void clamp_world_position(
    float *x,
    float *z
)
{
    if (*x < -88.0f) *x = -88.0f;
    if (*x >  88.0f) *x =  88.0f;
    if (*z < -86.0f) *z = -86.0f;
    if (*z >  88.0f) *z =  88.0f;
}

static int nearest_vehicle(void)
{
    int i;
    int best = -1;
    float best_d2 = 25.0f;

    for (i = 0; i < CAR_COUNT; ++i)
    {
        float dx =
            cars[i].x - player_x;

        float dz =
            cars[i].z - player_z;

        float d2 =
            dx * dx +
            dz * dz;

        if (d2 < best_d2)
        {
            best_d2 = d2;
            best = i;
        }
    }

    return best;
}

static void move_walker(
    const SceCtrlData *pad
)
{
    float lx;
    float ly;
    float mag;
    float move_x;
    float move_z;

    lx =
        ((float)pad->Lx - 128.0f) /
        127.0f;

    ly =
        ((float)pad->Ly - 128.0f) /
        127.0f;

    if (pad->Buttons & PSP_CTRL_LEFT)
        lx = -1.0f;

    if (pad->Buttons & PSP_CTRL_RIGHT)
        lx = 1.0f;

    if (pad->Buttons & PSP_CTRL_UP)
        ly = -1.0f;

    if (pad->Buttons & PSP_CTRL_DOWN)
        ly = 1.0f;

    mag =
        sqrtf(lx * lx + ly * ly);

    if (mag < 0.18f)
    {
        walk_bob *= 0.90f;
        return;
    }

    if (mag > 1.0f)
        mag = 1.0f;

    {
        const float forward_x = sinf(player_yaw);
        const float forward_z = -cosf(player_yaw);
        const float right_x = cosf(player_yaw);
        const float right_z = sinf(player_yaw);
        const float speed = 0.20f * mag;

        move_x = forward_x * (-ly) + right_x * lx;
        move_z = forward_z * (-ly) + right_z * lx;

        player_x += move_x * speed;
        player_z += move_z * speed;

        clamp_world_position(
            &player_x,
            &player_z
        );

        walk_bob += 0.32f * (mag + 0.25f);

        if (walk_bob > 6.2831853f)
            walk_bob -= 6.2831853f;
    }
}

static void update_car(
    const SceCtrlData *pad
)
{
    CityCar *c;

    float lx;
    float ly;
    float forward_x;
    float forward_z;

    if (current_vehicle < 0)
        return;

    c = &cars[current_vehicle];

    lx =
        ((float)pad->Lx - 128.0f) /
        127.0f;

    ly =
        ((float)pad->Ly - 128.0f) /
        127.0f;

    if (pad->Buttons & PSP_CTRL_LEFT)
        lx = -1.0f;

    if (pad->Buttons & PSP_CTRL_RIGHT)
        lx = 1.0f;

    if (pad->Buttons & PSP_CTRL_UP)
        ly = -1.0f;

    if (pad->Buttons & PSP_CTRL_DOWN)
        ly = 1.0f;

    c->speed +=
        -ly * 0.055f;

    c->speed *= 0.97f;

    if (c->speed > 8.0f)
        c->speed = 8.0f;

    if (c->speed < -3.0f)
        c->speed = -3.0f;

    c->yaw +=
        lx *
        (fabsf(c->speed) + 0.8f) *
        0.028f;

    forward_x =
        sinf(c->yaw);

    forward_z =
        -cosf(c->yaw);

    c->x +=
        forward_x *
        c->speed *
        0.10f;

    c->z +=
        forward_z *
        c->speed *
        0.10f;

    clamp_world_position(
        &c->x,
        &c->z
    );

    player_x = c->x;
    player_z = c->z;
    player_yaw = c->yaw;
}

static void update_brother_follow(void)
{
    float dx =
        player_x -
        brother_x;

    float dz =
        player_z -
        brother_z;

    float d2 =
        dx * dx +
        dz * dz;

    if (d2 > 6.0f)
    {
        brother_x +=
            dx * 0.025f;

        brother_z +=
            dz * 0.025f;
    }

    if (fabsf(dx) + fabsf(dz) > 0.1f)
    {
        brother_yaw =
            atan2f(
                dx,
                -dz
            );
    }
}

static void update_pedestrians(void)
{
    int i;

    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
    {
        Pedestrian *p =
            &pedestrians[i];

        p->phase += 0.02f;

        if (p->phase > 1000.0f)
            p->phase -= 1000.0f;

        if (i & 1)
        {
            p->x =
                -9.0f +
                sinf(p->phase) * 7.0f;
        }
        else
        {
            p->z =
                9.0f +
                cosf(p->phase) * 7.0f;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* GU / init                                                                 */
/* ------------------------------------------------------------------------- */

static void gu_init(void)
{
    sceGuInit();

    sceGuStart(
        GU_DIRECT,
        list
    );

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
        2048 - SCREEN_WIDTH / 2,
        2048 - SCREEN_HEIGHT / 2
    );

    sceGuViewport(
        2048,
        2048,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    sceGuDepthRange(
        65535,
        0
    );

    sceGuScissor(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    sceGuEnable(
        GU_SCISSOR_TEST
    );

    sceGuDisable(
        GU_CULL_FACE
    );

    sceGuClearDepth(
        0
    );

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceGuDisplay(
        GU_TRUE
    );
}

/* ------------------------------------------------------------------------- */
/* HUD                                                                       */
/* ------------------------------------------------------------------------- */

static void begin_2d(void)
{
    /* Reset common 3D state before drawing UI. */
    /* 2D assets can replace the active GU texture outside the world cache. */
    world_texture_bound = -1;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);

    sceGumMatrixMode(
        GU_PROJECTION
    );

    sceGumLoadIdentity();

    sceGumOrtho(
        0.0f,
        (float)SCREEN_WIDTH,
        (float)SCREEN_HEIGHT,
        0.0f,
        -1.0f,
        1.0f
    );

    sceGumMatrixMode(
        GU_VIEW
    );

    sceGumLoadIdentity();

    sceGumMatrixMode(
        GU_MODEL
    );

    sceGumLoadIdentity();
}

static int lerp_channel(int a, int b, int t)
{
    return a + ((b - a) * t) / 255;
}

static void render_sunset_sky(void)
{
    const int bands = 34;
    int i;

    /* A PSP-era violet-to-coral dusk gradient, drawn before the 3D world. */
    begin_2d();

    for (i = 0; i < bands; ++i)
    {
        int y0 = (SCREEN_HEIGHT * i) / bands;
        int y1 = (SCREEN_HEIGHT * (i + 1)) / bands;
        int r0, g0, b0, r1, g1, b1;
        int r, g, b;
        int t = (i * 255) / (bands - 1);

        if (t < 160)
        {
            int local = (t * 255) / 160;
            r0 = 23;  g0 = 28;  b0 = 79;
            r1 = 93;  g1 = 91;  b1 = 151;
            r = lerp_channel(r0, r1, local);
            g = lerp_channel(g0, g1, local);
            b = lerp_channel(b0, b1, local);
        }
        else if (t < 220)
        {
            int local = ((t - 160) * 255) / 60;
            r0 = 93;  g0 = 91;  b0 = 151;
            r1 = 255; g1 = 157; b1 = 111;
            r = lerp_channel(r0, r1, local);
            g = lerp_channel(g0, g1, local);
            b = lerp_channel(b0, b1, local);
        }
        else
        {
            int local = ((t - 220) * 255) / 35;
            if (local > 255) local = 255;
            r0 = 255; g0 = 157; b0 = 111;
            r1 = 147; g1 = 66;  b1 = 112;
            r = lerp_channel(r0, r1, local);
            g = lerp_channel(g0, g1, local);
            b = lerp_channel(b0, b1, local);
        }

        draw_rect_2d(
            0,
            y0,
            SCREEN_WIDTH,
            y1 - y0,
            gu_color_rgba(r, g, b, 255)
        );
    }

    /* The PSP depth buffer is reversed; retain GEQUAL and allow Z writes. */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuFog(
        32.0f,
        145.0f,
        gu_color_rgba(206, 126, 143, 255)
    );
    sceGuEnable(GU_FOG);
}

static void render_game_hud(
    const char *title
)
{
    begin_2d();

    draw_rect_2d(
        12,
        8,
        456,
        34,
        0xcc121a23
    );

    draw_text(
        title,
        24,
        17,
        3,
        0xffffffff
    );

    draw_rect_2d(
        0,
        244,
        SCREEN_WIDTH,
        28,
        0xcc121a23
    );

    draw_text(
        "O BACK",
        18,
        253,
        2,
        0xffffffff
    );

    if (in_vehicle)
    {
        draw_text(
            "X EXIT",
            420,
            253,
            2,
            0xffffffff
        );
    }
    else
    {
        draw_text(
            "X ENTER",
            380,
            253,
            2,
            0xffffffff
        );
    }

    if (state == GAME_STATE_STORY)
    {
        if (story_step == 0)
        {
            draw_text(
                "WALK TO THE BRIDGE",
                24,
                51,
                2,
                0xff172126
            );
        }
        else
        {
            draw_text(
                "STORY COMPLETE",
                24,
                51,
                2,
                0xff172126
            );
        }
    }
    else if (state == GAME_STATE_FREE_WORLD)
    {
        draw_text(
            "EXPLORE PEINE",
            24,
            51,
            2,
            0xff172126
        );
    }
    else if (state == GAME_STATE_MULTIPLAYER)
    {
        draw_text(
            "P1 ANALOG  P2 D PAD",
            24,
            51,
            2,
            0xff172126
        );
    }
}

/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

int psp_game_init(void)
{
    /* Leave the PSP clock at the system/emulator default. */
    world_texture_bound = -1;
    sceCtrlSetSamplingCycle(0);

    sceCtrlSetSamplingMode(
        PSP_CTRL_MODE_ANALOG
    );

    gu_init();
    make_world_textures();

    /* The textures are generated by the CPU and then consumed directly by
     * the GU. Flush the data cache before the first textured frame. */
    sceKernelDcacheWritebackAll();

    state = GAME_STATE_TITLE;

    selected_language = 0;
    selected_menu = 0;
    settings_selection = 0;

    story_step = 0;

    player_x = 0.0f;
    player_y = 0.0f;
    player_z = 2.0f;
    player_yaw = 0.0f;

    brother_x = -2.0f;
    brother_z = 4.5f;

    in_vehicle = 0;
    current_vehicle = -1;

    transition_active = 0;
    transition_alpha = 0;
    transition_phase = 0;

    saving_timer = 0;
    save_finished_timer = 0;

    old_buttons = 0;

    memset(
        &save_data,
        0,
        sizeof(save_data)
    );

    game_initialized = 1;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Update                                                                    */
/* ------------------------------------------------------------------------- */

void psp_game_update(void)
{
    SceCtrlData pad;
    unsigned int pressed;

    if (!game_initialized)
        return;

    if (transition_active)
    {
        update_transition();
        return;
    }

    if (state == GAME_STATE_SAVING)
    {
        ++saving_timer;

        if (saving_timer >= 80)
        {
            start_transition(
                GAME_STATE_SAVE_FINISHED
            );
        }

        return;
    }

    if (state == GAME_STATE_SAVE_FINISHED)
    {
        ++save_finished_timer;

        if (save_finished_timer >= 110)
        {
            start_transition(
                GAME_STATE_MAIN_MENU
            );
        }

        return;
    }

    if (state == GAME_STATE_STORY_INTRO)
    {
        update_story_intro();
        return;
    }

    sceCtrlReadBufferPositive(
        &pad,
        1
    );

    pressed =
        pad.Buttons &
        ~old_buttons;

    old_buttons =
        pad.Buttons;

    switch (state)
    {
        case GAME_STATE_TITLE:

            if (pressed & PSP_CTRL_START)
            {
                music_stop();

                if (save_exists())
                {
                    load_game();

                    start_transition(
                        GAME_STATE_MAIN_MENU
                    );
                }
                else
                {
                    start_transition(
                        GAME_STATE_LANGUAGE
                    );
                }
            }

            break;

        case GAME_STATE_LANGUAGE:

            if (pressed & PSP_CTRL_UP)
            {
                if (selected_language > 0)
                    --selected_language;
            }

            if (pressed & PSP_CTRL_DOWN)
            {
                if (selected_language < 8)
                    ++selected_language;
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_TITLE
                );
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                save_data.language =
                    selected_language;

                start_transition(
                    GAME_STATE_SAVE
                );
            }

            break;

        case GAME_STATE_SAVE:

            if (pressed &
                (PSP_CTRL_CROSS |
                 PSP_CTRL_START))
            {
                save_game();

                start_transition(
                    GAME_STATE_SAVING
                );
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_LANGUAGE
                );
            }

            break;

        case GAME_STATE_MAIN_MENU:

            if (pressed & PSP_CTRL_UP)
            {
                if (selected_menu > 0)
                    --selected_menu;
            }

            if (pressed & PSP_CTRL_DOWN)
            {
                if (selected_menu < 3)
                    ++selected_menu;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                switch (selected_menu)
                {
                    case 0:
                        player_x = -10.6f;
                        player_z = 9.1f;
                        player_yaw = PI_F;
                        brother_x = -14.8f;
                        brother_z = 10.8f;
                        story_step = 0;
                        in_vehicle = 0;
                        current_vehicle = -1;

                        start_transition(
                            GAME_STATE_STORY_INTRO
                        );
                        break;

                    case 1:
                        player_x = 0.0f;
                        player_z = 2.0f;
                        player_yaw = 0.0f;
                        in_vehicle = 0;
                        current_vehicle = -1;

                        start_transition(
                            GAME_STATE_FREE_WORLD
                        );
                        break;

                    case 2:
                        player_x = -4.0f;
                        player_z = 4.0f;
                        player_yaw = 0.0f;

                        player2_x = 4.0f;
                        player2_z = 4.0f;
                        player2_yaw = PI_F;

                        in_vehicle = 0;
                        current_vehicle = -1;

                        start_transition(
                            GAME_STATE_MULTIPLAYER
                        );
                        break;

                    case 3:
                        settings_selection = 0;

                        start_transition(
                            GAME_STATE_SETTINGS
                        );
                        break;
                }
            }

            break;

        case GAME_STATE_STORY:

            update_camera_turn_input(&pad);

            if (in_vehicle)
                update_car(&pad);
            else
                move_walker(&pad);

            update_brother_follow();
            update_pedestrians();

            /*
             * Enter/exit the nearest peaceful city car.
             */
            if (pressed & PSP_CTRL_CROSS)
            {
                if (in_vehicle)
                {
                    in_vehicle = 0;
                    current_vehicle = -1;
                }
                else
                {
                    int nearest =
                        nearest_vehicle();

                    if (nearest >= 0)
                    {
                        current_vehicle = nearest;
                        in_vehicle = 1;

                        cars[nearest].speed =
                            0.0f;
                    }
                }
            }

            /*
             * Story objective = reach the bridge.
             */
            if (!in_vehicle &&
                story_step == 0)
            {
                float dx =
                    player_x - 0.0f;

                float dz =
                    player_z - 82.0f;

                if (dx * dx + dz * dz <
                    11.0f * 11.0f)
                {
                    story_step = 1;
                }
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                in_vehicle = 0;
                current_vehicle = -1;

                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        case GAME_STATE_FREE_WORLD:

            update_camera_turn_input(&pad);

            if (in_vehicle)
                update_car(&pad);
            else
                move_walker(&pad);

            update_pedestrians();

            if (pressed & PSP_CTRL_CROSS)
            {
                if (in_vehicle)
                {
                    in_vehicle = 0;
                    current_vehicle = -1;
                }
                else
                {
                    int nearest =
                        nearest_vehicle();

                    if (nearest >= 0)
                    {
                        current_vehicle = nearest;
                        in_vehicle = 1;

                        cars[nearest].speed =
                            0.0f;
                    }
                }
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                in_vehicle = 0;
                current_vehicle = -1;

                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        case GAME_STATE_MULTIPLAYER:

            /*
             * Player 1 = analog.
             */
            {
                float lx =
                    ((float)pad.Lx - 128.0f) /
                    127.0f;

                float ly =
                    ((float)pad.Ly - 128.0f) /
                    127.0f;

                if (fabsf(lx) > 0.18f ||
                    fabsf(ly) > 0.18f)
                {
                    player_yaw = atan2f(lx, -ly);
                    player_x += lx * 0.17f;
                    player_z += ly * 0.17f;

                    clamp_world_position(
                        &player_x,
                        &player_z
                    );
                }
            }

            /*
             * Player 2 = D-pad.
             */
            if (pad.Buttons & PSP_CTRL_LEFT)
            {
                player2_x -= 0.17f;
                player2_yaw = -PI_F * 0.5f;
            }

            if (pad.Buttons & PSP_CTRL_RIGHT)
            {
                player2_x += 0.17f;
                player2_yaw = PI_F * 0.5f;
            }

            if (pad.Buttons & PSP_CTRL_UP)
            {
                player2_z -= 0.17f;
                player2_yaw = 0.0f;
            }

            if (pad.Buttons & PSP_CTRL_DOWN)
            {
                player2_z += 0.17f;
                player2_yaw = PI_F;
            }

            if (pad.Buttons &
                (PSP_CTRL_LEFT | PSP_CTRL_RIGHT |
                 PSP_CTRL_UP | PSP_CTRL_DOWN))
            {
                player2_walk_phase += 0.32f;
                if (player2_walk_phase > 6.2831853f)
                    player2_walk_phase -= 6.2831853f;
            }
            else
            {
                player2_walk_phase *= 0.90f;
            }

            clamp_world_position(
                &player2_x,
                &player2_z
            );

            update_pedestrians();

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        case GAME_STATE_SETTINGS:

            if (pressed & PSP_CTRL_UP)
            {
                if (settings_selection > 0)
                    --settings_selection;
            }

            if (pressed & PSP_CTRL_DOWN)
            {
                if (settings_selection < 1)
                    ++settings_selection;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                if (settings_selection == 0)
                {
                    music_stop();
                }
                else
                {
                    sceIoRemove(
                        SAVE_FILE
                    );

                    music_stop();

                    start_transition(
                        GAME_STATE_TITLE
                    );
                }
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Render                                                                    */
/* ------------------------------------------------------------------------- */

static void render_story_intro(void)
{
    float camera_x;
    float camera_y;
    float camera_z;
    float center_x;
    float center_y;
    float center_z;

    /*
     * Cinematic camera moves closer to the van and the entrance.
     */
    if (intro_phase == 0)
    {
        camera_x =
            intro_van_x - 10.0f;

        camera_y = 5.0f;
        camera_z = intro_van_z + 14.0f;

        center_x = intro_van_x;
        center_y = 1.6f;
        center_z = intro_van_z;
    }
    else if (intro_phase == 1 ||
             intro_phase == 2)
    {
        camera_x = -23.0f;
        camera_y = 4.8f;
        camera_z = 19.0f;

        center_x = -13.0f;
        center_y = 1.7f;
        center_z = 8.0f;
    }
    else if (intro_phase == 3)
    {
        camera_x = -8.0f;
        camera_y = 4.4f;
        camera_z = 18.0f;

        center_x = -12.0f;
        center_y = 1.5f;
        center_z = 10.0f;
    }
    else
    {
        camera_x = -8.0f;
        camera_y = 4.0f;
        camera_z = 17.0f;

        center_x = -11.0f;
        center_y = 1.5f;
        center_z = 12.0f;
    }

    set_3d_camera(
        camera_x,
        camera_y,
        camera_z,
        center_x,
        center_y,
        center_z
    );

    /* City backdrop */
    render_city_world();

    /* Intro van */
    draw_car_model(
        intro_van_x,
        intro_van_z,
        intro_van_yaw,
        0xff2d67b7,
        1
    );

    /*
     * Two brothers.
     */
    if (intro_frame >=
        INTRO_PHASE_APPROACH +
        INTRO_PHASE_PARK)
    {
        draw_human(
            intro_bro1_x,
            0.0f,
            intro_bro1_z,
            PI_F,
            0xff4d79af
        );

        draw_child(
            intro_bro2_x,
            0.0f,
            intro_bro2_z,
            PI_F,
            0xff7f8f5d
        );
    }

    /*
     * Cousin at the door.
     */
    draw_human(
        -10.5f,
        0.0f,
        12.0f,
        0.0f,
        0xff9a6a49
    );

    /*
     * House entrance / door.
     */
    draw_box_textured(
        -10.5f,
        1.1f,
        13.7f,
        2.0f,
        2.2f,
        0.25f,
        TEX_WOOD,
        cousin_door_open
            ? 0xffb38a64
            : 0xff6c5748
    );

    begin_2d();

    draw_text(
        "STORY",
        22,
        18,
        3,
        0xffffffff
    );

    if (intro_phase <= 1)
    {
        draw_text(
            "THE VAN ARRIVES",
            22,
            50,
            2,
            0xfff4f4f4
        );
    }
    else if (intro_phase == 2)
    {
        draw_text(
            "TWO BROTHERS ARRIVE",
            22,
            50,
            2,
            0xfff4f4f4
        );
    }
    else if (intro_phase == 3)
    {
        draw_text(
            "THEY WALK TO THE DOOR",
            22,
            50,
            2,
            0xfff4f4f4
        );
    }
    else
    {
        draw_text(
            "THE COUSIN OPENS",
            22,
            50,
            2,
            0xfff4f4f4
        );
    }
}

void psp_game_render(void)
{
    if (!game_initialized)
        return;

    sceGuStart(
        GU_DIRECT,
        list
    );

    /* Start every frame from a known GU state. */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);

    /*
     * PSP GU depth setup for 3D.
     */
    sceGuEnable(
        GU_DEPTH_TEST
    );

    sceGuDepthFunc(
        GU_GEQUAL
    );

    sceGuDepthMask(
        GU_FALSE
    );

    sceGuClearColor(
        gu_color_rgba(23, 28, 79, 255)
    );

    sceGuClear(
        GU_COLOR_BUFFER_BIT |
        GU_DEPTH_BUFFER_BIT
    );

    switch (state)
    {
        case GAME_STATE_TITLE:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            draw_texture(
                Title_start
            );

            break;

        case GAME_STATE_LANGUAGE:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            draw_texture(
                LanguageSelection_start
            );

            /*
             * Underline selection only.
             */
            draw_rect_2d(
                145,
                48 +
                selected_language * 29,
                150,
                2,
                0xff63b5ff
            );

            break;

        case GAME_STATE_SAVE:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            {
                int x =
                    (SCREEN_WIDTH -
                     text_width(
                         "SAVE FILE",
                         4
                     )) / 2;

                draw_text(
                    "SAVE FILE",
                    x,
                    65,
                    4,
                    0xffffffff
                );

                x =
                    (SCREEN_WIDTH -
                     text_width(
                         "PRESS X TO SAVE",
                         3
                     )) / 2;

                draw_text(
                    "PRESS X TO SAVE",
                    x,
                    150,
                    3,
                    0xffffffff
                );
            }

            break;

        case GAME_STATE_SAVING:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            {
                int x =
                    (SCREEN_WIDTH -
                     text_width(
                         "SAVING",
                         5
                     )) / 2;

                draw_text(
                    "SAVING",
                    x,
                    110,
                    5,
                    0xffffffff
                );
            }

            break;

        case GAME_STATE_SAVE_FINISHED:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            {
                int x =
                    (SCREEN_WIDTH -
                     text_width(
                         "FINISH SAVE",
                         3
                     )) / 2;

                draw_text(
                    "FINISH SAVE",
                    x,
                    70,
                    3,
                    0xffffffff
                );

                x =
                    (SCREEN_WIDTH -
                     text_width(
                         "DATA SAVED",
                         3
                     )) / 2;

                draw_text(
                    "DATA SAVED",
                    x,
                    128,
                    3,
                    0xffffffff
                );
            }

            break;

        case GAME_STATE_MAIN_MENU:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            draw_texture(
                MainMenu_start
            );

            /*
             * Underline selection without hiding text.
             */
            {
                int y;

                switch (selected_menu)
                {
                    case 0: y = 151; break;
                    case 1: y = 180; break;
                    case 2: y = 209; break;
                    default: y = 238; break;
                }

                draw_rect_2d(
                    22,
                    y,
                    180,
                    2,
                    0xff63b5ff
                );
            }

            break;

        case GAME_STATE_STORY_INTRO:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            /* GU_TRUE masks (disables) depth writes; 3D needs them enabled. */
            sceGuDepthMask(GU_FALSE);

            render_sunset_sky();

            render_story_intro();
            break;

        case GAME_STATE_STORY:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            sceGuDepthMask(GU_FALSE);

            render_sunset_sky();

            set_third_person_camera();

            render_city_world();

            if (!in_vehicle)
            {
                draw_human_pose(player_x, 0.0f, player_z, player_yaw, 0xff536b78, walk_bob);
                draw_human_pose(brother_x, 0.0f, brother_z, brother_yaw, 0xff4c6078, walk_bob * 0.85f);
            }

            /* Third cousin remains near the house after the intro. */
            draw_human(
                -10.5f,
                0.0f,
                12.0f,
                0.0f,
                0xff9a6a49
            );

            render_game_hud(
                "STORY MODE"
            );

            break;

        case GAME_STATE_FREE_WORLD:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            sceGuDepthMask(GU_FALSE);

            render_sunset_sky();

            set_third_person_camera();

            render_city_world();

            if (!in_vehicle)
                draw_human_pose(player_x, 0.0f, player_z, player_yaw, 0xff536b78, walk_bob);

            render_game_hud(
                "FREE OPEN WORLD"
            );

            break;

        case GAME_STATE_MULTIPLAYER:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            sceGuDepthMask(GU_FALSE);

            render_sunset_sky();

            set_third_person_camera();

            render_city_world();

            draw_human_pose(player_x, 0.0f, player_z, player_yaw, 0xff536b78, walk_bob);

            draw_human_pose(
                player2_x,
                0.0f,
                player2_z,
                player2_yaw,
                0xff4f9a68,
                player2_walk_phase
            );

            render_game_hud(
                "MULTIPLAYER LOCAL"
            );

            break;

        case GAME_STATE_SETTINGS:

            sceGuDisable(
                GU_DEPTH_TEST
            );

            begin_2d();

            draw_rect_2d(
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0xff677a89
            );

            draw_text(
                "SYSTEM SETTINGS",
                24,
                24,
                4,
                0xffffffff
            );

            if (settings_selection == 0)
            {
                draw_rect_2d(
                    24,
                    78,
                    360,
                    40,
                    0xff344a56
                );
            }
            else
            {
                draw_rect_2d(
                    24,
                    140,
                    360,
                    40,
                    0xff344a56
                );
            }

            draw_text(
                "MUSIC",
                42,
                92,
                3,
                0xffffffff
            );

            draw_text(
                "RESET SAVE",
                42,
                154,
                3,
                0xffffffff
            );

            draw_text(
                "X SELECT",
                24,
                238,
                2,
                0xffffffff
            );

            draw_text(
                "O BACK",
                364,
                238,
                2,
                0xffffffff
            );

            break;

        default:
            break;
    }

    /*
     * Fade overlay always goes last.
     */
    if (transition_active)
    {
        sceGuDisable(
            GU_DEPTH_TEST
        );

        begin_2d();
        render_fade();
    }

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();

    sceGuSwapBuffers();
}

/* ------------------------------------------------------------------------- */
/* Shutdown                                                                  */
/* ------------------------------------------------------------------------- */

void psp_game_shutdown(void)
{
    if (!game_initialized)
        return;

    music_stop();

    sceGuDisplay(
        GU_FALSE
    );

    sceGuTerm();

    game_initialized = 0;
}

/* ------------------------------------------------------------------------- */
/* Compatibility wrappers                                                    */
/* ------------------------------------------------------------------------- */

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
