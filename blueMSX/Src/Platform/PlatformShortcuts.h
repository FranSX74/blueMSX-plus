/*****************************************************************************
** blueMSX+ - Platform layer: keyboard shortcuts
**
** Profiles are the Windows blueMSX+ files ("<Shortcut Profiles>/<name>.shortcuts",
** [Shortcuts] name=XXXXXXXX[,XXXXXXXX...]), so a profile copied from Windows
** works unchanged. Their Windows virtual key codes are translated to the USB
** HID scancodes the platform layer uses; joystick bindings are kept in the
** file but not active yet.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef PLATFORM_SHORTCUTS_H
#define PLATFORM_SHORTCUTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define HOTKEY_TYPE_NONE     0
#define HOTKEY_TYPE_KEYBOARD 1
#define HOTKEY_TYPE_JOYSTICK 2

/* Modifier bits, as in the Windows profiles. */
#define HOTKEY_MOD_LSHIFT 0x01
#define HOTKEY_MOD_RSHIFT 0x02
#define HOTKEY_MOD_LCTRL  0x04
#define HOTKEY_MOD_RCTRL  0x08
#define HOTKEY_MOD_LALT   0x10
#define HOTKEY_MOD_RALT   0x20
#define HOTKEY_MOD_LGUI   0x40
#define HOTKEY_MOD_RGUI   0x80

#define SHORTCUT_MAX_BINDINGS 3

typedef struct {
    int type;           /* HOTKEY_TYPE_* */
    int mods;           /* HOTKEY_MOD_* */
    int key;            /* scancode for keyboard bindings */
} PlatformHotkey;

typedef struct {
    PlatformHotkey slots[SHORTCUT_MAX_BINDINGS];
} PlatformHotkeySet;

typedef enum {
    SC_SPRITES_ENABLE, SC_FDC_TIMING, SC_HDD_SD_BOOST, SC_NO_SPRITE_LIMITS,
    SC_MSX_KEYBOARD_QUIRK, SC_MSX_AUDIO_SWITCH, SC_FRONT_SWITCH, SC_PAUSE_SWITCH,
    SC_QUIT, SC_WAV_CAPTURE, SC_WAV_CAPTURE_START_AS,
    SC_VIDEO_CAP_LOAD, SC_VIDEO_CAP_PLAY, SC_VIDEO_CAP_REC, SC_VIDEO_CAP_REC_AS,
    SC_VIDEO_CAP_STOP, SC_VIDEO_CAP_SAVE,
    SC_RECORD_VIDEO_START, SC_RECORD_VIDEO_START_AS, SC_RECORD_VIDEO_STOP,
    SC_RECORD_VIDEO_TOGGLE, SC_YM2413_BACKEND_CYCLE, SC_Y8950_BACKEND_CYCLE,
    SC_SCREEN_CAPTURE, SC_SCREEN_CAPTURE_AS, SC_SCREEN_CAPTURE_SMALL, SC_SCREEN_CAPTURE_LARGE,
    SC_STATE_LOAD, SC_STATE_SAVE, SC_STATE_QUICK_LOAD, SC_STATE_QUICK_SAVE,
    SC_STATE_QUICK_SAVE_UNDO,
    SC_CART1_INSERT, SC_CART2_INSERT, SC_CART1_SPECIAL, SC_CART2_SPECIAL,
    SC_CART1_REMOVE, SC_CART2_REMOVE, SC_CART_AUTORESET,
    SC_DISKA_INSERT, SC_DISKB_INSERT, SC_DISKA_DIR_INSERT, SC_DISKB_DIR_INSERT,
    SC_DISK_QUICK_CHANGE, SC_DISKA_REMOVE, SC_DISKB_REMOVE, SC_DISK_AUTORESET,
    SC_CAS_INSERT, SC_CAS_REWIND, SC_CAS_SET_POS, SC_CAS_REMOVE, SC_CAS_READONLY,
    SC_CAS_AUTOREWIND, SC_CAS_SAVE,
    SC_PRN_FORMFEED, SC_MOUSE_LOCK, SC_RUN_PAUSE, SC_STOP,
    SC_SPEED_MAX_HOLD, SC_PLAY_REVERSE_HOLD, SC_SPEED_NORMAL, SC_SPEED_INC,
    SC_SPEED_DEC, SC_SPEED_MAX_TOGGLE,
    SC_SIZE_1X, SC_SIZE_2X, SC_SIZE_3X, SC_SIZE_4X, SC_SIZE_5X, SC_SIZE_6X,
    SC_SIZE_7X, SC_SIZE_8X, SC_SIZE_MINIMIZED, SC_SIZE_FULLSCREEN, SC_FULLSCREEN_TOGGLE,
    SC_RESET_SOFT, SC_RESET_HARD, SC_RESET_CLEAN,
    SC_VOLUME_INC, SC_VOLUME_DEC, SC_VOLUME_MUTE, SC_VOLUME_STEREO, SC_THEME_SWITCH,
    SC_PROP_EMULATION, SC_PROP_VIDEO, SC_PROP_AUDIO, SC_PROP_EFFECTS, SC_PROP_SETTINGS,
    SC_PROP_APPEARANCE, SC_PROP_PORTS, SC_LANGUAGE,
    SC_TOOL_MACHINE_EDITOR, SC_TOOL_SHORTCUT_EDITOR, SC_TOOL_KEYBOARD_EDITOR,
    SC_TOOL_MIXER, SC_TOOL_DEBUGGER, SC_TOOL_TRAINER, SC_HELP, SC_ABOUT,
    SC_COUNT
} ShortcutId;

/* Where profiles live (created by the platform). */
void shortcutsSetDirectory(const char* directory);

/* Loads `profile` (a name without ".shortcuts"); falls back to "blueMSX" or
** the first profile found. Writes the name actually loaded back to `profile`
** when it is not NULL and large enough (64 bytes). */
void platformShortcutsLoad(char* profile);
void platformShortcutsFree(void);

/* Profile names in the shortcuts directory; returns how many were written. */
int  platformShortcutsListProfiles(char names[][64], int max);
int  platformShortcutsSave(const char* profile);

/* The bindings of the active profile. The editor changes them in place. */
PlatformHotkeySet* platformShortcutGet(ShortcutId id);
const char*        platformShortcutIniName(ShortcutId id);

/* "Ctrl+F12, F11" for menus; "" when unbound. */
const char* platformHotkeySetToString(const PlatformHotkeySet* set);
const char* platformHotkeyToString(const PlatformHotkey* hotkey);
const char* platformShortcutText(ShortcutId id);

/* Runs the action bound to `id`, as the key would. */
void platformShortcutRun(ShortcutId id);

/* Keyboard events from the frontend (UI thread). Returns 1 when the key is a
** shortcut, which then does not reach the emulated keyboard. */
int  platformShortcutKey(int scancode, int pressed);
int  platformShortcutMods(void);
void platformShortcutsReleaseAll(void);

#ifdef __cplusplus
}
#endif

#endif
