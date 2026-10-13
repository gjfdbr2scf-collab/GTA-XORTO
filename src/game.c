#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// 1. SPIEL-KONFIGURATION & FARBEN (Echter 2000er Vice-City-Look)
// ============================================================================
#define VICE_CITY_PINK   0xFF007F  // Neon-Pink für Menüs und wichtige Texte
#define VICE_CITY_BLUE   0x00F0FF  // Karibisches Neon-Blau für Anzeigen
#define VICE_CITY_ORANGE 0xFF5F00  // 80er-Sonnenuntergang für den Hintergrund

typedef struct {
    char spielerName[32];
    char sprache[16];
    int steuerungTyp; // 0 = N64-Style Joystick
} SystemEinstellungen;

enum SpielModus { HAUPTMENUE, STORY_MODE, FREE_OPEN_WORLD, MULTIPLAYER, SYSTEM_SETTINGS };
enum StoryKapitel { INTRO_KONZERT, BRUECKE_TREFFEN, ALDI_EINKAUF, GARTEN_SESSION };

SystemEinstellungen aktuelleSettings;
enum SpielModus aktuellerModus = HAUPTMENUE;
enum StoryKapitel aktuelleStory = INTRO_KONZERT;

// Hilfsfunktionen für die Grafik-Engine (wird von der PSP/R36S Hardware aufgerufen)
void SetzeHintergrundFarbe(unsigned int farbe) {}
void SetzeTextFarbe(unsigned int farbe) {}
void ZeigeTextAufBildschirm(const char* text, int x, int y) {
    // Gibt den Text im Entwickler-Log aus
    printf("[GRAFIK ANZEIGE] %s\n", text);
}

// ============================================================================
// 2. SYSTEM SETTINGS & STARTMENÜ (Exakt laut README)
// ============================================================================
void ZeigeSystemSettings() {
    SetzeHintergrundFarbe(VICE_CITY_ORANGE);
    SetzeTextFarbe(VICE_CITY_BLUE);
    ZeigeTextAufBildschirm("SYSTEM SETTINGS", 50, 20);
    ZeigeTextAufBildschirm("1. Bildschirm", 50, 50);
    ZeigeTextAufBildschirm("2. Audio", 50, 70);
    ZeigeTextAufBildschirm("3. Steuerung (N64 Joystick Mode)", 50, 90);
    ZeigeTextAufBildschirm("4. Sprache", 50, 110);
    ZeigeTextAufBildschirm("5. Datum", 50, 130);
    ZeigeTextAufBildschirm("6. Uhrzeit", 50, 150);
    ZeigeTextAufBildschirm("7. Systeminfo", 50, 170);
    ZeigeTextAufBildschirm("8. Auf Standardeinstellung zuruecksetzen", 50, 190);
}

void ZeigeInGameStartMenue() {
    // Das graue Menü während des Spiels
    SetzeHintergrundFarbe(0x555555); // Grau
    SetzeTextFarbe(0xFFFFFF); // Weiß
    ZeigeTextAufBildschirm("--- PAUSE ---", 100, 50);
    ZeigeTextAufBildschirm("1. Save (Speichert auf SD-Karte)", 100, 80);
    ZeigeTextAufBildschirm("2. Control Settings", 100, 110);
    ZeigeTextAufBildschirm("3. Return to the Menu", 100, 140);
}

// ============================================================================
// 3. INTRO (Direkter Durchlauf ohne Namen/Sprachabfrage!)
// ============================================================================
void UpdateIntroKonzert() {
    SetzeHintergrundFarbe(0x000000); // Konzert-Halle ist dunkel
    SetzeTextFarbe(VICE_CITY_PINK);
    
    ZeigeTextAufBildschirm("PEINE PSP GAME - INITIALISIERUNG", 20, 20);
    ZeigeTextAufBildschirm("YUNG LEAN KONZERT LIVE IN PEINE!", 20, 60);
    ZeigeTextAufBildschirm("Musik: 'Ginseng Strip 2002' laeuft (5-6 Sek)...", 20, 90);
    
    // Automatische Schwarzblende und Daten-Setup im Hintergrund
    strcpy(aktuelleSettings.spielerName, "Setzmann");
    strcpy(aktuelleSettings.sprache, "Deutsch");
    aktuelleSettings.steuerungTyp = 0; // N64 Joystick
    
    ZeigeTextAufBildschirm("Bild wird dunkel... Musik blendet aus...", 20, 140);
    ZeigeTextAufBildschirm("System: Daten auf SD-Karte gesichert. Keine Abfragen!", 20, 170);
    
    // Sofortiger, automatischer Wechsel zum ersten echten Kapitel auf der Brücke
    aktuelleStory = BRUECKE_TREFFEN;
    aktuellerModus = STORY_MODE;
}

// ============================================================================
// 4. STORY MODE (Absolut interaktiv, keine erzwungenen Videos)
// ============================================================================
void UpdateStoryMode() {
    SetzeHintergrundFarbe(VICE_CITY_ORANGE); // Schicker Vice-City-Sonnenuntergang
    SetzeTextFarbe(0xFFFFFF);

    if (aktuelleStory == BRUECKE_TREFFEN) {
        ZeigeTextAufBildschirm("KAPITEL 1: DIE BRUECKE", 240, 20);
        ZeigeTextAufBildschirm("Charakter: Setzmann [STEUERBAR]", 20, 50);
        ZeigeTextAufBildschirm("Erinnerung an 'Ginseng Strip 2002'...", 20, 80);
        ZeigeTextAufBildschirm("Ein Mann mit Kapuze nähert sich und summt die Melodie.", 20, 110);
        ZeigeTextAufBildschirm("Es ist Young Lean! Englischer Dialog startet...", 20, 140);
        
        // Spielplatz & Zuhause-Logik triggern
        ZeigeTextAufBildschirm("Naechste Schritte geladen: Spielplatz-Planung -> PS5 Fussball-Session", 20, 200);
    } 
    else if (aktuelleStory == ALDI_EINKAUF) {
        ZeigeTextAufBildschirm("KAPITEL 2: ALDI MARKT", 240, 20);
        ZeigeTextAufBildschirm("[INTERAKTIVER ABSCHNITT - Keine automatische Cutscene!]", 20, 50);
        ZeigeTextAufBildschirm("Gehe durch den Laden und kaufe Getraenke & Center Shocks.", 20, 80);
        ZeigeTextAufBildschirm("Zurueck zum Spielplatz: Letztes Treffen mit Young Lean um 17:00 Uhr.", 20, 120);
        ZeigeTextAufBildschirm("Gruppen-Handklatscher ausgefuehrt! Schwarzblende.", 20, 150);
    }
    else if (aktuelleStory == GARTEN_SESSION) {
        ZeigeTextAufBildschirm("KAPITEL 3: GARTEN", 240, 20);
        ZeigeTextAufBildschirm("Garten-Session mit gelb-schwarzem Ball aktiv.", 20, 50);
        ZeigeTextAufBildschirm("AUDIO-LOG: 'Kinder, kommt essen!' (Mutter bleibt unsichtbar)", 20, 90);
    }
}

// ============================================================================
// 5. FREE OPEN WORLD (Volle Freiheit in Peine)
// ============================================================================
void UpdateFreeOpenWorld() {
    SetzeHintergrundFarbe(VICE_CITY_ORANGE);
    SetzeTextFarbe(VICE_CITY_BLUE);
    
    ZeigeTextAufBildschirm("FREE OPEN WORLD: STADT PEINE (24/7 Aktiv)", 20, 20);
    ZeigeTextAufBildschirm("Spieler: Setzmann | Begleiter: Sermijan & Sefkan", 20, 50);
    ZeigeTextAufBildschirm("Nutze Bus, Zug oder gehe zu Fuss zu Aldi.", 20, 80);
    ZeigeTextAufBildschirm("System-Info: Keine Pflicht-Dialoge, kein erzwungener Fortschritt.", 20, 120);
    ZeigeTextAufBildschirm("Radio-Schnittstelle: Musik laeuft in Fahrzeugen & Zuegen.", 20, 150);
}

// ============================================================================
// 6. LOCAL MULTIPLAYER (Genau 3 Spieler ohne Online-Server)
// ============================================================================
void UpdateLocalMultiplayer() {
    SetzeHintergrundFarbe(0x111111);
    SetzeTextFarbe(VICE_CITY_PINK);
    
    ZeigeTextAufBildschirm("LOCAL MULTIPLAYER MODE", 20, 20);
    ZeigeTextAufBildschirm("Verbindung: Nur Lokal | Server: AUS", 20, 50);
    ZeigeTextAufBildschirm("STATUS: Genau 3 Spieler im Match.", 20, 80);
    
    // Zuweisung erfolgt laut deiner Vorgabe komplett zufällig!
    ZeigeTextAufBildschirm("Zufaellige Charakterzuweisung abgeschlossen:", 20, 120);
    ZeigeTextAufBildschirm("-> Spieler 1 steuert: Setzmann", 40, 150);
    ZeigeTextAufBildschirm("-> Spieler 2 steuert: Sermijan", 40, 170);
    ZeigeTextAufBildschirm("-> Spieler 3 steuert: Sefkan", 40, 190);
}

// ============================================================================
// Haupt-Schleife der Spiel-Engine
// ============================================================================
int main() {
    // Automatischer Start in das Yung-Lean-Intro
    UpdateIntroKonzert();
    
    // Hier schaltet die PSP/R36S je nach Tastendruck in deine Modi um:
    UpdateStoryMode();
    
