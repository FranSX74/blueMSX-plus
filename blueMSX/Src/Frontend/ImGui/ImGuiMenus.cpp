/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: menus
**
** Draws the shared menus (Frontend/Common/FrontendMenus) with ImGui, every
** frame. Items defer their action, since an action may open a modal chooser.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ImGuiApp.h"
#include "FrontendMenus.h"

extern "C" {
#include "PlatformShortcuts.h"
}

#include "imgui.h"
#include <string>

namespace app {

class ImGuiMenuBuilder : public fe::MenuBuilder {
public:
    void item(const std::string& label, int sc, bool enabled, int check,
              std::function<void()> fn) override
    {
        const char* keys = sc >= 0 ? platformShortcutText((ShortcutId)sc) : "";
        ImGui::PushID(next++);
        if (ImGui::MenuItem(label.c_str(), *keys ? keys : NULL, check == 1, enabled)) {
            defer(std::move(fn));
        }
        ImGui::PopID();
    }
    void separator() override { ImGui::Separator(); }
    bool hasDebugger() const override { return true; }
    void caption(const std::string& label) override { ImGui::TextDisabled("%s", label.c_str()); }
    bool beginMenu(const std::string& label, bool enabled) override
    {
        ImGui::PushID(next++);
        bool open = ImGui::BeginMenu(label.c_str(), enabled);
        if (!open) ImGui::PopID();
        return open;
    }
    void endMenu() override
    {
        ImGui::EndMenu();
        ImGui::PopID();
    }

private:
    int next = 0;
};

void drawMainMenuBar()
{
    if (menuBarHeight() <= 0) return;
    if (!ImGui::BeginMainMenuBar()) return;
    ImGuiMenuBuilder b;
    for (const fe::MainMenu& m : fe::mainMenus()) {
        if (ImGui::BeginMenu(m.title)) {
            fe::buildMenu(m.menu, b);
            ImGui::EndMenu();
        }
    }

    /* Machine and state on the right, as a status line. */
    std::string status = fe::machineStatus();
    float w = ImGui::CalcTextSize(status.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::EndMainMenuBar();
}

/* ---- Popup menus asked for by actions (shortcuts) -------------------------- */

static int pendingPopup = -1;
static int activePopup = -1;

void requestPopupMenu(FrontendMenu menu)
{
    pendingPopup = menu;
}

void drawPopupMenus()
{
    if (pendingPopup >= 0) {
        activePopup = pendingPopup;
        pendingPopup = -1;
        ImGui::OpenPopup("##actionmenu");
    }
    if (!ImGui::BeginPopup("##actionmenu")) return;
    ImGuiMenuBuilder b;
    fe::buildMenu((FrontendMenu)activePopup, b);
    ImGui::EndPopup();
}

} // namespace app
