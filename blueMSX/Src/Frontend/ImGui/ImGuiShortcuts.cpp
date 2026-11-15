/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: shortcut editor
**
** Edits the active profile in place (each action has up to three keys) and
** saves it in the Windows profile format.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ImGuiApp.h"

extern "C" {
#include "PlatformShortcuts.h"
#include "Language.h"
}

#include "FrontendMenus.h"
#include "imgui.h"
#include <cstring>
#include <string>

namespace app {

static bool open;
static bool dirty;
static int  waitId = -1;            /* shortcut waiting for a key */
static int  waitSlot;
static char filter[64];
static char newName[64];

void openShortcutEditor()
{
    open = true;
    waitId = -1;
}

bool shortcutEditorWantsKey(int scancode, int mods)
{
    if (!open || waitId < 0) return false;
    switch (scancode) {
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI:
        platformShortcutKey(scancode, 1);   /* track the modifier, wait for a key */
        return true;
    case SDL_SCANCODE_ESCAPE:
        if (mods == 0) { waitId = -1; return true; }
        break;
    default:
        break;
    }
    PlatformHotkeySet* set = platformShortcutGet((ShortcutId)waitId);
    if (set != NULL) {
        set->slots[waitSlot].type = HOTKEY_TYPE_KEYBOARD;
        set->slots[waitSlot].mods = mods;
        set->slots[waitSlot].key  = scancode;
        dirty = true;
    }
    waitId = -1;
    return true;
}

/* Other actions already using this key. */
static std::string conflicts(int self, const PlatformHotkey& h)
{
    std::string out;
    if (h.type != HOTKEY_TYPE_KEYBOARD) return out;
    for (int i = 0; i < SC_COUNT; i++) {
        if (i == self) continue;
        const PlatformHotkeySet* s = platformShortcutGet((ShortcutId)i);
        for (const auto& o : s->slots) {
            if (o.type == h.type && o.key == h.key && o.mods == h.mods) {
                if (!out.empty()) out += "\n";
                out += fe::shortcutDescription((ShortcutId)i);
            }
        }
    }
    return out;
}

static bool contains(const char* text, const char* needle)
{
    if (!*needle) return true;
    std::string a = text, b = needle;
    for (auto& c : a) c = (char)tolower((unsigned char)c);
    for (auto& c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

void drawShortcutEditor()
{
    if (!open) return;
    Properties* p = platformGetProperties();

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40, ImGui::GetFontSize() * 32), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(langShortcutConfigTitle(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    /* Profile */
    char profiles[32][64];
    int n = platformShortcutsListProfiles(profiles, 32);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::BeginCombo(langShortcutScheme(), p->emulation.shortcutProfile)) {
        for (int i = 0; i < n; i++) {
            if (ImGui::Selectable(profiles[i], strcmp(profiles[i], p->emulation.shortcutProfile) == 0)) {
                snprintf(p->emulation.shortcutProfile, sizeof(p->emulation.shortcutProfile), "%s", profiles[i]);
                platformShortcutsLoad(p->emulation.shortcutProfile);
                dirty = false;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    /* The langShortcut*Config strings are Win32 dialog titles and questions,
    ** too long for buttons; they become tooltips. */
    if (ImGui::Button(langDlgSave())) {
        platformShortcutsSave(p->emulation.shortcutProfile);
        dirty = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(langDlgCancel())) {
        platformShortcutsLoad(p->emulation.shortcutProfile);
        dirty = false;
    }
    ImGui::SetItemTooltip("%s", langShortcutDiscardConfig());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(langDlgSaveAs())) ImGui::OpenPopup("##saveas");
    ImGui::SetItemTooltip("%s", langShortcutSaveConfigAs());
    if (ImGui::BeginPopup("##saveas")) {
        ImGui::TextUnformatted(langShortcutConfigName());
        bool go = ImGui::InputText("##name", newName, sizeof(newName), ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button(langDlgOK()) || go) && newName[0] && !strchr(newName, '/')) {
            platformShortcutsSave(newName);
            snprintf(p->emulation.shortcutProfile, sizeof(p->emulation.shortcutProfile), "%s", newName);
            dirty = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
    ImGui::TextDisabled("Click a key to change it, right click to clear it. Esc cancels.");

    if (ImGui::BeginTable("##shortcuts", 1 + SHORTCUT_MAX_BINDINGS,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(langShortcutDescription(), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        for (int b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
            ImGui::TableSetupColumn(langShortcutKey(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        ImGui::TableHeadersRow();
        for (int i = 0; i < SC_COUNT; i++) {
            const char* desc = fe::shortcutDescription((ShortcutId)i);
            if (!contains(desc, filter)) continue;
            PlatformHotkeySet* set = platformShortcutGet((ShortcutId)i);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(desc);
            for (int b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
                ImGui::TableNextColumn();
                ImGui::PushID(b);
                const PlatformHotkey& h = set->slots[b];
                bool waiting = waitId == i && waitSlot == b;
                std::string label = waiting ? std::string(langShortcutPressText()) :
                                    h.type == HOTKEY_TYPE_KEYBOARD ? platformHotkeyToString(&h) : "-";
                std::string clash = conflicts(i, h);
                if (!clash.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.3f, 1));
                if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                    waitId = i;
                    waitSlot = b;
                }
                if (!clash.empty()) {
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("%s\n%s", langShortcutTooltipAlsoBound(), clash.c_str());
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    set->slots[b] = PlatformHotkey{ HOTKEY_TYPE_NONE, 0, 0 };
                    dirty = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();

    if (!open) waitId = -1;
}

} // namespace app
