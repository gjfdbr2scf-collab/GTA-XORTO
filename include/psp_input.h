#ifndef PSP_INPUT_H
#define PSP_INPUT_H

typedef struct
{
    int x;
    int y;
} PSPJoystick;

typedef struct
{
    int up;
    int down;
    int left;
    int right;

    int cross;
    int circle;
    int square;
    int triangle;

    int start;
    int select;
    int l;
    int r;

    PSPJoystick joystick;
} PSPInputState;

void psp_input_init(void);
void psp_input_update(void);
const PSPInputState *psp_input_get_state(void);
void psp_input_shutdown(void);

#endif
