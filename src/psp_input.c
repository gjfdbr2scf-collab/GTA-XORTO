#include <pspctrl.h>

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

    input_state.joystick.x = 128;
    input_state.joystick.y = 128;

    /* PSP-Controller-Abfrage aktivieren */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void psp_input_update(void)
{
    SceCtrlData pad;

    /* Aktuellen PSP-Controllerzustand auslesen */
    sceCtrlReadBufferPositive(&pad, 1);

    input_state.up =
        (pad.Buttons & PSP_CTRL_UP) != 0;

    input_state.down =
        (pad.Buttons & PSP_CTRL_DOWN) != 0;

    input_state.left =
        (pad.Buttons & PSP_CTRL_LEFT) != 0;

    input_state.right =
        (pad.Buttons & PSP_CTRL_RIGHT) != 0;

    input_state.cross =
        (pad.Buttons & PSP_CTRL_CROSS) != 0;

    input_state.circle =
        (pad.Buttons & PSP_CTRL_CIRCLE) != 0;

    input_state.square =
        (pad.Buttons & PSP_CTRL_SQUARE) != 0;

    input_state.triangle =
        (pad.Buttons & PSP_CTRL_TRIANGLE) != 0;

    input_state.start =
        (pad.Buttons & PSP_CTRL_START) != 0;

    input_state.select =
        (pad.Buttons & PSP_CTRL_SELECT) != 0;

    input_state.l =
        (pad.Buttons & PSP_CTRL_LTRIGGER) != 0;

    input_state.r =
        (pad.Buttons & PSP_CTRL_RTRIGGER) != 0;

    /* Analogstick-Werte: 0 bis 255 */
    input_state.joystick.x = pad.Lx;
    input_state.joystick.y = pad.Ly;
}

const PSPInputState *psp_input_get_state(void)
{
    return &input_state;
}

void psp_input_shutdown(void)
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

    input_state.joystick.x = 128;
    input_state.joystick.y = 128;
}
