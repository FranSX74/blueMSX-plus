/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: shared declarations
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef IMGUI_APP_H
#define IMGUI_APP_H

extern "C" {
#include "Platform.h"
#include "Properties.h"
}

#include <SDL3/SDL.h>
#include <functional>
#include <string>

struct ImFont;

namespace app {

/* Fixed-width font (debugger listings); NULL until the fonts are set up. */
extern ImFont* monoFont;

extern SDL_Window*   window;
extern SDL_Renderer* renderer;

/* Work that must run outside an ImGui frame (anything that may open a modal
** chooser, which runs frames of its own). Runs after the current frame. */
void defer(std::function<void()> fn);

/* A modal loop: keeps the emulated screen updating and calls `draw` inside
** each frame until it returns something other than -2. */
int runModal(const std::function<int()>& draw);

/* Main menu bar height in pixels (0 when hidden). */
float menuBarHeight();

/* ---- ImGuiMenus.cpp ---- */
void drawMainMenuBar();
void requestPopupMenu(FrontendMenu menu);
void drawPopupMenus();

/* ---- ImGuiDialogs.cpp ---- */
void openDialog(FrontendDialog dialog, int arg);
void drawDialogs();
void showMessage(FrontendMessageKind kind, const std::string& text);
void drawNotifications();
int  chooseFromList(FrontendListRequest* req);

/* ---- ImGuiFileDialog.cpp ---- */
int  chooseFile(const FrontendFileRequest* req, char* out, int outSize);

/* ---- ImGuiProperties.cpp ---- */
void openProperties(int page);
void drawProperties();

/* ---- ImGuiShortcuts.cpp ---- */
void openShortcutEditor();
void drawShortcutEditor();
/* While the editor waits for a key, it takes the next key press. */
bool shortcutEditorWantsKey(int scancode, int mods);

/* ---- ImGuiDebugger.cpp ---- */
void openDebugger();
void drawDebugger();
void closeDebugger();
/* The debugger window has the focus: keys are its own. */
bool debuggerWantsKeys();

/* Helpers */
const char* clean(const char* path);    /* file name of a path, "" for NULL */

} // namespace app

#endif
