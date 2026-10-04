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

/*
 * GINSENG STRIP 2002
 *
 * Self-contained PSP game core:
 * - Title / PRESS START
 * - Language selection
 * - Save File / Saving / Finish Save
 * - Main Menu
 * - Story Mode
 * - Free Open World
 * - Local Co-Op prototype
 * - System Settings
 * - Peaceful 3D third-person city
 * - Walk / drive
 * - Music only from Main Menu / playable game states
 * - 480x272 PSP framebuffer
 *
 * This file uses the PSP GU/GUM 3D API for the world. PSP GUM provides
 * perspective, look-at camera, matrix transforms and 3D array drawing.
 */

#include "psp_game.h"

/* Embedded assets. These names match the Makefile bin2o targets. */
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

#define PI_F 3.14159265358979323846f

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
    char username[32]; /* kept for compatibility with old saves */
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
    GAME_STATE_STORY,
    GAME_STATE_FREE_WORLD,
    GAME_STATE_MULTIPLAYER,
    GAME_STATE_SETTINGS
} GameState;

/* ------------------------------------------------------------------------- */
/* Global state                                                              */
/* ------------------------------------------------------------------------- */

static unsigned int __attribute__((aligned(64)))
    list[0x20000 / 4];

static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;

static int selected_language = 0;
static int selected_menu = 0;
static int settings_selection = 0;

static unsigned int old_buttons = 0;
static SaveData save_data;

/* Fade */
static int transition_active = 0;
static int transition_phase = 0;
static int transition_alpha = 0;
static GameState transition_target = GAME_STATE_TITLE;

/* Save messages */
static int saving_timer = 0;
static int save_finished_timer = 0;

/* ------------------------------------------------------------------------- */
/* Music                                                                    */
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
/* 3D player / vehicle                                                       */
/* ------------------------------------------------------------------------- */

static float player_x = 0.0f;
static float player_y = 0.0f;
static float player_z = 0.0f;

static float player_yaw = 0.0f;

static int in_vehicle = 0;
static int current_vehicle = -1;

static float player2_x = -3.0f;
static float player2_y = 0.0f;
static float player2_z = 2.0f;
static float player2_yaw = 0.0f;

/* ------------------------------------------------------------------------- */
/* Story                                                                     */
/* ------------------------------------------------------------------------- */

static int story_step = 0;
static int flower_collected = 0;

/* ------------------------------------------------------------------------- */
/* 3D camera                                                                 */
/* ------------------------------------------------------------------------- */

static ScePspFVector3 camera_eye;
static ScePspFVector3 camera_center;
static ScePspFVector3 camera_up;

/* ------------------------------------------------------------------------- */
/* City data                                                                 */
/* ------------------------------------------------------------------------- */

typedef struct
{
    float x;
    float z;
    float w;
    float d;
    float h;
    unsigned int color;
} Building;

static const Building buildings[] =
{
    { -36.0f, -34.0f, 18.0f, 18.0f, 16.0f, 0xffd9b47b },
    {   4.0f, -38.0f, 22.0f, 14.0f, 11.0f, 0xffc9d8e2 },
    {  34.0f, -36.0f, 18.0f, 19.0f, 20.0f, 0xffe5c5a5 },

    { -42.0f,  -2.0f, 14.0f, 20.0f,  8.0f, 0xffc8b58d },
    {  42.0f,   4.0f, 20.0f, 18.0f, 14.0f, 0xffc7d1b6 },

    { -38.0f,  34.0f, 20.0f, 20.0f, 17.0f, 0xffd5a475 },
    {   2.0f,  38.0f, 20.0f, 16.0f, 12.0f, 0xffb9c9d8 },
    {  38.0f,  36.0f, 18.0f, 19.0f, 22.0f, 0xffd8b8a0 },

    { -78.0f, -72.0f, 24.0f, 18.0f, 11.0f, 0xffd9c4a6 },
    { -46.0f, -72.0f, 18.0f, 18.0f, 13.0f, 0xffe0d2b8 },
    {   0.0f, -72.0f, 24.0f, 18.0f, 15.0f, 0xffd4b0a0 },
    {  42.0f, -72.0f, 18.0f, 18.0f, 10.0f, 0xffc6d0d8 },

    { -76.0f,  72.0f, 22.0f, 18.0f, 10.0f, 0xffc9b78f },
    { -43.0f,  72.0f, 18.0f, 18.0f, 16.0f, 0xffd7b5a6 },
    {   0.0f,  72.0f, 22.0f, 20.0f, 18.0f, 0xffbecbaf },
    {  42.0f,  72.0f, 20.0f, 18.0f, 13.0f, 0xffcdb9d1 },

    {  76.0f, -28.0f, 14.0f, 22.0f, 18.0f, 0xffb9c4d8 },
    {  75.0f,  26.0f, 16.0f, 20.0f, 14.0f, 0xffe0bd89 },
    { -74.0f, -22.0f, 16.0f, 22.0f, 19.0f, 0xffc9d0b5 },
    { -76.0f,  24.0f, 18.0f, 20.0f, 12.0f, 0xffd1b9a8 }
};

#define BUILDING_COUNT ((int)(sizeof(buildings) / sizeof(buildings[0])))

typedef struct
{
    float x;
    float z;
    float yaw;
    float speed;
    unsigned int color;
} CityCar;

static CityCar cars[] =
{
    { -20.0f,  -4.0f, 0.0f, 4.0f, 0xffc7372f },
    {  18.0f,  12.0f, PI_F, 4.5f, 0xfff0d54e },
    {  58.0f, -46.0f, PI_F * 0.5f, 3.8f, 0xff3d77b5 },
    { -58.0f,  48.0f, PI_F * 1.5f, 3.4f, 0xffe8e8e8 }
};

#define CAR_COUNT ((int)(sizeof(cars) / sizeof(cars[0])))

typedef struct
{
    float x;
    float z;
    float phase;
    float path;
    unsigned int color;
} Pedestrian;

static Pedestrian pedestrians[] =
{
    { -8.0f, -8.0f, 0.0f, 0.0f, 0xffe7d6c6 },
    {  8.0f,  8.0f, 1.0f, 0.0f, 0xff7a9ad6 },
    { -8.0f, 16.0f, 2.0f, 0.0f, 0xffd9b15c },
    { 16.0f,-16.0f, 3.0f, 0.0f, 0xff78a963 },
    { 40.0f,  6.0f, 4.0f, 0.0f, 0xffbe7da1 },
    {-40.0f, -6.0f, 5.0f, 0.0f, 0xff9d8a63 }
};

#define PEDESTRIAN_COUNT ((int)(sizeof(pedestrians) / sizeof(pedestrians[0])))

/* ------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* ------------------------------------------------------------------------- */

static void start_transition(GameState next_state);
static void update_transition(void);
static void render_fade(void);

static int save_exists(void);
static void save_game(void);
static void load_game(void);

static void music_start(void);
static void music_stop(void);

static void draw_texture(const void *texture);
static void draw_rect_2d(int x, int y, int w, int h, unsigned int color);
static void draw_text(const char *text, int x, int y, int scale, unsigned int color);
static int text_width(const char *text, int scale);

static void render_main_menu(void);
static void render_language(void);
static void render_save(void);

static void setup_3d_camera(void);
static void render_3d_world(void);
static void render_hud_3d(const char *mode_name);

static void update_story(void);
static void update_free_world(void);
static void update_multiplayer(void);
static void update_settings(void);

static void draw_cube(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
    unsigned int color
);

static void draw_tree(float x, float z);
static void draw_car_3d(const CityCar *car);
static void draw_player_3d(float x, float y, float z, float yaw, unsigned int shirt_color);
static void draw_pedestrian_3d(const Pedestrian *ped);

/* ============================================================ */
/* Save                                                          */
/* ============================================================ */

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

    save_data.magic = SAVE_MAGIC;
    save_data.language = selected_language;

    /* Username is intentionally unused in this version. */
    memset(
        save_data.username,
        0,
        sizeof(save_data.username)
    );

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
        != sizeof(save_data))
    {
        sceIoClose(fd);
        return;
    }

    sceIoClose(fd);

    if (save_data.magic != SAVE_MAGIC)
        return;

    selected_language = save_data.language;
}

/* ============================================================ */
/* Small UI font                                                 */
/* ============================================================ */

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
        default: return NULL;
    }
}

static int text_width(
    const char *text,
    int scale
)
{
    int width = 0;

    while (*text)
    {
        width += 6 * scale;
        ++text;
    }

    if (width > 0)
        width -= scale;

    return width;
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

    int pixel_count = 0;
    const char *p;
    Vertex *v;
    int n = 0;
    int current_x = x;

    for (p = text; *p; ++p)
    {
        const unsigned char *g = get_glyph(*p);
        int row;
        int col;

        if (!g)
            continue;

        for (row = 0; row < 7; ++row)
        {
            for (col = 0; col < 5; ++col)
            {
                if (g[row] & (1 << (4 - col)))
                    ++pixel_count;
            }
        }
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
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        n,
        NULL,
        v
    );
}

/* ============================================================ */
/* 2D primitives                                                 */
/* ============================================================ */

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
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );
}

/* ============================================================ */
/* Texture rendering                                             */
/* ============================================================ */

static void draw_texture(
    const void *texture
)
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

    Vertex *v =
        (Vertex *)sceGuGetMemory(
            2 * sizeof(Vertex)
        );

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
        0
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

/* ============================================================ */
/* Fade                                                          */
/* ============================================================ */

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
        transition_alpha += 20;

        if (transition_alpha >= 255)
        {
            transition_alpha = 255;

            state = transition_target;

            if (state == GAME_STATE_SAVING)
                saving_timer = 0;

            if (state == GAME_STATE_SAVE_FINISHED)
                save_finished_timer = 0;

            if (state == GAME_STATE_MAIN_MENU ||
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
        transition_alpha -= 12;

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

/* ============================================================ */
/* Music                                                          */
/* ============================================================ */

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

/* ============================================================ */
/* 3D primitives                                                  */
/* ============================================================ */

typedef struct
{
    float x;
    float y;
    float z;
} Vertex3D;

static const Vertex3D __attribute__((aligned(16))) cube_vertices[36] =
{
    /* Front */
    {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f},
    {-0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},

    /* Back */
    {0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,0.5f}, {-0.5f,0.5f,0.5f},
    {0.5f,-0.5f,0.5f}, {-0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f},

    /* Left */
    {-0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
    {-0.5f,-0.5f,0.5f}, {-0.5f,0.5f,-0.5f}, {-0.5f,0.5f,0.5f},

    /* Right */
    {0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f},
    {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,-0.5f},

    /* Top */
    {-0.5f,0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {0.5f,0.5f,0.5f},
    {-0.5f,0.5f,-0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f},

    /* Bottom */
    {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}
};

static void draw_cube(
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
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

    sceGuColor(color);

    sceGumPushMatrix();

    sceGumTranslate(&pos);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();
}

static void draw_tree(
    float x,
    float z
)
{
    draw_cube(
        x,
        2.0f,
        z,
        0.65f,
        4.0f,
        0.65f,
        0xff6b4c30
    );

    draw_cube(
        x,
        5.0f,
        z,
        3.2f,
        3.2f,
        3.2f,
        0xff3e914a
    );

    draw_cube(
        x,
        6.4f,
        z,
        2.2f,
        2.2f,
        2.2f,
        0xff4da555
    );
}

static void draw_car_3d(
    const CityCar *car
)
{
    ScePspFVector3 pos;
    ScePspFVector3 scale;
    ScePspFVector3 wheel_pos;
    ScePspFVector3 wheel_scale;

    float wheel_x;
    float wheel_z;

    /*
     * Body.
     */
    pos.x = car->x;
    pos.y = 1.0f;
    pos.z = car->z;

    sceGuColor(car->color);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(car->yaw);

    scale.x = 3.4f;
    scale.y = 0.9f;
    scale.z = 1.7f;

    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /*
     * Roof.
     */
    pos.y = 1.65f;

    sceGuColor(0xffe5e7e8);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(car->yaw);

    scale.x = 1.9f;
    scale.y = 0.65f;
    scale.z = 1.35f;

    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /*
     * Simple windows.
     */
    pos.y = 1.72f;

    sceGuColor(0xff496b7e);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(car->yaw);

    scale.x = 1.65f;
    scale.y = 0.35f;
    scale.z = 1.38f;

    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /*
     * Wheels as compact boxes; good enough for a PSP-scale model.
     */
    wheel_x = car->x + cosf(car->yaw) * 1.25f;
    wheel_z = car->z + sinf(car->yaw) * 1.25f;

    wheel_pos.y = 0.55f;
    wheel_scale.x = 0.5f;
    wheel_scale.y = 0.55f;
    wheel_scale.z = 0.35f;

    sceGuColor(0xff202326);

    wheel_pos.x = wheel_x;
    wheel_pos.z = wheel_z + 0.72f;

    sceGumPushMatrix();
    sceGumTranslate(&wheel_pos);
    sceGumRotateY(car->yaw);
    sceGumScale(&wheel_scale);
    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );
    sceGumPopMatrix();

    wheel_pos.x = wheel_x;
    wheel_pos.z = wheel_z - 0.72f;

    sceGumPushMatrix();
    sceGumTranslate(&wheel_pos);
    sceGumRotateY(car->yaw);
    sceGumScale(&wheel_scale);
    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );
    sceGumPopMatrix();

    wheel_x = car->x - cosf(car->yaw) * 1.25f;
    wheel_z = car->z - sinf(car->yaw) * 1.25f;

    wheel_pos.x = wheel_x;
    wheel_pos.z = wheel_z + 0.72f;

    sceGumPushMatrix();
    sceGumTranslate(&wheel_pos);
    sceGumRotateY(car->yaw);
    sceGumScale(&wheel_scale);
    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );
    sceGumPopMatrix();

    wheel_pos.x = wheel_x;
    wheel_pos.z = wheel_z - 0.72f;

    sceGumPushMatrix();
    sceGumTranslate(&wheel_pos);
    sceGumRotateY(car->yaw);
    sceGumScale(&wheel_scale);
    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );
    sceGumPopMatrix();
}

static void draw_player_3d(
    float x,
    float y,
    float z,
    float yaw,
    unsigned int shirt_color
)
{
    ScePspFVector3 pos;
    ScePspFVector3 scale;

    if (in_vehicle && current_vehicle >= 0)
        return;

    /*
     * Legs.
     */
    pos.x = x;
    pos.y = y + 0.65f;
    pos.z = z;

    sceGuColor(0xff2b3441);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);

    scale.x = 0.62f;
    scale.y = 1.2f;
    scale.z = 0.40f;

    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /*
     * Torso.
     */
    pos.y = y + 1.55f;

    sceGuColor(shirt_color);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);

    scale.x = 0.95f;
    scale.y = 1.15f;
    scale.z = 0.65f;

    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF |
        GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /*
     * Head.
     */
    draw_cube(
        x,
        y + 2.55f,
        z,
        0.62f,
        0.68f,
        0.62f,
        0xffd6a37c
    );

    /*
     * Hair.
     */
    draw_cube(
        x,
        y + 2.90f,
        z,
        0.68f,
        0.25f,
        0.68f,
        0xff3c2d25
    );
}

static void draw_pedestrian_3d(
    const Pedestrian *ped
)
{
    draw_cube(
        ped->x,
        0.75f,
        ped->z,
        0.55f,
        1.4f,
        0.55f,
        ped->color
    );

    draw_cube(
        ped->x,
        1.72f,
        ped->z,
        0.45f,
        0.45f,
        0.45f,
        0xffd5a17a
    );
}

/* ============================================================ */
/* 3D camera                                                     */
/* ============================================================ */

static void setup_3d_camera(void)
{
    float forward_x;
    float forward_z;

    if (in_vehicle && current_vehicle >= 0)
    {
        forward_x = sinf(cars[current_vehicle].yaw);
        forward_z = -cosf(cars[current_vehicle].yaw);

        camera_eye.x =
            cars[current_vehicle].x -
            forward_x * 8.0f;

        camera_eye.y = 5.2f;

        camera_eye.z =
            cars[current_vehicle].z -
            forward_z * 8.0f;

        camera_center.x =
            cars[current_vehicle].x +
            forward_x * 2.0f;

        camera_center.y = 1.5f;

        camera_center.z =
            cars[current_vehicle].z +
            forward_z * 2.0f;
    }
    else
    {
        forward_x = sinf(player_yaw);
        forward_z = -cosf(player_yaw);

        camera_eye.x =
            player_x -
            forward_x * 8.0f;

        camera_eye.y = 5.0f;

        camera_eye.z =
            player_z -
            forward_z * 8.0f;

        camera_center.x =
            player_x +
            forward_x * 2.0f;

        camera_center.y = 1.45f;

        camera_center.z =
            player_z +
            forward_z * 2.0f;
    }

    camera_up.x = 0.0f;
    camera_up.y = 1.0f;
    camera_up.z = 0.0f;

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();

    sceGumPerspective(
        62.0f,
        (float)SCREEN_WIDTH /
        (float)SCREEN_HEIGHT,
        0.25f,
        500.0f
    );

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    sceGumLookAt(
        &camera_eye,
        &camera_center,
        &camera_up
    );

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

/* ============================================================ */
/* 3D world                                                       */
/* ============================================================ */

static void render_3d_world(void)
{
    int i;

    setup_3d_camera();

    /* Sky backdrop at ground/world level. */
    draw_cube(
        0.0f,
        -1.5f,
        0.0f,
        220.0f,
        0.2f,
        220.0f,
        0xffa8d7f0
    );

    /* Grass / city ground. */
    draw_cube(
        0.0f,
        -0.30f,
        0.0f,
        190.0f,
        0.35f,
        190.0f,
        0xff5f9d58
    );

    /* Main roads. */
    draw_cube(
        0.0f,
        -0.08f,
        0.0f,
        190.0f,
        0.12f,
        13.0f,
        0xff4c4d52
    );

    draw_cube(
        0.0f,
        -0.07f,
        0.0f,
        13.0f,
        0.12f,
        190.0f,
        0xff4c4d52
    );

    /* Side roads. */
    draw_cube(
        0.0f,
        -0.075f,
        -52.0f,
        190.0f,
        0.12f,
        10.0f,
        0xff595a60
    );

    draw_cube(
        0.0f,
        -0.075f,
        52.0f,
        190.0f,
        0.12f,
        10.0f,
        0xff595a60
    );

    draw_cube(
        -52.0f,
        -0.075f,
        0.0f,
        10.0f,
        0.12f,
        190.0f,
        0xff595a60
    );

    draw_cube(
        52.0f,
        -0.075f,
        0.0f,
        10.0f,
        0.12f,
        190.0f,
        0xff595a60
    );

    /* Beach / water. */
    draw_cube(
        0.0f,
        -0.24f,
        -94.0f,
        190.0f,
        0.15f,
        22.0f,
        0xff4e99c8
    );

    draw_cube(
        0.0f,
        -0.15f,
        -82.0f,
        190.0f,
        0.10f,
        3.0f,
        0xffe9d6a1
    );

    /* Buildings. */
    for (i = 0; i < BUILDING_COUNT; ++i)
    {
        const Building *b = &buildings[i];

        draw_cube(
            b->x,
            b->h * 0.5f,
            b->z,
            b->w,
            b->h,
            b->d,
            b->color
        );

        /*
         * Simple darker roof cap gives buildings extra depth.
         */
        draw_cube(
            b->x,
            b->h + 0.12f,
            b->z,
            b->w * 1.03f,
            0.25f,
            b->d * 1.03f,
            0xff8b6e68
        );
    }

    /* Palm trees / city trees. */
    draw_tree(-60.0f, -60.0f);
    draw_tree(-24.0f, -58.0f);
    draw_tree(26.0f, -58.0f);
    draw_tree(63.0f, -61.0f);

    draw_tree(-62.0f, 60.0f);
    draw_tree(-26.0f, 61.0f);
    draw_tree(26.0f, 61.0f);
    draw_tree(64.0f, 60.0f);

    draw_tree(-62.0f, 7.0f);
    draw_tree(63.0f, -4.0f);
    draw_tree(7.0f, -62.0f);
    draw_tree(4.0f, 63.0f);

    /* Cars. */
    for (i = 0; i < CAR_COUNT; ++i)
        draw_car_3d(&cars[i]);

    /* Pedestrians. */
    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
        draw_pedestrian_3d(&pedestrians[i]);

    /* Player. */
    draw_player_3d(
        player_x,
        player_y,
        player_z,
        player_yaw,
        0xff4f77ad
    );

    /* Local co-op second player. */
    if (state == GAME_STATE_MULTIPLAYER)
    {
        draw_player_3d(
            player2_x,
            player2_y,
            player2_z,
            player2_yaw,
            0xff4f9a68
        );
    }

    /* Story targets. */
    if (state == GAME_STATE_STORY)
    {
        if (story_step == 0)
        {
            draw_cube(
                32.0f,
                1.5f,
                -28.0f,
                1.8f,
                3.0f,
                1.8f,
                0xfff2cf45
            );
        }
        else if (story_step == 1)
        {
            draw_cube(
                50.0f,
                1.4f,
                -82.0f,
                2.0f,
                2.8f,
                2.0f,
                0xfff2cf45
            );
        }
        else if (story_step == 2)
        {
            draw_cube(
                -10.0f,
                1.5f,
                10.0f,
                1.8f,
                3.0f,
                1.8f,
                0xfff2cf45
            );
        }
    }
}

/* ============================================================ */
/* 3D HUD                                                        */
/* ============================================================ */

static void render_hud_3d(
    const char *mode_name
)
{
    int w;

    /*
     * Switch to a pixel-like orthographic projection for HUD.
     */
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();

    sceGumOrtho(
        0.0f,
        (float)SCREEN_WIDTH,
        (float)SCREEN_HEIGHT,
        0.0f,
        -1.0f,
        1.0f
    );

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    draw_rect_2d(
        12,
        8,
        456,
        34,
        0xcc16252d
    );

    draw_text(
        mode_name,
        24,
        17,
        3,
        0xffffffff
    );

    draw_rect_2d(
        0,
        244,
        480,
        28,
        0xcc16252d
    );

    if (in_vehicle)
    {
        draw_text(
            "DRIVE X EXIT",
            18,
            253,
            2,
            0xffffffff
        );
    }
    else
    {
        draw_text(
            "O BACK",
            18,
            253,
            2,
            0xffffffff
        );

        draw_text(
            "X ENTER",
            372,
            253,
            2,
            0xffffffff
        );
    }

    if (state == GAME_STATE_STORY)
    {
        if (story_step == 0)
            draw_text("VISIT CAFE", 24, 51, 2, 0xff101820);
        else if (story_step == 1)
            draw_text("GO TO BEACH", 24, 51, 2, 0xff101820);
        else if (story_step == 2)
            draw_text("RETURN HOME", 24, 51, 2, 0xff101820);
        else
            draw_text("STORY COMPLETE", 24, 51, 2, 0xff101820);
    }
    else if (state == GAME_STATE_FREE_WORLD)
    {
        draw_text(
            "EXPLORE THE CITY",
            24,
            51,
            2,
            0xff101820
        );
    }
    else if (state == GAME_STATE_MULTIPLAYER)
    {
        draw_text(
            "P1 ANALOG  P2 D PAD",
            24,
            51,
            2,
            0xff101820
        );
    }

    /*
     * Tiny speed meter while driving.
     */
    if (in_vehicle &&
        current_vehicle >= 0)
    {
        float speed =
            fabsf(cars[current_vehicle].speed);

        int meter = (int)(speed * 10.0f);

        if (meter > 120)
            meter = 120;

        draw_rect_2d(
            330,
            18,
            meter,
            5,
            0xffdfefff
        );
    }

    w = 0;
    (void)w;
}

/* ============================================================ */
/* Gameplay movement                                             */
/* ============================================================ */

static void clamp_world_position(
    float *x,
    float *z
)
{
    if (*x < -88.0f)
        *x = -88.0f;

    if (*x > 88.0f)
        *x = 88.0f;

    if (*z < -86.0f)
        *z = -86.0f;

    if (*z > 86.0f)
        *z = 86.0f;
}

static int nearest_vehicle(void)
{
    int i;
    int best = -1;
    float best_d2 = 16.0f;

    for (i = 0; i < CAR_COUNT; ++i)
    {
        float dx = cars[i].x - player_x;
        float dz = cars[i].z - player_z;
        float d2 = dx * dx + dz * dz;

        if (d2 < best_d2)
        {
            best_d2 = d2;
            best = i;
        }
    }

    return best;
}

static void update_walk(
    const SceCtrlData *pad
)
{
    float lx;
    float ly;
    float magnitude;
    float move_forward;
    float move_side;
    float forward_x;
    float forward_z;
    float right_x;
    float right_z;
    float speed = 0.18f;

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

    magnitude =
        sqrtf(lx * lx + ly * ly);

    if (magnitude > 1.0f)
        magnitude = 1.0f;

    if (magnitude < 0.18f)
        return;

    /*
     * Analog vertical is forward/back.
     */
    move_forward = -ly;
    move_side = lx;

    forward_x = sinf(player_yaw);
    forward_z = -cosf(player_yaw);

    right_x = cosf(player_yaw);
    right_z = sinf(player_yaw);

    player_x +=
        (forward_x * move_forward +
         right_x * move_side) *
        speed;

    player_z +=
        (forward_z * move_forward +
         right_z * move_side) *
        speed;

    clamp_world_position(
        &player_x,
        &player_z
    );

    /*
     * Turn toward movement direction.
     */
    if (magnitude > 0.20f)
    {
        float target_yaw =
            atan2f(
                forward_x * move_forward +
                right_x * move_side,
                -(forward_z * move_forward +
                  right_z * move_side)
            );

        float delta =
            target_yaw - player_yaw;

        while (delta > PI_F)
            delta -= 2.0f * PI_F;

        while (delta < -PI_F)
            delta += 2.0f * PI_F;

        player_yaw += delta * 0.18f;
    }
}

static void update_vehicle(
    const SceCtrlData *pad
)
{
    CityCar *car;

    float lx;
    float ly;
    float accel;
    float forward_x;
    float forward_z;

    if (current_vehicle < 0)
        return;

    car = &cars[current_vehicle];

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

    accel = -ly * 0.045f;

    car->speed += accel;

    car->speed *= 0.965f;

    if (car->speed > 7.0f)
        car->speed = 7.0f;

    if (car->speed < -3.0f)
        car->speed = -3.0f;

    /*
     * Steering is stronger at speed.
     */
    car->yaw += lx *
                0.030f *
                (fabsf(car->speed) + 0.8f);

    forward_x = sinf(car->yaw);
    forward_z = -cosf(car->yaw);

    car->x +=
        forward_x *
        car->speed *
        0.10f;

    car->z +=
        forward_z *
        car->speed *
        0.10f;

    clamp_world_position(
        &car->x,
        &car->z
    );

    player_x = car->x;
    player_z = car->z;
    player_yaw = car->yaw;
}

static void update_pedestrians(void)
{
    int i;

    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
    {
        Pedestrian *p = &pedestrians[i];

        p->path +=
            0.035f +
            (p->phase * 0.001f);

        if (p->path > 1.0f)
            p->path -= 1.0f;

        /*
         * Small peaceful walking loops around the central roads.
         */
        if (i & 1)
        {
            p->x =
                -8.0f +
                p->path * 16.0f +
                p->phase;

            p->z =
                8.0f *
                sinf(
                    p->path *
                    2.0f *
                    PI_F
                );
        }
        else
        {
            p->z =
                -8.0f +
                p->path * 16.0f;

            p->x =
                8.0f *
                cosf(
                    p->path *
                    2.0f *
                    PI_F
                ) +
                p->phase;
        }
    }
}

/* ============================================================ */
/* Story update                                                  */
/* ============================================================ */

static float distance2_to(
    float x1,
    float z1,
    float x2,
    float z2
)
{
    float dx = x1 - x2;
    float dz = z1 - z2;

    return dx * dx + dz * dz;
}

static void update_story(void)
{
    if (in_vehicle)
        return;

    if (story_step == 0)
    {
        if (distance2_to(
                player_x,
                player_z,
                32.0f,
                -28.0f
            ) < 49.0f)
        {
            story_step = 1;
        }
    }
    else if (story_step == 1)
    {
        if (distance2_to(
                player_x,
                player_z,
                50.0f,
                -82.0f
            ) < 64.0f)
        {
            flower_collected = 1;
            story_step = 2;
        }
    }
    else if (story_step == 2)
    {
        if (distance2_to(
                player_x,
                player_z,
                -10.0f,
                10.0f
            ) < 64.0f)
        {
            story_step = 3;
        }
    }
}

/* ============================================================ */
/* Game-specific update                                          */
/* ============================================================ */

static void update_free_world(void)
{
    /* The free world uses normal walk / car update. */
}

static void update_multiplayer(void)
{
    float x;
    float y;
    float p2_speed = 0.13f;

    /*
     * Player 1 = analog stick.
     * Player 2 = D-pad.
     * This is local co-op on the single handheld.
     */
    (void)x;
    (void)y;

    player2_yaw += 0.0f;

    if (old_buttons & PSP_CTRL_LEFT)
        player2_x -= p2_speed;

    if (old_buttons & PSP_CTRL_RIGHT)
        player2_x += p2_speed;

    if (old_buttons & PSP_CTRL_UP)
        player2_z -= p2_speed;

    if (old_buttons & PSP_CTRL_DOWN)
        player2_z += p2_speed;

    clamp_world_position(
        &player2_x,
        &player2_z
    );
}

static void update_settings(void)
{
    /* Settings are handled directly in psp_game_update(). */
}

/* ============================================================ */
/* Render: menu screens                                           */
/* ============================================================ */

static void render_language(void)
{
    int y;

    draw_texture(
        LanguageSelection_start
    );

    /*
     * Underline only: no opaque white selection box.
     */
    y =
        48 +
        selected_language * 29;

    draw_rect_2d(
        145,
        y,
        150,
        2,
        0xff63b5ff
    );
}

static void render_save(void)
{
    int x;

    x =
        (SCREEN_WIDTH -
         text_width(
             "SAVE FILE",
             4
         )) / 2;

    draw_text(
        "SAVE FILE",
        x,
        64,
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

    x =
        (SCREEN_WIDTH -
         text_width(
             "O BACK",
             2
         )) / 2;

    draw_text(
        "O BACK",
        x,
        202,
        2,
        0xffc8d8e8
    );
}

static void render_main_menu(void)
{
    int y;

    draw_texture(
        MainMenu_start
    );

    switch (selected_menu)
    {
        case 0:
            y = 151;
            break;

        case 1:
            y = 180;
            break;

        case 2:
            y = 209;
            break;

        default:
            y = 238;
            break;
    }

    /*
     * Thin selection line instead of a solid overlay.
     */
    draw_rect_2d(
        22,
        y,
        180,
        2,
        0xff63b5ff
    );
}

/* ============================================================ */
/* GU initialization                                              */
/* ============================================================ */

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

    sceGuEnable(
        GU_SCISSOR_TEST
    );

    sceGuDisable(
        GU_CULL_FACE
    );

    sceGuShadeModel(
        GU_SMOOTH
    );

    sceGuDepthBuffer(
        (void *)0x110000,
        BUF_WIDTH
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

/* ============================================================ */
/* Init                                                            */
/* ============================================================ */

int psp_game_init(void)
{
    sceCtrlSetSamplingCycle(0);

    sceCtrlSetSamplingMode(
        PSP_CTRL_MODE_ANALOG
    );

    gu_init();

    state = GAME_STATE_TITLE;

    selected_language = 0;
    selected_menu = 0;
    settings_selection = 0;

    story_step = 0;
    flower_collected = 0;

    player_x = 0.0f;
    player_y = 0.0f;
    player_z = 0.0f;

    player_yaw = 0.0f;

    in_vehicle = 0;
    current_vehicle = -1;

    player2_x = -3.0f;
    player2_y = 0.0f;
    player2_z = 2.0f;
    player2_yaw = 0.0f;

    transition_active = 0;
    transition_phase = 0;
    transition_alpha = 0;

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

/* ============================================================ */
/* Update                                                           */
/* ============================================================ */

void psp_game_update(void)
{
    SceCtrlData pad;
    unsigned int pressed;
    int nearest;

    if (!game_initialized)
        return;

    /*
     * Fades lock input.
     */
    if (transition_active)
    {
        update_transition();
        return;
    }

    /*
     * Save status screens.
     */
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

        if (save_finished_timer >= 105)
        {
            start_transition(
                GAME_STATE_MAIN_MENU
            );
        }

        return;
    }

    sceCtrlReadBufferPositive(
        &pad,
        1
    );

    pressed =
        pad.Buttons &
        ~old_buttons;

    /*
     * Keep previous physical state available to the local
     * second player prototype.
     */
    old_buttons =
        pad.Buttons;

    switch (state)
    {
        /* ---------------------------------------------------- */
        /* TITLE                                                  */
        /* ---------------------------------------------------- */
        case GAME_STATE_TITLE:

            if (pressed &
                PSP_CTRL_START)
            {
                /*
                 * No music on title.
                 */
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

        /* ---------------------------------------------------- */
        /* LANGUAGE                                                */
        /* ---------------------------------------------------- */
        case GAME_STATE_LANGUAGE:

            if (pressed &
                PSP_CTRL_UP)
            {
                if (selected_language > 0)
                    --selected_language;
            }

            if (pressed &
                PSP_CTRL_DOWN)
            {
                if (selected_language < 8)
                    ++selected_language;
            }

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_TITLE
                );
            }

            if (pressed &
                PSP_CTRL_CROSS)
            {
                /*
                 * No username screen.
                 * Go directly to SAVE.
                 */
                save_data.language =
                    selected_language;

                save_data.username[0] =
                    '\0';

                start_transition(
                    GAME_STATE_SAVE
                );
            }

            break;

        /* ---------------------------------------------------- */
        /* SAVE                                                     */
        /* ---------------------------------------------------- */
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

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_LANGUAGE
                );
            }

            break;

        /* ---------------------------------------------------- */
        /* MAIN MENU                                                */
        /* ---------------------------------------------------- */
        case GAME_STATE_MAIN_MENU:

            if (pressed &
                PSP_CTRL_UP)
            {
                if (selected_menu > 0)
                    --selected_menu;
            }

            if (pressed &
                PSP_CTRL_DOWN)
            {
                if (selected_menu < 3)
                    ++selected_menu;
            }

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                /*
                 * Leave menu without deleting save.
                 */
                start_transition(
                    GAME_STATE_TITLE
                );
            }

            if (pressed &
                PSP_CTRL_CROSS)
            {
                switch (selected_menu)
                {
                    case 0:

                        story_step = 0;
                        flower_collected = 0;

                        player_x = 0.0f;
                        player_z = 8.0f;
                        player_yaw = 0.0f;

                        in_vehicle = 0;
                        current_vehicle = -1;

                        start_transition(
                            GAME_STATE_STORY
                        );

                        break;

                    case 1:

                        player_x = 0.0f;
                        player_z = 8.0f;
                        player_yaw = 0.0f;

                        in_vehicle = 0;
                        current_vehicle = -1;

                        start_transition(
                            GAME_STATE_FREE_WORLD
                        );

                        break;

                    case 2:

                        player_x = -4.0f;
                        player_z = 5.0f;
                        player_yaw = 0.0f;

                        player2_x = 4.0f;
                        player2_z = 5.0f;
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

        /* ---------------------------------------------------- */
        /* STORY                                                    */
        /* ---------------------------------------------------- */
        case GAME_STATE_STORY:

            if (in_vehicle)
            {
                update_vehicle(&pad);
            }
            else
            {
                update_walk(&pad);
            }

            update_story();
            update_pedestrians();

            /*
             * X enters the closest parked car or exits the
             * current car.
             */
            if (pressed &
                PSP_CTRL_CROSS)
            {
                if (in_vehicle)
                {
                    in_vehicle = 0;
                    current_vehicle = -1;
                }
                else
                {
                    nearest = nearest_vehicle();

                    if (nearest >= 0)
                    {
                        current_vehicle = nearest;
                        in_vehicle = 1;
                        cars[nearest].speed = 0.0f;

                        player_x =
                            cars[nearest].x;

                        player_z =
                            cars[nearest].z;

                        player_yaw =
                            cars[nearest].yaw;
                    }
                }
            }

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                in_vehicle = 0;
                current_vehicle = -1;

                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        /* ---------------------------------------------------- */
        /* FREE WORLD                                              */
        /* ---------------------------------------------------- */
        case GAME_STATE_FREE_WORLD:

            if (in_vehicle)
            {
                update_vehicle(&pad);
            }
            else
            {
                update_walk(&pad);
            }

            update_free_world();
            update_pedestrians();

            if (pressed &
                PSP_CTRL_CROSS)
            {
                if (in_vehicle)
                {
                    in_vehicle = 0;
                    current_vehicle = -1;
                }
                else
                {
                    nearest = nearest_vehicle();

                    if (nearest >= 0)
                    {
                        current_vehicle = nearest;
                        in_vehicle = 1;
                        cars[nearest].speed = 0.0f;

                        player_x =
                            cars[nearest].x;

                        player_z =
                            cars[nearest].z;

                        player_yaw =
                            cars[nearest].yaw;
                    }
                }
            }

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                in_vehicle = 0;
                current_vehicle = -1;

                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        /* ---------------------------------------------------- */
        /* LOCAL CO-OP                                             */
        /* ---------------------------------------------------- */
        case GAME_STATE_MULTIPLAYER:

            /*
             * P1: analog + normal walk.
             */
            update_walk(&pad);

            /*
             * P2: D-pad movement.
             */
            if (pad.Buttons & PSP_CTRL_LEFT)
                player2_x -= 0.13f;

            if (pad.Buttons & PSP_CTRL_RIGHT)
                player2_x += 0.13f;

            if (pad.Buttons & PSP_CTRL_UP)
                player2_z -= 0.13f;

            if (pad.Buttons & PSP_CTRL_DOWN)
                player2_z += 0.13f;

            clamp_world_position(
                &player2_x,
                &player2_z
            );

            update_multiplayer();
            update_pedestrians();

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        /* ---------------------------------------------------- */
        /* SETTINGS                                                */
        /* ---------------------------------------------------- */
        case GAME_STATE_SETTINGS:

            update_settings();

            if (pressed &
                PSP_CTRL_UP)
            {
                if (settings_selection > 0)
                    --settings_selection;
            }

            if (pressed &
                PSP_CTRL_DOWN)
            {
                if (settings_selection < 1)
                    ++settings_selection;
            }

            if (pressed &
                PSP_CTRL_CROSS)
            {
                if (settings_selection == 0)
                {
                    /*
                     * Music is toggled off in settings.
                     * A new entry into Main Menu starts it again.
                     */
                    music_stop();
                }
                else
                {
                    /*
                     * Reset save completely.
                     */
                    sceIoRemove(
                        SAVE_FILE
                    );

                    music_stop();

                    start_transition(
                        GAME_STATE_TITLE
                    );
                }
            }

            if (pressed &
                PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        default:
            break;
    }

    if (state != GAME_STATE_TITLE &&
        state != GAME_STATE_LANGUAGE &&
        state != GAME_STATE_SAVE &&
        state != GAME_STATE_SAVING &&
        state != GAME_STATE_SAVE_FINISHED)
    {
        /*
         * During playable modes, keep the city characters moving.
         */
        if (state == GAME_STATE_MAIN_MENU)
        {
            /* nothing */
        }
    }
}

/* ============================================================ */
/* Render                                                          */
/* ============================================================ */

void psp_game_render(void)
{
    if (!game_initialized)
        return;

    /*
     * Normal frame.
     */
    sceGuStart(
        GU_DIRECT,
        list
    );

    sceGuClearColor(
        0xff000000
    );

    /*
     * Enable depth testing for the 3D world.
     */
    sceGuEnable(
        GU_DEPTH_TEST
    );

    sceGuDepthFunc(
        GU_GEQUAL
    );

    sceGuDepthMask(
        GU_TRUE
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

            draw_texture(
                Title_start
            );

            break;

        case GAME_STATE_LANGUAGE:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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

            render_language();

            break;

        case GAME_STATE_SAVE:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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

            render_save();

            break;

        case GAME_STATE_SAVING:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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
                         "SAVING",
                         4
                     )) / 2;

                draw_text(
                    "SAVING",
                    x,
                    135,
                    4,
                    0xffffffff
                );
            }

            break;

        case GAME_STATE_SAVE_FINISHED:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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
                    76,
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
                    130,
                    3,
                    0xffffffff
                );
            }

            break;

        case GAME_STATE_MAIN_MENU:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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

            render_main_menu();

            break;

        case GAME_STATE_STORY:

            render_3d_world();

            render_hud_3d(
                "STORY MODE"
            );

            break;

        case GAME_STATE_FREE_WORLD:

            render_3d_world();

            render_hud_3d(
                "FREE WORLD"
            );

            break;

        case GAME_STATE_MULTIPLAYER:

            render_3d_world();

            render_hud_3d(
                "LOCAL CO OP"
            );

            break;

        case GAME_STATE_SETTINGS:

            sceGuDisable(
                GU_DEPTH_TEST
            );

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

            draw_rect_2d(
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0xff687b89
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
                    0xff334955
                );
            }
            else
            {
                draw_rect_2d(
                    24,
                    140,
                    360,
                    40,
                    0xff334955
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
     * Fade is rendered last so that it covers both 2D and 3D scenes.
     */
    if (transition_active)
    {
        /*
         * The fade uses the current 2D projection.
         * Switch explicitly so the overlay always covers the screen.
         */
        sceGuDisable(
            GU_DEPTH_TEST
        );

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

/* ============================================================ */
/* Shutdown                                                        */
/* ============================================================ */

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

/* ============================================================ */
/* Compatibility wrappers                                          */
/* ============================================================ */

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
