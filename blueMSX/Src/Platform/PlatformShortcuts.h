/*****************************************************************************
** blueMSX+ - Platform layer: keyboard shortcuts
**
** Port of the classic blueMSX SDL shortcut handling. Hotkeys are read from
** "<Shortcut Profiles>/blueMSX.shortcuts" ([Shortcuts] name=Key|modifier...),
** key names being SDL scancode names.
**
** Copyright (C) 2003-2006 Daniel Vik
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef PLATFORM_SHORTCUTS_H
#define PLATFORM_SHORTCUTS_H

#define HOTKEY_TYPE_NONE     0
#define HOTKEY_TYPE_KEYBOARD 1
#define HOTKEY_TYPE_JOYSTICK 2

typedef struct Shortcuts Shortcuts;

void shortcutsSetDirectory(const char* directory);
Shortcuts* shortcutsCreate(void);
void shortcutsDestroy(Shortcuts* shortcuts);
void shortcutCheckDown(Shortcuts* s, int type, int mods, int keySym);
void shortcutCheckUp(Shortcuts* s, int type, int mods, int keySym);

/* Active profile, used by platformKeyEvent. */
void platformShortcutsLoad(void);
void platformShortcutsFree(void);
void platformShortcutDown(int mods, int scancode);
void platformShortcutUp(int mods, int scancode);

#endif
