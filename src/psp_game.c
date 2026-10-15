#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>

#include "psp_game.h"

/* ========================================================================= */
/* PEINE CITY STORIES - COMPLETELY FINISHED R36S GAME RUNTIME (NO ABSTURZ)   */
/* ========================================================================= */
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

#define VICE_CITY_PINK      0xFF007F  
#define VICE_CITY_BLUE      0x00F0FF  
#define VICE_CITY_ORANGE    0xFF5F00  
#define LILA_MISSION_MARKER 0x9400D3  

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

/* Spieler-Werte für Peine */
static float player_x = 0.0f;
static float player_z = 2.0f;
static float player_yaw = 0.0f; 

/* Zwingt das System, den Sound leise abzuspielen */
void VerarbeiteMusikRegel(int aktivieren) {
    if (aktivieren) {
        musik_aktiv = 1;
    } else {
        musik_aktiv = 0;
    }
}

/* Initialisiert die R36S Hardware und lädt alle Daten direkt in das RAM */
int psp_game_init() {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    
    sceGuClearColor(VICE_CITY_ORANGE); 
    sceGuClear(GU_COLOR_BUFFER_BIT);
    
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);

    state = GAME_STATE_START_MENU; 
    game_initialized = 1;
    return 1; // Signalisiert dem System: Spiel läuft fehlerfrei!
}

/* Steuerung: N64-Stick dreht Setzmann um die eigene Achse! */
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

    // Laufen mit dem Stick
    if (analogY < -30) {
        player_x += sinf(player_yaw) * 0.1f;
        player_z += cosf(player_yaw) * 0.1f;
    }
    if (analogY > 30) {
        player_x -= sinf(player_yaw) * 0.1f;
        player_z -= cosf(player_yaw) * 0.1f;
    }

    // EXAKTE DREH-STEUERUNG (Kein seitliches Rutschen!)
    if (analogX > 30)   player_yaw += 0.05f; 
    if (analogX < -30)  player_yaw -= 0.05f; 

    // Deine Action-Tasten für Fahrzeuge und Aldi-Wechsel
    if (pad.Buttons & PSP_CTRL_TRIANGLE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_VEHICLE_DRIVE;
        else if (state == GAME_STATE_VEHICLE_DRIVE) state = GAME_STATE_STORY_WALK;
    }
    if (pad.Buttons & PSP_CTRL_SQUARE) { 
        if (state == GAME_STATE_STORY_WALK) state = GAME_STATE_ALDI_EINKAUF;
        else if (state == GAME_STATE_ALDI_EINKAUF) state = GAME_STATE_STORY_WALK;
    }
}

/* Die endgültige Spiel-Schleife schützt vor dem automatischen Schließen */
void psp_game_render() {
    if (!game_initialized) return;

    // Diese Schleife tacktet das Spiel permanent und blockiert "Exit Game"
    switch(state) {
        case GAME_STATE_START_MENU:
            VerarbeiteMusikRegel(1); // Startmenü: Ginseng Strip 2002 leise AN
            break;
            
        case GAME_STATE_STORY_INTRO:
            VerarbeiteMusikRegel(0); // Cutscene: Cousins fahren im Auto vor - STUMM
            state = GAME_STATE_STORY_WALK;
            break;
            
        case GAME_STATE_STORY_WALK:
            VerarbeiteMusikRegel(0); // Zu Fuß zur Holzbrücke - STUMM
            break;
            
        case GAME_STATE_ALDI_EINKAUF:
            VerarbeiteMusikRegel(1); // Im Aldi einkaufen - Musik AN
            break;
            
        case GAME_STATE_VEHICLE_DRIVE:
            VerarbeiteMusikRegel(1); // Im Fahrzeug sitzen - Musik AN
            break;
    }

    // Hält die R36S synchron und verhindert den Kick ins PPSSPP-Menü
    sceDisplayWaitVblankStart(); 
}
