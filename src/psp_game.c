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
 * - third-person 3D camera
 * - peaceful city exploration
 * - classic early-2000s console / PS2-style low-poly look
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
/* 3D primitive vertex data                                                   */
/* ------------------------------------------------------------------------- */

typedef struct
{
    float x;
    float y;
    float z;
} Vertex3D;

static const Vertex3D __attribute__((aligned(16))) cube_vertices[36] =
{
    {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},

    { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},

    {-0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f},

    { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f},

    {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},

    {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}
};

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
        transition_alpha -= 10;

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
/* 3D helpers                                                                 */
/* ------------------------------------------------------------------------- */

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
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();
}

static void draw_pyramid_roof(
    float x,
    float y,
    float z,
    float sx,
    float sz,
    unsigned int color
)
{
    typedef struct
    {
        float x;
        float y;
        float z;
    } V;

    static const V __attribute__((aligned(16))) roof[12] =
    {
        {-0.5f,0.0f,-0.5f}, {0.5f,0.0f,-0.5f}, {0.0f,0.7f,0.0f},
        { 0.5f,0.0f,-0.5f}, {0.5f,0.0f, 0.5f}, {0.0f,0.7f,0.0f},
        { 0.5f,0.0f, 0.5f}, {-0.5f,0.0f, 0.5f}, {0.0f,0.7f,0.0f},
        {-0.5f,0.0f, 0.5f}, {-0.5f,0.0f,-0.5f}, {0.0f,0.7f,0.0f}
    };

    ScePspFVector3 pos;
    ScePspFVector3 scale;

    pos.x = x;
    pos.y = y;
    pos.z = z;

    scale.x = sx;
    scale.y = 2.2f;
    scale.z = sz;

    sceGuColor(color);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        12,
        NULL,
        roof
    );

    sceGumPopMatrix();
}

static void draw_tree(
    float x,
    float z,
    float scale_factor
)
{
    draw_cube(
        x,
        1.6f * scale_factor,
        z,
        0.55f * scale_factor,
        3.2f * scale_factor,
        0.55f * scale_factor,
        0xff75533a
    );

    draw_cube(
        x,
        4.1f * scale_factor,
        z,
        2.5f * scale_factor,
        2.8f * scale_factor,
        2.5f * scale_factor,
        0xff327c43
    );

    draw_cube(
        x,
        5.2f * scale_factor,
        z,
        1.8f * scale_factor,
        1.8f * scale_factor,
        1.8f * scale_factor,
        0xff4b9b53
    );
}

static void draw_building(
    const Building *b
)
{
    draw_cube(
        b->x,
        b->h * 0.5f,
        b->z,
        b->w,
        b->h,
        b->d,
        b->wall
    );

    draw_pyramid_roof(
        b->x,
        b->h,
        b->z,
        b->w * 1.05f,
        b->d * 1.05f,
        b->roof
    );

    /*
     * Simple window bands give the facades readable detail
     * without relying on additional texture assets.
     */
    draw_cube(
        b->x - b->w * 0.20f,
        b->h * 0.58f,
        b->z - b->d * 0.51f,
        b->w * 0.16f,
        b->h * 0.16f,
        0.08f,
        0xff31556a
    );

    draw_cube(
        b->x + b->w * 0.20f,
        b->h * 0.58f,
        b->z - b->d * 0.51f,
        b->w * 0.16f,
        b->h * 0.16f,
        0.08f,
        0xff31556a
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
    ScePspFVector3 pos;
    ScePspFVector3 scale;

    /* Legs */
    draw_cube(
        x,
        y + 0.55f,
        z,
        0.55f,
        1.1f,
        0.45f,
        0xff293342
    );

    /* Torso */
    pos.x = x;
    pos.y = y + 1.35f;
    pos.z = z;

    scale.x = 0.85f;
    scale.y = 1.0f;
    scale.z = 0.55f;

    sceGuColor(shirt);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /* Head */
    draw_cube(
        x,
        y + 2.35f,
        z,
        0.60f,
        0.64f,
        0.60f,
        0xffd3a17d
    );

    /* Hair */
    draw_cube(
        x,
        y + 2.66f,
        z,
        0.66f,
        0.22f,
        0.66f,
        0xff392e29
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
    ScePspFVector3 pos;
    ScePspFVector3 scale;

    /* Younger brother: smaller, child-sized proportions. */
    draw_cube(
        x,
        y + 0.42f,
        z,
        0.46f,
        0.84f,
        0.38f,
        0xff293342
    );

    pos.x = x;
    pos.y = y + 1.05f;
    pos.z = z;

    scale.x = 0.72f;
    scale.y = 0.82f;
    scale.z = 0.50f;

    sceGuColor(shirt);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    draw_cube(
        x,
        y + 1.74f,
        z,
        0.54f,
        0.58f,
        0.54f,
        0xffd3a17d
    );

    draw_cube(
        x,
        y + 2.02f,
        z,
        0.58f,
        0.18f,
        0.58f,
        0xff392e29
    );
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
    ScePspFVector3 scale;

    /* Main body */
    pos.x = x;
    pos.y = van_style ? 1.05f : 0.85f;
    pos.z = z;

    scale.x = van_style ? 4.2f : 3.4f;
    scale.y = van_style ? 1.05f : 0.75f;
    scale.z = van_style ? 1.70f : 1.55f;

    sceGuColor(color);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /* Cabin */
    pos.y = van_style ? 2.0f : 1.5f;

    scale.x = van_style ? 2.6f : 1.9f;
    scale.y = van_style ? 1.0f : 0.65f;
    scale.z = van_style ? 1.48f : 1.30f;

    sceGuColor(0xffe1e5e7);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /* Window band */
    pos.y = van_style ? 2.02f : 1.54f;

    scale.x = van_style ? 2.35f : 1.68f;
    scale.y = van_style ? 0.48f : 0.34f;
    scale.z = van_style ? 1.36f : 1.18f;

    sceGuColor(0xff3e6175);

    sceGumPushMatrix();
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);
    sceGumScale(&scale);

    sceGumDrawArray(
        GU_TRIANGLES,
        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36,
        NULL,
        cube_vertices
    );

    sceGumPopMatrix();

    /* Wheels */
    {
        int side;
        for (side = -1; side <= 1; side += 2)
        {
            float offset_z =
                van_style ? 0.74f : 0.66f;

            float offset_x =
                van_style ? 1.45f : 1.15f;

            pos.y = 0.48f;

            /* Front pair */
            pos.x =
                x +
                cosf(yaw) *
                offset_x -
                sinf(yaw) *
                (float)(side) *
                offset_z;

            pos.z =
                z +
                sinf(yaw) *
                offset_x +
                cosf(yaw) *
                (float)(side) *
                offset_z;

            scale.x = 0.45f;
            scale.y = 0.50f;
            scale.z = 0.30f;

            sceGuColor(0xff1c2024);

            sceGumPushMatrix();
            sceGumTranslate(&pos);
            sceGumRotateY(yaw);
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

            /* Rear pair */
            pos.x =
                x -
                cosf(yaw) *
                offset_x -
                sinf(yaw) *
                (float)(side) *
                offset_z;

            pos.z =
                z -
                sinf(yaw) *
                offset_x +
                cosf(yaw) *
                (float)(side) *
                offset_z;

            sceGumPushMatrix();
            sceGumTranslate(&pos);
            sceGumRotateY(yaw);
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
    }
}

static void draw_bridge(void)
{
    float z = 82.0f;

    /* Approach road */
    draw_cube(
        0.0f,
        -0.06f,
        z,
        32.0f,
        0.12f,
        7.0f,
        0xff595b61
    );

    /* Bridge deck */
    draw_cube(
        0.0f,
        2.5f,
        z + 7.0f,
        28.0f,
        0.55f,
        18.0f,
        0xff8b8c90
    );

    /* Railings */
    draw_cube(
        -13.0f,
        3.8f,
        z + 7.0f,
        0.35f,
        2.2f,
        17.0f,
        0xffd3d7da
    );

    draw_cube(
        13.0f,
        3.8f,
        z + 7.0f,
        0.35f,
        2.2f,
        17.0f,
        0xffd3d7da
    );

    /* Water below */
    draw_cube(
        0.0f,
        -0.30f,
        z + 7.0f,
        40.0f,
        0.15f,
        32.0f,
        0xff4d91bc
    );
}

static void render_city_world(void)
{
    int i;

    /* Ground */
    draw_cube(
        0.0f,
        -0.30f,
        0.0f,
        190.0f,
        0.35f,
        190.0f,
        0xff6b9e5b
    );

    /* Main roads */
    draw_cube(
        0.0f,
        -0.06f,
        0.0f,
        190.0f,
        0.12f,
        12.0f,
        0xff4b4c50
    );

    draw_cube(
        0.0f,
        -0.06f,
        0.0f,
        12.0f,
        0.12f,
        190.0f,
        0xff4b4c50
    );

    /* Secondary roads */
    draw_cube(
        0.0f,
        -0.055f,
        -50.0f,
        190.0f,
        0.11f,
        9.0f,
        0xff56575b
    );

    draw_cube(
        0.0f,
        -0.055f,
        50.0f,
        190.0f,
        0.11f,
        9.0f,
        0xff56575b
    );

    draw_cube(
        -50.0f,
        -0.055f,
        0.0f,
        9.0f,
        0.11f,
        190.0f,
        0xff56575b
    );

    draw_cube(
        50.0f,
        -0.055f,
        0.0f,
        9.0f,
        0.11f,
        190.0f,
        0xff56575b
    );

    /* Waterfront */
    draw_cube(
        0.0f,
        -0.20f,
        -94.0f,
        190.0f,
        0.12f,
        18.0f,
        0xff4c91bd
    );

    draw_cube(
        0.0f,
        -0.10f,
        -84.0f,
        190.0f,
        0.10f,
        4.0f,
        0xffe8d7a4
    );

    /* Buildings */
    for (i = 0; i < BUILDING_COUNT; ++i)
        draw_building(&buildings[i]);

    /* Trees */
    draw_tree(-62.0f, -60.0f, 1.15f);
    draw_tree(-28.0f, -59.0f, 0.90f);
    draw_tree( 28.0f, -60.0f, 1.10f);
    draw_tree( 63.0f, -59.0f, 0.85f);

    draw_tree(-62.0f, 60.0f, 0.95f);
    draw_tree(-28.0f, 61.0f, 1.15f);
    draw_tree( 28.0f, 60.0f, 0.90f);
    draw_tree( 64.0f, 60.0f, 1.10f);

    /* Bridge */
    draw_bridge();

    /* Cars */
    for (i = 0; i < CAR_COUNT; ++i)
    {
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

    /* Pedestrians */
    for (i = 0; i < PEDESTRIAN_COUNT; ++i)
    {
        Pedestrian *p = &pedestrians[i];

        draw_human(
            p->x,
            0.0f,
            p->z,
            0.0f,
            p->shirt
        );
    }
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
    draw_cube(
        -10.5f,
        1.1f,
        13.7f,
        2.0f,
        2.2f,
        0.25f,
        cousin_door_open
            ? 0xff8a6d50
            : 0xff584c44
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
