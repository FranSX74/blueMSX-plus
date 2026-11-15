/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: properties
**
** Changes apply as they are made, as in openMSX; settings are saved when the
** window closes. Labels come from the blueMSX language tables.
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
#include "ArrayList.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Language.h"
#include "Machine.h"
#include "PrinterIO.h"
}

#include "imgui.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app {

static bool open;
static int  requestedTab = -1;
static std::vector<std::string> machines;
static std::unique_ptr<Properties> before;

void openProperties(int page)
{
    open = true;
    switch (page) {
    case PROP_EMULATION:                  requestedTab = 0; break;
    /* blueMSX opens its Video page as PROP_PERFORMANCE and the effects
    ** as PROP_VIDEO; both live on the Video tab here. */
    case PROP_VIDEO: case PROP_D3D:
    case PROP_PERFORMANCE:                requestedTab = 1; break;
    case PROP_SOUND: case PROP_MIDI:      requestedTab = 2; break;
    case PROP_PORTS:                      requestedTab = 3; break;
    default:                              requestedTab = 4; break;
    }
    machines.clear();
    ArrayList* list = arrayListCreate();
    machineFillAvailable(list, 1);
    for (int i = 0; i < arrayListGetSize(list); i++) {
        machines.push_back((const char*)arrayListGetObject(list, i));
    }
    arrayListDestroy(list);
}

/* ---- Small widgets -------------------------------------------------------- */

static bool check(const char* label, int* value)
{
    bool v = *value != 0;
    if (!ImGui::Checkbox(label, &v)) return false;
    *value = v;
    return true;
}

static bool combo(const char* label, int* value, const char* const* items, int count)
{
    int v = *value;
    if (v < 0 || v >= count) v = 0;
    if (!ImGui::Combo(label, &v, items, count)) return false;
    *value = v;
    return true;
}

static void heading(const char* text)
{
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

static bool tab(const char* label, int index)
{
    ImGuiTabItemFlags flags = requestedTab == index ? ImGuiTabItemFlags_SetSelected : 0;
    return ImGui::BeginTabItem(label, NULL, flags);
}

static void hint(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/* ---- Pages ---------------------------------------------------------------- */

static void pageEmulation(Properties* p)
{
    heading(langPropEmuGeneralGB());
    if (ImGui::BeginCombo(langPropEmuFamilyText(), p->emulation.machineName)) {
        for (const auto& m : machines) {
            if (ImGui::Selectable(m.c_str(), m == p->emulation.machineName)) {
                snprintf(p->emulation.machineName, sizeof(p->emulation.machineName), "%s", m.c_str());
            }
        }
        ImGui::EndCombo();
    }
    if (emulatorGetState() != EMU_STOPPED) {
        ImGui::SameLine();
        if (ImGui::SmallButton(langMenuRunHardReset())) defer([]() { actionEmuResetHard(); });
        hint("The machine changes on the next hard reset.");
    }

    heading(langPropEmuSpeedGB());
    {
        int hz = emulatorLogFrequencyToHz(p->emulation.speed);
        char text[64];
        snprintf(text, sizeof(text), "%d.%03d MHz (%d%%%%)", hz / 1000000, (hz / 1000) % 1000, hz * 10 / 357954);
        ImGui::SliderInt(langPropEmuSpeedText(), &p->emulation.speed, 0, 100, text);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) p->emulation.speed = 50;
    }
    ImGui::SliderInt(langPropEmuVdpCmdSpeedText(), &p->emulation.vdpCmdSpeed, 0, 100, "%d%%");
    const char* vdpFreq[] = { langPropVideoFreqAuto(), "50 Hz", "60 Hz" };
    combo(langPropVideoFreqText(), &p->emulation.vdpSyncMode, vdpFreq, 3);

    heading(langPropEmuBoostText());
    check(langPropEmuFdcTiming(), &p->emulation.enableFdcTiming);
    check(langPropEmuHddSdBoost(), &p->emulation.enableHddSdBoost);
    check(langPropEmuCasBoost(), &p->emulation.enableCasBoost);
    check(langPropEmuNoSpriteLimits(), &p->emulation.noSpriteLimits);
    check(langPropEmuReversePlay(), &p->emulation.reverseEnable);

    heading(langPropEmuFrontSwitchGB());
    check(langPropEmuFrontSwitch(), &p->emulation.frontSwitch);
    check(langPropEmuPauseSwitch(), &p->emulation.pauseSwitch);
    check(langPropEmuAudioSwitch(), &p->emulation.audioSwitch);
    check(langShortcutEnableMsxKeyboardQuirk(), &p->keyboard.enableKeyboardQuirk);
}

static void pageVideo(Properties* p)
{
    heading(langPropMonMonGB());
    const char* mon[] = { langEnumVideoMonColor(), langEnumVideoMonGrey(), langEnumVideoMonGreen(),
                          langEnumVideoMonAmber() };
    combo(langPropMonTypeText(), &p->video.monitorColor, mon, 4);
    const char* emu[] = { langEnumVideoEmuNone(), langEnumVideoEmuMonitor(), langEnumVideoEmuYc(),
                          langEnumVideoEmuYcBlur(), langEnumVideoEmuComp(), langEnumVideoEmuCompBlur(),
                          langEnumVideoEmuScale2x(), langEnumVideoEmuHq2x() };
    combo(langPropMonEmuText(), &p->video.monitorType, emu, 8);
    const char* skip[] = { langEnumVideoFrameskip0(), langEnumVideoFrameskip1(), langEnumVideoFrameskip2(),
                           langEnumVideoFrameskip3(), langEnumVideoFrameskip4(), langEnumVideoFrameskip5() };
    combo(langPropPerfFrameSkipText(), &p->video.frameSkip, skip, 6);

    heading(langPropMonEffectsGB());
    check(langPropMonHorizStretch(), &p->video.horizontalStretch);
    check(langPropMonVertStretch(), &p->video.verticalStretch);
    check(langPropMonDeInterlace(), &p->video.deInterlace);
    check(langPropMonBlendFrames(), &p->video.blendFrames);
    check(langPropMonScanlines(), &p->video.scanlinesEnable);
    if (p->video.scanlinesEnable) {
        ImGui::Indent();
        ImGui::SliderInt(langPropMonScanlinesDepth(), &p->video.scanlinesPct, 0, 100, "%d%%");
        ImGui::Unindent();
    }
    check(langPropMonColorGhosting(), &p->video.colorSaturationEnable);
    if (p->video.colorSaturationEnable) {
        ImGui::Indent();
        ImGui::SliderInt("##ghost", &p->video.colorSaturationWidth, 0, 4);
        ImGui::Unindent();
    }

    heading(langPropVideo());
    struct { const char* label; int* value; } sliders[] = {
        { langPropMonBrightness(), &p->video.brightness }, { langPropMonContrast(), &p->video.contrast },
        { langPropMonSaturation(), &p->video.saturation }, { langPropMonGamma(), &p->video.gamma },
    };
    for (auto& s : sliders) {
        ImGui::SliderInt(s.label, s.value, 0, 200);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) *s.value = 100;
    }
    ImGui::TextDisabled("Right click a slider to reset it.");
}

static void pageSound(Properties* p)
{
    static const int bufSizes[] = { 5, 10, 15, 25, 50, 75, 100, 150, 200 };
    static const char* const bufLabels[] = { "5 ms", "10 ms", "15 ms", "25 ms", "50 ms", "75 ms",
                                             "100 ms", "150 ms", "200 ms" };
    bool stopped = emulatorGetState() == EMU_STOPPED;

    heading(langPropPerfAudioDrvGB());
    int idx = 0;
    while (idx < 8 && p->sound.bufSize > bufSizes[idx]) idx++;
    if (ImGui::Combo(langPropPerfAudioBufSzText(), &idx, bufLabels, 9)) p->sound.bufSize = bufSizes[idx];
    check("Stereo", &p->sound.stereo);
    check("##master", &p->sound.masterEnable);
    ImGui::SameLine();
    ImGui::SliderInt("Master", &p->sound.masterVolume, 0, 100);
    if (ImGui::Button(langMenuToolsMixer())) defer([]() { actionToolsShowMixer(); });

    heading(langPropSndChipEmuGB());
    ImGui::BeginDisabled(!stopped);
    check(langPropSndMsxMusic(), &p->sound.chip.enableYM2413);
    check(langPropSndMsxAudio(), &p->sound.chip.enableY8950);
    check(langPropSndMoonsound(), &p->sound.chip.enableMoonsound);
    ImGui::EndDisabled();
    if (!stopped) ImGui::TextDisabled("%s", langPropSoundChipsHint());

    heading(langPropSoundChipsActive());
    /* Same labels as the Win32 dialog, indexed by PROP_YM2413_BACKEND_*. */
    static const char* const ym[] = { "openMSX", "original blueMSX", "emu2413", "Nuked OPLL" };
    static const char* const y8950[] = { "original blueMSX", "emu8950", "openMSX" };
    int ya = p->sound.chip.ym2413BackendActive, yb = p->sound.chip.y8950BackendActive;
    ImGui::Text("MSX-MUSIC: %s", ya >= 0 && ya < 4 ? ym[ya] : "?");
    ImGui::SameLine();
    if (ImGui::SmallButton(">##ym")) defer([]() { actionYm2413BackendCycle(); });
    ImGui::Text("MSX-AUDIO: %s", yb >= 0 && yb < 3 ? y8950[yb] : "?");
    ImGui::SameLine();
    if (ImGui::SmallButton(">##y8950")) defer([]() { actionY8950BackendCycle(); });

    const char* filters[] = { langEnumOpllFilterOff(), langEnumOpllFilterBright(), langEnumOpllFilterClear(),
                              langEnumOpllFilterStandard(), langEnumOpllFilterSoft(),
                              langEnumOpllFilterMellow(), langEnumOpllFilterCustom() };
    /* The original blueMSX OPLL has its own output filter, so the analog
    ** filter is bypassed with it (YM2413.cpp), as the Win32 dialog shows. */
    bool filterUsed = ya != PROP_YM2413_BACKEND_OPENMSX_2;
    ImGui::BeginDisabled(!filterUsed);
    combo(langPropSndOpllAnalogText(), &p->sound.chip.ym2413AnalogFilterMode, filters, PROP_OPLL_FILTER_COUNT);
    if (p->sound.chip.ym2413AnalogFilterMode == PROP_OPLL_FILTER_CUSTOM) {
        ImGui::SliderInt(langPropSndOpllAnalogLpfText(), &p->sound.chip.ym2413AnalogFilterLpfHz, 1000, 15000, "%d Hz");
    }
    ImGui::EndDisabled();
    if (!filterUsed) {
        ImGui::SetItemTooltip("Not used with the %s MSX-MUSIC backend.", ym[ya]);
    }
}

static void pagePorts(Properties* p)
{
    heading(langPropPortsLptGB());
    const char* lpt[] = { langPropPortsNone(), langPropPortsSimplCovox(), langPropPortsFile() };
    combo(langPropPortsLptText(), &p->ports.Lpt.type, lpt, 3);
    if (p->ports.Lpt.type == P_LPT_FILE) {
        ImGui::InputText("##lptfile", p->ports.Lpt.fileName, sizeof(p->ports.Lpt.fileName),
                         ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("...##lpt")) {
            defer([]() {
                FrontendFileRequest req = {};
                char out[512];
                req.kind = FRONTEND_FILE_SAVE_GENERIC;
                req.save = 1;
                req.allowCreate = 1;
                req.title = langPropPortsOpenLogFile();
                if (chooseFile(&req, out, sizeof(out))) {
                    Properties* q = platformGetProperties();
                    Properties old = *q;
                    snprintf(q->ports.Lpt.fileName, sizeof(q->ports.Lpt.fileName), "%s", out);
                    platformApplyProperties(&old);
                }
            });
        }
    }
    const char* emu[] = { "Raw", "MSX printer", "SVI printer", "Epson FX-80" };
    combo(langPropPortsEmulateMsxPrn(), &p->ports.Lpt.emulation, emu, 4);

    heading(langPropPortsComGB());
    const char* com[] = { langPropPortsNone(), langPropPortsComFile() };
    combo(langPropPortsCom1Text(), &p->ports.Com.type, com, 2);
    if (p->ports.Com.type == P_COM_FILE) {
        ImGui::InputText("##comfile", p->ports.Com.fileName, sizeof(p->ports.Com.fileName));
    }

    heading(langPropSndMidiOutGB());
    const char* midi[] = { langPropPortsNone(), langPropPortsFile() };
    combo("MIDI out", &p->sound.MidiOut.type, midi, 2);
    if (p->sound.MidiOut.type == P_MIDI_FILE) {
        ImGui::InputText("##midifile", p->sound.MidiOut.fileName, sizeof(p->sound.MidiOut.fileName));
    }
}

static void pageSettings(Properties* p)
{
    heading(langPropSetFileHistoryGB());
    ImGui::SliderInt(langPropSetFileHistorySize(), &p->filehistory.count, 0, MAX_HISTORY);
    if (ImGui::Button(langPropSetFileHistoryClear())) ImGui::OpenPopup("##clearhistory");
    if (ImGui::BeginPopup("##clearhistory")) {
        ImGui::TextUnformatted(langPropClearFileHistory());
        if (ImGui::Button(langDlgOK())) {
            for (int i = 0; i < MAX_HISTORY; i++) {
                p->filehistory.cartridge[0][i][0] = 0;
                p->filehistory.cartridge[1][i][0] = 0;
                p->filehistory.diskdrive[0][i][0] = 0;
                p->filehistory.diskdrive[1][i][0] = 0;
                p->filehistory.cassette[0][i][0] = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(langDlgCancel())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    check(langPropEjectMediaOnExit(), &p->emulation.ejectMediaOnExit);
    check(langPropScreenshotPng(), &p->settings.usePngScreenshots);

    heading(langShortcutConfigTitle());
    char profiles[32][64];
    int n = platformShortcutsListProfiles(profiles, 32);
    if (ImGui::BeginCombo(langShortcutScheme(), p->emulation.shortcutProfile)) {
        for (int i = 0; i < n; i++) {
            if (ImGui::Selectable(profiles[i], strcmp(profiles[i], p->emulation.shortcutProfile) == 0)) {
                snprintf(p->emulation.shortcutProfile, sizeof(p->emulation.shortcutProfile), "%s", profiles[i]);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(langMenuToolsShortcuts())) defer([]() { actionToolsShowShorcutEditor(); });

    heading(langMenuPropsLanguage());
    if (ImGui::Button(langToName((EmuLanguageType)p->language, 1))) defer([]() { actionOptionsShowLanguage(); });
}

/* ---- Window --------------------------------------------------------------- */

void drawProperties()
{
    if (!open) return;
    Properties* p = platformGetProperties();
    if (p == NULL) return;
    if (!before) before.reset(new Properties);
    *before = *p;

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34, ImGui::GetFontSize() * 30), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(langPropTitle(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::BeginTabBar("##props")) {
            if (tab(langPropEmulation(), 0)) { pageEmulation(p); ImGui::EndTabItem(); }
            if (tab(langPropVideo(), 1))     { pageVideo(p);     ImGui::EndTabItem(); }
            if (tab(langPropSound(), 2))     { pageSound(p);     ImGui::EndTabItem(); }
            if (tab(langPropPorts(), 3))     { pagePorts(p);     ImGui::EndTabItem(); }
            if (tab(langPropSettings(), 4))  { pageSettings(p);  ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        requestedTab = -1;
        ImGui::PopItemWidth();
    }
    ImGui::End();

    if (memcmp(before.get(), p, sizeof(Properties)) != 0) {
        Properties old = *before;
        platformApplyProperties(&old);
    }
    if (!open) platformSaveProperties();
}

} // namespace app
