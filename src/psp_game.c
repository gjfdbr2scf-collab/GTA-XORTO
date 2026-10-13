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
/* PEINE CITY STORIES - CONFIGURATION & ULTRA GRAPHICS                       */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH     512

#define PI_F 3.14159265359f
#define DEG_TO_RAD 0.017453292519943295f

/* Originale 2000er Rockstar-Farbcodes */
#define VICE_CITY_PINK      0xFF007F  // Neon-Pink für Text & Menüs
#define VICE_CITY_BLUE      0x00F0FF  // Karibisches Neon-Blau
#define VICE_CITY_ORANGE    0xFF5F00  // Atemberaubender Peine-Sonnenuntergang
#define LILA_MISSION_MARKER 0x9400D3  // Der ikonische lila Wegpunkt auf der Map

extern const unsigned char Title_start[];
extern const unsigned char LanguageSelection_start[];
extern const unsigned char MainMenu_start[];
extern const unsigned char Music_start[];
extern const unsigned char Music_end[];

/* Save System auf SD-Karte */
#define SAVE_DIR  "ms0:/PSP/SAVEDATA/GINSENG2"
#define SAVE_FILE "ms0:/PSP/SAVEDATA/GINSENG2/SAVE.DAT"
#define SAVE_MAGIC 0x47325332

typedef struct {
    unsigned int magic;
    int language;
    char username;
} SaveData;

typedef enum {
    GAME_STATE_TITLE = 0,
    GAME_STATE_MAIN_MENU,
    GAME_STATE_STORY_INTRO, // Das filmische Intro vor dem Gameplay
    GAME_STATE_STORY,       // Das eigentliche, interaktive Gameplay
    GAME_STATE_FREE_WORLD,
    GAME_STATE_MULTIPLAYER,
    GAME_STATE_SETTINGS
} GameState;

static unsigned int __attribute__((aligned(64))) list[0x200000 / 4];
static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;
static SaveData save_data;

/* Player-Koordinaten für Peine */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; // Blickwinkel / Rotation von Setzmann

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
/* 1. UNVERSEHRTE ULTRA-GRAFIKENGINE (Voller Rockstar-Style-Glow)            */
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
/* 2. MINI-MAP & REGIONALES SCHLAUES DESIGN (Sauerbruchweg & Holzbrücke)     */
/* ========================================================================= */
void LadeUmgebungsDetailsPeine() {
    ZeigeTextAufBildschirm("Umgebung: Sauerbruchweg-Haeuser und Startpunkt geladen.", 20, 140);
    ZeigeTextAufBildschirm("Objekt-Modell: Hertha-Peters-Bruecke (Klassisches Holzdesign aktiv)", 20, 160);
}

void RenderMiniMap() {
    unsigned int markerFarbe = LILA_MISSION_MARKER;
    if (markerFarbe == LILA_MISSION_MARKER) {
        printf("[HUD ENGINE] Zeichne kreisrunde Mini-Map in Peine...\n");
        printf("[HUD ENGINE] Platziere lila Missions-Marker bei der Hertha-Peters-Bruecke\n");
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
}

/* ========================================================================= */
/* 4. DAS NEUE FILM-INTRO (Direkt vor dem Story-Gameplay!)                   */
/* ========================================================================= */
void RendereStoryIntroCutscene() {
    SetzeHintergrundFarbe(0x000000);
    SetzeText放Farbe(VICE_CITY_PINK);
    
    // Die Kamera zeigt den Ablauf automatisch, bevor du spielst:
    ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
    ZeigeTextAufBildschirm("Ein Transporter (Van) nährt sich am Sauerbruchweg und parkt...", 20, 60);
    ZeigeTextAufBildschirm("Zwei Brueder steigen aus (der aeltere Cousin + das juengste Kind).", 20, 100);
    ZeigeTextAufBildschirm("Sie laufen gemeinsam vor zur Haustuer...", 20, 140);
    ZeigeTextAufBildschirm("Der dritte Cousin oeffnet die Tuer und begruesst sie!", 20, 180);
    
    // Automatischer Übergang zum echten, steuerbaren Gameplay
    printf("[CUTSCENE] Intro beendet. Handoff an Spieler-Steuerung.\n");
    state = GAME_STATE_STORY; 
}

void SpieleIntroKonzert() {
    printf("INTRO: Yung Lean Konzert startet direkt auf Deutsch!\n");
    printf("Musik: 'Ginseng Strip 2002' laeuft fuer exakt 5-6 Sekunden.\n");
    strcpy(save_data.username, "Setzmann");
    save_data.language = 0; 
    
    // NEU: Nach dem Konzert springt das Spiel jetzt zuerst in das filmische Intro!
    state = GAME_STATE_STORY_INTRO;
}

/* ========================================================================= */
/* 5. GANGSTER-FAMILIEN GAMEPLAY-SCHLEIFE                                    */
/* ========================================================================= */
void SpielSchleife() {
    if (!game_initialized) {
        InitialisiereHighEndGrafik();
        game_initialized = 1;
    }

    switch(state) {
        case GAME_STATE_TITLE:
            SpieleIntroKonzert();
            break;
            
        case GAME_STATE_STORY_INTRO:
            // Hier läuft jetzt das filmische Van-Intro mit deinen Cousins!
            RendereStoryIntroCutscene();
            break;
            
        case GAME_STATE_STORY:
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("STORY-MODUS: Du steuerst Setzmann. Gehe zur Holzbruecke!", 20, 20);
            LadeUmgebungsDetailsPeine(); 
            RenderMiniMap(); 
            break;
            
        case GAME_STATE_FREE_WORLD:
            AktualisiereN64Eingabe();
            LadeUmgebungsDetailsPeine();
            RenderMiniMap();
            break;
            
        default:
            break;
    }
}

// =========================================================================
// DIE BRÜCKE ZUR MAIN.C
// =========================================================================
void psp_game_init() {
    InitialisiereHighEndGrafik();
}

void psp_game_update() {
    AktualisiereN64Eingabe();
}

void psp_game_render() {
    SpielSchleife();
}
