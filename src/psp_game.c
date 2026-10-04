#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psputility.h>
#include <psputility_osk.h>
#include <psputility_sysparam.h>
#include <pspiofilemgr.h>
#include <pspvaudio.h>

#include <string.h>

#include "psp_game.h"

/* Embedded assets */
extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

/* PSP screen */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

/* Save data */
#define SAVE_DIR  "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

/* Music */
#define MUSIC_RATE   22050
#define MUSIC_SAMPLES 1024
#define MUSIC_VOLUME 0x6000

typedef enum
{
    GAME_STATE_TITLE = 0,
    GAME_STATE_LANGUAGE,
    GAME_STATE_USERNAME,
    GAME_STATE_SAVE,
    GAME_STATE_SAVE_SUCCESS,
    GAME_STATE_MAIN_MENU,
    GAME_STATE_STORY,
    GAME_STATE_FREE_WORLD,
    GAME_STATE_MULTIPLAYER,
    GAME_STATE_SETTINGS
} GameState;

typedef struct
{
    unsigned int magic;
    int language;
    char username[32];
} SaveData;

static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;
static int selected_language = 0;
static int selected_menu = 0;
static int settings_selection = 0;
static unsigned int old_buttons = 0;

/* Smooth fade transitions */
static int fade_alpha = 0;
static int fade_direction = 0;   /* 1 = to black, -1 = from black */
static GameState fade_next_state = GAME_STATE_TITLE;
static int success_timer = 0;

#define FADE_STEP 10
#define SUCCESS_HOLD_FRAMES 90

static SaveData save_data;

static unsigned int __attribute__((aligned(64)))
    list[0x20000 / 4];

/* Story progress */
static int story_step = 0;

/* Peaceful top-down world */
static float player_x = 240.0f;
static float player_y = 145.0f;

static float player2_x = 320.0f;
static float player2_y = 145.0f;

static int flower_collected = 0;

/* Forward declarations used before their definitions */
static void username_begin(void);
static void start_transition(GameState next_state);

/* Music */
static volatile int music_running = 0;
static volatile unsigned int music_position = 0;
static SceUID music_thread = -1;

static short __attribute__((aligned(64)))
    music_buffer[MUSIC_SAMPLES];

/* Username / OSK */
static SceUtilityOskParams osk_params;
static SceUtilityOskData osk_data;
static unsigned short osk_desc[64];
static unsigned short osk_input[32];
static unsigned short osk_output[32];
static int osk_started = 0;
static int osk_shutdown_requested = 0;

/* ------------------------------------------------------------------------- */
/* Save                                                                       */
/* ------------------------------------------------------------------------- */

static int save_exists(void)
{
    SceUID fd;
    SaveData data;

    fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0);
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

static int save_game(void)
{
    SceUID fd;
    int written;

    sceIoMkdir(SAVE_DIR, 0777);

    /*
     * Keep the username that was just entered.
     * Only update the save header and language here.
     */
    save_data.magic = SAVE_MAGIC;
    save_data.language = selected_language;

    if (save_data.username[0] == '\0')
        strcpy(save_data.username, "PLAYER");

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC,
        0777
    );

    if (fd < 0)
        return 0;

    written = sceIoWrite(
        fd,
        &save_data,
        sizeof(save_data)
    );

    sceIoClose(fd);

    return written == sizeof(save_data);
}

static void load_game(void)
{
    SceUID fd;

    fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;

    if (sceIoRead(fd, &save_data, sizeof(save_data)) != sizeof(save_data))
    {
        sceIoClose(fd);
        return;
    }

    sceIoClose(fd);

    if (save_data.magic != SAVE_MAGIC)
        return;

    selected_language = save_data.language;
}

/* ------------------------------------------------------------------------- */
/* Small 5x7 font                                                            */
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

    return width ? width - scale : 0;
}

static void draw_text(
    const char *text,
    int x,
    int y,
    int scale,
    unsigned int color
)
{
    int pixel_count = 0;
    const char *p;

    for (p = text; *p; ++p)
    {
        const unsigned char *g = get_glyph(*p);
        int row, col;

        if (!g)
            continue;

        for (row = 0; row < 7; ++row)
            for (col = 0; col < 5; ++col)
                if (g[row] & (1 << (4 - col)))
                    ++pixel_count;
    }

    if (pixel_count <= 0)
        return;

    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    Vertex *v = (Vertex *)sceGuGetMemory(
        pixel_count * 2 * sizeof(Vertex)
    );

    int n = 0;
    int current_x = x;

    sceGuColor(color);

    for (p = text; *p; ++p)
    {
        const unsigned char *g = get_glyph(*p);
        int row, col;

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


static void draw_text_centered(
    const char *text,
    int y,
    int scale,
    unsigned int color
)
{
    int width = text_width(text, scale);
    int x = (SCREEN_WIDTH - width) / 2;

    draw_text(
        text,
        x,
        y,
        scale,
        color
    );
}

/* ------------------------------------------------------------------------- */
/* Primitive drawing                                                         */
/* ------------------------------------------------------------------------- */

static void draw_rect(
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

    Vertex *v = (Vertex *)sceGuGetMemory(
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

static void draw_circle(
    int x,
    int y,
    int radius,
    unsigned int color
)
{
    int i;
    int segments = 16;

    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    Vertex *v = (Vertex *)sceGuGetMemory(
        (segments + 2) * sizeof(Vertex)
    );

    v[0].x = (short)x;
    v[0].y = (short)y;
    v[0].z = 0;

    for (i = 0; i <= segments; ++i)
    {
        /* Small integer circle approximation without libm. */
        static const short cx[17] =
        {100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38,0,38,71,92,100};

        static const short cy[17] =
        {0,38,71,92,100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38,0};

        v[i + 1].x =
            (short)(x + (radius * cx[i]) / 100);
        v[i + 1].y =
            (short)(y + (radius * cy[i]) / 100);
        v[i + 1].z = 0;
    }

    sceGuColor(color);

    sceGuDrawArray(
        GU_TRIANGLE_FAN,
        GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        segments + 2,
        NULL,
        v
    );
}

/* ------------------------------------------------------------------------- */
/* Texture                                                                    */
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
    } Vertex;

    Vertex *v = (Vertex *)sceGuGetMemory(
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

    sceGuTexMode(GU_PSM_5650, 0, 0, 0);
    sceGuTexImage(0, 512, 512, 512, texture);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    sceGuEnable(GU_TEXTURE_2D);

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

    sceGuDisable(GU_TEXTURE_2D);
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

    if (!sample_count)
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
/* Username                                                                   */
/* ------------------------------------------------------------------------- */

static void ascii_to_ushort(
    unsigned short *dst,
    unsigned int cap,
    const char *src
)
{
    unsigned int i = 0;

    while (i + 1 < cap && src[i])
    {
        dst[i] =
            (unsigned short)(unsigned char)src[i];
        ++i;
    }

    dst[i] = 0;
}

static int osk_language_for_game_language(int language)
{
    switch (language)
    {
        case 0: return PSP_UTILITY_OSK_LANGUAGE_GERMAN;
        case 1: return PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
        case 2: return PSP_UTILITY_OSK_LANGUAGE_FRENCH;
        case 3: return PSP_UTILITY_OSK_LANGUAGE_SPANISH;
        case 4: return PSP_UTILITY_OSK_LANGUAGE_ITALIAN;
        case 5: return PSP_UTILITY_OSK_LANGUAGE_DUTCH;
        default: return PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
    }
}

static void username_begin(void)
{
    memset(&osk_params, 0, sizeof(osk_params));
    memset(&osk_data, 0, sizeof(osk_data));
    memset(osk_desc, 0, sizeof(osk_desc));
    memset(osk_input, 0, sizeof(osk_input));
    memset(osk_output, 0, sizeof(osk_output));

    ascii_to_ushort(
        osk_desc,
        64,
        "CHOOSE USERNAME"
    );

    osk_data.language =
        osk_language_for_game_language(
            selected_language
        );

    osk_data.inputtype =
        PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE |
        PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE |
        PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT;

    osk_data.lines = 1;
    osk_data.desc = osk_desc;
    osk_data.intext = osk_input;
    osk_data.outtextlength = 32;
    osk_data.outtext = osk_output;
    osk_data.outtextlimit = 16;

    osk_params.base.size = sizeof(osk_params);
    osk_params.base.language =
        PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    osk_params.base.buttonSwap = 1;
    osk_params.base.graphicsThread = 0x11;
    osk_params.base.accessThread = 0x13;
    osk_params.base.fontThread = 0x12;
    osk_params.base.soundThread = 0x10;

    osk_params.datacount = 1;
    osk_params.data = &osk_data;

    if (sceUtilityOskInitStart(&osk_params) >= 0)
    {
        osk_started = 1;
        osk_shutdown_requested = 0;
    }
}

static void username_update(void)
{
    int status = sceUtilityOskGetStatus();

    if (!osk_started)
        return;

    switch (status)
    {
        case PSP_UTILITY_OSK_DIALOG_VISIBLE:
        case PSP_UTILITY_OSK_DIALOG_INITING:
        case PSP_UTILITY_OSK_DIALOG_INITED:
            sceUtilityOskUpdate(1);
            break;

        case PSP_UTILITY_OSK_DIALOG_QUIT:
            if (!osk_shutdown_requested)
            {
                sceUtilityOskShutdownStart();
                osk_shutdown_requested = 1;
            }
            break;

        case PSP_UTILITY_OSK_DIALOG_FINISHED:
            if (!osk_shutdown_requested)
            {
                sceUtilityOskShutdownStart();
                osk_shutdown_requested = 1;
            }
            else
            {
                unsigned int i;

                osk_started = 0;
                osk_shutdown_requested = 0;

                if (osk_data.result !=
                    PSP_UTILITY_OSK_RESULT_CANCELLED)
                {
                    memset(
                        save_data.username,
                        0,
                        sizeof(save_data.username)
                    );

                    for (i = 0;
                         i + 1 < sizeof(save_data.username) &&
                         i < 31 &&
                         osk_output[i] != 0;
                         ++i)
                    {
                        save_data.username[i] =
                            (osk_output[i] < 128)
                            ? (char)osk_output[i]
                            : '?';
                    }

                    if (!save_data.username[0])
                        strcpy(save_data.username, "PLAYER");

                    start_transition(GAME_STATE_SAVE);
                }
                else
                {
                    start_transition(GAME_STATE_LANGUAGE);
                }
            }
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Fade transitions                                                          */
/* ------------------------------------------------------------------------- */

static void start_transition(GameState next_state)
{
    fade_next_state = next_state;
    fade_direction = 1;
}

static void update_fade(void)
{
    if (fade_direction == 1)
    {
        fade_alpha += FADE_STEP;

        if (fade_alpha >= 255)
        {
            fade_alpha = 255;
            state = fade_next_state;
            fade_direction = -1;

            if (state == GAME_STATE_SAVE_SUCCESS)
            {
                success_timer = SUCCESS_HOLD_FRAMES;
            }
            else if (state == GAME_STATE_USERNAME)
            {
                username_begin();
            }
        }

        return;
    }

    if (fade_direction == -1)
    {
        fade_alpha -= FADE_STEP;

        if (fade_alpha <= 0)
        {
            fade_alpha = 0;
            fade_direction = 0;
        }
    }
}

static void draw_fade_overlay(void)
{
    if (fade_alpha <= 0)
        return;

    sceGuEnable(GU_BLEND);

    sceGuBlendFunc(
        GU_ADD,
        GU_SRC_ALPHA,
        GU_ONE_MINUS_SRC_ALPHA,
        0,
        0
    );

    draw_rect(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        ((unsigned int)fade_alpha << 24) | 0x000000
    );

    sceGuDisable(GU_BLEND);
}

/* ------------------------------------------------------------------------- */
/* World helpers                                                              */
/* ------------------------------------------------------------------------- */

static void clamp_player(
    float *x,
    float *y
)
{
    if (*x < 18.0f) *x = 18.0f;
    if (*y < 58.0f) *y = 58.0f;
    if (*x > SCREEN_WIDTH - 18.0f)
        *x = SCREEN_WIDTH - 18.0f;
    if (*y > SCREEN_HEIGHT - 18.0f)
        *y = SCREEN_HEIGHT - 18.0f;
}

static void update_player_from_dpad(
    float *x,
    float *y,
    unsigned int buttons,
    float speed
)
{
    if (buttons & PSP_CTRL_LEFT)  *x -= speed;
    if (buttons & PSP_CTRL_RIGHT) *x += speed;
    if (buttons & PSP_CTRL_UP)    *y -= speed;
    if (buttons & PSP_CTRL_DOWN)  *y += speed;

    clamp_player(x, y);
}

static float absf_local(float v)
{
    return v < 0.0f ? -v : v;
}

static float distance2_local(
    float ax,
    float ay,
    float bx,
    float by
)
{
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

/* ------------------------------------------------------------------------- */
/* GU setup                                                                   */
/* ------------------------------------------------------------------------- */

static void gu_init(void)
{
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

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceGuDisplay(GU_TRUE);
}

/* ------------------------------------------------------------------------- */
/* Init                                                                        */
/* ------------------------------------------------------------------------- */

int psp_game_init(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    gu_init();

    state = GAME_STATE_TITLE;
    selected_language = 0;
    selected_menu = 0;
    settings_selection = 0;
    story_step = 0;
    flower_collected = 0;

    player_x = 240.0f;
    player_y = 145.0f;

    player2_x = 320.0f;
    player2_y = 145.0f;

    old_buttons = 0;

    fade_alpha = 0;
    fade_direction = 0;
    fade_next_state = GAME_STATE_TITLE;
    success_timer = 0;

    memset(&save_data, 0, sizeof(save_data));

    game_initialized = 1;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Update                                                                      */
/* ------------------------------------------------------------------------- */

void psp_game_update(void)
{
    SceCtrlData pad;
    unsigned int pressed;
    float ax;
    float ay;

    if (!game_initialized)
        return;

    /*
     * Fade transitions lock input so the next screen appears
     * only after the transition is complete.
     */
    if (fade_direction != 0)
    {
        update_fade();
        return;
    }

    if (state == GAME_STATE_USERNAME)
    {
        username_update();
        return;
    }

    if (state == GAME_STATE_SAVE_SUCCESS)
    {
        if (success_timer > 0)
        {
            --success_timer;

            if (success_timer == 0)
                start_transition(GAME_STATE_MAIN_MENU);
        }

        return;
    }

    sceCtrlReadBufferPositive(&pad, 1);

    pressed = pad.Buttons & ~old_buttons;
    old_buttons = pad.Buttons;

    switch (state)
    {
        case GAME_STATE_TITLE:
            if (pressed & PSP_CTRL_START)
            {
                music_start();

                if (save_exists())
                {
                    load_game();
                    start_transition(GAME_STATE_MAIN_MENU);
                }
                else
                {
                    start_transition(GAME_STATE_LANGUAGE);
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
                state = GAME_STATE_TITLE;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                state = GAME_STATE_USERNAME;
                username_begin();
            }
            break;

        case GAME_STATE_SAVE:
            if (pressed & PSP_CTRL_CROSS)
            {
                if (save_game())
                {
                    selected_menu = 0;
                    start_transition(GAME_STATE_SAVE_SUCCESS);
                }
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(GAME_STATE_USERNAME);
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

            if (pressed & PSP_CTRL_CIRCLE)
            {
                /*
                 * Circle goes back to the title screen.
                 * It does not erase the save.
                 */
                state = GAME_STATE_TITLE;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                switch (selected_menu)
                {
                    case 0:
                        story_step = 0;
                        flower_collected = 0;
                        player_x = 80.0f;
                        player_y = 180.0f;
                        state = GAME_STATE_STORY;
                        break;

                    case 1:
                        player_x = 240.0f;
                        player_y = 145.0f;
                        state = GAME_STATE_FREE_WORLD;
                        break;

                    case 2:
                        player_x = 145.0f;
                        player_y = 180.0f;
                        player2_x = 335.0f;
                        player2_y = 180.0f;
                        state = GAME_STATE_MULTIPLAYER;
                        break;

                    case 3:
                        settings_selection = 0;
                        state = GAME_STATE_SETTINGS;
                        break;
                }
            }
            break;

        case GAME_STATE_STORY:
            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.0f
            );

            if (pressed & PSP_CTRL_CIRCLE)
            {
                state = GAME_STATE_MAIN_MENU;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                if (story_step == 0 &&
                    distance2_local(
                        player_x, player_y,
                        395.0f, 88.0f
                    ) < 32.0f * 32.0f)
                {
                    story_step = 1;
                }
                else if (story_step == 1 &&
                         distance2_local(
                             player_x, player_y,
                             108.0f, 215.0f
                         ) < 32.0f * 32.0f)
                {
                    flower_collected = 1;
                    story_step = 2;
                }
                else if (story_step == 2 &&
                         distance2_local(
                             player_x, player_y,
                             80.0f, 180.0f
                         ) < 32.0f * 32.0f)
                {
                    story_step = 3;
                }
            }
            break;

        case GAME_STATE_FREE_WORLD:
            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.5f
            );

            if (pressed & PSP_CTRL_CIRCLE)
                state = GAME_STATE_MAIN_MENU;
            break;

        case GAME_STATE_MULTIPLAYER:
            /*
             * Local co-op prototype:
             * Player 1 uses D-pad.
             * Player 2 uses the analog stick.
             */
            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.0f
            );

            ax = ((float)pad.Lx - 128.0f) / 127.0f;
            ay = ((float)pad.Ly - 128.0f) / 127.0f;

            if (absf_local(ax) > 0.20f)
                player2_x += ax * 2.0f;

            if (absf_local(ay) > 0.20f)
                player2_y += ay * 2.0f;

            clamp_player(
                &player2_x,
                &player2_y
            );

            if (pressed & PSP_CTRL_CIRCLE)
                state = GAME_STATE_MAIN_MENU;
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
                    /*
                     * Music toggle is deliberately simple:
                     * selecting it stops current music.
                     * Starting a new session starts it again.
                     */
                    music_stop();
                }
                else
                {
                    /*
                     * Reset save.
                     */
                    sceIoRemove(SAVE_FILE);
                    state = GAME_STATE_TITLE;
                }
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                state = GAME_STATE_MAIN_MENU;
            }
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Render                                                                      */
/* ------------------------------------------------------------------------- */

static void render_peaceful_world(
    const char *mode_name
)
{
    int i;

    /* Sky */
    draw_rect(
        0, 0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0xff9fd6f5
    );

    /* Grass */
    draw_rect(
        0, 58,
        SCREEN_WIDTH,
        SCREEN_HEIGHT - 58,
        0xff70ad59
    );

    /* Road */
    draw_rect(
        0, 120,
        SCREEN_WIDTH,
        52,
        0xffd4c19b
    );

    draw_rect(
        210, 58,
        60,
        SCREEN_HEIGHT - 58,
        0xffd4c19b
    );

    /* Water */
    draw_rect(
        305, 185,
        155,
        60,
        0xff5daed0
    );

    /* House */
    draw_rect(
        45, 82,
        72,
        55,
        0xfff2dfb8
    );

    draw_rect(
        37, 68,
        88,
        20,
        0xffb45e4d
    );

    /* Trees */
    for (i = 0; i < 6; ++i)
    {
        int tx = 145 + (i * 47) % 300;
        int ty = 80 + (i * 29) % 150;

        draw_rect(
            tx - 3,
            ty + 14,
            6,
            18,
            0xff835f3d
        );

        draw_circle(
            tx,
            ty,
            13,
            0xff2f8748
        );
    }

    draw_rect(
        12, 8,
        456, 38,
        0xcc16252d
    );

    draw_text(
        mode_name,
        24,
        19,
        3,
        0xffffffff
    );

    draw_rect(
        0, 244,
        480, 28,
        0xcc16252d
    );

    draw_text(
        "O BACK",
        18,
        253,
        2,
        0xffffffff
    );

    draw_text(
        "X INTERACT",
        356,
        253,
        2,
        0xffffffff
    );

    /* Player 1 */
    draw_circle(
        (int)player_x,
        (int)player_y,
        9,
        0xfff4f1e8
    );

    draw_rect(
        (int)player_x - 5,
        (int)player_y + 6,
        10,
        7,
        0xff446b9a
    );
}

/* ------------------------------------------------------------------------- */
/* Frame render                                                                */
/* ------------------------------------------------------------------------- */

void psp_game_render(void)
{
    int menu_y;
    int language_y;
    int settings_y;
    int center_x;

    if (!game_initialized)
        return;

    if (state == GAME_STATE_USERNAME)
        return;

    sceGuStart(GU_DIRECT, list);

    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    switch (state)
    {
        case GAME_STATE_TITLE:
            draw_texture(Title_start);
            break;

        case GAME_STATE_LANGUAGE:
            draw_texture(LanguageSelection_start);

            /*
             * Thin underline instead of a white overlay.
             * This leaves the language text completely readable.
             */
            language_y =
                201 +
                selected_language * 33;

            draw_rect(
                376,
                language_y,
                70,
                3,
                0xff63b5ff
            );
            break;

        case GAME_STATE_SAVE:
            center_x =
                (SCREEN_WIDTH -
                 text_width("SAVE", 6)) / 2;

            draw_text(
                "SAVE",
                center_x,
                75,
                6,
                0xffffffff
            );

            center_x =
                (SCREEN_WIDTH -
                 text_width("PRESS X TO SAVE", 3)) / 2;

            draw_text(
                "PRESS X TO SAVE",
                center_x,
                160,
                3,
                0xffffffff
            );

            center_x =
                (SCREEN_WIDTH -
                 text_width("O BACK", 2)) / 2;

            draw_text(
                "O BACK",
                center_x,
                205,
                2,
                0xffc8d8e8
            );
            break;

        case GAME_STATE_SAVE_SUCCESS:
            /*
             * Exact message requested by the user.
             * It fades in, stays visible briefly, then fades
             * into the Main Menu automatically.
             */
            draw_text_centered(
                "THE DATA HAS SUCCESSFUL SAVED",
                105,
                2,
                0xffffffff
            );
            break;

        case GAME_STATE_MAIN_MENU:
            draw_texture(MainMenu_start);

            /*
             * Thin underline only.
             * No opaque highlight over the words.
             */
            switch (selected_menu)
            {
                case 0: menu_y = 153; break;
                case 1: menu_y = 184; break;
                case 2: menu_y = 215; break;
                default: menu_y = 246; break;
            }

            draw_rect(
                72,
                menu_y,
                235,
                3,
                0xff63b5ff
            );
            break;

        case GAME_STATE_STORY:
            render_peaceful_world("STORY");

            /*
             * Three peaceful objectives.
             */
            if (story_step == 0)
            {
                draw_circle(
                    395,
                    88,
                    12,
                    0xfff7d85a
                );

                draw_text(
                    "VISIT THE HOUSE",
                    24,
                    54,
                    2,
                    0xff172126
                );
            }
            else if (story_step == 1)
            {
                draw_circle(
                    108,
                    215,
                    12,
                    0xfff7d85a
                );

                draw_text(
                    "FIND THE FLOWER",
                    24,
                    54,
                    2,
                    0xff172126
                );
            }
            else if (story_step == 2)
            {
                draw_text(
                    "RETURN HOME",
                    24,
                    54,
                    2,
                    0xff172126
                );
            }
            else
            {
                draw_text(
                    "STORY COMPLETE",
                    24,
                    54,
                    2,
                    0xff172126
                );
            }

            if (flower_collected)
            {
                draw_circle(
                    108,
                    215,
                    7,
                    0xfff26b91
                );
            }
            break;

        case GAME_STATE_FREE_WORLD:
            render_peaceful_world("FREE WORLD");

            draw_text(
                "EXPLORE THE WORLD",
                24,
                54,
                2,
                0xff172126
            );
            break;

        case GAME_STATE_MULTIPLAYER:
            render_peaceful_world("LOCAL CO OP");

            /*
             * Player 2 is green and uses the analog stick.
             */
            draw_circle(
                (int)player2_x,
                (int)player2_y,
                9,
                0xfff4f1e8
            );

            draw_rect(
                (int)player2_x - 5,
                (int)player2_y + 6,
                10,
                7,
                0xff3f8f62
            );

            draw_text(
                "P1 D PAD  P2 ANALOG",
                24,
                54,
                2,
                0xff172126
            );
            break;

        case GAME_STATE_SETTINGS:
            draw_rect(
                0, 0,
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

            settings_y =
                90 +
                settings_selection * 62;

            draw_rect(
                24,
                settings_y - 8,
                360,
                44,
                0xff334955
            );

            draw_rect(
                24,
                settings_y + 32,
                360,
                2,
                0xff8ec7ff
            );

            draw_text(
                "MUSIC",
                42,
                102,
                3,
                0xffffffff
            );

            draw_text(
                "RESET SAVE",
                42,
                164,
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
     * Fade is drawn last so it covers the complete 480x272 frame.
     */
    draw_fade_overlay();

    sceGuFinish();

    sceGuSync(
        GU_SYNC_FINISH,
        GU_SYNC_WHAT_DONE
    );

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ------------------------------------------------------------------------- */
/* Shutdown                                                                    */
/* ------------------------------------------------------------------------- */

void psp_game_shutdown(void)
{
    if (!game_initialized)
        return;

    music_stop();

    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    game_initialized = 0;
}

/* Compatibility wrappers */
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
