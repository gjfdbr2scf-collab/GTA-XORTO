#include "psp_input.h"

static PSPInputState input_state;

void psp_input_init(void)
{
    input_state.up = 0;
    input_state.down = 0;
    input_state.left = 0;
    input_state.right = 0;

    input_state.cross = 0;
    input_state.circle = 0;
    input_state.square = 0;
    input_state.triangle = 0;

    input_state.start = 0;
    input_state.select = 0;
    input_state.l = 0;
    input_state.r = 0;

    input_state.joystick.x = 0;
    input_state.joystick.y = 0;
}

void psp_input_update(void)
{
    /*
     * Die eigentliche PSP-Tasten- und
     * Joystick-Abfrage kommt in einem
     * späteren Schritt.
     */
}

const PSPInputState *psp_input_get_state(void)
{
    return &input_state;
}

void psp_input_shutdown(void)
{
    input_state.joystick.x = 0;
    input_state.joystick.y = 0;
}
