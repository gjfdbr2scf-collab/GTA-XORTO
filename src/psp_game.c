#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psputility.h>
#include <psputility_osk.h>
#include <psputility_sysparam.h>
#include <pspvaudio.h>

#include <string.h>

#include "psp_game.h"

/* Embedded assets */
extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

/* Screen */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

/* Save data */
#define SAVE_DIR  "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

/* Music */
#define MUSIC_RATE   22050
#define MUSIC_SAMPLES_PER_BUFFER 1024
#define MUSIC_VOLUME 0x6000

typedef enum
{
    GAME_STATE_TITLE = 0,
    GAME_STATE_LANGUAGE,
    GAME_STATE_USERNAME,
    GAME_STATE_MAIN_MENU
} GameState;

static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;
static int selected_language = 0;
static int selected_menu = 0;
static unsigned int old_buttons = 0;

static unsigned int __attribute__((aligned(64))) list[0x20000 / 4];

/* ------------------------------------------------------------------------- */
/* Save data                                                                 */
/* ------------------------------------------------------------------------- */

typedef struct
{
    unsigned int magic;
    int language;
    char username[32];
} SaveData;

static SaveData save_data;

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
    }

    sceIoClose(fd);

    return ok;
}

static void save_game(void)
{
    SceUID fd;

    memset(&save_data, 0, sizeof(save_data));

    save_data.magic = SAVE_MAGIC;
    save_data.language = selected_language;

    if (save_data.username[0] == '\0')
    {
        strncpy(
            save_data.username,
            "PLAYER",
            sizeof(save_data.username) - 1
        );
    }

    sceIoMkdir(SAVE_DIR, 0777);

    fd = sceIoOpen(
        SAVE_FILE,
        PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC,
        0777
    );

    if (fd >= 0)
    {
        sceIoWrite(fd, &save_data, sizeof(save_data));
        sceIoClose(fd);
    }
}

/* ------------------------------------------------------------------------- */
/* Texture rendering                                                         */
/* ------------------------------------------------------------------------- */

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
} ColorVertex;

static void draw_texture(const void *texture)
{
    TextureVertex *v =
        (TextureVertex *)sceGuGetMemory(2 * sizeof(TextureVertex));

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

static void draw_highlight(int x, int y, int w, int h)
{
    ColorVertex *v =
        (ColorVertex *)sceGuGetMemory(2 * sizeof(ColorVertex));

    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].x = x + w;
    v[1].y = y + h;
    v[1].z = 0;

    sceGuEnable(GU_BLEND);

    sceGuBlendFunc(
        GU_ADD,
        GU_SRC_ALPHA,
        GU_ONE_MINUS_SRC_ALPHA,
        0,
        0
    );

    sceGuColor(0x60ffffff);

    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        2,
        NULL,
        v
    );

    sceGuDisable(GU_BLEND);
}

/* ------------------------------------------------------------------------- */
/* Music                                                                     */
/* ------------------------------------------------------------------------- */

static SceUID music_thread = -1;
static volatile int music_running = 0;
static volatile unsigned int music_position = 0;

static short __attribute__((aligned(64)))
    music_buffer[MUSIC_SAMPLES_PER_BUFFER];

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
        return 0;

    channel = sceVaudioChReserve(
        MUSIC_SAMPLES_PER_BUFFER,
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
        for (i = 0; i < MUSIC_SAMPLES_PER_BUFFER; ++i)
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
/* Username / PSP OSK                                                        */
/* ------------------------------------------------------------------------- */

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
        case 0:
            return PSP_UTILITY_OSK_LANGUAGE_GERMAN;

        case 1:
            return PSP_UTILITY_OSK_LANGUAGE_ENGLISH;

        case 2:
            return PSP_UTILITY_OSK_LANGUAGE_FRENCH;

        case 3:
            return PSP_UTILITY_OSK_LANGUAGE_SPANISH;

        case 4:
            return PSP_UTILITY_OSK_LANGUAGE_ITALIAN;

        case 5:
            return PSP_UTILITY_OSK_LANGUAGE_DUTCH;

        default:
            return PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
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

    osk_params.base.size =
        sizeof(osk_params);

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

                    if (save_data.username[0] == '\0')
                    {
                        strncpy(
                            save_data.username,
                            "PLAYER",
                            sizeof(save_data.username) - 1
                        );
                    }

                    save_game();

                    state =
                        GAME_STATE_MAIN_MENU;
                }
                else
                {
                    state =
                        GAME_STATE_LANGUAGE;
                }
            }

            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* GU setup                                                                   */
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
/* Game API                                                                  */
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
    old_buttons = 0;

    game_initialized = 1;

    return 0;
}

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

    sceCtrlReadBufferPositive(
        &pad,
        1
    );

    pressed =
        pad.Buttons & ~old_buttons;

    old_buttons = pad.Buttons;

    switch (state)
    {
        case GAME_STATE_TITLE:

            if (pressed & PSP_CTRL_START)
            {
                /*
                 * Musik startet ausschließlich
                 * nach PRESS START.
                 */
                music_start();

                if (save_exists())
                    state = GAME_STATE_MAIN_MENU;
                else
                    state = GAME_STATE_LANGUAGE;
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

            break;

        default:
            break;
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

    sceGuClearColor(0x00000000);

    sceGuClear(
        GU_COLOR_BUFFER_BIT
    );

    switch (state)
    {
        case GAME_STATE_TITLE:

            draw_texture(Title_start);

            break;

        case GAME_STATE_LANGUAGE:

            draw_texture(
                LanguageSelection_start
            );

            draw_highlight(
                82,
                25 + (selected_language * 19),
                278,
                18
            );

            break;

        case GAME_STATE_USERNAME:

            /*
             * Die PSP-OSK übernimmt hier
             * die Bildschirmdarstellung.
             */

            break;

        case GAME_STATE_MAIN_MENU:

            draw_texture(
                MainMenu_start
            );

            draw_highlight(
                12,
                119 + (selected_menu * 28),
                280,
                26
            );

            break;

        default:
            break;
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
