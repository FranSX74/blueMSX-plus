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

static const char* describe(ShortcutId id)
{
    switch (id) {
    case SC_SPRITES_ENABLE:        return langShortcutToggleSpriteEnable();
    case SC_FDC_TIMING:            return langShortcutToggleFdcTiming();
    case SC_HDD_SD_BOOST:          return langShortcutToggleHddSdBoost();
    case SC_NO_SPRITE_LIMITS:      return langShortcutToggleNoSpriteLimits();
    case SC_MSX_KEYBOARD_QUIRK:    return langShortcutEnableMsxKeyboardQuirk();
    case SC_MSX_AUDIO_SWITCH:      return langShortcutSwitchMsxAudio();
    case SC_FRONT_SWITCH:          return langShortcutSwitchFront();
    case SC_PAUSE_SWITCH:          return langShortcutSwitchPause();
    case SC_QUIT:                  return langShortcutQuit();
    case SC_WAV_CAPTURE:           return langShortcutAudioCapture();
    case SC_WAV_CAPTURE_START_AS:  return langShortcutAudioCaptureAs();
    case SC_VIDEO_CAP_LOAD:        return langShortcutVideoLoad();
    case SC_VIDEO_CAP_PLAY:        return langShortcutVideoPlay();
    case SC_VIDEO_CAP_REC:         return langShortcutVideoRecord();
    case SC_VIDEO_CAP_REC_AS:      return langShortcutVideoRecordAs();
    case SC_VIDEO_CAP_STOP:        return langShortcutVideoStop();
    case SC_VIDEO_CAP_SAVE:        return langShortcutVideoRender();
    case SC_RECORD_VIDEO_START:    return langShortcutRecordVideoStart();
    case SC_RECORD_VIDEO_START_AS: return langShortcutRecordVideoStartAs();
    case SC_RECORD_VIDEO_STOP:     return langShortcutRecordVideoStop();
    case SC_RECORD_VIDEO_TOGGLE:   return langShortcutRecordVideoToggle();
    case SC_YM2413_BACKEND_CYCLE:  return langShortcutYm2413BackendCycle();
    case SC_Y8950_BACKEND_CYCLE:   return langShortcutY8950BackendCycle();
    case SC_SCREEN_CAPTURE:        return langShortcutScreenshotOrig();
    case SC_SCREEN_CAPTURE_AS:     return langShortcutScreenshotAs();
    case SC_SCREEN_CAPTURE_SMALL:  return langShortcutScreenshotSmall();
    case SC_SCREEN_CAPTURE_LARGE:  return langShortcutScreenshotLarge();
    case SC_STATE_LOAD:            return langShortcutCpuStateLoad();
    case SC_STATE_SAVE:            return langShortcutCpuStateSave();
    case SC_STATE_QUICK_LOAD:      return langShortcutCpuStateQload();
    case SC_STATE_QUICK_SAVE:      return langShortcutCpuStateQsave();
    case SC_CART1_INSERT:          return langShortcutCartInsert1();
    case SC_CART2_INSERT:          return langShortcutCartInsert2();
    case SC_CART1_SPECIAL:         return langShortcutCartSpecialMenu1();
    case SC_CART2_SPECIAL:         return langShortcutCartSpecialMenu2();
    case SC_CART1_REMOVE:          return langShortcutCartRemove1();
    case SC_CART2_REMOVE:          return langShortcutCartRemove2();
    case SC_CART_AUTORESET:        return langShortcutCartAutoReset();
    case SC_DISKA_INSERT:          return langShortcutDiskInsertA();
    case SC_DISKB_INSERT:          return langShortcutDiskInsertB();
    case SC_DISKA_DIR_INSERT:      return langShortcutDiskDirInsertA();
    case SC_DISKB_DIR_INSERT:      return langShortcutDiskDirInsertB();
    case SC_DISK_QUICK_CHANGE:     return langShortcutDiskChangeA();
    case SC_DISKA_REMOVE:          return langShortcutDiskRemoveA();
    case SC_DISKB_REMOVE:          return langShortcutDiskRemoveB();
    case SC_DISK_AUTORESET:        return langShortcutDiskAutoResetA();
    case SC_CAS_INSERT:            return langShortcutCasInsert();
    case SC_CAS_REWIND:            return langShortcutCasRewind();
    case SC_CAS_SET_POS:           return langShortcutCasSetPosition();
    case SC_CAS_REMOVE:            return langShortcutCasEject();
    case SC_CAS_READONLY:          return langShortcutCasReadOnly();
    case SC_CAS_AUTOREWIND:        return langShortcutCasAutorewind();
    case SC_CAS_SAVE:              return langShortcutCasSave();
    case SC_PRN_FORMFEED:          return langShortcutPrnFormFeed();
    case SC_MOUSE_LOCK:            return langShortcutToggleMouseLock();
    case SC_RUN_PAUSE:             return langShortcutRunPause();
    case SC_STOP:                  return langShortcutStop();
    case SC_SPEED_MAX_HOLD:        return langShortcutEmuSpeedMax();
    case SC_PLAY_REVERSE_HOLD:     return langShortcutEmuPlayReverse();
    case SC_SPEED_NORMAL:          return langShortcutEmuSpeedNormal();
    case SC_SPEED_INC:             return langShortcutEmuSpeedInc();
    case SC_SPEED_DEC:             return langShortcutEmuSpeedDec();
    case SC_SPEED_MAX_TOGGLE:      return langShortcutEmuSpeedMaxToggle();
    case SC_SIZE_1X:               return langShortcutSize1x();
    case SC_SIZE_2X:               return langShortcutSize2x();
    case SC_SIZE_3X:               return langShortcutSize3x();
    case SC_SIZE_4X:               return langShortcutSize4x();
    case SC_SIZE_5X:               return langShortcutSize5x();
    case SC_SIZE_6X:               return langShortcutSize6x();
    case SC_SIZE_7X:               return langShortcutSize7x();
    case SC_SIZE_8X:               return langShortcutSize8x();
    case SC_SIZE_MINIMIZED:        return langShortcutSizeMinimized();
    case SC_SIZE_FULLSCREEN:       return langShortcutSizeFullscreen();
    case SC_FULLSCREEN_TOGGLE:     return langShortcutToggleFullscren();
    case SC_RESET_SOFT:            return langShortcutResetSoft();
    case SC_RESET_HARD:            return langShortcutResetHard();
    case SC_RESET_CLEAN:           return langShortcutResetClean();
    case SC_VOLUME_INC:            return langShortcutVolumeIncrease();
    case SC_VOLUME_DEC:            return langShortcutVolumeDecrease();
    case SC_VOLUME_MUTE:           return langShortcutVolumeMute();
    case SC_VOLUME_STEREO:         return langShortcutVolumeStereo();
    case SC_THEME_SWITCH:          return langShortcutThemeSwitch();
    case SC_PROP_EMULATION:        return langShortcutShowEmuProp();
    case SC_PROP_VIDEO:            return langShortcutShowVideoProp();
    case SC_PROP_AUDIO:            return langShortcutShowAudioProp();
    case SC_PROP_EFFECTS:          return langShortcutShowEffectsProp();
    case SC_PROP_SETTINGS:         return langShortcutShowSettProp();
    case SC_PROP_APPEARANCE:       return langShortcutShowCtrlProp();
    case SC_PROP_PORTS:            return langShortcutShowPorts();
    case SC_LANGUAGE:              return langShortcutShowLanguage();
    case SC_TOOL_MACHINE_EDITOR:   return langShortcutShowMachines();
    case SC_TOOL_SHORTCUT_EDITOR:  return langShortcutShowShortcuts();
    case SC_TOOL_KEYBOARD_EDITOR:  return langShortcutShowKeyboard();
    case SC_TOOL_MIXER:            return langShortcutShowMixer();
    case SC_TOOL_DEBUGGER:         return langShortcutShowDebugger();
    case SC_TOOL_TRAINER:          return langShortcutShowTrainer();
    case SC_HELP:                  return langShortcutShowHelp();
    case SC_ABOUT:                 return langShortcutShowAbout();
    default:                       return platformShortcutIniName(id);
    }
}

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
                out += describe((ShortcutId)i);
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
            const char* desc = describe((ShortcutId)i);
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
