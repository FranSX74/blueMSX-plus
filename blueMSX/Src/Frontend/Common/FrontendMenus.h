/*****************************************************************************
** blueMSX+ - menus shared by the frontends
**
** The contents of the blueMSX menus (as in Win32Menu.c), described once
** against a small builder interface. Each frontend implements the builder
** with its own toolkit: Dear ImGui draws the items every frame, Qt fills a
** QMenu when it is about to show.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef FRONTEND_MENUS_H
#define FRONTEND_MENUS_H

extern "C" {
#include "Frontend.h"
}

#include <functional>
#include <string>
#include <vector>

namespace fe {

enum { NO_CHECK = -1 };

class MenuBuilder {
public:
    virtual ~MenuBuilder() {}

    /* `shortcut` is a ShortcutId or -1. `check` is NO_CHECK for a plain item,
    ** 0 or 1 for a toggle. `fn` runs once the menu is closed. */
    virtual void item(const std::string& label, int shortcut, bool enabled, int check,
                      std::function<void()> fn) = 0;
    virtual void separator() = 0;
    /* A disabled caption inside a menu (history groups). */
    virtual void caption(const std::string& label) = 0;
    /* Returns true when the submenu's contents must be built now; then
    ** endMenu() closes it. */
    virtual bool beginMenu(const std::string& label, bool enabled = true) = 0;
    virtual void endMenu() = 0;
};

struct MainMenu {
    const char*  title;
    FrontendMenu menu;
};

/* The menu bar: File, Emulation, Window, Options, Tools, Help. Titles come
** from the current language. */
std::vector<MainMenu> mainMenus();

/* Builds any menu the core may ask for, the menu bar's included. */
void buildMenu(FrontendMenu menu, MenuBuilder& b);

/* "Machine name", with a pause or stop mark, for a status line. */
std::string machineStatus();

/* The description of a ShortcutId, in the current language. */
const char* shortcutDescription(int shortcut);

/* File name without its directory. */
const char* baseName(const char* path);

}

#endif
