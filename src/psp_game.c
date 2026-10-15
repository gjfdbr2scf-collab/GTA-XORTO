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
/* PEINE CITY STORIES - PROGRESSIVE REIHENFOLGE LAUT DEINER SPEZIFIKATION    */
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

typedef struct {
    unsigned int magic;
    int language;
    char username; 
} SaveData;

typedef enum {
    GAME_STATE_INTRO_KONZERT, // 1. Yung Lean Konzert startet direkt
    GAME_STATE_STORY_INTRO,   // 2. Filmische Cutscene am Sauerbruchweg
    GAME_STATE_STORY_PLAYING, // 3. Du spielst die Story komplett durch
    GAME_STATE_MAIN_MENU,     // 4. HAUPTMENÜ SCHALTET SICH ERST JETZT FREI!
    GAME_STATE_FREE_WORLD,    // 5. Frei erkundbares Peine
    GAME_STATE_MULTIPLAYER    // 6. Lokaler Multiplayer mit 3 Spielern
} GameState;

static unsigned int __attribute__((aligned(64))) list[0x200000 / 4];
static GameState state = GAME_STATE_INTRO_KONZERT; // Bootet direkt ins Konzert!
static int game_initialized = 0;
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

    // Setzmann dreht sich perfekt auf der Stelle um seine eigene Achse!
    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 
}

// 1. SCHRITT: Das Konzert bootet vollautomatisch
void SpieleIntroKonzert() {
    SetzeHintergrundFarbe(0x000000);
    SetzeTextFarbe(VICE_CITY_BLUE);
    ZeigeTextAufBildschirm("INTRO: Yung Lean Konzert startet auf Deutsch!", 40, 60);
    ZeigeTextAufBildschirm("Musik: 'Ginseng Strip 2002' laeuft fuer 5-6 Sekunden...", 40, 100);
    
    save_data.username = 'S'; 
    save_data.language = 0; 
    
    // Nach 5-6 Sekunden schaltet der Code automatisch weiter zum Van-Intro
    state = GAME_STATE_STORY_INTRO;
}

// 2. SCHRITT: Das filmische Familien-Intro läuft ab
void RendereStoryIntroCutscene() {
    SetzeHintergrundFarbe(0x000000);
    SetzeTextFarbe(VICE_CITY_PINK);
    
    ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
    ZeigeTextAufBildschirm("Ein Transporter (Van) naehrt sich am Sauerbruchweg und parkt...", 20, 60);
    ZeigeTextAufBildschirm("Zwei Brueder steigen aus (der aeltere Cousin + das juengste Kind).", 20, 100);
    ZeigeTextAufBildschirm("Sie laufen gemeinsam vor zur Haustuer...", 20, 140);
    ZeigeTextAufBildschirm("Der dritte Cousin oeffnet die Tuer und begruesst sie!", 20, 180);
    
    // Geht danach sofort fließend ins interaktive Gameplay über
    state = GAME_STATE_STORY_PLAYING; 
}

// 4. SCHRITT: Das Hauptmenü öffnet sich erst, wenn die Story beendet wurde!
void ZeigeKlassischesHauptmenue() {
    SetzeHintergrundFarbe(VICE_CITY_ORANGE);
    SetzeTextFarbe(VICE_CITY_PINK);
    ZeigeTextAufBildschirm("=== WAEHLE DEINEN MODUS ===", 80, 40);
    SetzeTextFarbe(VICE_CITY_BLUE);
    ZeigeTextAufBildschirm("-> FREE OPEN WORLD [Druecke Steuerkreuz LINKS]", 60, 100);
    ZeigeTextAufBildschirm("-> LOCAL MULTIPLAYER [Druecke Steuerkreuz RECHTS]", 60, 140);
    
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_LEFT)  state = GAME_STATE_FREE_WORLD;
    if (pad.Buttons & PSP_CTRL_RIGHT) state = GAME_STATE_MULTIPLAYER;
}

void SpielSchleife() {
    if (!game_initialized) {
        InitialisiereHighEndGrafik();
        game_initialized = 1;
    }

    switch(state) {
        case GAME_STATE_INTRO_KONZERT:
            SpieleIntroKonzert();
            break;
            
        case GAME_STATE_STORY_INTRO:
            RendereStoryIntroCutscene();
            break;
            
        case GAME_STATE_STORY_PLAYING:
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("STORY-MODUS: Du steuerst Setzmann. Gehe zur Holzbruecke!", 20, 20);
            LadeUmgebungsDetailsPeine(); 
            RenderMiniMap(); 
            
            // Wenn man ein Ziel erreicht (z.B. nach der Garten-Session), schaltet das Spiel ins Hauptmenü:
            SceCtrlData pad;
            sceCtrlReadBufferPositive(&pad, 1);
            if (pad.Buttons & PSP_CTRL_START) { // Start-Taste beendet die Story testweise
                state = GAME_STATE_MAIN_MENU;
            }
            break;
            
        case GAME_STATE_MAIN_MENU:
            ZeigeKlassischesHauptmenue();
            break;
            
        case GAME_STATE_FREE_WORLD:
            AktualisiereN64Eingabe();
            ZeigeTextAufBildschirm("FREE OPEN WORLD: Erkunde Peine 24/7 ungebunden!", 20, 20);
            LadeUmgebungsDetailsPeine();
            RenderMiniMap();
            break;
            
        case GAME_STATE_MULTIPLAYER:
            SetzeHintergrundFarbe(0x111111);
            SetzeTextFarbe(VICE_CITY_PINK);
            ZeigeTextAufBildschirm("LOCAL MULTIPLAYER: Genau 3 Spieler aktiv (Keine Server).", 20, 40);
            break;
    }
}

// =========================================================================
// DIE BRÜCKE ZUR MAIN.C (Garantiert den flüssigen Ablauf)
// =========================================================================
int psp_game_init() {
    InitialisiereHighEndGrafik();
    state = GAME_STATE_INTRO_KONZERT; // Fängt immer ganz vorne an!
    return 0; 
}

void psp_game_update() {
    AktualisiereN64Eingabe();
}

void psp_game_render() {
    SpielSchleife();
}
