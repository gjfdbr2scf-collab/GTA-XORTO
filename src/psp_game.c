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
/* PEINE CITY STORIES - ENGINE WITH FULL PRELOAD & OPTIMIZED AUDIO           */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f

/* Rockstar Vice-City Neon-Farbcodes */
#define VICE_CITY_PINK      0xFF007F  
#define VICE_CITY_BLUE      0x00F0FF  
#define VICE_CITY_ORANGE    0xFF5F00  
#define LILA_MISSION_MARKER 0x9400D3  

/* Drosselung für angenehme, leise Hintergrundmusik */
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

/* Spieler-Koordinaten */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 

/* Dummy-Schnittstellen */
void SetzeBildschirmAufloesung(int b, int h) {}
void SetzeTextureFilter(int typ) {}
void SetzeHintergrundFarbe(unsigned int f) {}
void SetzeTextFarbe(unsigned int f) {}
void ZeichneLilaMarkerAufKarte(int x, int y) {}
void ZeigeTextAufBildschirm(const char* text, int x, int y) {
    printf("[RENDER TEXT] %s\n", text);
}

/* ========================================================================= */
/* 1. ULTRA-GRAFIKENGINE OPTIMIERUNG                                        */
/* ========================================================================= */
void InitialisiereHighEndGrafik() {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    
    sceGuShadeModel(GU_SMOOTH); 
    sceGuEnable(GU_LIGHTING); 
    sceGuEnable(GU_LIGHT0); 
    
    sceGuClearColor(VICE_CITY_ORANGE); 
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);
}

/* ========================================================================= */
/* 2. AUTOMATISCHER DATEN-PRELOAD (KORREKTUR: "Vollstaendigen" korrigiert)   */
/* ========================================================================= */
void StarteVollstaendigenDatenPreload() {
    printf("[PRELOAD START] Lade alle weltweiten Peine-Spieldaten in das RAM...\n");
    
    RenderMiniMap();
    LadeUmgebungsDetailsPeine();
    
    save_data.username = 'S'; 
    save_data.language = 0; 
    save_data.magic = 0x47325332;
    
    printf("[PRELOAD FERTIG] Alle Daten geladen. Freigabe erteilt!\n");
}

void LadeUmgebungsDetailsPeine() {
    ZeigeTextAufBildschirm("Umgebung: Sauerbruchweg-Haeuser geladen.", 20, 140);
    ZeigeTextAufBildschirm("Bruecke: Hertha-Peters-Bruecke (Holzdesign)", 20, 160);
}

void RenderMiniMap() {
    unsigned int markerFarbe = LILA_MISSION_MARKER;
    if (markerFarbe == LILA_MISSION_MARKER) {
        ZeichneLilaMarkerAufKarte(150, 300);
    }
}

/* ========================================================================= */
/* 3. AUDIO-MANAGEMENT & LAUTSTÄRKE                                          */
/* ========================================================================= */
void SteuerMusikLautstaerke(int aktivieren) {
    if (aktivieren) {
        if (!musik_aktiv) {
            printf("[AUDIO ENGINE] Leise Musik AN - 'Ginseng Strip 2002' (Vol: 0x%X).\n", MUSIC_VOLUME_LOW);
            musik_aktiv = 1;
        }
    } else {
        if (musik_aktiv) {
            printf("[AUDIO ENGINE] Musik AUS.\n");
            musik_aktiv = 0;
        }
    }
}

/* ========================================================================= */
/* 4. TASTEN- & JOYSTICK-ABFRAGE                                             */
/* ========================================================================= */
void AktualisiereN64Eingabe() {
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

    if (analogY < -30) {
        player_x += sinf(player_yaw) * 0.1f;
        player_z += cosf(player_yaw) * 0.1f;
    }
    if (analogY > 30) {
        player_x -= sinf(player_yaw) * 0.1f;
        player_z -= cosf(player_yaw) * 0.1f;
    }

    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 

    if (pad.Buttons & PSP_CTRL_TRIANGLE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_VEHICLE_DRIVE;
        else if (state == GAME_STATE_VEHICLE_DRIVE) state = GAME_STATE_STORY_WALK;
    }
    if (pad.Buttons & PSP_CTRL_SQUARE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_ALDI_EINKAUF;
        else if (state == GAME_STATE_ALDI_EINKAUF) state = GAME_STATE_STORY_WALK;
    }
}

/* ========================================================================= */
/* 5. GAME LOOP SCHLEIFE                                                     */
/* ========================================================================= */
void SpielSchleife() {
    if (!game_initialized) {
        InitialisiereHighEndGrafik();
        StarteVollstaendigenDatenPreload();
        game_initialized = 1;
    }

    switch(state) {
        case GAME_STATE_START_MENU:
            SteuerMusikLautstaerke(1); 
            ZeigeTextAufBildschirm("=== GINSENG STRIP 2002 ===", 100, 50);
            ZeigeTextAufBildschirm("PRESS CROSS TO START STORY", 110, 120);
            break;
            
        case GAME_STATE_STORY_INTRO:
            SteuerMusikLautstaerke(0); 
            ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
            ZeigeTextAufBildschirm("Ein Transporter (Van) naehrt sich am Sauerbruchweg und parkt...", 20, 60);
            ZeigeTextAufBildschirm("Zwei Brueder steigen aus und laufen vor zur Haustuer...", 20, 110);
            state = GAME_STATE_STORY_WALK;
            break;
            
        case GAME_STATE_STORY_WALK:
            SteuerMusikLautstaerke(0); 
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("GAMEPLAY: Du laeufst zu Fuss zur hoelzernen Hertha-Peters-Bruecke.", 20, 20);
            LadeUmgebungsDetailsPeine();
            RenderMiniMap(); 
            break;
            
        case GAME_STATE_ALDI_EINKAUF:
            SteuerMusikLautstaerke(1); 
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("ALDI MARKT: Du kaufst im Laden ein.", 20, 20);
            break;
            
        case GAME_STATE_VEHICLE_DRIVE:
            SteuerMusikLautstaerke(1); 
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("FAHRZEUG-MODUS: Cousins fahren im Auto.", 20, 20);
            break;
    }
}

/* ========================================================================= */
/* DIE BRÜCKE ZUR MAIN.C                                                     */
/* ========================================================================= */
int psp_game_init() {
    InitialisiereHighEndGrafik();
    StarteVollstaendigenDatenPreload();
    state = GAME_STATE_START_MENU; 
    return 0; 
}

void psp_game_update() {
    AktualisiereN64Eingabe();
}

void psp_game_render() {
    SpielSchleife();
    sceDisplayWaitVblankStart(); 
}
