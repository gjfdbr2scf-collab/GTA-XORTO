#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <pspiofilemgr.h>
#include <pspvaudio.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "psp_game.h"

/* ========================================================================= */
/* PEINE CITY STORIES - DEFINITIONEN & LAUTSTAERKE                           */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f

#define VICE_CITY_PINK      0xFF007F  
#define VICE_CITY_BLUE      0x00F0FF  
#define VICE_CITY_ORANGE    0xFF5F00  
#define LILA_MISSION_MARKER 0x9400D3  

#define MUSIC_VOLUME_LOW    0x2000 

extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

typedef struct {
    unsigned int magic;
    int language;
    char username; 
} SaveData;

typedef enum {
    GAME_STATE_START_MENU,   
    GAME_STATE_STORY_INTRO,  
    GAME_STATE_STORY_WALK,   
    GAME_STATE_ALDI_EINKAUF, 
    GAME_STATE_VEHICLE_DRIVE 
} GameState;

static unsigned int __attribute__((aligned(64))) list[0x200000 / 4];
static GameState state = GAME_STATE_START_MENU; 
static int game_initialized = 0;
static int musik_aktiv = 0;
static SaveData save_data;

static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 

/* ========================================================================= */
/* INTERNE STEUERUNG FÜR DIE LEISE MUSIK                                     */
/* ========================================================================= */
void SteuerMusikLautstaerke(int aktivieren) {
    if (aktivieren) {
        if (!musik_aktiv) {
            printf("[AUDIO] Leise Musik AN - Vol: 0x%X\n", MUSIC_VOLUME_LOW);
            musik_aktiv = 1;
        }
    } else {
        if (musik_aktiv) {
            printf("[AUDIO] Musik AUS\n");
            musik_aktiv = 0;
        }
    }
}

/* ========================================================================= */
/* SYSTEMFUNKTIONEN FÜR DEINE REGELN (DIREKT IN DEN ORIGINAL-SCHLEIFEN)      */
/* ========================================================================= */
int psp_game_init() {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuShadeModel(GU_SMOOTH); 
    sceGuClearColor(VICE_CITY_ORANGE); 
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);

    save_data.username = 'S'; 
    save_data.language = 0; 
    save_data.magic = 0x47325332;

    state = GAME_STATE_START_MENU; 
    game_initialized = 1;
    return 1;
}

void psp_game_update() {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    
    int analogX = pad.Lx - 128; 
    int analogY = pad.Ly - 128; 

    if (state == GAME_STATE_START_MENU) {
        if (pad.Buttons & PSP_CTRL_CROSS) {
            state = GAME_STATE_STORY_INTRO; 
        }
        return;
    }

    // N64-Analogstick-Laufen
    if (analogY < -30) {
        player_x += sinf(player_yaw) * 0.1f;
        player_z += cosf(player_yaw) * 0.1f;
    }
    if (analogY > 30) {
        player_x -= sinf(player_yaw) * 0.1f;
        player_z -= cosf(player_yaw) * 0.1f;
    }

    // Setzmann dreht sich perfekt um die eigene Achse (Kein seitliches Rutschen!)
    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 

    // Tasten-Wechsel für Aldi und Fahrzeuge
    if (pad.Buttons & PSP_CTRL_TRIANGLE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_VEHICLE_DRIVE;
        else if (state == GAME_STATE_VEHICLE_DRIVE) state = GAME_STATE_STORY_WALK;
    }
    if (pad.Buttons & PSP_CTRL_SQUARE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_ALDI_EINKAUF;
        else if (state == GAME_STATE_ALDI_EINKAUF) state = GAME_STATE_STORY_WALK;
    }
}

void psp_game_render() {
    if (!game_initialized) return;

    switch(state) {
        case GAME_STATE_START_MENU:
            SteuerMusikLautstaerke(1); // Im Menü Musik leise an
            break;
            
        case GAME_STATE_STORY_INTRO:
            SteuerMusikLautstaerke(0); // Cutscene stumm
            state = GAME_STATE_STORY_WALK;
            break;
            
        case GAME_STATE_STORY_WALK:
            SteuerMusikLautstaerke(0); // Zu Fuß stumm
            break;
            
        case GAME_STATE_ALDI_EINKAUF:
            SteuerMusikLautstaerke(1); // Im Aldi Musik an
            break;
            
        case GAME_STATE_VEHICLE_DRIVE:
            SteuerMusikLautstaerke(1); // Im Auto Musik an
            break;
    }
    sceDisplayWaitVblankStart(); 
}
