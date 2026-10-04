#include <pspkernel.h>
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
 * - first-person 3D camera during gameplay
 * - peaceful city exploration
 * - classic early-2000s console / PS2-style textured look
 * - runtime-generated tiled textures, faceted meshes, baked light and shadows
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

static unsigned int __attribute__((aligned(64)))
    list[0x22000 / 4];

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
/* Runtime 3D texture set + mesh helpers                                    */
/* ------------------------------------------------------------------------- */

#define WORLD_TEX_SIZE 32
#define WORLD_TEX_PIXELS (WORLD_TEX_SIZE * WORLD_TEX_SIZE)

static unsigned short __attribute__((aligned(64)))
    tex_grass[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_asphalt[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_sidewalk[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_plaster[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_brick[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_facade[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_roof[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_glass[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_water[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_metal[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_fabric[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_skin[WORLD_TEX_PIXELS];
static unsigned short __attribute__((aligned(64)))
    tex_tire[WORLD_TEX_PIXELS];

static float character_anim = 0.0f;
static float wheel_spin = 0.0f;

typedef struct
{
    unsigned short u;
    unsigned short v;
    unsigned int color;
    float x;
    float y;
    float z;
} WorldVertex;

typedef struct
{
    float x;
    float y;
    float z;
} MeshPoint;

static unsigned short rgb565(
    int r,
    int g,
    int b
)
{
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    return (unsigned short)(
        ((r & 0xf8) << 8) |
        ((g & 0xfc) << 3) |
        ((b & 0xf8) >> 3)
    );
}

static int tex_noise(
    int x,
    int y,
    int seed
)
{
    unsigned int n =
        (unsigned int)(x * 73856093) ^
        (unsigned int)(y * 19349663) ^
        (unsigned int)(seed * 83492791);

    n ^= n >> 13;
    n *= 0x5bd1e995U;
    n ^= n >> 15;

    return (int)(n & 31U) - 15;
}

static void world_textures_init(void)
{
    int y;
    int x;

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 1);
            int r = 68 + n;
            int g = 105 + n;
            int b = 56 + n;

            if (((x + y) % 11) == 0)
            {
                r += 8;
                g += 12;
            }

            tex_grass[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 2);
            int r = 48 + n;
            int g = 50 + n;
            int b = 52 + n;

            if (((x * 3 + y * 5) & 15) == 0)
            {
                r += 12;
                g += 12;
                b += 12;
            }

            tex_asphalt[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 3);
            int mortar =
                ((x % 8) == 0) ||
                ((y % 8) == 0);

            int r = 148 + n;
            int g = 145 + n;
            int b = 137 + n;

            if (mortar)
            {
                r -= 18;
                g -= 16;
                b -= 12;
            }

            tex_sidewalk[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 4);
            int line = ((y % 10) == 0);

            int r = 190 + n;
            int g = 173 + n;
            int b = 153 + n;

            if (line)
            {
                r -= 16;
                g -= 16;
                b -= 14;
            }

            tex_plaster[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 5);
            int mortar =
                ((y % 8) == 0) ||
                (((x + ((y / 8) & 1) * 8) % 16) == 0);

            int r = 132 + n;
            int g = 73 + n;
            int b = 55 + n;

            if (mortar)
            {
                r = 175 + n / 2;
                g = 145 + n / 2;
                b = 121 + n / 2;
            }

            tex_brick[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 6);
            int in_window =
                ((x >= 5 && x <= 11) ||
                 (x >= 20 && x <= 26)) &&
                ((y % 9) >= 2 && (y % 9) <= 6);

            int r = 188 + n;
            int g = 169 + n;
            int b = 148 + n;

            if (in_window)
            {
                r = 53 + n;
                g = 88 + n;
                b = 104 + n;

                if ((x & 3) == 0)
                {
                    r += 18;
                    g += 20;
                    b += 22;
                }
            }

            if ((y % 9) == 0)
            {
                r -= 14;
                g -= 12;
                b -= 10;
            }

            tex_facade[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 7);
            int tile = ((x + y * 2) % 7) == 0;

            int r = 74 + n;
            int g = 59 + n;
            int b = 52 + n;

            if (tile)
            {
                r += 18;
                g += 14;
                b += 12;
            }

            tex_roof[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 8);
            int r = 47 + n;
            int g = 78 + n;
            int b = 95 + n;

            if (((x + y) % 13) == 0)
            {
                r += 45;
                g += 42;
                b += 36;
            }

            tex_glass[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 9);
            int wave =
                ((y + (x / 4)) % 7) == 0;

            int r = 38 + n;
            int g = 92 + n;
            int b = 126 + n;

            if (wave)
            {
                r += 16;
                g += 24;
                b += 28;
            }

            tex_water[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 10);
            int r = 105 + n;
            int g = 110 + n;
            int b = 116 + n;

            if (((x ^ y) & 7) == 0)
            {
                r += 16;
                g += 16;
                b += 16;
            }

            tex_metal[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 11);
            int r = 72 + n;
            int g = 78 + n;
            int b = 88 + n;

            if ((y % 6) == 0)
            {
                r += 12;
                g += 10;
                b += 9;
            }

            tex_fabric[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 12);
            int r = 186 + n;
            int g = 132 + n;
            int b = 104 + n;

            if (((x + y) % 9) == 0)
            {
                r += 12;
                g += 7;
                b += 5;
            }

            tex_skin[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    for (y = 0; y < WORLD_TEX_SIZE; ++y)
    {
        for (x = 0; x < WORLD_TEX_SIZE; ++x)
        {
            int n = tex_noise(x, y, 13);
            int r = 27 + n / 3;
            int g = 29 + n / 3;
            int b = 30 + n / 3;

            tex_tire[y * WORLD_TEX_SIZE + x] =
                rgb565(r, g, b);
        }
    }

    sceKernelDcacheWritebackAll();
}

static unsigned int multiply_color(
    unsigned int a,
    unsigned int b
)
{
    unsigned int ar = (a >> 16) & 255U;
    unsigned int ag = (a >> 8) & 255U;
    unsigned int ab = a & 255U;
    unsigned int br = (b >> 16) & 255U;
    unsigned int bg = (b >> 8) & 255U;
    unsigned int bb = b & 255U;

    ar = (ar * br) / 255U;
    ag = (ag * bg) / 255U;
    ab = (ab * bb) / 255U;

    return 0xff000000U |
           (ar << 16) |
           (ag << 8) |
           ab;
}

static void bind_world_texture(
    const void *texture
)
{
    sceGuTexMode(
        GU_PSM_5650,
        0,
        0,
        GU_FALSE
    );

    sceGuTexImage(
        0,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        texture
    );

    sceGuTexFunc(
        GU_TFX_MODULATE,
        GU_TCC_RGB
    );

    sceGuTexWrap(
        GU_REPEAT,
        GU_REPEAT
    );

    sceGuTexFilter(
        GU_LINEAR,
        GU_LINEAR
    );

    sceGuEnable(
        GU_TEXTURE_2D
    );
}

static void unbind_world_texture(void)
{
    sceGuDisable(
        GU_TEXTURE_2D
    );
}

static void draw_textured_quad(
    const MeshPoint *p0,
    const MeshPoint *p1,
    const MeshPoint *p2,
    const MeshPoint *p3,
    unsigned short u0,
    unsigned short v0,
    unsigned short u1,
    unsigned short v1,
    unsigned int color
)
{
    WorldVertex v[4];

    v[0].u = u0;
    v[0].v = v0;
    v[0].color = color;
    v[0].x = p0->x;
    v[0].y = p0->y;
    v[0].z = p0->z;

    v[1].u = u1;
    v[1].v = v0;
    v[1].color = color;
    v[1].x = p1->x;
    v[1].y = p1->y;
    v[1].z = p1->z;

    v[2].u = u0;
    v[2].v = v1;
    v[2].color = color;
    v[2].x = p2->x;
    v[2].y = p2->y;
    v[2].z = p2->z;

    v[3].u = u1;
    v[3].v = v1;
    v[3].color = color;
    v[3].x = p3->x;
    v[3].y = p3->y;
    v[3].z = p3->z;

    sceGuDrawArray(
        GU_TRIANGLE_STRIP,
        GU_TEXTURE_16BIT |
        GU_COLOR_8888 |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        4,
        NULL,
        v
    );
}

static void draw_textured_box(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
    float yaw,
    const void *texture,
    unsigned int tint
)
{
    WorldVertex v[4];
    float u = (float)WORLD_TEX_SIZE * 1.5f;
    float vv = (float)WORLD_TEX_SIZE * 1.5f;
    unsigned int shades[6];
    int face;

    shades[0] = multiply_color(tint, 0xffe1e1e1);
    shades[1] = multiply_color(tint, 0xffc4c4c4);
    shades[2] = multiply_color(tint, 0xffd1d1d1);
    shades[3] = multiply_color(tint, 0xffaeaeae);
    shades[4] = multiply_color(tint, 0xffffffff);
    shades[5] = multiply_color(tint, 0xff777777);

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        ScePspFVector3 scale;

        pos.x = x;
        pos.y = y;
        pos.z = z;

        scale.x = sx;
        scale.y = sy;
        scale.z = sz;

        sceGumTranslate(&pos);
        sceGumRotateY(yaw);
        sceGumScale(&scale);
    }

    bind_world_texture(texture);

    for (face = 0; face < 6; ++face)
    {
        float x0 = -0.5f;
        float x1 =  0.5f;
        float y0 = -0.5f;
        float y1 =  0.5f;
        float z0 = -0.5f;
        float z1 =  0.5f;
        unsigned short tu = (unsigned short)u;
        unsigned short tv = (unsigned short)vv;

        if (face == 0)
        {
            v[0] = (WorldVertex){0,0,shades[face],x0,y0,z0};
            v[1] = (WorldVertex){tu,0,shades[face],x1,y0,z0};
            v[2] = (WorldVertex){0,tv,shades[face],x0,y1,z0};
            v[3] = (WorldVertex){tu,tv,shades[face],x1,y1,z0};
        }
        else if (face == 1)
        {
            v[0] = (WorldVertex){0,0,shades[face],x1,y0,z1};
            v[1] = (WorldVertex){tu,0,shades[face],x0,y0,z1};
            v[2] = (WorldVertex){0,tv,shades[face],x1,y1,z1};
            v[3] = (WorldVertex){tu,tv,shades[face],x0,y1,z1};
        }
        else if (face == 2)
        {
            v[0] = (WorldVertex){0,0,shades[face],x0,y0,z1};
            v[1] = (WorldVertex){tu,0,shades[face],x0,y0,z0};
            v[2] = (WorldVertex){0,tv,shades[face],x0,y1,z1};
            v[3] = (WorldVertex){tu,tv,shades[face],x0,y1,z0};
        }
        else if (face == 3)
        {
            v[0] = (WorldVertex){0,0,shades[face],x1,y0,z0};
            v[1] = (WorldVertex){tu,0,shades[face],x1,y0,z1};
            v[2] = (WorldVertex){0,tv,shades[face],x1,y1,z0};
            v[3] = (WorldVertex){tu,tv,shades[face],x1,y1,z1};
        }
        else if (face == 4)
        {
            v[0] = (WorldVertex){0,0,shades[face],x0,y1,z0};
            v[1] = (WorldVertex){tu,0,shades[face],x1,y1,z0};
            v[2] = (WorldVertex){0,tv,shades[face],x0,y1,z1};
            v[3] = (WorldVertex){tu,tv,shades[face],x1,y1,z1};
        }
        else
        {
            v[0] = (WorldVertex){0,0,shades[face],x0,y0,z1};
            v[1] = (WorldVertex){tu,0,shades[face],x1,y0,z1};
            v[2] = (WorldVertex){0,tv,shades[face],x0,y0,z0};
            v[3] = (WorldVertex){tu,tv,shades[face],x1,y0,z0};
        }

        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT |
            GU_COLOR_8888 |
            GU_VERTEX_32BITF |
            GU_TRANSFORM_3D,
            4,
            NULL,
            v
        );
    }

    unbind_world_texture();

    sceGumPopMatrix();
}

static void draw_shape8(
    const MeshPoint p[8],
    const int faces[6][4],
    const void *texture,
    unsigned int tint
)
{
    WorldVertex v[4];
    int f;

    bind_world_texture(texture);

    for (f = 0; f < 6; ++f)
    {
        const MeshPoint *a = &p[faces[f][0]];
        const MeshPoint *b = &p[faces[f][1]];
        const MeshPoint *c = &p[faces[f][2]];
        const MeshPoint *d = &p[faces[f][3]];

        unsigned int face_color =
            multiply_color(
                tint,
                (f == 4) ? 0xffffffff :
                (f == 0) ? 0xffe0e0e0 :
                (f == 1) ? 0xffbcbcbc :
                (f == 2) ? 0xffc8c8c8 :
                (f == 3) ? 0xffaaaaaa :
                           0xff787878
            );

        v[0] = (WorldVertex){0,0,face_color,a->x,a->y,a->z};
        v[1] = (WorldVertex){WORLD_TEX_SIZE,0,face_color,b->x,b->y,b->z};
        v[2] = (WorldVertex){0,WORLD_TEX_SIZE,face_color,c->x,c->y,c->z};
        v[3] = (WorldVertex){WORLD_TEX_SIZE,WORLD_TEX_SIZE,face_color,d->x,d->y,d->z};

        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT |
            GU_COLOR_8888 |
            GU_VERTEX_32BITF |
            GU_TRANSFORM_3D,
            4,
            NULL,
            v
        );
    }

    unbind_world_texture();
}

static void draw_horizontal_plane(
    float x,
    float y,
    float z,
    float sx,
    float sz,
    const void *texture,
    unsigned short repeat_u,
    unsigned short repeat_v,
    unsigned int tint
)
{
    MeshPoint a;
    MeshPoint b;
    MeshPoint c;
    MeshPoint d;

    a.x = x - sx * 0.5f;
    a.y = y;
    a.z = z - sz * 0.5f;

    b.x = x + sx * 0.5f;
    b.y = y;
    b.z = z - sz * 0.5f;

    c.x = x - sx * 0.5f;
    c.y = y;
    c.z = z + sz * 0.5f;

    d.x = x + sx * 0.5f;
    d.y = y;
    d.z = z + sz * 0.5f;

    bind_world_texture(texture);
    draw_textured_quad(
        &a, &b, &c, &d,
        0, 0,
        repeat_u,
        repeat_v,
        tint
    );
    unbind_world_texture();
}

static void draw_vertical_plane(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float yaw,
    const void *texture,
    unsigned short repeat_u,
    unsigned short repeat_v,
    unsigned int tint
)
{
    MeshPoint p0;
    MeshPoint p1;
    MeshPoint p2;
    MeshPoint p3;
    float cx = cosf(yaw);
    float sz = sinf(yaw);
    float hx = sx * 0.5f;
    float xz = -sz * hx;
    float zz =  cx * hx;

    p0.x = x - xz;
    p0.y = y;
    p0.z = z - zz;

    p1.x = x + xz;
    p1.y = y;
    p1.z = z + zz;

    p2.x = p0.x;
    p2.y = y + sy;
    p2.z = p0.z;

    p3.x = p1.x;
    p3.y = y + sy;
    p3.z = p1.z;

    bind_world_texture(texture);
    draw_textured_quad(
        &p0, &p1, &p2, &p3,
        0, 0,
        repeat_u,
        repeat_v,
        tint
    );
    unbind_world_texture();
}

static void draw_shadow(
    float x,
    float z,
    float sx,
    float sz
)
{
    typedef struct
    {
        unsigned int color;
        float x;
        float y;
        float z;
    } ShadowVertex;

    ShadowVertex v[10];
    int i;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuColor(0x56000000U);

    v[0].color = 0x56000000U;
    v[0].x = x;
    v[0].y = 0.018f;
    v[0].z = z;

    for (i = 0; i < 9; ++i)
    {
        float a =
            ((float)i / 8.0f) * 2.0f * PI_F;

        v[i + 1].color = 0x56000000U;
        v[i + 1].x = x + cosf(a) * sx;
        v[i + 1].y = 0.018f;
        v[i + 1].z = z + sinf(a) * sz;
    }

    sceGuDrawArray(
        GU_TRIANGLE_FAN,
        GU_COLOR_8888 |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        10,
        NULL,
        v
    );
}

static void draw_cylinder(
    float x,
    float y,
    float z,
    float radius,
    float height,
    float yaw,
    float rot_x,
    const void *texture,
    unsigned int tint
)
{
    WorldVertex v[18];
    int i;

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        pos.x = x;
        pos.y = y;
        pos.z = z;

        sceGumTranslate(&pos);
        sceGumRotateY(yaw);
        sceGumRotateX(rot_x);
    }

    bind_world_texture(texture);

    for (i = 0; i < 8; ++i)
    {
        float a0 =
            ((float)i / 8.0f) * 2.0f * PI_F;

        float a1 =
            ((float)(i + 1) / 8.0f) * 2.0f * PI_F;

        unsigned int c0 =
            multiply_color(
                tint,
                (i & 1) ?
                    0xffbdbdbd :
                    0xffe0e0e0
            );

        v[0] = (WorldVertex){
            0, 0, c0,
            cosf(a0) * radius,
            0.0f,
            sinf(a0) * radius
        };

        v[1] = (WorldVertex){
            WORLD_TEX_SIZE / 4, 0, c0,
            cosf(a1) * radius,
            0.0f,
            sinf(a1) * radius
        };

        v[2] = (WorldVertex){
            0, WORLD_TEX_SIZE, c0,
            cosf(a0) * radius,
            height,
            sinf(a0) * radius
        };

        v[3] = (WorldVertex){
            WORLD_TEX_SIZE / 4, WORLD_TEX_SIZE, c0,
            cosf(a1) * radius,
            height,
            sinf(a1) * radius
        };

        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT |
            GU_COLOR_8888 |
            GU_VERTEX_32BITF |
            GU_TRANSFORM_3D,
            4,
            NULL,
            v
        );
    }

    unbind_world_texture();
    sceGumPopMatrix();
}

static void draw_cone(
    float x,
    float y,
    float z,
    float radius,
    float height,
    float yaw,
    const void *texture,
    unsigned int tint
)
{
    WorldVertex v[3];
    int i;

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        pos.x = x;
        pos.y = y;
        pos.z = z;
        sceGumTranslate(&pos);
        sceGumRotateY(yaw);
    }

    bind_world_texture(texture);

    for (i = 0; i < 8; ++i)
    {
        float a0 =
            ((float)i / 8.0f) * 2.0f * PI_F;

        float a1 =
            ((float)(i + 1) / 8.0f) * 2.0f * PI_F;

        v[0] = (WorldVertex){
            0, 0, tint,
            cosf(a0) * radius, 0.0f, sinf(a0) * radius
        };

        v[1] = (WorldVertex){
            WORLD_TEX_SIZE, 0, tint,
            cosf(a1) * radius, 0.0f, sinf(a1) * radius
        };

        v[2] = (WorldVertex){
            WORLD_TEX_SIZE / 2, WORLD_TEX_SIZE, tint,
            0.0f, height, 0.0f
        };

        sceGuDrawArray(
            GU_TRIANGLES,
            GU_TEXTURE_16BIT |
            GU_COLOR_8888 |
            GU_VERTEX_32BITF |
            GU_TRANSFORM_3D,
            3,
            NULL,
            v
        );
    }

    unbind_world_texture();
    sceGumPopMatrix();
}

static void draw_lowpoly_sphere(
    float x,
    float y,
    float z,
    float radius,
    float sx,
    float sy,
    float sz,
    const void *texture,
    unsigned int tint
)
{
    int ring;
    int seg;

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        ScePspFVector3 scale;

        pos.x = x;
        pos.y = y;
        pos.z = z;

        scale.x = sx;
        scale.y = sy;
        scale.z = sz;

        sceGumTranslate(&pos);
        sceGumScale(&scale);
    }

    bind_world_texture(texture);

    for (ring = 0; ring < 4; ++ring)
    {
        float p0 =
            -PI_F * 0.5f +
            ((float)ring / 4.0f) * PI_F;

        float p1 =
            -PI_F * 0.5f +
            ((float)(ring + 1) / 4.0f) * PI_F;

        WorldVertex v[18];

        for (seg = 0; seg <= 8; ++seg)
        {
            float a =
                ((float)seg / 8.0f) * 2.0f * PI_F;

            float cp0 = cosf(p0);
            float cp1 = cosf(p1);
            float sp0 = sinf(p0);
            float sp1 = sinf(p1);

            v[seg * 2] = (WorldVertex){
                (unsigned short)((seg * WORLD_TEX_SIZE) / 8),
                (unsigned short)(ring * (WORLD_TEX_SIZE / 4)),
                tint,
                cosf(a) * cp0 * radius,
                (sp0 + 1.0f) * 0.5f * radius * 2.0f,
                sinf(a) * cp0 * radius
            };

            v[seg * 2 + 1] = (WorldVertex){
                (unsigned short)((seg * WORLD_TEX_SIZE) / 8),
                (unsigned short)((ring + 1) * (WORLD_TEX_SIZE / 4)),
                tint,
                cosf(a) * cp1 * radius,
                (sp1 + 1.0f) * 0.5f * radius * 2.0f,
                sinf(a) * cp1 * radius
            };
        }

        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT |
            GU_COLOR_8888 |
            GU_VERTEX_32BITF |
            GU_TRANSFORM_3D,
            18,
            NULL,
            v
        );
    }

    unbind_world_texture();
    sceGumPopMatrix();
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
        transition_alpha += 12;

        if (transition_alpha >= 255)
        {
            transition_alpha = 255;

            state = transition_target;

            if (state == GAME_STATE_SAVING)
                saving_timer = 0;

            if (state == GAME_STATE_SAVE_FINISHED)
                save_finished_timer = 0;

            if (state == GAME_STATE_STORY_INTRO)
            {
                /*
                 * Story intro deliberately starts silent.
                 */
                music_stop();

                intro_frame = 0;
                intro_phase = 0;

                intro_van_x = -42.0f;
                intro_van_z = 8.0f;

                cousin_door_open = 0;
            }
            else if (state == GAME_STATE_MAIN_MENU ||
                     state == GAME_STATE_STORY ||
                     state == GAME_STATE_FREE_WORLD ||
                     state == GAME_STATE_MULTIPLAYER)
            {
                music_start();
            }
            else
            {
                music_stop();
            }

            transition_phase = 1;
        }
    }
    else
    {
        transition_alpha -= 8;

        if (transition_alpha <= 0)
        {
            transition_alpha = 0;
            transition_active = 0;
        }
    }
}

static void render_fade(void)
{
    if (!transition_active && transition_alpha == 0)
        return;

    draw_rect_2d(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        ((unsigned int)transition_alpha << 24)
    );
}


/* ------------------------------------------------------------------------- */
/* 3D world                                                                  */
/* ------------------------------------------------------------------------- */

static void draw_pitched_roof(
    float x,
    float y,
    float z,
    float w,
    float d,
    float roof_h,
    float yaw,
    unsigned int tint
)
{
    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        ScePspFVector3 scale;

        pos.x = x;
        pos.y = y;
        pos.z = z;

        scale.x = w;
        scale.y = 1.0f;
        scale.z = d;

        sceGumTranslate(&pos);
        sceGumRotateY(yaw);
        sceGumScale(&scale);
    }

    bind_world_texture(tex_roof);

    {
        WorldVertex v[4];
        unsigned int c1 = multiply_color(tint, 0xffc5c5c5);
        unsigned int c2 = multiply_color(tint, 0xffa8a8a8);
        unsigned int c3 = multiply_color(tint, 0xff919191);
        unsigned int c4 = multiply_color(tint, 0xffb0b0b0);

        /* Left roof slope. */
        v[0] = (WorldVertex){0,0,c1,-0.5f,0.0f,-0.5f};
        v[1] = (WorldVertex){WORLD_TEX_SIZE,0,c2,0.0f,roof_h,-0.5f};
        v[2] = (WorldVertex){0,WORLD_TEX_SIZE,c1,-0.5f,0.0f,0.5f};
        v[3] = (WorldVertex){WORLD_TEX_SIZE,WORLD_TEX_SIZE,c2,0.0f,roof_h,0.5f};
        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            4, NULL, v
        );

        /* Right roof slope. */
        v[0] = (WorldVertex){0,0,c2,0.0f,roof_h,-0.5f};
        v[1] = (WorldVertex){WORLD_TEX_SIZE,0,c3,0.5f,0.0f,-0.5f};
        v[2] = (WorldVertex){0,WORLD_TEX_SIZE,c2,0.0f,roof_h,0.5f};
        v[3] = (WorldVertex){WORLD_TEX_SIZE,WORLD_TEX_SIZE,c3,0.5f,0.0f,0.5f};
        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            4, NULL, v
        );

        /* Front triangular gable. */
        v[0] = (WorldVertex){0,0,c1,-0.5f,0.0f,-0.5f};
        v[1] = (WorldVertex){WORLD_TEX_SIZE,0,c1,0.5f,0.0f,-0.5f};
        v[2] = (WorldVertex){WORLD_TEX_SIZE/2,WORLD_TEX_SIZE,c4,0.0f,roof_h,-0.5f};
        sceGuDrawArray(
            GU_TRIANGLES,
            GU_TEXTURE_16BIT | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            3, NULL, v
        );

        /* Rear triangular gable. */
        v[0] = (WorldVertex){0,0,c4,0.5f,0.0f,0.5f};
        v[1] = (WorldVertex){WORLD_TEX_SIZE,0,c4,-0.5f,0.0f,0.5f};
        v[2] = (WorldVertex){WORLD_TEX_SIZE/2,WORLD_TEX_SIZE,c4,0.0f,roof_h,0.5f};
        sceGuDrawArray(
            GU_TRIANGLES,
            GU_TEXTURE_16BIT | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            3, NULL, v
        );
    }

    unbind_world_texture();
    sceGumPopMatrix();
}

static void draw_building(
    const Building *b,
    int index
)
{
    const void *wall_texture;
    unsigned int tint = b->wall;
    float front_z;
    float roof_height;

    if ((index % 3) == 1)
        wall_texture = tex_brick;
    else
        wall_texture = tex_plaster;

    draw_shadow(
        b->x + 1.5f,
        b->z + 1.5f,
        b->w * 0.42f,
        b->d * 0.38f
    );

    draw_textured_box(
        b->x,
        b->h * 0.5f,
        b->z,
        b->w,
        b->h,
        b->d,
        0.0f,
        wall_texture,
        tint
    );

    /*
     * A full facade panel gives the building a real repeated window pattern
     * rather than a single flat colour.
     */
    front_z = b->z - b->d * 0.505f;

    draw_vertical_plane(
        b->x,
        0.55f,
        front_z,
        b->w * 0.88f,
        b->h * 0.78f,
        0.0f,
        tex_facade,
        (unsigned short)(WORLD_TEX_SIZE * 2),
        (unsigned short)(WORLD_TEX_SIZE * 2),
        multiply_color(
            tint,
            0xfff4f4f4
        )
    );

    /*
     * Main entrance.
     */
    draw_vertical_plane(
        b->x - b->w * 0.25f,
        0.05f,
        front_z - 0.015f,
        b->w * 0.11f,
        b->h * 0.30f,
        0.0f,
        tex_glass,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xff7a6e61
    );

    /*
     * Small awning above the entrance.
     */
    draw_textured_box(
        b->x - b->w * 0.25f,
        b->h * 0.35f,
        front_z - 0.38f,
        b->w * 0.22f,
        0.22f,
        0.60f,
        0.0f,
        tex_roof,
        0xffb4b0aa
    );

    roof_height = 1.5f + (float)((index * 3) % 4) * 0.22f;

    draw_pitched_roof(
        b->x,
        b->h,
        b->z,
        b->w * 1.06f,
        b->d * 1.06f,
        roof_height,
        0.0f,
        b->roof
    );

    /*
     * A few buildings get a small rooftop chimney for extra silhouette
     * detail.
     */
    if ((index % 4) == 0)
    {
        draw_textured_box(
            b->x + b->w * 0.20f,
            b->h + 0.9f,
            b->z,
            0.55f,
            1.6f,
            0.55f,
            0.0f,
            tex_brick,
            0xff9f9a94
        );
    }
}

static void draw_tree(
    float x,
    float z,
    float scale_factor
)
{
    draw_shadow(
        x + 0.4f,
        z + 0.35f,
        1.15f * scale_factor,
        0.85f * scale_factor
    );

    draw_cylinder(
        x,
        0.0f,
        z,
        0.34f * scale_factor,
        3.2f * scale_factor,
        0.0f,
        0.0f,
        tex_brick,
        0xff9a704f
    );

    draw_cone(
        x,
        2.8f * scale_factor,
        z,
        2.15f * scale_factor,
        3.2f * scale_factor,
        0.0f,
        tex_grass,
        0xffa9c58f
    );

    draw_cone(
        x,
        4.35f * scale_factor,
        z,
        1.65f * scale_factor,
        2.7f * scale_factor,
        0.35f,
        tex_grass,
        0xff87ad75
    );
}

static void draw_park(
    float x,
    float z,
    float sx,
    float sz
)
{
    draw_horizontal_plane(
        x,
        0.01f,
        z,
        sx,
        sz,
        tex_grass,
        (unsigned short)(WORLD_TEX_SIZE * 3),
        (unsigned short)(WORLD_TEX_SIZE * 3),
        0xffffffff
    );

    draw_horizontal_plane(
        x,
        0.022f,
        z,
        sx * 0.46f,
        1.6f,
        tex_sidewalk,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xffd8d2c5
    );

    draw_tree(x - sx * 0.28f, z - sz * 0.20f, 0.9f);
    draw_tree(x + sx * 0.26f, z - sz * 0.22f, 1.05f);
    draw_tree(x - sx * 0.22f, z + sz * 0.25f, 0.84f);
    draw_tree(x + sx * 0.24f, z + sz * 0.22f, 0.95f);
}

static void draw_road_network(void)
{
    int i;
    float dash;

    /*
     * Main east-west and north-south roads.
     */
    draw_horizontal_plane(
        0.0f, 0.01f, 0.0f,
        190.0f, 12.0f,
        tex_asphalt,
        WORLD_TEX_SIZE * 10,
        WORLD_TEX_SIZE,
        0xffeeeeee
    );

    draw_horizontal_plane(
        0.0f, 0.012f, 0.0f,
        12.0f, 190.0f,
        tex_asphalt,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE * 10,
        0xffeeeeee
    );

    /*
     * Secondary ring roads.
     */
    draw_horizontal_plane(
        0.0f, 0.011f, -50.0f,
        190.0f, 9.0f,
        tex_asphalt,
        WORLD_TEX_SIZE * 10,
        WORLD_TEX_SIZE,
        0xffeeeeee
    );

    draw_horizontal_plane(
        0.0f, 0.011f, 50.0f,
        190.0f, 9.0f,
        tex_asphalt,
        WORLD_TEX_SIZE * 10,
        WORLD_TEX_SIZE,
        0xffeeeeee
    );

    draw_horizontal_plane(
        -50.0f, 0.012f, 0.0f,
        9.0f, 190.0f,
        tex_asphalt,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE * 10,
        0xffeeeeee
    );

    draw_horizontal_plane(
        50.0f, 0.012f, 0.0f,
        9.0f, 190.0f,
        tex_asphalt,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE * 10,
        0xffeeeeee
    );

    /*
     * Pavement strips.
     */
    draw_horizontal_plane(
        0.0f, 0.022f, -7.25f,
        190.0f, 1.45f,
        tex_sidewalk,
        WORLD_TEX_SIZE * 12,
        WORLD_TEX_SIZE,
        0xffffffff
    );

    draw_horizontal_plane(
        0.0f, 0.022f, 7.25f,
        190.0f, 1.45f,
        tex_sidewalk,
        WORLD_TEX_SIZE * 12,
        WORLD_TEX_SIZE,
        0xffffffff
    );

    draw_horizontal_plane(
        -7.25f, 0.023f, 0.0f,
        1.45f, 190.0f,
        tex_sidewalk,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE * 12,
        0xffffffff
    );

    draw_horizontal_plane(
        7.25f, 0.023f, 0.0f,
        1.45f, 190.0f,
        tex_sidewalk,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE * 12,
        0xffffffff
    );

    /*
     * Central broken line. Small planes keep the road readable at PSP
     * resolution without adding hundreds of polygons.
     */
    for (i = -7; i <= 7; ++i)
    {
        dash = (float)i * 12.0f;

        draw_horizontal_plane(
            dash,
            0.036f,
            0.0f,
            5.0f,
            0.14f,
            tex_sidewalk,
            WORLD_TEX_SIZE,
            WORLD_TEX_SIZE,
            0xffeee7cf
        );

        draw_horizontal_plane(
            0.0f,
            0.036f,
            dash,
            0.14f,
            5.0f,
            tex_sidewalk,
            WORLD_TEX_SIZE,
            WORLD_TEX_SIZE,
            0xffeee7cf
        );
    }
}

static void draw_streetlight(
    float x,
    float z,
    float yaw
)
{
    draw_cylinder(
        x,
        0.0f,
        z,
        0.08f,
        4.2f,
        yaw,
        0.0f,
        tex_metal,
        0xffb8bec0
    );

    draw_textured_box(
        x + cosf(yaw) * 0.55f,
        4.05f,
        z + sinf(yaw) * 0.55f,
        0.95f,
        0.12f,
        0.12f,
        yaw,
        tex_metal,
        0xffc6c7bd
    );

    draw_textured_box(
        x + cosf(yaw) * 0.97f,
        3.86f,
        z + sinf(yaw) * 0.97f,
        0.28f,
        0.26f,
        0.28f,
        yaw,
        tex_glass,
        0xffffd28c
    );
}

static void draw_lowhill(
    float x,
    float z,
    float radius,
    float height
)
{
    draw_cone(
        x,
        0.0f,
        z,
        radius,
        height,
        0.0f,
        tex_grass,
        0xff8faf78
    );
}

static void draw_bridge(void)
{
    float z = 82.0f;

    draw_horizontal_plane(
        0.0f,
        0.02f,
        z,
        42.0f,
        12.0f,
        tex_asphalt,
        WORLD_TEX_SIZE * 3,
        WORLD_TEX_SIZE,
        0xffeeeeee
    );

    draw_horizontal_plane(
        0.0f,
        2.45f,
        z + 7.0f,
        28.0f,
        18.0f,
        tex_asphalt,
        WORLD_TEX_SIZE * 2,
        WORLD_TEX_SIZE * 2,
        0xffeeeeee
    );

    draw_textured_box(
        -13.0f,
        3.65f,
        z + 7.0f,
        0.28f,
        2.2f,
        18.0f,
        0.0f,
        tex_metal,
        0xffd6d9db
    );

    draw_textured_box(
        13.0f,
        3.65f,
        z + 7.0f,
        0.28f,
        2.2f,
        18.0f,
        0.0f,
        tex_metal,
        0xffd6d9db
    );

    draw_horizontal_plane(
        0.0f,
        -0.26f,
        z + 7.0f,
        40.0f,
        32.0f,
        tex_water,
        WORLD_TEX_SIZE * 8,
        WORLD_TEX_SIZE * 7,
        0xffffffff
    );

    draw_lowhill(-68.0f, 78.0f, 16.0f, 7.0f);
    draw_lowhill( 68.0f, 80.0f, 18.0f, 8.0f);
}

static void draw_wheel(
    float x,
    float y,
    float z,
    float yaw,
    float side_x,
    float axle_z,
    float radius,
    float width
)
{
    WorldVertex v[18];
    float local_x = side_x;
    float local_z = axle_z;
    float world_x =
        x + cosf(yaw) * local_x -
        sinf(yaw) * local_z;
    float world_z =
        z + sinf(yaw) * local_x +
        cosf(yaw) * local_z;
    int i;

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;

        pos.x = world_x;
        pos.y = y;
        pos.z = world_z;

        sceGumTranslate(&pos);

        /*
         * Cylinder starts along local +Y.
         * Rotate it onto the axle (X), then spin around that axle.
         */
        sceGumRotateY(yaw);
        sceGumRotateZ(PI_F * 0.5f);
        sceGumRotateX(wheel_spin);
    }

    bind_world_texture(tex_tire);

    for (i = 0; i < 8; ++i)
    {
        float a0 =
            ((float)i / 8.0f) * 2.0f * PI_F;

        float a1 =
            ((float)(i + 1) / 8.0f) * 2.0f * PI_F;

        v[0] = (WorldVertex){
            0, 0, 0xffffffffU,
            cosf(a0) * radius,
            -width * 0.5f,
            sinf(a0) * radius
        };

        v[1] = (WorldVertex){
            WORLD_TEX_SIZE, 0, 0xffffffffU,
            cosf(a1) * radius,
            -width * 0.5f,
            sinf(a1) * radius
        };

        v[2] = (WorldVertex){
            0, WORLD_TEX_SIZE, 0xffd6d6d6U,
            cosf(a0) * radius,
            width * 0.5f,
            sinf(a0) * radius
        };

        v[3] = (WorldVertex){
            WORLD_TEX_SIZE, WORLD_TEX_SIZE, 0xffd6d6d6U,
            cosf(a1) * radius,
            width * 0.5f,
            sinf(a1) * radius
        };

        sceGuDrawArray(
            GU_TRIANGLE_STRIP,
            GU_TEXTURE_16BIT | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            4, NULL, v
        );
    }

    unbind_world_texture();
    sceGumPopMatrix();
}

static void draw_car_model(
    float x,
    float z,
    float yaw,
    unsigned int color,
    int van_style
)
{
    MeshPoint body[8];
    static const int body_faces[6][4] =
    {
        {0,1,5,4},
        {1,3,7,5},
        {3,2,6,7},
        {2,0,4,6},
        {4,5,7,6},
        {0,2,3,1}
    };

    float s = van_style ? 1.22f : 1.0f;
    float body_w = 2.55f * s;
    float body_l = 3.55f * s;

    draw_shadow(
        x + 0.2f,
        z + 0.22f,
        2.05f * s,
        1.35f * s
    );

    /*
     * Faceted lower shell, wider at the centre than at the ends.
     */
    body[0] = (MeshPoint){
        -body_w * 0.45f,
        0.28f,
        -body_l * 0.50f
    };
    body[1] = (MeshPoint){
        body_w * 0.45f,
        0.28f,
        -body_l * 0.50f
    };
    body[2] = (MeshPoint){
        -body_w * 0.47f,
        0.28f,
        body_l * 0.50f
    };
    body[3] = (MeshPoint){
        body_w * 0.47f,
        0.28f,
        body_l * 0.50f
    };
    body[4] = (MeshPoint){
        -body_w * 0.50f,
        1.05f * s,
        -body_l * 0.43f
    };
    body[5] = (MeshPoint){
        body_w * 0.50f,
        1.05f * s,
        -body_l * 0.43f
    };
    body[6] = (MeshPoint){
        -body_w * 0.48f,
        0.92f * s,
        body_l * 0.42f
    };
    body[7] = (MeshPoint){
        body_w * 0.48f,
        0.92f * s,
        body_l * 0.42f
    };

    sceGumPushMatrix();

    {
        ScePspFVector3 pos;
        pos.x = x;
        pos.y = 0.0f;
        pos.z = z;
        sceGumTranslate(&pos);
        sceGumRotateY(yaw);
    }

    draw_shape8(
        body,
        body_faces,
        tex_metal,
        color
    );

    /*
     * Cabin: a smaller trapezoid with sloped front/rear pillars.
     */
    {
        MeshPoint cabin[8];
        static const int cabin_faces[6][4] =
        {
            {0,1,5,4},
            {1,3,7,5},
            {3,2,6,7},
            {2,0,4,6},
            {4,5,7,6},
            {0,2,3,1}
        };

        float cw = 1.72f * s;
        float cl = 1.95f * s;
        float ch = van_style ? 1.85f : 1.62f;

        cabin[0] = (MeshPoint){-cw * 0.5f,1.00f,-cl * 0.50f};
        cabin[1] = (MeshPoint){ cw * 0.5f,1.00f,-cl * 0.50f};
        cabin[2] = (MeshPoint){-cw * 0.5f,1.00f, cl * 0.50f};
        cabin[3] = (MeshPoint){ cw * 0.5f,1.00f, cl * 0.50f};

        cabin[4] = (MeshPoint){-cw * 0.40f,ch,-cl * 0.32f};
        cabin[5] = (MeshPoint){ cw * 0.40f,ch,-cl * 0.32f};
        cabin[6] = (MeshPoint){-cw * 0.40f,ch, cl * 0.28f};
        cabin[7] = (MeshPoint){ cw * 0.40f,ch, cl * 0.28f};

        draw_shape8(
            cabin,
            cabin_faces,
            tex_glass,
            van_style
                ? 0xffaebdc0
                : 0xffb7c6c8
        );

        /*
         * Roof panel retains body colour so the vehicle reads as painted
         * metal + glass rather than as a single dark block.
         */
        draw_textured_box(
            x,
            ch + 0.05f,
            z,
            cw * 0.78f,
            0.10f,
            cl * 0.62f,
            yaw,
            tex_metal,
            color
        );
    }

    sceGumPopMatrix();

    /*
     * Lamps.
     */
    draw_vertical_plane(
        x - 0.75f * s,
        0.58f,
        z - 1.81f * s,
        0.38f * s,
        0.22f * s,
        yaw,
        tex_glass,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xfffff3bf
    );

    draw_vertical_plane(
        x + 0.75f * s,
        0.58f,
        z - 1.81f * s,
        0.38f * s,
        0.22f * s,
        yaw,
        tex_glass,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xfffff3bf
    );

    draw_vertical_plane(
        x - 0.65f * s,
        0.58f,
        z + 1.81f * s,
        0.34f * s,
        0.18f * s,
        yaw,
        tex_glass,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xffcf5d54
    );

    draw_vertical_plane(
        x + 0.65f * s,
        0.58f,
        z + 1.81f * s,
        0.34f * s,
        0.18f * s,
        yaw,
        tex_glass,
        WORLD_TEX_SIZE,
        WORLD_TEX_SIZE,
        0xffcf5d54
    );

    /*
     * Four detailed low-poly wheels.
     */
    draw_wheel(x, 0.47f, z, yaw, -1.25f*s, -1.12f*s, 0.46f*s, 0.28f*s);
    draw_wheel(x, 0.47f, z, yaw,  1.25f*s, -1.12f*s, 0.46f*s, 0.28f*s);
    draw_wheel(x, 0.47f, z, yaw, -1.25f*s,  1.12f*s, 0.46f*s, 0.28f*s);
    draw_wheel(x, 0.47f, z, yaw,  1.25f*s,  1.12f*s, 0.46f*s, 0.28f*s);
}

static void draw_human_model(
    float x,
    float y,
    float z,
    float yaw,
    float scale_factor,
    unsigned int shirt
)
{
    float phase =
        character_anim +
        x * 0.15f +
        z * 0.11f;

    float swing =
        sinf(phase) * 0.38f;

    float leg_swing =
        -swing * 0.75f;

    float body_h = 1.05f * scale_factor;

    draw_shadow(
        x + 0.10f,
        z + 0.10f,
        0.55f * scale_factor,
        0.38f * scale_factor
    );

    /*
     * Legs.
     */
    {
        float sep = 0.17f * scale_factor;

        draw_cylinder(
            x - sep,
            y + 0.08f * scale_factor,
            z,
            0.12f * scale_factor,
            0.82f * scale_factor,
            yaw,
            leg_swing,
            tex_fabric,
            0xff5d6570
        );

        draw_cylinder(
            x + sep,
            y + 0.08f * scale_factor,
            z,
            0.12f * scale_factor,
            0.82f * scale_factor,
            yaw,
            -leg_swing,
            tex_fabric,
            0xff343a44
        );
    }

    /*
     * Torso.
     */
    draw_cylinder(
        x,
        y + 0.74f * scale_factor,
        z,
        0.42f * scale_factor,
        body_h,
        yaw,
        0.0f,
        tex_fabric,
        shirt
    );

    /*
     * Arms.
     */
    {
        float shoulder_y =
            y + 1.74f * scale_factor;

        draw_cylinder(
            x - 0.49f * scale_factor,
            shoulder_y,
            z,
            0.105f * scale_factor,
            0.72f * scale_factor,
            yaw,
            PI_F + swing,
            tex_skin,
            0xffd4a37d
        );

        draw_cylinder(
            x + 0.49f * scale_factor,
            shoulder_y,
            z,
            0.105f * scale_factor,
            0.72f * scale_factor,
            yaw,
            PI_F - swing,
            tex_skin,
            0xffd0a078
        );
    }

    /*
     * Head and hair cap.
     */
    draw_lowpoly_sphere(
        x,
        y + 1.98f * scale_factor,
        z,
        0.34f * scale_factor,
        1.0f,
        1.0f,
        1.0f,
        tex_skin,
        0xffffffff
    );

    draw_lowpoly_sphere(
        x,
        y + 2.13f * scale_factor,
        z - 0.02f * scale_factor,
        0.36f * scale_factor,
        1.0f,
        0.34f,
        1.0f,
        tex_fabric,
        0xff3c302b
    );
}

static void draw_human(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt
)
{
    draw_human_model(
        x, y, z, yaw,
        1.0f,
        shirt
    );
}

static void draw_child(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt
)
{
    draw_human_model(
        x, y, z, yaw,
        0.72f,
        shirt
    );
}

static void render_city_world(void)
{
    int i;

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);

    /*
     * Large grass base plus four calmer park zones.
     */
    draw_horizontal_plane(
        0.0f,
        -0.02f,
        0.0f,
        190.0f,
        190.0f,
        tex_grass,
        WORLD_TEX_SIZE * 12,
        WORLD_TEX_SIZE * 12,
        0xffffffff
    );

    draw_park(-65.0f, -24.0f, 24.0f, 20.0f);
    draw_park( 65.0f, -24.0f, 24.0f, 20.0f);
    draw_park(-65.0f,  26.0f, 24.0f, 20.0f);
    draw_park( 65.0f,  26.0f, 24.0f, 20.0f);

    draw_road_network();

    /*
     * Waterfront/river.
     */
    draw_horizontal_plane(
        0.0f,
        -0.05f,
        -94.0f,
        190.0f,
        18.0f,
        tex_water,
        WORLD_TEX_SIZE * 12,
        WORLD_TEX_SIZE * 2,
        0xffffffff
    );

    /*
     * Building shadows first, then buildings.
     */
    sceGuEnable(GU_BLEND);

    for (i = 0; i < BUILDING_COUNT; ++i)
        draw_building(&buildings[i], i);

    /*
     * Streetlights.
     */
    draw_streetlight(-26.0f, -6.2f, 0.0f);
    draw_streetlight( 26.0f,  6.2f, PI_F);
    draw_streetlight(-6.2f, -26.0f, PI_F * 0.5f);
    draw_streetlight( 6.2f,  26.0f, PI_F * 1.5f);

    /*
     * A few trees outside the parks create a less artificial skyline.
     */
    draw_tree(-58.0f, -61.0f, 1.10f);
    draw_tree(-29.0f, -61.0f, 0.82f);
    draw_tree( 30.0f, -61.0f, 1.02f);
    draw_tree( 61.0f, -60.0f, 0.90f);

    draw_tree(-59.0f, 60.0f, 0.92f);
    draw_tree(-30.0f, 62.0f, 1.12f);
    draw_tree( 29.0f, 61.0f, 0.88f);
    draw_tree( 61.0f, 60.0f, 1.06f);

    draw_bridge();

    /*
     * Cars.
     */
    sceGuDisable(GU_BLEND);

    for (i = 0; i < CAR_COUNT; ++i)
    {
        if (i == STORY_VAN_INDEX &&
            state == GAME_STATE_STORY_INTRO)
            continue;

        draw_car_model(
            cars[i].x,
            cars[i].z,
            cars[i].yaw,
            cars[i].body,
            i == STORY_VAN_INDEX
        );
    }

    /*
     * Pedestrians.
     */
    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
    {
        Pedestrian *p =
            &pedestrians[i];

        draw_human(
            p->x,
            0.0f,
            p->z,
            0.0f,
            p->shirt
        );
    }

    sceGuEnable(GU_BLEND);
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

static void set_first_person_camera(void)
{
    float fx;
    float fz;
    float eye_y;

    if (in_vehicle && current_vehicle >= 0)
    {
        CityCar *c = &cars[current_vehicle];

        fx = sinf(c->yaw);
        fz = -cosf(c->yaw);

        /* Driver/passenger eye position, just ahead of the car center. */
        set_3d_camera(
            c->x + fx * 0.18f,
            1.48f,
            c->z + fz * 0.18f,
            c->x + fx * 8.0f,
            1.42f,
            c->z + fz * 8.0f
        );
        return;
    }

    fx = sinf(player_yaw);
    fz = -cosf(player_yaw);

    eye_y = 1.58f + sinf(walk_bob) * 0.018f;

    set_3d_camera(
        player_x,
        eye_y,
        player_z,
        player_x + fx * 8.0f,
        eye_y + camera_pitch,
        player_z + fz * 8.0f
    );
}

static void update_first_person_look(
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

    sceGuBlendFunc(
        GU_ADD,
        GU_SRC_ALPHA,
        GU_ONE_MINUS_SRC_ALPHA,
        0,
        0
    );

    sceGuEnable(
        GU_BLEND
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
    sceCtrlSetSamplingCycle(0);

    sceCtrlSetSamplingMode(
        PSP_CTRL_MODE_ANALOG
    );

    gu_init();
    world_textures_init();

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

    character_anim += 0.085f;

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

            update_first_person_look(&pad);

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

            update_first_person_look(&pad);

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
                player2_x -= 0.17f;

            if (pad.Buttons & PSP_CTRL_RIGHT)
                player2_x += 0.17f;

            if (pad.Buttons & PSP_CTRL_UP)
                player2_z -= 0.17f;

            if (pad.Buttons & PSP_CTRL_DOWN)
                player2_z += 0.17f;

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
    draw_textured_box(
        -10.5f,
        1.1f,
        13.7f,
        2.0f,
        2.2f,
        0.25f,
        0.0f,
        cousin_door_open
            ? tex_plaster
            : tex_brick,
        cousin_door_open
            ? 0xffc7b28f
            : 0xff66554a
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
        0xff80b7d9
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
            sceGuDepthMask(GU_TRUE);

            render_story_intro();
            break;

        case GAME_STATE_STORY:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            sceGuDepthMask(GU_TRUE);

            set_first_person_camera();

            render_city_world();

            /* Local player is represented by the first-person camera. */
            draw_human(
                brother_x,
                brother_y,
                brother_z,
                brother_yaw,
                0xff7f8f5d
            );

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
            sceGuDepthMask(GU_TRUE);

            set_first_person_camera();

            render_city_world();

            render_game_hud(
                "FREE OPEN WORLD"
            );

            break;

        case GAME_STATE_MULTIPLAYER:

            sceGuEnable(GU_DEPTH_TEST);
            sceGuDepthFunc(GU_GEQUAL);
            sceGuDepthMask(GU_TRUE);

            set_first_person_camera();

            render_city_world();

            draw_human(
                player2_x,
                0.0f,
                player2_z,
                player2_yaw,
                0xff4f9a68
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
