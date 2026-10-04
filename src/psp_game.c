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

/*
 * ------------------------------------------------------------
 * Eingebettete Assets
 * ------------------------------------------------------------
 *
 * Diese Namen müssen zu den bin2o-Zielen im Makefile passen:
 *   title.o    -> Title_start
 *   language.o -> LanguageSelection_start
 *   mainmenu.o -> MainMenu_start
 *   music.o    -> Music_start / Music_end
 */
extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

/* ------------------------------------------------------------
 * Bildschirm
 * ------------------------------------------------------------ */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

/* ------------------------------------------------------------
 * Save
 * ------------------------------------------------------------ */
#define SAVE_DIR  "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

/* ------------------------------------------------------------
 * Musik
 * ------------------------------------------------------------ */
#define MUSIC_RATE    22050
#define MUSIC_SAMPLES 1024
#define MUSIC_VOLUME  0x6000

/* ------------------------------------------------------------
 * Zustände
 * ------------------------------------------------------------ */
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

/* ------------------------------------------------------------
 * Save-Struktur
 * ------------------------------------------------------------ */
typedef struct
{
    unsigned int magic;
    int language;
    char username[32];
} SaveData;

/* ------------------------------------------------------------
 * GU / allgemeiner Zustand
 * ------------------------------------------------------------ */
static unsigned int __attribute__((aligned(64)))
    list[0x20000 / 4];

static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;

static int selected_language = 0;
static int selected_menu = 0;
static int settings_selection = 0;

static unsigned int old_buttons = 0;

static SaveData save_data;

/* ------------------------------------------------------------
 * Fade / Übergänge
 * ------------------------------------------------------------ */
static int transition_active = 0;
static int transition_phase = 0; /* 0 = dunkel werden, 1 = hell werden */
static int transition_alpha = 0;
static GameState transition_target = GAME_STATE_TITLE;

/* Wie lange die Erfolgsmeldung stehen bleibt */
static int save_success_timer = 0;

/* ------------------------------------------------------------
 * Story / Free World / Multiplayer
 * ------------------------------------------------------------ */
static int story_step = 0;
static int flower_collected = 0;

static float player_x = 240.0f;
static float player_y = 145.0f;

static float player2_x = 320.0f;
static float player2_y = 145.0f;

/* ------------------------------------------------------------
 * Musik
 * ------------------------------------------------------------ */
static volatile int music_running = 0;
static volatile unsigned int music_position = 0;
static SceUID music_thread = -1;

static short __attribute__((aligned(64)))
    music_buffer[MUSIC_SAMPLES];

/* ------------------------------------------------------------
 * PSP OSK / Username
 * ------------------------------------------------------------ */
static SceUtilityOskParams osk_params;
static SceUtilityOskData osk_data;

static unsigned short osk_desc[64];
static unsigned short osk_input[32];
static unsigned short osk_output[32];

static int osk_started = 0;
static int osk_shutdown_requested = 0;

/* ============================================================
 * FORWARD DECLARATIONS
 * ============================================================ */

static void start_transition(GameState next_state);
static void update_transition(void);
static void render_fade(void);

static int save_exists(void);
static void save_game(void);
static void load_game(void);

static void music_start(void);
static void music_stop(void);

static void username_begin(void);
static void username_update(void);

/* ============================================================
 * SAVE
 * ============================================================ */

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

        if (data.magic == SAVE_MAGIC)
            return 1;

        return 0;
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

    if (save_data.username[0] == '\0')
    {
        strcpy(
            save_data.username,
            "PLAYER"
        );
    }

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

/* ============================================================
 * 5x7 FONT
 * ============================================================ */

static const unsigned char glyph_A[7] =
{
    14,17,17,31,17,17,17
};

static const unsigned char glyph_B[7] =
{
    30,17,17,30,17,17,30
};

static const unsigned char glyph_C[7] =
{
    14,17,16,16,16,17,14
};

static const unsigned char glyph_D[7] =
{
    30,17,17,17,17,17,30
};

static const unsigned char glyph_E[7] =
{
    31,16,16,30,16,16,31
};

static const unsigned char glyph_F[7] =
{
    31,16,16,30,16,16,16
};

static const unsigned char glyph_G[7] =
{
    14,17,16,23,17,17,14
};

static const unsigned char glyph_H[7] =
{
    17,17,17,31,17,17,17
};

static const unsigned char glyph_I[7] =
{
    31,4,4,4,4,4,31
};

static const unsigned char glyph_L[7] =
{
    16,16,16,16,16,16,31
};

static const unsigned char glyph_M[7] =
{
    17,27,21,21,17,17,17
};

static const unsigned char glyph_N[7] =
{
    17,25,21,19,17,17,17
};

static const unsigned char glyph_O[7] =
{
    14,17,17,17,17,17,14
};

static const unsigned char glyph_P[7] =
{
    30,17,17,30,16,16,16
};

static const unsigned char glyph_R[7] =
{
    30,17,17,30,20,18,17
};

static const unsigned char glyph_S[7] =
{
    15,16,16,14,1,1,30
};

static const unsigned char glyph_T[7] =
{
    31,4,4,4,4,4,4
};

static const unsigned char glyph_U[7] =
{
    17,17,17,17,17,17,14
};

static const unsigned char glyph_V[7] =
{
    17,17,17,17,17,10,4
};

static const unsigned char glyph_W[7] =
{
    17,17,17,21,21,21,10
};

static const unsigned char glyph_Y[7] =
{
    17,17,10,4,4,4,4
};

static const unsigned char glyph_0[7] =
{
    14,17,19,21,25,17,14
};

static const unsigned char glyph_1[7] =
{
    4,12,4,4,4,4,14
};

static const unsigned char glyph_2[7] =
{
    14,17,1,2,4,8,31
};

static const unsigned char glyph_3[7] =
{
    30,1,1,14,1,1,30
};

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
        default:  return NULL;
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
    int pixel_count = 0;
    const char *p;

    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    for (p = text; *p; ++p)
    {
        const unsigned char *g =
            get_glyph(*p);

        int row;
        int col;

        if (!g)
            continue;

        for (row = 0; row < 7; ++row)
        {
            for (col = 0; col < 5; ++col)
            {
                if (g[row] &
                    (1 << (4 - col)))
                {
                    ++pixel_count;
                }
            }
        }
    }

    if (pixel_count <= 0)
        return;

    Vertex *v =
        (Vertex *)sceGuGetMemory(
            pixel_count * 2 * sizeof(Vertex)
        );

    int n = 0;
    int current_x = x;

    sceGuColor(color);

    for (p = text; *p; ++p)
    {
        const unsigned char *g =
            get_glyph(*p);

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
                if (g[row] &
                    (1 << (4 - col)))
                {
                    v[n].x =
                        (short)(current_x +
                        col * scale);

                    v[n].y =
                        (short)(y +
                        row * scale);

                    v[n].z = 0;

                    v[n + 1].x =
                        (short)(current_x +
                        (col + 1) * scale);

                    v[n + 1].y =
                        (short)(y +
                        (row + 1) * scale);

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

/* ============================================================
 * PRIMITIVES
 * ============================================================ */

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

static void draw_circle(
    int x,
    int y,
    int radius,
    unsigned int color
)
{
    static const short cx[17] =
    {
        100,92,71,38,0,-38,-71,-92,
        -100,-92,-71,-38,0,38,71,92,100
    };

    static const short cy[17] =
    {
        0,38,71,92,100,92,71,38,
        0,-38,-71,-92,-100,-92,-71,-38,0
    };

    typedef struct
    {
        short x;
        short y;
        short z;
    } Vertex;

    int i;
    int segments = 16;

    Vertex *v =
        (Vertex *)sceGuGetMemory(
            (segments + 2) *
            sizeof(Vertex)
        );

    v[0].x = (short)x;
    v[0].y = (short)y;
    v[0].z = 0;

    for (i = 0; i <= segments; ++i)
    {
        v[i + 1].x =
            (short)(x +
            (radius * cx[i]) / 100);

        v[i + 1].y =
            (short)(y +
            (radius * cy[i]) / 100);

        v[i + 1].z = 0;
    }

    sceGuColor(color);

    sceGuDrawArray(
        GU_TRIANGLE_FAN,
        GU_VERTEX_16BIT |
        GU_TRANSFORM_2D,
        segments + 2,
        NULL,
        v
    );
}

/* ============================================================
 * TEXTURE
 * ============================================================ */

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

    /*
     * Die RAW-Texturen sind 512 Pixel breit.
     * Der sichtbare Spielbereich ist 480x272.
     */
    v[0].u = 0;
    v[0].v = 0;
    v[0].color = 0xffffffff;
    v[0].x = 0;
    v[0].y = 0;
    v[0].z = 0;

    v[1].u = 480;
    v[1].v = 272;
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

/* ============================================================
 * FADE
 * ============================================================ */

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
        transition_alpha += 24;

        if (transition_alpha >= 255)
        {
            transition_alpha = 255;

            state = transition_target;

            if (state == GAME_STATE_SAVE_SUCCESS)
                save_success_timer = 0;

            transition_phase = 1;
        }
    }
    else
    {
        transition_alpha -= 24;

        if (transition_alpha <= 0)
        {
            transition_alpha = 0;
            transition_active = 0;
        }
    }
}

static void render_fade(void)
{
    unsigned int alpha =
        (unsigned int)transition_alpha;

    if (!transition_active && alpha == 0)
        return;

    draw_rect(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (alpha << 24)
    );
}

/* ============================================================
 * MUSIK
 * ============================================================ */

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

/* ============================================================
 * USERNAME / OSK
 * ============================================================ */

static void ascii_to_ushort(
    unsigned short *dst,
    unsigned int cap,
    const char *src
)
{
    unsigned int i = 0;

    while (i + 1 < cap &&
           src[i] != '\0')
    {
        dst[i] =
            (unsigned short)
            (unsigned char)src[i];

        ++i;
    }

    dst[i] = 0;
}

static int osk_language_for_game_language(
    int language
)
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
    memset(
        &osk_params,
        0,
        sizeof(osk_params)
    );

    memset(
        &osk_data,
        0,
        sizeof(osk_data)
    );

    memset(
        osk_desc,
        0,
        sizeof(osk_desc)
    );

    memset(
        osk_input,
        0,
        sizeof(osk_input)
    );

    memset(
        osk_output,
        0,
        sizeof(osk_output)
    );

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
                         i + 1 <
                         sizeof(save_data.username) &&
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
                        strcpy(
                            save_data.username,
                            "PLAYER"
                        );
                    }

                    /*
                     * Username fertig:
                     * erst dunkel werden,
                     * dann SAVE sichtbar machen.
                     */
                    start_transition(
                        GAME_STATE_SAVE
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

        default:
            break;
    }
}

/* ============================================================
 * WELT
 * ============================================================ */

static void clamp_player(
    float *x,
    float *y
)
{
    if (*x < 18.0f)
        *x = 18.0f;

    if (*y < 58.0f)
        *y = 58.0f;

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
    if (buttons & PSP_CTRL_LEFT)
        *x -= speed;

    if (buttons & PSP_CTRL_RIGHT)
        *x += speed;

    if (buttons & PSP_CTRL_UP)
        *y -= speed;

    if (buttons & PSP_CTRL_DOWN)
        *y += speed;

    clamp_player(x, y);
}

static float absf_local(float value)
{
    return value < 0.0f ? -value : value;
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

static void render_peaceful_world(
    const char *mode_name
)
{
    int i;

    /* Himmel */
    draw_rect(
        0, 0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0xff9fd6f5
    );

    /* Wiese */
    draw_rect(
        0, 58,
        SCREEN_WIDTH,
        SCREEN_HEIGHT - 58,
        0xff70ad59
    );

    /* Straße */
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

    /* Wasser */
    draw_rect(
        305, 185,
        155, 60,
        0xff5daed0
    );

    /* Haus */
    draw_rect(
        45, 82,
        72, 55,
        0xfff2dfb8
    );

    draw_rect(
        37, 68,
        88, 20,
        0xffb45e4d
    );

    /* Bäume */
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

    /* Kopfzeile */
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

    /* Steuerung */
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

    /* Spieler */
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

/* ============================================================
 * GU INIT
 * ============================================================ */

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

    sceGuEnable(
        GU_SCISSOR_TEST
    );

    sceGuDisable(
        GU_DEPTH_TEST
    );

    sceGuDisable(
        GU_CULL_FACE
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

/* ============================================================
 * INIT
 * ============================================================ */

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

    player_x = 240.0f;
    player_y = 145.0f;

    player2_x = 320.0f;
    player2_y = 145.0f;

    old_buttons = 0;

    transition_active = 0;
    transition_phase = 0;
    transition_alpha = 0;

    save_success_timer = 0;

    memset(
        &save_data,
        0,
        sizeof(save_data)
    );

    game_initialized = 1;

    return 0;
}

/* ============================================================
 * UPDATE
 * ============================================================ */

void psp_game_update(void)
{
    SceCtrlData pad;
    unsigned int pressed;
    float ax;
    float ay;

    if (!game_initialized)
        return;

    /*
     * Übergang zuerst aktualisieren.
     * Während eines Fades werden keine Eingaben verarbeitet.
     */
    if (transition_active)
    {
        update_transition();
        return;
    }

    /*
     * Username-OSK verarbeitet sich separat.
     */
    if (state == GAME_STATE_USERNAME)
    {
        username_update();
        return;
    }

    /*
     * Erfolgsmeldung automatisch nach kurzer Zeit
     * zum Main Menu überblenden.
     */
    if (state == GAME_STATE_SAVE_SUCCESS)
    {
        ++save_success_timer;

        if (save_success_timer >= 100)
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
        pad.Buttons & ~old_buttons;

    old_buttons = pad.Buttons;

    switch (state)
    {
        /* ----------------------------------------------------
         * TITLE
         * ---------------------------------------------------- */
        case GAME_STATE_TITLE:

            if (pressed & PSP_CTRL_START)
            {
                /*
                 * Musik startet ERST jetzt.
                 */
                music_start();

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

        /* ----------------------------------------------------
         * LANGUAGE
         * ---------------------------------------------------- */
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
                state = GAME_STATE_USERNAME;
                username_begin();
            }

            break;

        /* ----------------------------------------------------
         * SAVE
         * ---------------------------------------------------- */
        case GAME_STATE_SAVE:

            /*
             * X UND START bestätigen.
             * Damit gibt es auf der R36S keinen Unterschied,
             * welche der beiden Tasten für Start benutzt wird.
             */
            if (pressed &
                (PSP_CTRL_CROSS | PSP_CTRL_START))
            {
                /*
                 * Sprache + Username speichern.
                 */
                save_game();

                /*
                 * Danach zuerst dunkel werden und
                 * die Erfolgsmeldung anzeigen.
                 */
                start_transition(
                    GAME_STATE_SAVE_SUCCESS
                );
            }

            if (pressed & PSP_CTRL_CIRCLE)
            {
                /*
                 * Zurück zur Username-Eingabe.
                 */
                state = GAME_STATE_USERNAME;
                username_begin();
            }

            break;

        /* ----------------------------------------------------
         * MAIN MENU
         * ---------------------------------------------------- */
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
                start_transition(
                    GAME_STATE_TITLE
                );
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

                        start_transition(
                            GAME_STATE_STORY
                        );
                        break;

                    case 1:
                        player_x = 240.0f;
                        player_y = 145.0f;

                        start_transition(
                            GAME_STATE_FREE_WORLD
                        );
                        break;

                    case 2:
                        player_x = 145.0f;
                        player_y = 180.0f;

                        player2_x = 335.0f;
                        player2_y = 180.0f;

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

        /* ----------------------------------------------------
         * STORY
         * ---------------------------------------------------- */
        case GAME_STATE_STORY:

            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.0f
            );

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            if (pressed & PSP_CTRL_CROSS)
            {
                if (story_step == 0 &&
                    distance2_local(
                        player_x,
                        player_y,
                        395.0f,
                        88.0f
                    ) <
                    32.0f * 32.0f)
                {
                    story_step = 1;
                }
                else if (
                    story_step == 1 &&
                    distance2_local(
                        player_x,
                        player_y,
                        108.0f,
                        215.0f
                    ) <
                    32.0f * 32.0f)
                {
                    flower_collected = 1;
                    story_step = 2;
                }
                else if (
                    story_step == 2 &&
                    distance2_local(
                        player_x,
                        player_y,
                        80.0f,
                        180.0f
                    ) <
                    32.0f * 32.0f)
                {
                    story_step = 3;
                }
            }

            break;

        /* ----------------------------------------------------
         * FREE WORLD
         * ---------------------------------------------------- */
        case GAME_STATE_FREE_WORLD:

            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.5f
            );

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        /* ----------------------------------------------------
         * MULTIPLAYER LOCAL
         * ---------------------------------------------------- */
        case GAME_STATE_MULTIPLAYER:

            update_player_from_dpad(
                &player_x,
                &player_y,
                pad.Buttons,
                2.0f
            );

            ax =
                ((float)pad.Lx - 128.0f) /
                127.0f;

            ay =
                ((float)pad.Ly - 128.0f) /
                127.0f;

            if (absf_local(ax) > 0.20f)
                player2_x += ax * 2.0f;

            if (absf_local(ay) > 0.20f)
                player2_y += ay * 2.0f;

            clamp_player(
                &player2_x,
                &player2_y
            );

            if (pressed & PSP_CTRL_CIRCLE)
            {
                start_transition(
                    GAME_STATE_MAIN_MENU
                );
            }

            break;

        /* ----------------------------------------------------
         * SETTINGS
         * ---------------------------------------------------- */
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
                     * Musik aus.
                     * Der nächste Start beginnt die Musik
                     * wieder nach PRESS START.
                     */
                    music_stop();
                }
                else
                {
                    /*
                     * Save löschen.
                     */
                    sceIoRemove(
                        SAVE_FILE
                    );

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

/* ============================================================
 * RENDER
 * ============================================================ */

void psp_game_render(void)
{
    int language_y;
    int menu_y;
    int center_x;

    if (!game_initialized)
        return;

    /*
     * Wenn die PSP-OSK offen ist, zeichnet das Betriebssystem
     * den Dialog selbst.
     */
    if (state == GAME_STATE_USERNAME &&
        !transition_active)
    {
        return;
    }

    sceGuStart(
        GU_DIRECT,
        list
    );

    sceGuClearColor(
        0xff000000
    );

    sceGuClear(
        GU_COLOR_BUFFER_BIT
    );

    switch (state)
    {
        /* ----------------------------------------------------
         * TITLE
         * ---------------------------------------------------- */
        case GAME_STATE_TITLE:

            draw_texture(
                Title_start
            );

            break;

        /* ----------------------------------------------------
         * LANGUAGE
         * ---------------------------------------------------- */
        case GAME_STATE_LANGUAGE:

            draw_texture(
                LanguageSelection_start
            );

            /*
             * Keine weiße Fläche mehr.
             * Nur eine dünne Linie unter der Auswahl.
             */
            language_y =
                48 +
                selected_language * 29;

            draw_rect(
                145,
                language_y,
                150,
                2,
                0xff63b5ff
            );

            break;

        /* ----------------------------------------------------
         * SAVE
         * ---------------------------------------------------- */
        case GAME_STATE_SAVE:

            center_x =
                (SCREEN_WIDTH -
                 text_width(
                     "SAVE",
                     6
                 )) / 2;

            draw_text(
                "SAVE",
                center_x,
                65,
                6,
                0xffffffff
            );

            center_x =
                (SCREEN_WIDTH -
                 text_width(
                     "PRESS X TO SAVE",
                     3
                 )) / 2;

            draw_text(
                "PRESS X TO SAVE",
                center_x,
                155,
                3,
                0xffffffff
            );

            center_x =
                (SCREEN_WIDTH -
                 text_width(
                     "O BACK",
                     2
                 )) / 2;

            draw_text(
                "O BACK",
                center_x,
                205,
                2,
                0xffc8d8e8
            );

            break;

        /* ----------------------------------------------------
         * SAVE SUCCESS
         * ---------------------------------------------------- */
        case GAME_STATE_SAVE_SUCCESS:

            center_x =
                (SCREEN_WIDTH -
                 text_width(
                     "THE DATA HAS SUCCESSFUL SAVED",
                     2
                 )) / 2;

            draw_text(
                "THE DATA HAS SUCCESSFUL SAVED",
                center_x,
                105,
                2,
                0xffffffff
            );

            break;

        /* ----------------------------------------------------
         * MAIN MENU
         * ---------------------------------------------------- */
        case GAME_STATE_MAIN_MENU:

            draw_texture(
                MainMenu_start
            );

            /*
             * Nur eine Unterstreichung.
             * Dadurch bleiben die Wörter lesbar.
             */
            switch (selected_menu)
            {
                case 0:
                    menu_y = 151;
                    break;

                case 1:
                    menu_y = 180;
                    break;

                case 2:
                    menu_y = 209;
                    break;

                default:
                    menu_y = 238;
                    break;
            }

            draw_rect(
                22,
                menu_y,
                180,
                2,
                0xff63b5ff
            );

            break;

        /* ----------------------------------------------------
         * STORY
         * ---------------------------------------------------- */
        case GAME_STATE_STORY:

            render_peaceful_world(
                "STORY"
            );

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

        /* ----------------------------------------------------
         * FREE WORLD
         * ---------------------------------------------------- */
        case GAME_STATE_FREE_WORLD:

            render_peaceful_world(
                "FREE WORLD"
            );

            draw_text(
                "EXPLORE THE WORLD",
                24,
                54,
                2,
                0xff172126
            );

            break;

        /* ----------------------------------------------------
         * MULTIPLAYER
         * ---------------------------------------------------- */
        case GAME_STATE_MULTIPLAYER:

            render_peaceful_world(
                "LOCAL CO OP"
            );

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
                "P1 D PAD P2 ANALOG",
                24,
                54,
                2,
                0xff172126
            );

            break;

        /* ----------------------------------------------------
         * SETTINGS
         * ---------------------------------------------------- */
        case GAME_STATE_SETTINGS:

            draw_rect(
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
                draw_rect(
                    24,
                    78,
                    360,
                    40,
                    0xff334955
                );
            }
            else
            {
                draw_rect(
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
     * Fade liegt immer ganz oben über dem aktuellen Bild.
     */
    if (transition_active)
    {
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

/* ============================================================
 * SHUTDOWN
 * ============================================================ */

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

/* ============================================================
 * WRAPPERS
 * ============================================================ */

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
