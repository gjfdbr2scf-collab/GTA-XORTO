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

/* Embedded assets created by bin2o in the Makefile. */
extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define SAVE_DIR   "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE  "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

#define MUSIC_RATE    22050
#define MUSIC_SAMPLES 1024
#define MUSIC_VOLUME  0x6000

typedef enum
{
    GAME_STATE_TITLE = 0,
    GAME_STATE_LANGUAGE,
    GAME_STATE_USERNAME,
    GAME_STATE_SAVE,
    GAME_STATE_MAIN_MENU
} GameState;

typedef struct
{
    unsigned int magic;
    int language;
    char username[32];
} SaveData;

typedef struct
{
    unsigned short u;
    unsigned short v;
    unsigned int color;
    short x;
    short y;
    short z;
} TextureVertex;

typedef struct
{
    short x;
    short y;
    short z;
} LineVertex;

static GameState state = GAME_STATE_TITLE;
static SaveData save_data;
static int game_initialized = 0;
static int selected_language = 0;
static int selected_menu = 0;
static unsigned int old_buttons = 0;

static unsigned int __attribute__((aligned(64))) list[0x20000 / 4];

/* =========================================================
 * SAVE
 * ========================================================= */

static int save_exists(void)
{
    SceUID fd;
    SaveData data;
    int ok = 0;

    fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;

    memset(&data, 0, sizeof(data));

    if (sceIoRead(fd, &data, sizeof(data)) == sizeof(data) &&
        data.magic == SAVE_MAGIC)
    {
        ok = 1;
        save_data = data;
        selected_language = data.language;
        if (selected_language < 0 || selected_language > 8)
            selected_language = 0;
    }

    sceIoClose(fd);
    return ok;
}

static int save_game(void)
{
    SceUID fd;
    int result = 0;

    save_data.magic = SAVE_MAGIC;
    save_data.language = selected_language;

    if (save_data.username[0] == '\0')
        strcpy(save_data.username, "PLAYER");

    sceIoMkdir(SAVE_DIR, 0777);

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC,
        0777
    );

    if (fd >= 0)
    {
        if (sceIoWrite(fd, &save_data, sizeof(save_data)) == sizeof(save_data))
            result = 1;

        sceIoClose(fd);
    }

    return result;
}

/* =========================================================
 * TEXTURE / UNDERLINE
 * ========================================================= */

static void draw_texture(const void *texture)
{
    TextureVertex *v = (TextureVertex *)sceGuGetMemory(
        2 * sizeof(TextureVertex)
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

    sceGuDisable(GU_BLEND);
    sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
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

static void draw_underline(int x, int y, int width)
{
    LineVertex *v = (LineVertex *)sceGuGetMemory(
        2 * sizeof(LineVertex)
    );

    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].x = x + width;
    v[1].y = y + 3;
    v[1].z = 0;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuColor(0xffffffff);

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );
}

/* =========================================================
 * SMALL BITMAP FONT - used only for SAVE screen
 * ========================================================= */

static const unsigned char G_S[7] = {0x1f,0x10,0x10,0x1f,0x01,0x01,0x1f};
static const unsigned char G_A[7] = {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11};
static const unsigned char G_V[7] = {0x11,0x11,0x11,0x11,0x11,0x0a,0x04};
static const unsigned char G_E[7] = {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f};
static const unsigned char G_P[7] = {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10};
static const unsigned char G_R[7] = {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11};
static const unsigned char G_X[7] = {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11};
static const unsigned char G_T[7] = {0x1f,0x04,0x04,0x04,0x04,0x04,0x04};
static const unsigned char G_O[7] = {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e};

static const unsigned char *glyph_for_char(char c)
{
    switch (c)
    {
        case 'S': return G_S;
        case 'A': return G_A;
        case 'V': return G_V;
        case 'E': return G_E;
        case 'P': return G_P;
        case 'R': return G_R;
        case 'X': return G_X;
        case 'T': return G_T;
        case 'O': return G_O;
        default:  return NULL;
    }
}

static int simple_text_width(const char *text, int scale)
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

static void draw_simple_text(
    const char *text,
    int x,
    int y,
    int scale
)
{
    int count = 0;
    int i = 0;
    int row;
    int col;
    int current_x = x;
    const char *p;
    LineVertex *v;

    p = text;
    while (*p)
    {
        const unsigned char *g = glyph_for_char(*p);
        if (g != NULL)
        {
            for (row = 0; row < 7; ++row)
                for (col = 0; col < 5; ++col)
                    if (g[row] & (1 << (4 - col)))
                        ++count;
        }
        ++p;
    }

    if (count == 0)
        return;

    v = (LineVertex *)sceGuGetMemory(
        count * 2 * sizeof(LineVertex)
    );

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuColor(0xffffffff);

    p = text;
    while (*p)
    {
        const unsigned char *g = glyph_for_char(*p);

        if (g != NULL)
        {
            for (row = 0; row < 7; ++row)
            {
                for (col = 0; col < 5; ++col)
                {
                    if (g[row] & (1 << (4 - col)))
                    {
                        v[i].x = current_x + col * scale;
                        v[i].y = y + row * scale;
                        v[i].z = 0;

                        v[i + 1].x = current_x + (col + 1) * scale;
                        v[i + 1].y = y + (row + 1) * scale;
                        v[i + 1].z = 0;

                        i += 2;
                    }
                }
            }
        }

        current_x += 6 * scale;
        ++p;
    }

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        i,
        NULL,
        v
    );
}

/* =========================================================
 * MUSIC
 * ========================================================= */

static volatile int music_running = 0;
static volatile unsigned int music_position = 0;
static SceUID music_thread = -1;
static short __attribute__((aligned(64))) music_buffer[MUSIC_SAMPLES];

static int music_thread_func(SceSize args, void *argp)
{
    const short *samples = (const short *)Music_start;
    unsigned int sample_count =
        (unsigned int)(Music_end - Music_start) / sizeof(short);
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

            music_buffer[i] = samples[music_position++];
        }

        sceVaudioOutputBlocking(MUSIC_VOLUME, music_buffer);
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
        sceKernelStartThread(music_thread, 0, NULL);
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
        sceKernelWaitThreadEnd(music_thread, NULL);
        music_thread = -1;
    }
}

/* =========================================================
 * USERNAME / OSK
 * ========================================================= */

static SceUtilityOskParams osk_params;
static SceUtilityOskData osk_data;
static unsigned short osk_desc[64];
static unsigned short osk_input[32];
static unsigned short osk_output[32];
static int osk_started = 0;
static int osk_shutdown_requested = 0;

static void ascii_to_ushort(
    unsigned short *dst,
    unsigned int cap,
    const char *src
)
{
    unsigned int i = 0;

    while (i + 1 < cap && src[i] != '\0')
    {
        dst[i] = (unsigned short)(unsigned char)src[i];
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
        "Choose your username"
    );

    osk_data.language =
        osk_language_for_game_language(selected_language);

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
    osk_params.base.language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
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
    int status;

    if (!osk_started)
        return;

    status = sceUtilityOskGetStatus();

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

                if (osk_data.result != PSP_UTILITY_OSK_RESULT_CANCELLED)
                {
                    memset(save_data.username, 0, sizeof(save_data.username));

                    for (i = 0;
                         i + 1 < sizeof(save_data.username) &&
                         i < 31 &&
                         osk_output[i] != 0;
                         ++i)
                    {
                        save_data.username[i] =
                            (osk_output[i] < 128) ?
                            (char)osk_output[i] : '?';
                    }

                    if (save_data.username[0] == '\0')
                        strcpy(save_data.username, "PLAYER");

                    /* Do not save yet. Go to explicit SAVE screen. */
                    state = GAME_STATE_SAVE;
                }
                else
                {
                    state = GAME_STATE_LANGUAGE;
                }
            }
            break;

        default:
            break;
    }
}

/* =========================================================
 * GU
 * ========================================================= */

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

    sceGuDisplay(GU_TRUE);
}

/* =========================================================
 * INIT
 * ========================================================= */

int psp_game_init(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    gu_init();

    state = GAME_STATE_TITLE;
    selected_language = 0;
    selected_menu = 0;
    old_buttons = 0;

    memset(&save_data, 0, sizeof(save_data));

    game_initialized = 1;

    return 0;
}

/* =========================================================
 * UPDATE
 * ========================================================= */

void psp_game_update(void)
{
    SceCtrlData pad;
    unsigned int pressed;

    if (!game_initialized)
        return;

    if (state == GAME_STATE_USERNAME)
    {
        username_update();
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
                    state = GAME_STATE_MAIN_MENU;
                }
                else
                {
                    state = GAME_STATE_LANGUAGE;
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
                music_stop();
                state = GAME_STATE_TITLE;
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                state = GAME_STATE_USERNAME;
                username_begin();
            }
            break;

        case GAME_STATE_SAVE:
            /* X is the actual SAVE button. */
            if (pressed & PSP_CTRL_CROSS)
            {
                save_game();
                selected_menu = 0;
                state = GAME_STATE_MAIN_MENU;
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                state = GAME_STATE_USERNAME;
                username_begin();
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

            /* X confirms the highlighted menu item. */
            if (pressed & PSP_CTRL_CROSS)
            {
                /* Game modes can be wired in next. */
            }
            break;

        default:
            break;
    }
}

/* =========================================================
 * RENDER
 * ========================================================= */

void psp_game_render(void)
{
    static const int language_line_y[9] =
    {
        54, 80, 107, 134, 161, 187, 214, 240, 266
    };

    static const int main_line_y[4] =
    {
        147, 175, 203, 231
    };

    static const int main_line_width[4] =
    {
        126, 165, 214, 176
    };

    int save_x;
    int prompt_x;

    if (!game_initialized)
        return;

    if (state == GAME_STATE_USERNAME)
        return;

    sceGuStart(GU_DIRECT, list);

    sceGuClearColor(0x00000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    switch (state)
    {
        case GAME_STATE_TITLE:
            draw_texture(Title_start);
            break;

        case GAME_STATE_LANGUAGE:
            draw_texture(LanguageSelection_start);

            /* No white rectangle: just underline the selected word. */
            draw_underline(
                145,
                language_line_y[selected_language],
                70
            );
            break;

        case GAME_STATE_SAVE:
            save_x =
                (SCREEN_WIDTH - simple_text_width("SAVE", 6)) / 2;

            prompt_x =
                (SCREEN_WIDTH - simple_text_width("PRESS X TO SAVE", 3)) / 2;

            draw_simple_text(
                "SAVE",
                save_x,
                80,
                6
            );

            draw_simple_text(
                "PRESS X TO SAVE",
                prompt_x,
                165,
                3
            );
            break;

        case GAME_STATE_MAIN_MENU:
            draw_texture(MainMenu_start);

            /* No filled white box: underline the selected option. */
            draw_underline(
                22,
                main_line_y[selected_menu],
                main_line_width[selected_menu]
            );
            break;

        default:
            break;
    }

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* =========================================================
 * SHUTDOWN
 * ========================================================= */

void psp_game_shutdown(void)
{
    if (!game_initialized)
        return;

    music_stop();

    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    game_initialized = 0;
}

/* =========================================================
 * COMPATIBILITY WRAPPERS
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
