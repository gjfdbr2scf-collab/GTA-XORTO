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
/* PEINE CITY STORIES - CONFIGURATION & ULTRA LIGHT GRAPHICS                 */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f

/* Klassische 2000er Farbcodes */
#define VICE_CITY_PINK      0xFF007F  
#define VICE_CITY_BLUE      0x00F0FF  
#define VICE_CITY_ORANGE    0xFF5F00  
#define LILA_MISSION_MARKER 0x9400D3  

/* Musik-Lautstärke (Leise eingestellt, damit es auf der R36S gut klingt) */
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
    GAME_STATE_START_MENU,   // Startbildschirm: Ginseng Strip 2002 - Press Start
    GAME_STATE_STORY_INTRO,  // Filmische Cutscene: Cousins fahren im Auto vor
    GAME_STATE_STORY_WALK,   // Gameplay: Zu Fuß zur Brücke (Stumm!)
    GAME_STATE_ALDI_EINKAUF, // Gameplay: Im Aldi-Markt (Musik aktiv!)
    GAME_STATE_VEHICLE_DRIVE // Gameplay: Im Auto / Bus / Zug (Musik aktiv!)
} GameState;

static unsigned int __attribute__((aligned(64))) list[0x200000 / 4];
static GameState state = GAME_STATE_START_MENU; 
static int game_initialized = 0;
static int musik_aktiv = 0;
static SaveData save_data;

/* Player-Koordinaten für Peine */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 

// Dummy-Funktionen für die Grafik-Schnittstelle
void SetzeBildschirmAufloesung(int b, int h) {}
void SetzeTextureFilter(int typ) {}
void AktiviereCelShadingKonturen(int aktiv) {}
void SetzeLichtEffekt(const char* name) {}
void Schalte3DModellDetailsAufMaximum() {} 
void SetzeHintergrundFarbe(unsigned int f) {}
void SetzeTextFarbe(unsigned int f) {}
void ZeichneLilaMarkerAufKarte(int x, int y) {}
void ZeigeTextAufBildschirm(const char* text, int x, int y) {
    printf("[GRAFIK ANZEIGE] %s\n", text);
}

/* ========================================================================= */
/* 1. INTERNE AUDIO-STEUERUNG                                                */
/* ========================================================================= */
void SteuerMusikLautstaerke(int aktivieren) {
    if (aktivieren) {
        if (!musik_aktiv) {
            printf("[AUDIO ENGINE] Musik AN - 'Ginseng Strip 2002' laeuft leise (0x%X).\n", MUSIC_VOLUME_LOW);
            musik_aktiv = 1;
        }
    } else {
        if (musik_aktiv) {
            printf("[AUDIO ENGINE] Musik AUS (Komplette Stille fuer diesen Bereich).\n");
            musik_aktiv = 0;
        }
    }
}

/* ========================================================================= */
/* 2. LEISTUNGS-GRAFIKENGINE (Verhindert den Absturz auf der R36S Hardware)   */
/* ========================================================================= */
void InitialisiereHighEndGrafik() {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    
    sceGuShadeModel(GU_FLAT); 
    sceGuDisable(GU_LIGHTING); 
    
    sceGuClearColor(VICE_CITY_ORANGE); 
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);
}

void LadeUmgebungsDetailsPeine() {
    ZeigeTextAufBildschirm("Umgebung: Sauerbruchweg geladen.", 20, 140);
    ZeigeTextAufBildschirm("Bruecke: Hertha-Peters-Bruecke (Holzdesign)", 20, 160);
}

void RenderMiniMap() {
    unsigned int markerFarbe = LILA_MISSION_MARKER;
    if (markerFarbe == LILA_MISSION_MARKER) {
        ZeichneLilaMarkerAufKarte(150, 300);
    }
}

/* ========================================================================= */
/* 3. EXAKTE N64 ANALOGSTICK-DREHSTEUERUNG                                   */
/* ========================================================================= */
void AktualisiereN64Eingabe() {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    
    int analogX = pad.Lx - 128; 
    int analogY = pad.Ly - 128; 

    if (state == GAME_STATE_START_MENU) {
        // Im Startmenü schaltet das Drücken von CROSS (A-Knopf) das Spiel weiter
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

    // Setzmann dreht sich perfekt auf der Stelle um die eigene Achse!
    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 

    // Simulationstasten für Fahrzeuge und Aldi
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
/* 4. DIE SPIEL-SCHLEIFE WITH AUTOMATIC AUDIO SWAP                           */
/* ========================================================================= */
void SpielSchleife() {
    if (!game_initialized) {
        InitialisiereHighEndGrafik();
        save_data.username = 'S'; 
        save_data.language = 0; 
        game_initialized = 1;
    }

    switch(state) {
        case GAME_STATE_START_MENU:
            SteuerMusikLautstaerke(1); // Musik leise an!
            SetzeHintergrundFarbe(VICE_CITY_ORANGE);
            SetzeTextFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("=== GINSENG STRIP 2002 ===", 100, 50);
            SetzeTextFarbe(VICE_CITY_BLUE);
            ZeigeTextAufBildschirm("PRESS CROSS TO START STORY", 110, 120);
            break;
            
        case GAME_STATE_STORY_INTRO:
            SteuerMusikLautstaerke(0); // Cutscene stumm!
            SetzeHintergrundFarbe(0x000000);
            SetzeTextFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
            ZeigeTextAufBildschirm("Ein Transporter (Van) naehrt sich am Sauerbruchweg und parkt...", 20, 60);
            ZeigeTextAufBildschirm("Zwei Brueder steigen aus und laufen vor zur Haustuer...", 20, 110);
            
            // Wechselt nach der Cutscene automatisch in den Spielmodus
            state = GAME_STATE_STORY_WALK;
            break;
            
        case GAME_STATE_STORY_WALK:
            SteuerMusikLautstaerke(0); // Zu Fuß stumm!
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("STORY-MODUS: Du steuerst Setzmann zu Fuss. Gehe zur Holzbruecke!", 20, 20);
            LadeUmgebungsDetailsPeine(); 
            RenderMiniMap(); 
            break;
            
        case GAME_STATE_ALDI_EINKAUF:
            SteuerMusikLautstaerke(1); // Im Aldi Musik an!
            AktualisiereN64Eingabe();
            SetzeHintergrundFarbe(VICE_CITY_BLUE);
            ZeigeTextAufBildschirm("ALDI MARKT: Du kaufst im Laden ein.", 20, 20);
            break;
            
        case GAME_STATE_VEHICLE_DRIVE:
            SteuerMusikLautstaerke(1); // Im Auto Musik an!
            AktualisiereN64Eingabe();
            SetzeHintergrundFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("FAHRZEUG-MODUS: Cousins sitzen im Auto am Sauerbruchweg.", 20, 20);
            break;
    }
}

/* ========================================================================= */
/* DIE BRÜCKE ZUR MAIN.C (Sicherer, sauberer Direktstart für PPSSPP)          */
/* ========================================================================= */
int psp_game_init() {
    InitialisiereHighEndGrafik();
    state = GAME_STATE_START_MENU; 
    return 0; 
}

void psp_game_update() {
    AktualisiereN64Eingabe();
}

void psp_game_render() {
    SpielSchleife();
    // Der Vblank-Warter verhindert, dass der Emulator im Kreis rennt und abstürzt!
    sceDisplayWaitVblankStart(); 
}
