/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: dialogs and notices
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
#include "Actions.h"
#include "AudioMixer.h"
#include "Casette.h"
#include "Emulator.h"
#include "Language.h"
#include "version.h"
}

#include "imgui.h"
#include <cstring>
#include <deque>
#include <string>

namespace app {

static bool aboutOpen, languageOpen, tapeOpen, mixerOpen, renderOpen;
static bool tapeSuspended;

/* ---- Notices ---------------------------------------------------------------- */

struct Notice {
    FrontendMessageKind kind;
    std::string text;
    Uint64 shown;
};

static std::deque<Notice> notices;
static std::deque<std::string> errors;

void showMessage(FrontendMessageKind kind, const std::string& text)
{
    if (kind == FRONTEND_MESSAGE_ERROR) {
        errors.push_back(text);
        return;
    }
    notices.push_back(Notice{ kind, text, SDL_GetTicks() });
    while (notices.size() > 4) notices.pop_front();
}

void drawNotifications()
{
    ImGuiIO& io = ImGui::GetIO();
    Uint64 now = SDL_GetTicks();
    float y = io.DisplaySize.y - 10;

    while (!notices.empty() && now - notices.front().shown > 4000) notices.pop_front();
    for (int i = (int)notices.size() - 1; i >= 0; i--) {
        const Notice& n = notices[i];
        char id[32];
        snprintf(id, sizeof(id), "##notice%d", i);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, y), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin(id, NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                               ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        if (n.kind == FRONTEND_MESSAGE_WARNING) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", n.text.c_str());
        else ImGui::TextUnformatted(n.text.c_str());
        y -= ImGui::GetWindowHeight() + 6;
        ImGui::End();
    }

    if (!errors.empty()) {
        if (!ImGui::IsPopupOpen("blueMSX+###error")) ImGui::OpenPopup("blueMSX+###error");
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("blueMSX+###error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
            ImGui::TextUnformatted(errors.front().c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button(langDlgOK(), ImVec2(ImGui::GetFontSize() * 6, 0)) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                errors.pop_front();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

/* ---- One file out of a zip -------------------------------------------------- */

int chooseFromList(FrontendListRequest* req)
{
    int selected = req->selected >= 0 ? req->selected : 0;
    bool autostart = req->autostart > 0;
    bool first = true;
    std::string id = std::string(req->title ? req->title : "") + "###ziplist";

    int rc = runModal([&]() -> int {
        ImGuiIO& io = ImGui::GetIO();
        if (first) { ImGui::OpenPopup(id.c_str()); first = false; }
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28, ImGui::GetFontSize() * 24), ImGuiCond_Appearing);
        bool open = true;
        if (!ImGui::BeginPopupModal(id.c_str(), &open)) return -1;
        int r = -2;
        if (req->description) ImGui::TextWrapped("%s", req->description);
        if (req->zipName) ImGui::TextDisabled("%s", clean(req->zipName));
        float footer = ImGui::GetFrameHeightWithSpacing() * (req->autostart >= 0 ? 2 : 1);
        ImGui::BeginChild("##items", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        for (int i = 0; i < req->count; i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(req->items[i], selected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected = i;
                if (ImGui::IsMouseDoubleClicked(0)) r = i;
            }
            if (selected == i && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && selected < req->count - 1) selected++;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && selected > 0) selected--;
        if (req->autostart >= 0) ImGui::Checkbox(langDlgZipReset(), &autostart);
        if (ImGui::Button(langDlgOK(), ImVec2(ImGui::GetFontSize() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) r = selected;
        ImGui::SameLine();
        if (ImGui::Button(langDlgCancel(), ImVec2(ImGui::GetFontSize() * 6, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape) || !open) r = -1;
        if (r != -2) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return r;
    });
    if (rc >= 0 && req->autostart >= 0) req->autostart = autostart ? 1 : 0;
    return rc;
}

/* ---- About ------------------------------------------------------------------ */

static void drawAbout()
{
    if (!aboutOpen) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(langDlgAboutTitle(), &aboutOpen, ImGuiWindowFlags_AlwaysAutoResize |
                                                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("blueMSX+ %s", BLUE_MSX_VERSION);
        ImGui::TextDisabled("Dear ImGui frontend for Linux, ImGui %s, SDL %d.%d.%d", IMGUI_VERSION,
                            SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                            SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
        ImGui::TextUnformatted(langDlgAboutForkNote());
        ImGui::Spacing();
        ImGui::TextUnformatted(langDlgAboutOrigDevel());
        ImGui::TextUnformatted("Daniel Vik, Benoit Delvaux, Ricardo Bittencourt, Tomas Karlsson and others.");
        ImGui::Spacing();
        ImGui::TextUnformatted(langDlgAboutLisence());
        ImGui::TextUnformatted("GNU General Public License, version 2 or later.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("https://github.com/Hesoten/blueMSX-plus")) {
            SDL_OpenURL("https://github.com/Hesoten/blueMSX-plus");
        }
    }
    ImGui::End();
}

/* ---- Language --------------------------------------------------------------- */

static void drawLanguage()
{
    if (!languageOpen) return;
    Properties* p = platformGetProperties();
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 16, ImGui::GetFontSize() * 22), ImGuiCond_Appearing);
    if (ImGui::Begin(langDlgLangTitle(), &languageOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(langDlgLangLangText());
        ImGui::BeginChild("##langs", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; ; i++) {
            EmuLanguageType t = langGetType(i);
            if (t == EMU_LANG_UNKNOWN) break;
            const char* name = langToName(t, 1);
            if (ImGui::Selectable(name, p->language == t)) {
                Properties old = *p;
                p->language = t;
                langSetLanguage(t);
                platformApplyProperties(&old);
                platformSaveProperties();
                if (t == EMU_LANG_JAPANESE || t == EMU_LANG_KOREAN ||
                    t == EMU_LANG_CHINESESIMP || t == EMU_LANG_CHINESETRAD) {
                    showMessage(FRONTEND_MESSAGE_INFO, "Restart blueMSX+ to load the CJK font.");
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

/* ---- Cassette position ------------------------------------------------------ */

static void closeTape()
{
    tapeOpen = false;
    if (tapeSuspended) {
        emulatorResume();
        tapeSuspended = false;
    }
}

static void drawTape()
{
    if (!tapeOpen) return;
    static const char* const types[] = { "ASCII", "Binary", "Basic", "Custom" };
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26, ImGui::GetFontSize() * 22), ImGuiCond_Appearing);
    if (ImGui::Begin(langDlgTapeTitle(), &open, ImGuiWindowFlags_NoCollapse)) {
        int count = 0;
        TapeContent* tc = tapeGetContent(&count);
        UInt32 cur = tapeGetCurrentPos();
        ImGui::Text("%s %u / %u", langDlgTapeCurrentPos(), (unsigned)cur, (unsigned)tapeGetLength());
        ImGui::Separator();
        if (ImGui::BeginTable("##tape", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
            ImGui::TableSetupColumn(langDlgTabPosition());
            ImGui::TableSetupColumn(langDlgTabType());
            ImGui::TableSetupColumn(langDlgTabFilename());
            ImGui::TableHeadersRow();
            for (int i = 0; tc && i < count; i++) {
                char name[9];
                memcpy(name, tc[i].fileName, 8);
                name[8] = 0;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char label[32];
                snprintf(label, sizeof(label), "%d##t%d", tc[i].pos, i);
                if (ImGui::Selectable(label, (UInt32)tc[i].pos == cur, ImGuiSelectableFlags_SpanAllColumns)) {
                    tapeSetCurrentPos(tc[i].pos);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(types[tc[i].type & 3]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button(langMenuCasRewind())) tapeSetCurrentPos(0);
        ImGui::SameLine();
        if (ImGui::Button(langDlgOK())) open = false;
    }
    ImGui::End();
    if (!open) closeTape();
}

/* ---- Mixer ------------------------------------------------------------------ */

static void drawMixer()
{
    if (!mixerOpen) return;
    static const char* const names[MIXER_CHANNEL_TYPE_COUNT] = {
        "PSG", "SCC", "MSX-MUSIC", "MSX-AUDIO", "MoonSound", "Yamaha SFG", "Keyboard", "PCM", "I/O",
        "MIDI", "Cassette",
    };
    Properties* p = platformGetProperties();
    Mixer* mixer = platformGetMixer();
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0), ImGuiCond_Appearing);
    if (ImGui::Begin(langMenuToolsMixer(), &mixerOpen, ImGuiWindowFlags_NoCollapse)) {
        bool master = p->sound.masterEnable != 0;
        if (ImGui::Checkbox("##master", &master)) {
            p->sound.masterEnable = master;
            mixerEnableMaster(mixer, master);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 6);
        if (ImGui::SliderInt("Master", &p->sound.masterVolume, 0, 100)) {
            mixerSetMasterVolume(mixer, p->sound.masterVolume);
        }
        bool stereo = p->sound.stereo != 0;
        if (ImGui::Checkbox("Stereo", &stereo)) defer([]() { actionVolumeToggleStereo(); });
        ImGui::Separator();
        if (ImGui::BeginTable("##mix", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("volume", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("pan", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            for (int i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
                if (names[i] == NULL) continue;
                MixerChannel& ch = p->sound.mixerChannel[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool on = ch.enable != 0;
                if (ImGui::Checkbox("##on", &on)) {
                    ch.enable = on;
                    mixerEnableChannelType(mixer, i, on);
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##vol", &ch.volume, 0, 100, (std::string(names[i]) + " %d").c_str())) {
                    mixerSetChannelTypeVolume(mixer, i, ch.volume);
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##pan", &ch.pan, 0, 100, ch.pan == 50 ? "C" : ch.pan < 50 ? "L %d" : "R %d")) {
                    mixerSetChannelTypePan(mixer, i, ch.pan);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ch.pan = 50;
                    mixerSetChannelTypePan(mixer, i, 50);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

/* ---- Dispatch ---------------------------------------------------------------- */

void openDialog(FrontendDialog dialog, int arg)
{
    switch (dialog) {
    case FRONTEND_DIALOG_PROPERTIES:   openProperties(arg); break;
    case FRONTEND_DIALOG_LANGUAGE:     languageOpen = true; break;
    case FRONTEND_DIALOG_ABOUT:        aboutOpen = true; break;
    case FRONTEND_DIALOG_HELP:
        SDL_OpenURL("https://github.com/Hesoten/blueMSX-plus#readme");
        break;
    case FRONTEND_DIALOG_CASSETTE_POS:
        if (!tapeOpen && emulatorGetState() == EMU_RUNNING) {
            emulatorSuspend();
            tapeSuspended = true;
        }
        tapeOpen = true;
        break;
    case FRONTEND_DIALOG_SHORTCUTS:    openShortcutEditor(); break;
    case FRONTEND_DIALOG_MIXER:        mixerOpen = true; break;
    case FRONTEND_DIALOG_RENDER_VIDEO: renderOpen = true; break;
    case FRONTEND_DIALOG_DEBUGGER:     openDebugger(); break;
    default:
        showMessage(FRONTEND_MESSAGE_INFO, "Not available in this version yet.");
        break;
    }
}

/* Create video clip: progress of a replay rendering to MP4. */
static void drawRenderProgress()
{
    if (!renderOpen) return;
    if (platformReplayRenderDone()) {
        platformReplayRenderStop(0);
        renderOpen = false;
        return;
    }
    int amount = platformReplayRenderProgress();
    if (amount < 0) {
        renderOpen = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24, 0));
    if (ImGui::Begin(langMenuVideoRender(), NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s %d.%d%%", langDlgAmountCompleted(), amount / 10, amount % 10);
        ImGui::ProgressBar(amount / 1000.0f, ImVec2(-1, 0), "");
        if (ImGui::Button(langDlgCancel())) {
            platformReplayRenderStop(1);
            renderOpen = false;
        }
    }
    ImGui::End();
}

void drawDialogs()
{
    drawDebugger();
    drawRenderProgress();
    drawProperties();
    drawShortcutEditor();
    drawAbout();
    drawLanguage();
    drawTape();
    drawMixer();
}

} // namespace app
