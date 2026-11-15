/*****************************************************************************
** blueMSX+ - Platform layer: keyboard shortcuts
**
** Reads and writes the Windows blueMSX+ profile format; see the header.
** A binding fires when its key is released with the same modifiers held, as
** on Windows; "max speed" and "play reverse" act while the key is held.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "PlatformShortcuts.h"
#include "PlatformPaths.h"
#include "IniFileParser.h"
#include "Actions.h"
#include "ArchGlob.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Windows virtual keys <-> scancodes ------------------------------------ */

static const struct { unsigned short vk; unsigned short sc; } vkMap[] = {
    { 0x03, SDL_SCANCODE_PAUSE },         /* VK_CANCEL: Ctrl+Break */
    { 0x08, SDL_SCANCODE_BACKSPACE },     { 0x09, SDL_SCANCODE_TAB },
    { 0x0C, SDL_SCANCODE_CLEAR },         { 0x0D, SDL_SCANCODE_RETURN },
    { 0x13, SDL_SCANCODE_PAUSE },         { 0x14, SDL_SCANCODE_CAPSLOCK },
    { 0x1B, SDL_SCANCODE_ESCAPE },        { 0x20, SDL_SCANCODE_SPACE },
    { 0x21, SDL_SCANCODE_PAGEUP },        { 0x22, SDL_SCANCODE_PAGEDOWN },
    { 0x23, SDL_SCANCODE_END },           { 0x24, SDL_SCANCODE_HOME },
    { 0x25, SDL_SCANCODE_LEFT },          { 0x26, SDL_SCANCODE_UP },
    { 0x27, SDL_SCANCODE_RIGHT },         { 0x28, SDL_SCANCODE_DOWN },
    { 0x2C, SDL_SCANCODE_PRINTSCREEN },   { 0x2D, SDL_SCANCODE_INSERT },
    { 0x2E, SDL_SCANCODE_DELETE },        { 0x5D, SDL_SCANCODE_APPLICATION },
    { 0x60, SDL_SCANCODE_KP_0 },          { 0x61, SDL_SCANCODE_KP_1 },
    { 0x62, SDL_SCANCODE_KP_2 },          { 0x63, SDL_SCANCODE_KP_3 },
    { 0x64, SDL_SCANCODE_KP_4 },          { 0x65, SDL_SCANCODE_KP_5 },
    { 0x66, SDL_SCANCODE_KP_6 },          { 0x67, SDL_SCANCODE_KP_7 },
    { 0x68, SDL_SCANCODE_KP_8 },          { 0x69, SDL_SCANCODE_KP_9 },
    { 0x6A, SDL_SCANCODE_KP_MULTIPLY },   { 0x6B, SDL_SCANCODE_KP_PLUS },
    { 0x6C, SDL_SCANCODE_KP_ENTER },      { 0x6D, SDL_SCANCODE_KP_MINUS },
    { 0x6E, SDL_SCANCODE_KP_PERIOD },     { 0x6F, SDL_SCANCODE_KP_DIVIDE },
    { 0x90, SDL_SCANCODE_NUMLOCKCLEAR },  { 0x91, SDL_SCANCODE_SCROLLLOCK },
    { 0xBA, SDL_SCANCODE_SEMICOLON },     { 0xBB, SDL_SCANCODE_EQUALS },
    { 0xBC, SDL_SCANCODE_COMMA },         { 0xBD, SDL_SCANCODE_MINUS },
    { 0xBE, SDL_SCANCODE_PERIOD },        { 0xBF, SDL_SCANCODE_SLASH },
    { 0xC0, SDL_SCANCODE_GRAVE },         { 0xDB, SDL_SCANCODE_LEFTBRACKET },
    { 0xDC, SDL_SCANCODE_BACKSLASH },     { 0xDD, SDL_SCANCODE_RIGHTBRACKET },
    { 0xDE, SDL_SCANCODE_APOSTROPHE },    { 0xE2, SDL_SCANCODE_NONUSBACKSLASH },
};

static int vkToScancode(int vk)
{
    size_t i;
    if (vk >= 'A' && vk <= 'Z') return SDL_SCANCODE_A + (vk - 'A');
    if (vk >= '1' && vk <= '9') return SDL_SCANCODE_1 + (vk - '1');
    if (vk == '0')              return SDL_SCANCODE_0;
    if (vk >= 0x70 && vk <= 0x7B) return SDL_SCANCODE_F1 + (vk - 0x70);     /* F1..F12 */
    if (vk >= 0x7C && vk <= 0x87) return SDL_SCANCODE_F13 + (vk - 0x7C);    /* F13..F24 */
    for (i = 0; i < sizeof(vkMap) / sizeof(vkMap[0]); i++) {
        if (vkMap[i].vk == vk) return vkMap[i].sc;
    }
    return 0;
}

static int scancodeToVk(int sc)
{
    size_t i;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0)                         return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)  return 0x70 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return 0x7C + (sc - SDL_SCANCODE_F13);
    for (i = 0; i < sizeof(vkMap) / sizeof(vkMap[0]); i++) {
        if (vkMap[i].sc == sc && vkMap[i].vk != 0x03) return vkMap[i].vk;
    }
    return 0;
}

/* ---- Actions --------------------------------------------------------------- */

static void cart1Special(void) { actionMenuSpecialCart1(-1, -1); }
static void cart2Special(void) { actionMenuSpecialCart2(-1, -1); }

typedef struct {
    const char* name;           /* key in the profile file */
    void (*action)(void);       /* NULL for the two held actions */
} ShortcutDef;

static const ShortcutDef defs[SC_COUNT] = {
    [SC_SPRITES_ENABLE]        = { "spritesEnable",                actionToggleSpriteEnable },
    [SC_FDC_TIMING]            = { "fdcTiming",                    actionToggleFdcTiming },
    [SC_HDD_SD_BOOST]          = { "hddSdBoost",                   actionToggleHddSdBoost },
    [SC_NO_SPRITE_LIMITS]      = { "noSpriteLimits",               actionToggleNoSpriteLimits },
    [SC_MSX_KEYBOARD_QUIRK]    = { "msxKeyboardQuirk",             actionToggleMsxKeyboardQuirk },
    [SC_MSX_AUDIO_SWITCH]      = { "msxAudioSwitch",               actionToggleMsxAudioSwitch },
    [SC_FRONT_SWITCH]          = { "frontSwitch",                  actionToggleFrontSwitch },
    [SC_PAUSE_SWITCH]          = { "pauseSwitch",                  actionTogglePauseSwitch },
    [SC_QUIT]                  = { "quit",                         actionQuit },
    [SC_WAV_CAPTURE]           = { "wavCapture",                   actionToggleWaveCapture },
    [SC_WAV_CAPTURE_START_AS]  = { "wavCaptureStartAs",            actionWaveCaptureStartAs },
    [SC_VIDEO_CAP_LOAD]        = { "videoCapLoad",                 actionVideoCaptureLoad },
    [SC_VIDEO_CAP_PLAY]        = { "videoCapPlay",                 actionVideoCapturePlay },
    [SC_VIDEO_CAP_REC]         = { "videoCapRec",                  actionVideoCaptureRec },
    [SC_VIDEO_CAP_REC_AS]      = { "videoCapRecAs",                actionVideoCaptureRecAs },
    [SC_VIDEO_CAP_STOP]        = { "videoCapStop",                 actionVideoCaptureStop },
    [SC_VIDEO_CAP_SAVE]        = { "videoCapSave",                 actionVideoCaptureSave },
    [SC_RECORD_VIDEO_START]    = { "recordVideoStart",             actionRecordVideoStart },
    [SC_RECORD_VIDEO_START_AS] = { "recordVideoStartAs",           actionRecordVideoStartAs },
    [SC_RECORD_VIDEO_STOP]     = { "recordVideoStop",              actionRecordVideoStop },
    [SC_RECORD_VIDEO_TOGGLE]   = { "recordVideoToggle",            actionRecordVideoToggle },
    [SC_YM2413_BACKEND_CYCLE]  = { "ym2413BackendCycle",           actionYm2413BackendCycle },
    [SC_Y8950_BACKEND_CYCLE]   = { "y8950BackendCycle",            actionY8950BackendCycle },
    [SC_SCREEN_CAPTURE]        = { "screenCapture",                actionScreenCapture },
    [SC_SCREEN_CAPTURE_AS]     = { "screenCaptureAs",              actionScreenCaptureAs },
    [SC_SCREEN_CAPTURE_SMALL]  = { "screenCaptureUnfilteredSmall", actionScreenCaptureUnfilteredSmall },
    [SC_SCREEN_CAPTURE_LARGE]  = { "screenCaptureUnfilteredLarge", actionScreenCaptureUnfilteredLarge },
    [SC_STATE_LOAD]            = { "cpuStateLoad",                 actionLoadState },
    [SC_STATE_SAVE]            = { "cpuStateSave",                 actionSaveState },
    [SC_STATE_QUICK_LOAD]      = { "cpuStateQuickLoad",            actionQuickLoadState },
    [SC_STATE_QUICK_SAVE]      = { "cpuStateQuickSave",            actionQuickSaveState },
    [SC_STATE_QUICK_SAVE_UNDO] = { "cpuStateQuickSaveUndo",        actionQuickSaveStateUndo },
    [SC_CART1_INSERT]          = { "cartInsert[0]",                actionCartInsert1 },
    [SC_CART2_INSERT]          = { "cartInsert[1]",                actionCartInsert2 },
    [SC_CART1_SPECIAL]         = { "cartSpecialMenu[0]",           cart1Special },
    [SC_CART2_SPECIAL]         = { "cartSpecialMenu[1]",           cart2Special },
    [SC_CART1_REMOVE]          = { "cartRemove[0]",                actionCartRemove1 },
    [SC_CART2_REMOVE]          = { "cartRemove[1]",                actionCartRemove2 },
    [SC_CART_AUTORESET]        = { "cartAutoReset[0]",             actionToggleCartAutoReset },
    [SC_DISKA_INSERT]          = { "diskInsert[0]",                actionDiskInsertA },
    [SC_DISKB_INSERT]          = { "diskInsert[1]",                actionDiskInsertB },
    [SC_DISKA_DIR_INSERT]      = { "diskDirInsert[0]",             actionDiskDirInsertA },
    [SC_DISKB_DIR_INSERT]      = { "diskDirInsert[1]",             actionDiskDirInsertB },
    [SC_DISK_QUICK_CHANGE]     = { "diskChange[0]",                actionDiskQuickChange },
    [SC_DISKA_REMOVE]          = { "diskRemove[0]",                actionDiskRemoveA },
    [SC_DISKB_REMOVE]          = { "diskRemove[1]",                actionDiskRemoveB },
    [SC_DISK_AUTORESET]        = { "diskAutoReset[0]",             actionToggleDiskAutoReset },
    [SC_CAS_INSERT]            = { "casInsert",                    actionCasInsert },
    [SC_CAS_REWIND]            = { "casRewind",                    actionCasRewind },
    [SC_CAS_SET_POS]           = { "casSetPos",                    actionCasSetPosition },
    [SC_CAS_REMOVE]            = { "casRemove",                    actionCasRemove },
    [SC_CAS_READONLY]          = { "casToggleReadonly",            actionCasToggleReadonly },
    [SC_CAS_AUTOREWIND]        = { "casAutoRewind",                actionToggleCasAutoRewind },
    [SC_CAS_SAVE]              = { "casSave",                      actionCasSave },
    [SC_PRN_FORMFEED]          = { "prnFormFeed",                  actionPrinterForceFormFeed },
    [SC_MOUSE_LOCK]            = { "mouseLockToggle",              actionToggleMouseCapture },
    [SC_RUN_PAUSE]             = { "emulationRunPause",            actionEmuTogglePause },
    [SC_STOP]                  = { "emulationStop",                actionEmuStop },
    [SC_SPEED_MAX_HOLD]        = { "emuSpeedFull",                 NULL },
    [SC_PLAY_REVERSE_HOLD]     = { "emuPlayReverse",               NULL },
    [SC_SPEED_NORMAL]          = { "emuSpeedNormal",               actionEmuSpeedNormal },
    [SC_SPEED_INC]             = { "emuSpeedInc",                  actionEmuSpeedIncrease },
    [SC_SPEED_DEC]             = { "emuSpeedDec",                  actionEmuSpeedDecrease },
    [SC_SPEED_MAX_TOGGLE]      = { "emuSpeedToggle",               actionMaxSpeedToggle },
    [SC_SIZE_1X]               = { "windowSize1x",                 actionWindowSize1x },
    [SC_SIZE_2X]               = { "windowSize2x",                 actionWindowSize2x },
    [SC_SIZE_3X]               = { "windowSize3x",                 actionWindowSize3x },
    [SC_SIZE_4X]               = { "windowSize4x",                 actionWindowSize4x },
    [SC_SIZE_5X]               = { "windowSize5x",                 actionWindowSize5x },
    [SC_SIZE_6X]               = { "windowSize6x",                 actionWindowSize6x },
    [SC_SIZE_7X]               = { "windowSize7x",                 actionWindowSize7x },
    [SC_SIZE_8X]               = { "windowSize8x",                 actionWindowSize8x },
    [SC_SIZE_MINIMIZED]        = { "windowSizeMinimized",          actionWindowSizeMinimized },
    [SC_SIZE_FULLSCREEN]       = { "windowSizeFullscreen",         actionWindowSizeFullscreen },
    [SC_FULLSCREEN_TOGGLE]     = { "windowSizeFullscreenToggle",   actionFullscreenToggle },
    [SC_RESET_SOFT]            = { "resetSoft",                    actionEmuResetSoft },
    [SC_RESET_HARD]            = { "resetHard",                    actionEmuResetHard },
    [SC_RESET_CLEAN]           = { "resetClean",                   actionEmuResetClean },
    [SC_VOLUME_INC]            = { "volumeIncrease",               actionVolumeIncrease },
    [SC_VOLUME_DEC]            = { "volumeDecrease",               actionVolumeDecrease },
    [SC_VOLUME_MUTE]           = { "volumeMute",                   actionMuteToggleMaster },
    [SC_VOLUME_STEREO]         = { "volumeStereo",                 actionVolumeToggleStereo },
    [SC_THEME_SWITCH]          = { "themeSwitch",                  actionNextTheme },
    [SC_PROP_EMULATION]        = { "propShowEmulation",            actionPropShowEmulation },
    [SC_PROP_VIDEO]            = { "propShowVideo",                actionPropShowVideo },
    [SC_PROP_AUDIO]            = { "propShowAudio",                actionPropShowAudio },
    [SC_PROP_EFFECTS]          = { "propShowEffects",              actionPropShowEffects },
    [SC_PROP_SETTINGS]         = { "propShowSettings",             actionPropShowSettings },
    [SC_PROP_APPEARANCE]       = { "propShowApearance",            actionPropShowApearance },
    [SC_PROP_PORTS]            = { "propShowPorts",                actionPropShowPorts },
    [SC_LANGUAGE]              = { "optionsShowLanguage",          actionOptionsShowLanguage },
    [SC_TOOL_MACHINE_EDITOR]   = { "toolsShowMachineEditor",       actionToolsShowMachineEditor },
    [SC_TOOL_SHORTCUT_EDITOR]  = { "toolsShowShorcutEditor",       actionToolsShowShorcutEditor },
    [SC_TOOL_KEYBOARD_EDITOR]  = { "toolsShowKeyboardEditor",      actionToolsShowKeyboardEditor },
    [SC_TOOL_MIXER]            = { "toolsShowMixer",               actionToolsShowMixer },
    [SC_TOOL_DEBUGGER]         = { "toolsShowDebugger",            actionToolsShowDebugger },
    [SC_TOOL_TRAINER]          = { "toolsShowTrainer",             actionToolsShowTrainer },
    [SC_HELP]                  = { "helpShowHelp",                 actionHelpShowHelp },
    [SC_ABOUT]                 = { "helpShowAbout",                actionHelpShowAbout },
};

/* ---- Profiles -------------------------------------------------------------- */

static char shortcutsDir[512];
static PlatformHotkeySet sets[SC_COUNT];
/* Joystick entries are not understood yet; keep their text for saving. */
static char joyText[SC_COUNT][256];

void shortcutsSetDirectory(const char* directory)
{
    snprintf(shortcutsDir, sizeof(shortcutsDir), "%s", directory);
}

static void profilePath(char* out, size_t outSize, const char* profile)
{
    snprintf(out, outSize, "%s/%s.shortcuts", shortcutsDir, profile);
}

static void parseSet(const char* text, PlatformHotkeySet* set, char* joy, size_t joySize)
{
    const char* p = text;
    int b = 0;

    memset(set, 0, sizeof(*set));
    joy[0] = 0;
    while (*p && b < SHORTCUT_MAX_BINDINGS) {
        char slot[256];
        size_t n = strcspn(p, ",");
        unsigned int value;

        if (n >= sizeof(slot)) n = sizeof(slot) - 1;
        memcpy(slot, p, n);
        slot[n] = 0;
        p += n;
        if (*p == ',') p++;

        if (strchr(slot, '@') != NULL) {
            /* A joystick binding: 8 hex digits followed by @"device : button". */
            size_t used = strlen(joy);
            snprintf(joy + used, joySize - used, "%s%s", used ? "," : "", slot);
            continue;
        }
        if (sscanf(slot, "%X", &value) != 1 || value == 0) continue;
        if ((value & 0xFF) != HOTKEY_TYPE_KEYBOARD) continue;
        set->slots[b].type = HOTKEY_TYPE_KEYBOARD;
        set->slots[b].mods = (value >> 8) & 0xFF;
        set->slots[b].key  = vkToScancode((value >> 16) & 0xFFFF);
        if (set->slots[b].key != 0) b++;
    }
}

static int profileExists(const char* profile)
{
    char path[600];
    profilePath(path, sizeof(path), profile);
    return platformIsFile(path);
}

int platformShortcutsListProfiles(char names[][64], int max)
{
    char pattern[600];
    ArchGlob* glob;
    int count = 0;
    int i;

    snprintf(pattern, sizeof(pattern), "%s/*.shortcuts", shortcutsDir);
    glob = archGlob(pattern, ARCH_GLOB_FILES);
    if (glob == NULL) return 0;
    for (i = 0; i < glob->count && count < max; i++) {
        const char* base = strrchr(glob->pathVector[i], '/');
        size_t len;
        base = base ? base + 1 : glob->pathVector[i];
        len = strlen(base) - strlen(".shortcuts");
        if (len == 0 || len >= 64) continue;
        memcpy(names[count], base, len);
        names[count][len] = 0;
        count++;
    }
    archGlobFree(glob);
    return count;
}

void platformShortcutsLoad(char* profile)
{
    char name[64];
    char path[600];
    IniFile* ini;
    int i;

    name[0] = 0;
    if (profile != NULL && *profile && profileExists(profile)) {
        snprintf(name, sizeof(name), "%s", profile);
    }
    else if (profileExists("blueMSX")) {
        strcpy(name, "blueMSX");
    }
    else {
        char list[1][64];
        if (platformShortcutsListProfiles(list, 1) > 0) strcpy(name, list[0]);
    }
    if (profile != NULL) strcpy(profile, name);

    memset(sets, 0, sizeof(sets));
    memset(joyText, 0, sizeof(joyText));
    if (name[0] == 0) return;

    profilePath(path, sizeof(path), name);
    ini = iniFileOpen(path);
    if (ini == NULL) return;
    for (i = 0; i < SC_COUNT; i++) {
        char buffer[512];
        char lookup[128];
        if (defs[i].name == NULL) continue;
        /* The parser matches entry prefixes, so "name=" keeps "casSave" from
        ** finding "casSaveAs". */
        snprintf(lookup, sizeof(lookup), "%s=", defs[i].name);
        iniFileGetString(ini, "Shortcuts", lookup, "0", buffer, sizeof(buffer));
        parseSet(buffer, &sets[i], joyText[i], sizeof(joyText[i]));
    }
    iniFileClose(ini);
}

int platformShortcutsSave(const char* profile)
{
    char path[600];
    IniFile* ini;
    int i;

    if (profile == NULL || *profile == 0) return 0;
    profilePath(path, sizeof(path), profile);
    ini = iniFileOpen(path);
    if (ini == NULL) return 0;
    for (i = 0; i < SC_COUNT; i++) {
        char buffer[512];
        int b;
        buffer[0] = 0;
        for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
            const PlatformHotkey* h = &sets[i].slots[b];
            int vk;
            size_t len = strlen(buffer);
            if (h->type != HOTKEY_TYPE_KEYBOARD) continue;
            vk = scancodeToVk(h->key);
            if (vk == 0) continue;
            snprintf(buffer + len, sizeof(buffer) - len, "%s%.8X", len ? "," : "",
                     (unsigned)((vk << 16) | (h->mods << 8) | HOTKEY_TYPE_KEYBOARD));
        }
        if (joyText[i][0]) {
            size_t len = strlen(buffer);
            snprintf(buffer + len, sizeof(buffer) - len, "%s%s", len ? "," : "", joyText[i]);
        }
        if (buffer[0] == 0) strcpy(buffer, "00000000");
        iniFileWriteString(ini, "Shortcuts", (char*)defs[i].name, buffer);
    }
    return iniFileClose(ini);
}

void platformShortcutsFree(void)
{
    memset(sets, 0, sizeof(sets));
}

PlatformHotkeySet* platformShortcutGet(ShortcutId id)
{
    return id >= 0 && id < SC_COUNT ? &sets[id] : NULL;
}

const char* platformShortcutIniName(ShortcutId id)
{
    return id >= 0 && id < SC_COUNT ? defs[id].name : "";
}

/* ---- Text ------------------------------------------------------------------ */

const char* platformHotkeyToString(const PlatformHotkey* h)
{
    static char text[128];
    const char* name;

    text[0] = 0;
    if (h == NULL || h->type != HOTKEY_TYPE_KEYBOARD) return text;
    if (h->mods & HOTKEY_MOD_LCTRL)  strcat(text, "Ctrl+");
    if (h->mods & HOTKEY_MOD_RCTRL)  strcat(text, "RCtrl+");
    if (h->mods & HOTKEY_MOD_LSHIFT) strcat(text, "Shift+");
    if (h->mods & HOTKEY_MOD_RSHIFT) strcat(text, "RShift+");
    if (h->mods & HOTKEY_MOD_LALT)   strcat(text, "Alt+");
    if (h->mods & HOTKEY_MOD_RALT)   strcat(text, "AltGr+");
    if (h->mods & HOTKEY_MOD_LGUI)   strcat(text, "Super+");
    if (h->mods & HOTKEY_MOD_RGUI)   strcat(text, "RSuper+");
    name = SDL_GetScancodeName((SDL_Scancode)h->key);
    strncat(text, name && *name ? name : "?", sizeof(text) - strlen(text) - 1);
    return text;
}

const char* platformHotkeySetToString(const PlatformHotkeySet* set)
{
    static char text[256];
    int b;

    text[0] = 0;
    if (set == NULL) return text;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (set->slots[b].type != HOTKEY_TYPE_KEYBOARD) continue;
        if (text[0]) strncat(text, ", ", sizeof(text) - strlen(text) - 1);
        strncat(text, platformHotkeyToString(&set->slots[b]), sizeof(text) - strlen(text) - 1);
    }
    return text;
}

const char* platformShortcutText(ShortcutId id)
{
    return platformHotkeySetToString(platformShortcutGet(id));
}

/* ---- Key handling ---------------------------------------------------------- */

static int mods;
static unsigned char consumed[SDL_SCANCODE_COUNT];
static int maxSpeedHeld;
static int reverseHeld;

static int modBit(int sc)
{
    switch (sc) {
    case SDL_SCANCODE_LSHIFT: return HOTKEY_MOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return HOTKEY_MOD_RSHIFT;
    case SDL_SCANCODE_LCTRL:  return HOTKEY_MOD_LCTRL;
    case SDL_SCANCODE_RCTRL:  return HOTKEY_MOD_RCTRL;
    case SDL_SCANCODE_LALT:   return HOTKEY_MOD_LALT;
    case SDL_SCANCODE_RALT:   return HOTKEY_MOD_RALT;
    case SDL_SCANCODE_LGUI:   return HOTKEY_MOD_LGUI;
    case SDL_SCANCODE_RGUI:   return HOTKEY_MOD_RGUI;
    }
    return 0;
}

static int setHas(const PlatformHotkeySet* set, int key, int m, int ignoreMods)
{
    int b;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (set->slots[b].type == HOTKEY_TYPE_KEYBOARD && set->slots[b].key == key &&
            (ignoreMods || set->slots[b].mods == m)) {
            return 1;
        }
    }
    return 0;
}

void platformShortcutRun(ShortcutId id)
{
    if (id == SC_SPEED_MAX_HOLD)    { actionMaxSpeedToggle(); return; }
    if (id == SC_PLAY_REVERSE_HOLD) return;
    if (id >= 0 && id < SC_COUNT && defs[id].action != NULL) defs[id].action();
}

int platformShortcutMods(void)
{
    return mods;
}

void platformShortcutsReleaseAll(void)
{
    mods = 0;
    memset(consumed, 0, sizeof(consumed));
    if (maxSpeedHeld) { actionMaxSpeedRelease(); maxSpeedHeld = 0; }
    if (reverseHeld)  { actionStopPlayReverse(); reverseHeld = 0; }
}

int platformShortcutKey(int sc, int pressed)
{
    int bit = modBit(sc);
    int i;

    if (sc <= 0 || sc >= SDL_SCANCODE_COUNT) return 0;
    if (bit) {
        if (pressed) mods |= bit; else mods &= ~bit;
        return 0;
    }

    if (pressed) {
        int any = 0;
        for (i = 0; i < SC_COUNT; i++) {
            if (setHas(&sets[i], sc, mods, 0)) any = 1;
        }
        if (!any) return 0;
        consumed[sc] = 1;
        if (setHas(&sets[SC_SPEED_MAX_HOLD], sc, mods, 0)) {
            actionMaxSpeedSet();
            maxSpeedHeld = 1;
        }
        if (setHas(&sets[SC_PLAY_REVERSE_HOLD], sc, mods, 0) && !reverseHeld) {
            actionStartPlayReverse();
            reverseHeld = 1;
        }
        return 1;
    }

    if (!consumed[sc]) return 0;
    consumed[sc] = 0;
    if (maxSpeedHeld && setHas(&sets[SC_SPEED_MAX_HOLD], sc, 0, 1)) {
        actionMaxSpeedRelease();
        maxSpeedHeld = 0;
    }
    if (reverseHeld && setHas(&sets[SC_PLAY_REVERSE_HOLD], sc, 0, 1)) {
        actionStopPlayReverse();
        reverseHeld = 0;
    }
    for (i = 0; i < SC_COUNT; i++) {
        if (defs[i].action != NULL && setHas(&sets[i], sc, mods, 0)) {
            defs[i].action();
        }
    }
    return 1;
}
