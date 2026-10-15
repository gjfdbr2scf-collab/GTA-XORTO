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

// =========================================================================
// WICHTIG: DIE KERNEL-ERLAUBNIS FÜR DIE R36S HARDWARE!
// =========================================================================
PSP_MODULE_INFO("GTAXORTO", 0, 1, 1);                  // Definiert das Spiel im PSP-System
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU); // Gibt dem Prozessor die Erlaubnis

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
    GAME_STATE_STORY_INTRO, 
    GAME_STATE_STORY,       
    GAME_STATE_FREE_WORLD,  
    GAME_STATE_MULTIPLAYER
} GameState;

static unsigned int __attribute__((aligned(64))) list[0x200000 / 4];
static GameState state = GAME_STATE_TITLE;
static int game_initialized = 0;
static SaveData save_data;

/* Player-Koordinaten für Peine */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 

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

    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 
}

void RendereStoryIntroCutscene() {
    SetzeHintergrundFarbe(0x000000);
    SetzeTextFarbe(VICE_CITY_PINK);
    
    ZeigeTextAufBildschirm("--- INTRO CINEMATIC ---", 160, 20);
    ZeigeTextAufBildschirm("Ein Transporter (Van) naehrt sich am Sauerbruchweg und parkt...", 20, 60);
    ZeigeTextAufBildschirm("Zwei Brueder steigen aus (der aeltere Cousin + das juengste Kind).", 20, 100);
    ZeigeTextAufBildschirm("Sie laufen gemeinsam vor zur Haustuer...", 20, 140);
    ZeigeTextAufBildschirm("Der dritte Cousin oeffnet die Tuer und begruesst sie!", 20, 180);
    
    state = GAME_STATE_STORY; 
}

void SpieleIntroKonzert() {
    printf("INTRO: Yung Lean Konzert startet auf Deutsch!\n");
    printf("Musik: 'Ginseng Strip 2002' laeuft fuer exakt 5-6 Sekunden.\n");
    
    save_data.username = 'S'; 
    save_data.language = 0; 
    
    state = GAME_STATE_STORY_INTRO;
}

void SpielSchleife() {
    if (!game_initialized) {
        InitialisiereHighEndGrafik();
        game_initialized = 1;
    }

    switch(state) {
        case GAME_STATE_TITLE:
            SpieleIntroKonzert();
            break;
            
        case GAME_STATE_MAIN_MENU:
            printf("[MENUE] Hauptmenue unversehrt geladen. Bereit fuer Spielstart.\n");
            break;
            
        case GAME_STATE_STORY_INTRO:
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

int psp_game_init() {
    InitialisiereHighEndGrafik();
    return 1;
}

void psp_game_update() {
    AktualisiereN64Eingabe();
}

void psp_game_render() {
    SpielSchleife();
}
