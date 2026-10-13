#include "include/psp_input.h"
#include <stdio.h>

// Verarbeitet die N64-Joystick-Steuerung für Setzmann in Peine
void VerarbeiteN64Steuerung(int analogX, int analogY, unsigned int tastenBitmaske) {
    
    // 1. Der Analog-Stick steuert die Bewegung vorwärts/rückwärts
    if (analogY > 50)  printf("Steuerung: Setzmann laeuft VORWAERTS\n");
    if (analogY < -50) printf("Steuerung: Setzmann laeuft RUECKWAERTS\n");

    // KORREKTUR: Setzmann dreht sich jetzt realistisch im Raum!
    if (analogX > 50)  printf("Steuerung: Setzmann DREHT SICH nach RECHTS\n");
    if (analogX < -50) printf("Steuerung: Setzmann DREHT SICH nach LINKS\n");

    // 2. Der A-Knopf für Interaktionen (Center Shocks im Aldi kaufen, PS5 starten)
    if (tastenBitmaske & 0x4000) {
        printf("A-Knopf gedrueckt: Aktion ausfuehren / Kaufen.\n");
    }

    // 3. Der B-Knopf zum Abbrechen oder Menü verlassen
    if (tastenBitmaske & 0x2000) {
        printf("B-Knopf gedrueckt: Zurueck / Abbrechen.\n");
    }
}
