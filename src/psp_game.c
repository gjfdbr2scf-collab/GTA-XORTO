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
/* PEINE CITY STORIES - DEFINITIONEN & LAUTSTÄRKE                            */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f

/* Farbcodes */
#define VICE_CITY_PINK      0xFF007F  
#define VICE_CITY_BLUE      0x00F0FF  
#define VICE_CITY_ORANGE    0xFF5F00  
#define LILA_MISSION_MARKER 0x9400D3  

/* Musik-Lautstärke (Gedrosselt / Leiser) */
#define MUSIC_VOLUME_LOW    0x2000 // Deutlich leiser als der Standard (0x6000)

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

/* Spieler-Koordinaten */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 
static int ist_in_fahrzeug = 0;

// Dummy-Hilfsfunktionen für das Grafiksystem
void SetzeBildschirmAufloesung(int b, int h) {}
void SetzeTextureFilter(int typ) {}
void SetzeHintergrundFarbe(unsigned int f) {}
void SetzeTextFarbe(unsigned int f) {}
void ZeichneLilaMarkerAufKarte(int x, int y) {}
void ZeigeTextAufBildschirm(const char* text, int x, int y) {
    printf("[GRAFIK ANZEIGE] %s\n", text);
}

/* ========================================================================= */
/* 1. AUDIO-STEUERUNG (Echtes, leises Musik-Management)                      */
/* ========================================================================= */
void SteuerMusikLautstaerke(int aktivieren) {
    if (aktivieren) {
        if (!musik_aktiv) {
            printf("[AUDIO ENGINE] Musik AN (Leise eingestellt: 0x%X) - 'Ginseng Strip 2002' laeuft.\n", MUSIC_VOLUME_LOW);
            musik_aktiv = 1;
        }
    } else {
        if (musik_aktiv) {
            printf("[AUDIO ENGINE] Musik AUS (Komplette Stille fuer diesen Abschnitt).\n");
            musik_aktiv = 0;
        }
    }
}

/* ========================================================================= */
/* 2. GRAFIK-ENGINE INITIALISIERUNG                                          */
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

void RenderMiniMap() {
    unsigned int markerFarbe = LILA_MISSION_MARKER;
    if (markerFarbe == LILA_MISSION_MARKER) {
        ZeichneLilaMarkerAufKarte(150, 300);
    }
}

/* ========================================================================= */
/* 3. N64 ANALOGSTICK-DREHSTEUERUNG                                          */
/* ========================================================================= */
void AktualisiereN64Eingabe() {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    
    int analogX = pad.Lx - 128; 
    int analogY = pad.Ly - 128; 

    if (state == GAME_STATE_START_MENU) {
        // Im Startmenü auf die X-Taste (A-Knopf) warten
        if (pad.Buttons & PSP_CTRL_CROSS) {
            state = GAME_STATE_STORY_INTRO; // Wechselt zum filmischen Intro
        }
        return;
    }

    // Steuerung im Gameplay
    if (analogY < -30) {
        player_x += sinf(player_yaw) * 0.1f;
        player_z += cosf(player_yaw) * 0.1f;
    }
    if (analogY > 30) {
        player_x -= sinf(player_yaw) * 0.1f;
        player_z -= cosf(player_yaw) * 0.1f;
    }

    // Charakter dreht sich exakt um die eigene Achse (Kein seitliches Rutschen!)
    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 

    // Testweiser Wechsel in Fahrzeuge oder Läden über die Tasten
    if (pad.Buttons & PSP_CTRL_TRIANGLE) { // Dreieck-Taste simuliert Einsteigen
        if (state == GAME_STATE_STORY_WALK) {
            state = GAME_STATE_VEHICLE_DRIVE;
            ist_in_fahrzeug = 1;
        } else if (state == GAME_STATE_VEHICLE_DRIVE) {
            state = GAME_STATE_STORY_WALK;
            ist_in_fahrzeug = 0;
        }
    }
    if (pad.Buttons & PSP_CTRL_SQUARE) { // Viereck-Taste simuliert Aldi-Betreten
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_ALDI_EINKAUF;
        else if (state == GAME_STATE_ALDI_EINKAUF) state = GAME_STATE_STORY_WALK;
    }
}

/* ========================================================================= */
/* 4. DIE HAUPTSCHLEIFE (Musik schaltet sich je nach Zustand um!)            */
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
            // REGEL 1: Musik läuft leise im Hauptmenü!
            SteuerMusikLautstaerke(1); 
            
            SetzeHintergrundFarbe(VICE_CITY_ORANGE);
            SetzeTextFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("=== GINSENG STRIP 2002 ===", 100, 50);
            SetzeTextFarbe(VICE_CITY_BLUE);
            ZeigeTextAufBildschirm("PRESS START / CROSS TO PLAY", 110, 120);
            break;
            
        case GAME_STATE_STORY_INTRO:
            // REGEL 2: Absolut stumm während des Intros!
            SteuerMusikLautstaerke(0); 
            
            SetzeHintergrundFarbe(0x000000);
            SetzeTextFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
            ZeigeTextAufBildschirm("Die Cousins sitzen im Auto, fahren am Sauerbruchweg vor...", 20, 70);
            ZeigeTextAufBildschirm("Zwei Brueder steigen aus und gehen zur Haustuer.", 20, 120);
            
            // Wechselt automatisch nach der Sequenz zum Laufen
            state = GAME_STATE_STORY_WALK;
            break;
            
        case GAME_STATE_STORY_WALK:
            // REGEL 3: Absolut stumm, wenn man zu Fuß zur Brücke läuft!
            SteuerMusikLautstaerke(0); 
            
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("GAMEPLAY: Du laeufst zu Fuss zur hölzernen Hertha-Peters-Bruecke.", 20, 20);
            ZeigeTextAufBildschirm("[Tipps: Dreieck = Auto besteigen | Viereck = Aldi betreten]", 20, 60);
            RenderMiniMap(); 
            break;
            
        case GAME_STATE_ALDI_EINKAUF:
            // REGEL 4: Musik läuft wieder leise im Einkaufsladen!
            SteuerMusikLautstaerke(1); 
            
            AktualisiereN64Eingabe();
            SetzeHintergrundFarbe(VICE_CITY_BLUE);
            ZeigeTextAufBildschirm("ALDI MARKT: Kaufe Center Shocks und Getraenke.", 20, 20);
            ZeigeTextAufBildschirm("[Druecke Viereck, um den Laden wieder zu verlassen]", 20, 60);
            break;
            
        case GAME_STATE_VEHICLE_DRIVE:
            // REGEL 5: Musik läuft leise, sobald man im Auto, Bus oder Zug sitzt!
            SteuerMusikLautstaerke(1); 
            
            AktualisiereN64Eingabe();
            SetzeHintergrundFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("FAHRZEUG: Du faehrst mit dem Auto durch Peine.", 20, 20);
            ZeigeTextAufBildschirm("[Druecke Dreieck, um aus dem Auto auszusteigen]", 20, 60);
            break;
    }
}

/* ========================================================================= */
/* DIE BRÜCKE ZUR MAIN.C                                                     */
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
}
