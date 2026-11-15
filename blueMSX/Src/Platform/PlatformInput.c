/*****************************************************************************
** blueMSX+ - Platform layer: keyboard, mouse and joysticks
**
** Frontends report keys as USB HID usage codes (SDL_Scancode numbering) with
** platformKeyEvent; the state is applied to the emulated keyboard when the
** emulation thread polls (archPollInput). The default mapping is the one of
** the classic blueMSX SDL port.
**
** Copyright (C) 2003-2006 Daniel Vik (keyboard tables)
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "Platform.h"
#include "PlatformInternal.h"
#include "PlatformShortcuts.h"
#include "ArchInput.h"
#include "InputEvent.h"
#include "MsxTypes.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mutex.h>
#include <string.h>

#define KBD_TABLE_LEN 512
#define KBD_TABLE_NUM 3

/* Pseudo keys for joystick directions and buttons, above the scancodes in use. */
#define SDL_SCANCODE_JOY1_BUTTON1  (480 + 1)
#define SDL_SCANCODE_JOY1_BUTTON2  (480 + 2)
#define SDL_SCANCODE_JOY1_UP       (480 + 3)
#define SDL_SCANCODE_JOY1_DOWN     (480 + 4)
#define SDL_SCANCODE_JOY1_LEFT     (480 + 5)
#define SDL_SCANCODE_JOY1_RIGHT    (480 + 6)
#define SDL_SCANCODE_JOY2_BUTTON1  (480 + 7)
#define SDL_SCANCODE_JOY2_BUTTON2  (480 + 8)
#define SDL_SCANCODE_JOY2_UP       (480 + 9)
#define SDL_SCANCODE_JOY2_DOWN     (480 + 10)
#define SDL_SCANCODE_JOY2_LEFT     (480 + 11)
#define SDL_SCANCODE_JOY2_RIGHT    (480 + 12)

static int kbdTable[KBD_TABLE_NUM][KBD_TABLE_LEN];
static int keyStatus[KBD_TABLE_NUM][KBD_TABLE_LEN];
static volatile unsigned char keyDown[KBD_TABLE_LEN];   /* written by the UI thread */
static volatile int hasFocus = 1;
static int kbdModifiers;

// initKbdTable initializes the keyboard table with default keys
static void initKbdTable(void)
{
    memset (kbdTable, 0, sizeof(kbdTable));

    kbdTable[0][SDL_SCANCODE_0          ] = EC_0;
    kbdTable[0][SDL_SCANCODE_1          ] = EC_1;
    kbdTable[0][SDL_SCANCODE_2          ] = EC_2;
    kbdTable[0][SDL_SCANCODE_3          ] = EC_3;
    kbdTable[0][SDL_SCANCODE_4          ] = EC_4;
    kbdTable[0][SDL_SCANCODE_5          ] = EC_5;
    kbdTable[0][SDL_SCANCODE_6          ] = EC_6;
    kbdTable[0][SDL_SCANCODE_7          ] = EC_7;
    kbdTable[0][SDL_SCANCODE_8          ] = EC_8;
    kbdTable[0][SDL_SCANCODE_9          ] = EC_9;
    
    kbdTable[0][SDL_SCANCODE_MINUS      ] = EC_NEG;
    kbdTable[0][SDL_SCANCODE_EQUALS     ] = EC_CIRCFLX;
    kbdTable[0][SDL_SCANCODE_BACKSLASH  ] = EC_BKSLASH;
    kbdTable[0][SDL_SCANCODE_LEFTBRACKET   ] = EC_AT;
    kbdTable[0][SDL_SCANCODE_RIGHTBRACKET  ] = EC_LBRACK;
    kbdTable[0][SDL_SCANCODE_SEMICOLON  ] = EC_SEMICOL;
    kbdTable[0][SDL_SCANCODE_APOSTROPHE ] = EC_COLON;
    kbdTable[0][SDL_SCANCODE_NONUSHASH  ] = EC_RBRACK;
    kbdTable[0][SDL_SCANCODE_COMMA      ] = EC_COMMA;
    kbdTable[0][SDL_SCANCODE_PERIOD     ] = EC_PERIOD;
    kbdTable[0][SDL_SCANCODE_SLASH      ] = EC_DIV;
    kbdTable[0][SDL_SCANCODE_RCTRL      ] = EC_UNDSCRE;

    kbdTable[0][SDL_SCANCODE_A          ] = EC_A;
    kbdTable[0][SDL_SCANCODE_B          ] = EC_B;
    kbdTable[0][SDL_SCANCODE_C          ] = EC_C;
    kbdTable[0][SDL_SCANCODE_D          ] = EC_D;
    kbdTable[0][SDL_SCANCODE_E          ] = EC_E;
    kbdTable[0][SDL_SCANCODE_F          ] = EC_F;
    kbdTable[0][SDL_SCANCODE_G          ] = EC_G;
    kbdTable[0][SDL_SCANCODE_H          ] = EC_H;
    kbdTable[0][SDL_SCANCODE_I          ] = EC_I;
    kbdTable[0][SDL_SCANCODE_J          ] = EC_J;
    kbdTable[0][SDL_SCANCODE_K          ] = EC_K;
    kbdTable[0][SDL_SCANCODE_L          ] = EC_L;
    kbdTable[0][SDL_SCANCODE_M          ] = EC_M;
    kbdTable[0][SDL_SCANCODE_N          ] = EC_N;
    kbdTable[0][SDL_SCANCODE_O          ] = EC_O;
    kbdTable[0][SDL_SCANCODE_P          ] = EC_P;
    kbdTable[0][SDL_SCANCODE_Q          ] = EC_Q;
    kbdTable[0][SDL_SCANCODE_R          ] = EC_R;
    kbdTable[0][SDL_SCANCODE_S          ] = EC_S;
    kbdTable[0][SDL_SCANCODE_T          ] = EC_T;
    kbdTable[0][SDL_SCANCODE_U          ] = EC_U;
    kbdTable[0][SDL_SCANCODE_V          ] = EC_V;
    kbdTable[0][SDL_SCANCODE_W          ] = EC_W;
    kbdTable[0][SDL_SCANCODE_X          ] = EC_X;
    kbdTable[0][SDL_SCANCODE_Y          ] = EC_Y;
    kbdTable[0][SDL_SCANCODE_Z          ] = EC_Z;

    kbdTable[0][SDL_SCANCODE_F1         ] = EC_F1;
    kbdTable[0][SDL_SCANCODE_F2         ] = EC_F2;
    kbdTable[0][SDL_SCANCODE_F3         ] = EC_F3;
    kbdTable[0][SDL_SCANCODE_F4         ] = EC_F4;
    kbdTable[0][SDL_SCANCODE_F5         ] = EC_F5;
    kbdTable[0][SDL_SCANCODE_ESCAPE     ] = EC_ESC;
    kbdTable[0][SDL_SCANCODE_TAB        ] = EC_TAB;
    kbdTable[0][SDL_SCANCODE_PAGEUP     ] = EC_STOP;
    kbdTable[0][SDL_SCANCODE_BACKSPACE  ] = EC_BKSPACE;
    kbdTable[0][SDL_SCANCODE_END        ] = EC_SELECT;
    kbdTable[0][SDL_SCANCODE_RETURN     ] = EC_RETURN;
    kbdTable[0][SDL_SCANCODE_SPACE      ] = EC_SPACE;
    kbdTable[0][SDL_SCANCODE_HOME       ] = EC_CLS;
    kbdTable[0][SDL_SCANCODE_INSERT     ] = EC_INS;
    kbdTable[0][SDL_SCANCODE_DELETE     ] = EC_DEL;
    kbdTable[0][SDL_SCANCODE_LEFT       ] = EC_LEFT;
    kbdTable[0][SDL_SCANCODE_UP         ] = EC_UP;
    kbdTable[0][SDL_SCANCODE_RIGHT      ] = EC_RIGHT;
    kbdTable[0][SDL_SCANCODE_DOWN       ] = EC_DOWN;

    kbdTable[0][SDL_SCANCODE_KP_MULTIPLY   ] = EC_NUMMUL;
    kbdTable[0][SDL_SCANCODE_KP_PLUS       ] = EC_NUMADD;
    kbdTable[0][SDL_SCANCODE_KP_DIVIDE     ] = EC_NUMDIV;
    kbdTable[0][SDL_SCANCODE_KP_MINUS      ] = EC_NUMSUB;
    kbdTable[0][SDL_SCANCODE_KP_PERIOD     ] = EC_NUMPER;
    kbdTable[0][SDL_SCANCODE_PAGEDOWN      ] = EC_NUMCOM;
    kbdTable[0][SDL_SCANCODE_KP_0    ] = EC_NUM0;
    kbdTable[0][SDL_SCANCODE_KP_1    ] = EC_NUM1;
    kbdTable[0][SDL_SCANCODE_KP_2    ] = EC_NUM2;
    kbdTable[0][SDL_SCANCODE_KP_3    ] = EC_NUM3;
    kbdTable[0][SDL_SCANCODE_KP_4    ] = EC_NUM4;
    kbdTable[0][SDL_SCANCODE_KP_5    ] = EC_NUM5;
    kbdTable[0][SDL_SCANCODE_KP_6    ] = EC_NUM6;
    kbdTable[0][SDL_SCANCODE_KP_7    ] = EC_NUM7;
    kbdTable[0][SDL_SCANCODE_KP_8    ] = EC_NUM8;
    kbdTable[0][SDL_SCANCODE_KP_9    ] = EC_NUM9;

    kbdTable[0][SDL_SCANCODE_LGUI     ] = EC_TORIKE;
    kbdTable[0][SDL_SCANCODE_RGUI     ] = EC_JIKKOU;
    kbdTable[0][SDL_SCANCODE_LSHIFT     ] = EC_LSHIFT;
    kbdTable[0][SDL_SCANCODE_RSHIFT     ] = EC_RSHIFT;
    kbdTable[0][SDL_SCANCODE_LCTRL      ] = EC_CTRL;
    kbdTable[0][SDL_SCANCODE_LALT       ] = EC_GRAPH;
    kbdTable[0][SDL_SCANCODE_RALT       ] = EC_CODE;
    kbdTable[0][SDL_SCANCODE_CAPSLOCK   ] = EC_CAPS;
    kbdTable[0][SDL_SCANCODE_KP_ENTER   ] = EC_PAUSE;
    kbdTable[0][SDL_SCANCODE_SYSREQ     ] = EC_PRINT;


    kbdTable[1][SDL_SCANCODE_JOY1_BUTTON1] = EC_JOY1_BUTTON1;
    kbdTable[1][SDL_SCANCODE_JOY1_BUTTON2] = EC_JOY1_BUTTON2;
    kbdTable[1][SDL_SCANCODE_JOY1_UP     ] = EC_JOY1_UP;
    kbdTable[1][SDL_SCANCODE_JOY1_DOWN   ] = EC_JOY1_DOWN;
    kbdTable[1][SDL_SCANCODE_JOY1_LEFT   ] = EC_JOY1_LEFT;
    kbdTable[1][SDL_SCANCODE_JOY1_RIGHT  ] = EC_JOY1_RIGHT;
    kbdTable[1][SDL_SCANCODE_0           ] = EC_COLECO1_0;
    kbdTable[1][SDL_SCANCODE_1           ] = EC_COLECO1_1;
    kbdTable[1][SDL_SCANCODE_2           ] = EC_COLECO1_2;
    kbdTable[1][SDL_SCANCODE_3           ] = EC_COLECO1_3;
    kbdTable[1][SDL_SCANCODE_4           ] = EC_COLECO1_4;
    kbdTable[1][SDL_SCANCODE_5           ] = EC_COLECO1_5;
    kbdTable[1][SDL_SCANCODE_6           ] = EC_COLECO1_6;
    kbdTable[1][SDL_SCANCODE_7           ] = EC_COLECO1_7;
    kbdTable[1][SDL_SCANCODE_8           ] = EC_COLECO1_8;
    kbdTable[1][SDL_SCANCODE_9           ] = EC_COLECO1_9;
    kbdTable[1][SDL_SCANCODE_MINUS       ] = EC_COLECO1_STAR;
    kbdTable[1][SDL_SCANCODE_EQUALS      ] = EC_COLECO1_HASH;
    

    kbdTable[2][SDL_SCANCODE_JOY2_BUTTON1] = EC_JOY2_BUTTON1;
    kbdTable[2][SDL_SCANCODE_JOY2_BUTTON2] = EC_JOY2_BUTTON2;
    kbdTable[2][SDL_SCANCODE_JOY2_UP     ] = EC_JOY2_UP;
    kbdTable[2][SDL_SCANCODE_JOY2_DOWN   ] = EC_JOY2_DOWN;
    kbdTable[2][SDL_SCANCODE_JOY2_LEFT   ] = EC_JOY2_LEFT;
    kbdTable[2][SDL_SCANCODE_JOY2_RIGHT  ] = EC_JOY2_RIGHT;
    kbdTable[2][SDL_SCANCODE_KP_0     ] = EC_COLECO2_0;
    kbdTable[2][SDL_SCANCODE_KP_1     ] = EC_COLECO2_1;
    kbdTable[2][SDL_SCANCODE_KP_2     ] = EC_COLECO2_2;
    kbdTable[2][SDL_SCANCODE_KP_3     ] = EC_COLECO2_3;
    kbdTable[2][SDL_SCANCODE_KP_4     ] = EC_COLECO2_4;
    kbdTable[2][SDL_SCANCODE_KP_5     ] = EC_COLECO2_5;
    kbdTable[2][SDL_SCANCODE_KP_6     ] = EC_COLECO2_6;
    kbdTable[2][SDL_SCANCODE_KP_7     ] = EC_COLECO2_7;
    kbdTable[2][SDL_SCANCODE_KP_8     ] = EC_COLECO2_8;
    kbdTable[2][SDL_SCANCODE_KP_9     ] = EC_COLECO2_9;
    kbdTable[2][SDL_SCANCODE_KP_MULTIPLY    ] = EC_COLECO2_STAR;
    kbdTable[2][SDL_SCANCODE_KP_DIVIDE      ] = EC_COLECO2_HASH;

//    keyboardSaveConfig(DefaultConfigName);
}

static void applyKey(int code, int pressed)
{
    int n;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        int keyCode = kbdTable[n][code];
        if (keyStatus[n][code] == pressed || keyCode == 0) {
            keyStatus[n][code] = pressed;
            continue;
        }
        keyStatus[n][code] = pressed;
        if (pressed) inputEventSet(keyCode);
        else         inputEventUnset(keyCode);
    }
}

static int modifiersFrom(const volatile unsigned char* down)
{
    return (down[SDL_SCANCODE_LSHIFT] ? 1 << 0 : 0) | (down[SDL_SCANCODE_RSHIFT] ? 1 << 1 : 0) |
           (down[SDL_SCANCODE_LCTRL]  ? 1 << 2 : 0) | (down[SDL_SCANCODE_RCTRL]  ? 1 << 3 : 0) |
           (down[SDL_SCANCODE_LALT]   ? 1 << 4 : 0) | (down[SDL_SCANCODE_RALT]   ? 1 << 5 : 0) |
           (down[SDL_SCANCODE_LGUI]   ? 1 << 6 : 0) | (down[SDL_SCANCODE_RGUI]   ? 1 << 7 : 0);
}

void platformInputInit(void)
{
    memset((void*)keyDown, 0, sizeof(keyDown));
    memset(keyStatus, 0, sizeof(keyStatus));
    initKbdTable();
    inputEventReset();
    kbdModifiers = 0;
}

void platformKeyEvent(int scancode, int pressed)
{
    int mods;
    if (scancode <= 0 || scancode >= KBD_TABLE_LEN) {
        return;
    }
    keyDown[scancode] = pressed ? 1 : 0;
    mods = modifiersFrom(keyDown);
    if (pressed) platformShortcutDown(mods, scancode);
    else         platformShortcutUp(mods, scancode);
}

void platformKeyboardFocus(int focused)
{
    hasFocus = focused;
    if (!focused) {
        memset((void*)keyDown, 0, sizeof(keyDown));
    }
}

/* Emulation thread. */
void archPollInput(void)
{
    int i;
    int hostShortcut;

    kbdModifiers = modifiersFrom(keyDown);

    /* Modifier + F6..F12 are host shortcuts: keep them from the MSX. */
    hostShortcut = kbdModifiers &&
        (keyDown[SDL_SCANCODE_F6] | keyDown[SDL_SCANCODE_F7] | keyDown[SDL_SCANCODE_F8] |
         keyDown[SDL_SCANCODE_F9] | keyDown[SDL_SCANCODE_F10] | keyDown[SDL_SCANCODE_F11] |
         keyDown[SDL_SCANCODE_F12]);

    for (i = 0; i < KBD_TABLE_LEN; i++) {
        applyKey(i, hasFocus && !hostShortcut ? keyDown[i] : 0);
    }
}

int archPollEvent(void) { return 0; }

/* Keyboard editor hooks (no editor yet). */
void  archKeyboardSetSelectedKey(int keyCode) {}
char* archGetSelectedKey(void) { return ""; }
char* archGetMappedKey(void) { return ""; }
int   archKeyboardIsKeyConfigured(int msxKeyCode) { return 0; }
int   archKeyboardIsKeySelected(int msxKeyCode) { return 0; }
char* archKeyconfigSelectedKeyTitle(void) { return ""; }
char* archKeyconfigMappedToTitle(void) { return ""; }
char* archKeyconfigMappingSchemeTitle(void) { return ""; }

/* ---- Joysticks (SDL gamepads come with the playable frontend step) ------- */

void  archUpdateJoystick(void) {}
UInt8 archJoystickGetState(int joystickNo) { return 0; }
int   archJoystickGetCount(void) { return 0; }
char* archJoystickGetName(int index) { return ""; }

/* ---- Mouse ---------------------------------------------------------------- */

static struct {
    SDL_Mutex* lock;
    AmEnableMode mode;
    int dx, dy;           /* accumulated relative motion */
    float x, y;           /* absolute, 0..1 */
    int buttons;
    int forceLock;
} mouse;

void platformMouseMotion(int dx, int dy, float x, float y)
{
    if (mouse.lock) SDL_LockMutex(mouse.lock);
    mouse.dx += dx;
    mouse.dy += dy;
    mouse.x = x;
    mouse.y = y;
    if (mouse.lock) SDL_UnlockMutex(mouse.lock);
}

void platformMouseButton(int button, int pressed)
{
    int bit = button == 1 ? 1 : (button == 2 || button == 3) ? 2 : 0;
    if (pressed) mouse.buttons |= bit;
    else         mouse.buttons &= ~bit;
}

void archMouseEmuEnable(AmEnableMode mode)
{
    if (mouse.lock == NULL) mouse.lock = SDL_CreateMutex();
    mouse.mode = mode;
    mouse.dx = mouse.dy = 0;
    platformFrontendMouseCapture(mode == AM_ENABLE_MOUSE || mouse.forceLock);
}

void archMouseSetForceLock(int lock)
{
    mouse.forceLock = lock;
    platformFrontendMouseCapture(mouse.mode == AM_ENABLE_MOUSE || lock);
}

void archMouseGetState(int* dx, int* dy)
{
    if (mouse.lock) SDL_LockMutex(mouse.lock);
    if (mouse.mode == AM_ENABLE_LASER) {
        *dx = (int)(mouse.x * 0x10000);
        *dy = (int)(mouse.y * 0x10000);
    }
    else {
        *dx = mouse.dx;
        *dy = mouse.dy;
        mouse.dx = mouse.dy = 0;
    }
    if (mouse.lock) SDL_UnlockMutex(mouse.lock);
}

int archMouseGetButtonState(int checkAlways)
{
    return mouse.mode != AM_DISABLE || checkAlways ? mouse.buttons : 0;
}
