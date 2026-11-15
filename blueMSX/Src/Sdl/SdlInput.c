/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Sdl/SdlInput.c,v $
**
** $Revision: 1.11 $
**
** $Date: 2008-03-31 19:42:23 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
#include "ArchInput.h"


#include "Language.h"
#include "InputEvent.h"
#include "IniFileParser.h"
#include <stdio.h>
#include <SDL.h>

#define MAX_JOYSTICKS 8


#define KBD_TABLE_LEN 512

#define KBD_TABLE_NUM 3
static int kbdTable[KBD_TABLE_NUM][KBD_TABLE_LEN];
static int kbdTableBackup[KBD_TABLE_NUM][KBD_TABLE_LEN];
static int keyStatus[KBD_TABLE_NUM][KBD_TABLE_LEN];
static int selectedKey;
static int selectedDikKey;
static int selectedTable;
static int editEnabled;
static int kbdModifiers;
static UInt32 buttonState = 0;
static int hasFocus = 1;

static char keyboardConfigDir[512];

static char DefaultConfigName[] = "blueMSX Default";

static char currentConfigFile[512];



#define SDLK_JOY1_BUTTON1  (480 + 1)
#define SDLK_JOY1_BUTTON2  (480 + 2)
#define SDLK_JOY1_UP       (480 + 3)
#define SDLK_JOY1_DOWN     (480 + 4)
#define SDLK_JOY1_LEFT     (480 + 5)
#define SDLK_JOY1_RIGHT    (480 + 6)

#define SDLK_JOY2_BUTTON1  (480 + 7)
#define SDLK_JOY2_BUTTON2  (480 + 8)
#define SDLK_JOY2_UP       (480 + 9)
#define SDLK_JOY2_DOWN     (480 + 10)
#define SDLK_JOY2_LEFT     (480 + 11)
#define SDLK_JOY2_RIGHT    (480 + 12)







#if 0

char* dik2str(int dikKey) 
{
    if (dikKey < 0 || dikKey >= KBD_TABLE_LEN) {
        return "";
    }
    return dikStrings[dikKey];
}

void keyboardSaveConfig(char* configName)
{
    char fileName[512];
    int i, n;
    
    if (configName[0] == 0) {
        return;
    }

    sprintf(fileName, "%s/%s.config", keyboardConfigDir, configName);
    
    iniFileOpen(fileName);
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        char profString[32];
        sprintf(profString, "Keymapping-%d", n);
        for (i = 0; i < EC_KEYCOUNT; i++) {
            const char* keyCode = inputEventCodeToString(i);
            const char* dikName = "";
            int j;
            for (j = 0; j < KBD_TABLE_LEN; j++) {
                if (kbdTable[n][j] == i) {
                    dikName = dik2str(j);
                    break;
                }
            }
            if (keyCode != NULL) {
                char key[32] = { 0 };
                strcat(key, keyCode);
                strcat(key, " ");
                iniFileWriteString(profString, key, (char*)dikName);
//            WritePrivateProfileString(profString, keyCode, dikName, fileName);
            }
        }
    
        memcpy(kbdTableBackup[n], kbdTable[n], sizeof(kbdTableBackup[n]));
    }
    sprintf(currentConfigFile, configName);
    iniFileClose();
}
int keyboardLoadConfig(char* configName)
{
    char fileName[MAX_PATH];
    FILE* file;
    int i;
    int n;

    keyboardResetKbd();

    if (configName[0] == 0) {
        sprintf(fileName, "%s/%s.config", keyboardConfigDir, DefaultConfigName);
    }
    else {
        sprintf(fileName, "%s/%s.config", keyboardConfigDir, configName);
    }

    file = fopen(fileName, "r");
    if (file == NULL) {
        if (kbdTable[0][DIK_NUMPADENTER] == 0) {
            kbdTable[0][DIK_NUMPADENTER] = kbdTable[0][DIK_RETURN];
        }
        return 0;
    }
    fclose(file);

    sprintf(currentConfigFile, *configName ? configName : DefaultConfigName);

    iniFileOpen(fileName);

    for (n = 0; n < KBD_TABLE_NUM; n++) {
        char profString[32];
        sprintf(profString, "Keymapping-%d", n);
        for (i = 0; i < EC_KEYCOUNT; i++) {
            const char* keyCode = inputEventCodeToString(i);
            if (keyCode != NULL && *keyCode != 0) {
                char dikName[256];
                int dikKey;
                char key[32] = { 0 };
                strcat(key, keyCode);
                strcat(key, " ");
                iniFileGetString(profString, key, "", dikName, sizeof(dikName));
//                GetPrivateProfileString(profString, keyCode, "", dikName, sizeof(dikName), fileName);
                dikKey = str2dik(dikName);
                if (dikKey > 0) {
                    int j;
                    for (j = 0; j < KBD_TABLE_LEN; j++) {
                        if (kbdTable[n][j] == i) {
                            kbdTable[n][j] = 0;
                        }
                    }
                    kbdTable[n][dikKey] = i;
                }
            }
        }
    }
    iniFileClose();

    if (kbdTable[0][DIK_NUMPADENTER] == 0) {
        kbdTable[0][DIK_NUMPADENTER] = kbdTable[0][DIK_RETURN];
    }

    return 1;
}

#endif




// initKbdTable initializes the keyboard table with default keys
static void initKbdTable()
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


    kbdTable[1][SDLK_JOY1_BUTTON1] = EC_JOY1_BUTTON1;
    kbdTable[1][SDLK_JOY1_BUTTON2] = EC_JOY1_BUTTON2;
    kbdTable[1][SDLK_JOY1_UP     ] = EC_JOY1_UP;
    kbdTable[1][SDLK_JOY1_DOWN   ] = EC_JOY1_DOWN;
    kbdTable[1][SDLK_JOY1_LEFT   ] = EC_JOY1_LEFT;
    kbdTable[1][SDLK_JOY1_RIGHT  ] = EC_JOY1_RIGHT;
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
    

    kbdTable[2][SDLK_JOY2_BUTTON1] = EC_JOY2_BUTTON1;
    kbdTable[2][SDLK_JOY2_BUTTON2] = EC_JOY2_BUTTON2;
    kbdTable[2][SDLK_JOY2_UP     ] = EC_JOY2_UP;
    kbdTable[2][SDLK_JOY2_DOWN   ] = EC_JOY2_DOWN;
    kbdTable[2][SDLK_JOY2_LEFT   ] = EC_JOY2_LEFT;
    kbdTable[2][SDLK_JOY2_RIGHT  ] = EC_JOY2_RIGHT;
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

void keyboardSetDirectory(char* directory)
{
    strcpy(keyboardConfigDir, directory);
}

void keyboardInit()
{
    char fileName[512];
    FILE* file;

    initKbdTable();

    inputEventReset();

    kbdModifiers = 0;
    
    sprintf(fileName, "%s/%s.config", keyboardConfigDir, DefaultConfigName);
    file = fopen(fileName, "r");
    if (file == NULL) {
//        keyboardSaveConfig(DefaultConfigName);
        return;
    }
    sprintf(currentConfigFile, DefaultConfigName);
    fclose(file);
}

void keyboardSetFocus(int handle, int focus) 
{
    if (focus) {
        hasFocus |= handle;
    }
    else {
        hasFocus &= ~handle;
    }
}

static void keyboardHanldeKeypress(int code, int pressed) 
{
    int n;

    for (n = 0; n < KBD_TABLE_NUM; n++) {
        int wasPressed = keyStatus[n][code];
        int keyCode = kbdTable[n][code];
        int isEditing = selectedTable == n && editEnabled && selectedKey != 0;
        keyStatus[n][code] = pressed;

        if (pressed == wasPressed || (keyCode == 0 && !isEditing)) {
            continue;
        }

        if (pressed) {
            if (isEditing) {
                int i;
                for (i = 0; i < KBD_TABLE_LEN; i++) {
                    if (kbdTable[n][i] == selectedKey) {
                        kbdTable[n][i] = 0;
                    }
                }

                kbdTable[n][code] = selectedKey;
                selectedDikKey = code;

                inputEventUnset(keyCode);
            }
            else {
                inputEventSet(keyCode);
            }
        }
        else {
            inputEventUnset(keyCode);
        }
    }
}

static void keyboardResetKbd() 
{
    int n;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        int i;
        for (i = 0; i < KBD_TABLE_LEN; i++) {
            kbdModifiers = 0;
            keyboardHanldeKeypress(kbdTable[n][i], 0);
        }
    }
    inputEventReset();
    buttonState = 0;
}

void keyboardUpdate() 
{ 
    const Uint8* keyBuf;
    int    keyNum;

    if (!hasFocus) {
        keyboardResetKbd();
        return;
    }

    keyBuf = SDL_GetKeyboardState(&keyNum);

    if (keyBuf == NULL) {
        return;
    }

    kbdModifiers = ((keyBuf[SDL_SCANCODE_LSHIFT] ? 1 : 0) << 0) | ((keyBuf[SDL_SCANCODE_RSHIFT] ? 1 : 0) << 1) | 
                   ((keyBuf[SDL_SCANCODE_LCTRL]  ? 1 : 0) << 2) | ((keyBuf[SDL_SCANCODE_RCTRL]  ? 1 : 0) << 3) | 
                   ((keyBuf[SDL_SCANCODE_LALT]   ? 1 : 0) << 4) | ((keyBuf[SDL_SCANCODE_RALT]   ? 1 : 0) << 5) | 
                   ((keyBuf[SDL_SCANCODE_LGUI] ? 1 : 0) << 6) | ((keyBuf[SDL_SCANCODE_RGUI] ? 1 : 0) << 7);

    if (kbdModifiers &&
            (keyBuf[SDL_SCANCODE_F6] | keyBuf[SDL_SCANCODE_F7]  | keyBuf[SDL_SCANCODE_F8]  | keyBuf[SDL_SCANCODE_F9]  | 
            keyBuf[SDL_SCANCODE_F10] | keyBuf[SDL_SCANCODE_F11] | keyBuf[SDL_SCANCODE_F12]))
    {
        int i;
        for (i = 0; i < keyNum; i++) {
            keyboardHanldeKeypress(i, 0);
        }
    }
    else {
        int i;
        for (i = 0; i < keyNum; i++) {
            keyboardHanldeKeypress(i, keyBuf[i]);
        }
    }
} 


int keyboardGetModifiers()
{
    return kbdModifiers;
}



void  archUpdateJoystick() {}

UInt8 archJoystickGetState(int joystickNo) { return 0; }
int  archJoystickGetCount() { return 0; }
char* archJoystickGetName(int index) { return ""; }
void archMouseSetForceLock(int lock) { }

void  archPollInput() 
{
    keyboardUpdate();
}
void  archKeyboardSetSelectedKey(int keyCode) {}
char* archGetSelectedKey() { return ""; }
char* archGetMappedKey() { return ""; }
int   archKeyboardIsKeyConfigured(int msxKeyCode) { return 0; }
int   archKeyboardIsKeySelected(int msxKeyCode) { return 0; }
char* archKeyconfigSelectedKeyTitle() { return ""; }
char* archKeyconfigMappedToTitle() { return ""; }
char* archKeyconfigMappingSchemeTitle() { return ""; }

