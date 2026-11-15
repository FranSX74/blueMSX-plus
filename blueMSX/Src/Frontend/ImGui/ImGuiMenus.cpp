/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: menus
**
** The same menus as Win32Menu.c, built every frame from the live state.
** Items defer their action, since an action may open a modal chooser.
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
#include "ArchNotifications.h"
#include "AudioMixer.h"
#include "Board.h"
#include "Casette.h"
#include "Disk.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "IsFileExtension.h"
#include "JoystickPort.h"
#include "Language.h"
#include "LaunchFile.h"
#include "MediaDb.h"
#include "VideoManager.h"
#include "ziphelper.h"
}

#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <strings.h>

namespace app {

static Properties* props() { return platformGetProperties(); }

/* A menu item running `fn` after the frame. */
static void item(const char* label, ShortcutId sc, bool enabled, bool checked,
                 std::function<void()> fn)
{
    const char* keys = (int)sc >= 0 ? platformShortcutText(sc) : "";
    if (ImGui::MenuItem(label, *keys ? keys : NULL, checked, enabled)) {
        defer(std::move(fn));
    }
}

static void item(const char* label, ShortcutId sc, std::function<void()> fn)
{
    item(label, sc, true, false, std::move(fn));
}

static const ShortcutId NO_SC = (ShortcutId)-1;

static std::string ejectLabel(const char* fileName, const char* id)
{
    std::string s = langMenuEject();
    if (fileName && *fileName) {
        s += ": ";
        s += clean(fileName);
    }
    return s + "##" + id;
}

/* ---- Cartridges ------------------------------------------------------------ */

struct SpecialCart { const char* label; const char* name; RomType type; };

static void specialItems(int slot, const SpecialCart* carts, int count)
{
    for (int i = 0; i < count; i++) {
        const SpecialCart c = carts[i];
        item(c.label, NO_SC, [slot, c]() {
            insertCartridge(props(), slot, c.name, NULL, c.type, 0);
        });
    }
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void menuCartSpecial(int slot)
{
    static const SpecialCart extRam[] = {
        { "16 kB", CARTNAME_EXTRAM16KB, ROM_EXTRAM16KB }, { "32 kB", CARTNAME_EXTRAM32KB, ROM_EXTRAM32KB },
        { "48 kB", CARTNAME_EXTRAM48KB, ROM_EXTRAM48KB }, { "64 kB", CARTNAME_EXTRAM64KB, ROM_EXTRAM64KB },
        { "512 kB", CARTNAME_EXTRAM512KB, ROM_EXTRAM512KB }, { "1 MB", CARTNAME_EXTRAM1MB, ROM_EXTRAM1MB },
        { "2 MB", CARTNAME_EXTRAM2MB, ROM_EXTRAM2MB }, { "4 MB", CARTNAME_EXTRAM4MB, ROM_EXTRAM4MB },
    };
    static const SpecialCart megaRam[] = {
        { "128 kB", CARTNAME_MEGARAM128, ROM_MEGARAM128 }, { "256 kB", CARTNAME_MEGARAM256, ROM_MEGARAM256 },
        { "512 kB", CARTNAME_MEGARAM512, ROM_MEGARAM512 }, { "768 kB", CARTNAME_MEGARAM768, ROM_MEGARAM768 },
        { "2 MB", CARTNAME_MEGARAM2M, ROM_MEGARAM2M },
    };
    static const SpecialCart megaScsi[] = {
        { "128 kB", CARTNAME_MEGASCSI128, SRAM_MEGASCSI128 }, { "256 kB", CARTNAME_MEGASCSI256, SRAM_MEGASCSI256 },
        { "512 kB", CARTNAME_MEGASCSI512, SRAM_MEGASCSI512 }, { "1 MB", CARTNAME_MEGASCSI1MB, SRAM_MEGASCSI1MB },
    };
    static const SpecialCart waveScsi[] = {
        { "128 kB", CARTNAME_WAVESCSI128, SRAM_WAVESCSI128 }, { "256 kB", CARTNAME_WAVESCSI256, SRAM_WAVESCSI256 },
        { "512 kB", CARTNAME_WAVESCSI512, SRAM_WAVESCSI512 }, { "1 MB", CARTNAME_WAVESCSI1MB, SRAM_WAVESCSI1MB },
    };
    static const SpecialCart eseRam[] = {
        { "128 kB", CARTNAME_ESERAM128, SRAM_ESERAM128 }, { "256 kB", CARTNAME_ESERAM256, SRAM_ESERAM256 },
        { "512 kB", CARTNAME_ESERAM512, SRAM_ESERAM512 }, { "1 MB", CARTNAME_ESERAM1MB, SRAM_ESERAM1MB },
    };
    static const SpecialCart eseScc[] = {
        { "128 kB", CARTNAME_ESESCC128, SRAM_ESESCC128 }, { "256 kB", CARTNAME_ESESCC256, SRAM_ESESCC256 },
        { "512 kB", CARTNAME_ESESCC512, SRAM_ESESCC512 },
    };
    static const SpecialCart nowind[] = {
        { "MSXDOS1", CARTNAME_NOWINDDOS1, ROM_NOWIND }, { "MSXDOS2", CARTNAME_NOWINDDOS2, ROM_NOWIND },
    };
    static const SpecialCart flashCart[] = {
        { "ASCII16-X", CARTNAME_ASCII16X, ROM_ASCII16X }, { "Yamanooto", CARTNAME_YAMANOOTO, ROM_YAMANOOTO },
        { "Flash-ROM SCC", CARTNAME_FLASHROMSCC, ROM_FLASHROMSCC },
    };
    const SpecialCart flashRom[] = {
        { langRomTypeMegaFlashRomScc(), CARTNAME_MEGAFLSHSCC, ROM_MEGAFLSHSCC },
        { "Mega Flash Rom SCC+", CARTNAME_MEGAFLSHSCCPLUS, ROM_MEGAFLSHSCCPLUS },
        { "Mega Flash Rom SCC+ SD", CARTNAME_MEGAFLSHSCCPLUS_SD, ROM_MEGAFLSHSCCPLUS_SD },
    };
    const SpecialCart sound[] = {
        { langMenuCartJoyrexPsg(), CARTNAME_JOYREXPSG, ROM_JOYREXPSG },
        { langMenuCartSCC(), CARTNAME_SCC, ROM_SCC },
        { langMenuCartSCCPlus(), CARTNAME_SCCPLUS, ROM_SCCPLUS },
    };
    const SpecialCart ide[] = {
        { langMenuCartSunriseIde(), CARTNAME_SUNRISEIDE, ROM_SUNRISEIDE },
        { langMenuCartBeerIde(), CARTNAME_BEERIDE, ROM_BEERIDE },
        { langMenuCartGIde(), CARTNAME_GIDE, ROM_GIDE },
    };
    const SpecialCart fmpac[] = {
        { langMenuCartFMPac(), CARTNAME_FMPAC, ROM_FMPAC },
        { langMenuCartPac(), CARTNAME_PAC, ROM_PAC },
    };
    const SpecialCart hbi55[] = { { langMenuCartHBI55(), CARTNAME_SONYHBI55, ROM_SONYHBI55 } };
    const SpecialCart nms1210[] = { { "Philips NMS1210 Serial Interface", CARTNAME_NMS1210, ROM_NMS1210 } };
    const SpecialCart gouda[] = { { langMenuCartGoudaSCSI(), CARTNAME_GOUDASCSI, ROM_GOUDASCSI } };

    specialItems(slot, sound, N(sound));
    if (ImGui::BeginMenu(langMenuCartEseSCC())) { specialItems(slot, eseScc, N(eseScc)); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartIde())) { specialItems(slot, ide, N(ide)); ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuCartScsi())) {
        if (ImGui::BeginMenu(langMenuCartMegaSCSI())) { specialItems(slot, megaScsi, N(megaScsi)); ImGui::EndMenu(); }
        if (ImGui::BeginMenu(langMenuCartWaveSCSI())) { specialItems(slot, waveScsi, N(waveScsi)); ImGui::EndMenu(); }
        specialItems(slot, gouda, N(gouda));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Nowind USB Disk Controller")) { specialItems(slot, nowind, N(nowind)); ImGui::EndMenu(); }
    ImGui::Separator();
    specialItems(slot, fmpac, N(fmpac));
    ImGui::Separator();
    specialItems(slot, hbi55, N(hbi55));
    ImGui::Separator();
    specialItems(slot, nms1210, N(nms1210));
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartExternalRam())) { specialItems(slot, extRam, N(extRam)); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartMegaRam())) { specialItems(slot, megaRam, N(megaRam)); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartEseRam())) { specialItems(slot, eseRam, N(eseRam)); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartMegaFlashRom())) { specialItems(slot, flashRom, N(flashRom)); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuCartFlashCart())) { specialItems(slot, flashCart, N(flashCart)); ImGui::EndMenu(); }
}

static const char* systemOfRomType(RomType t)
{
    switch ((int)t) {
    case 94: case 95: case 107: case 108: case 112: return "Sega SG-1000[+]";
    case 76: case 113: return "Coleco";
    case 74: return "SVI-318/328";
    default: return "MSX";
    }
}

static void menuCart(int slot, bool enableSpecial)
{
    Properties* p = props();

    item(langMenuInsert(), slot ? SC_CART2_INSERT : SC_CART1_INSERT, [slot]() { actionCartInsert(slot); });
    if (enableSpecial && ImGui::BeginMenu(langMenuCartInsertSpecial())) {
        menuCartSpecial(slot);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    item(ejectLabel(p->media.carts[slot].fileName, "cart").c_str(),
         slot ? SC_CART2_REMOVE : SC_CART1_REMOVE, p->media.carts[slot].fileName[0] != 0, false,
         [slot]() { actionCartRemove(slot); });
    if (slot == 0) {
        ImGui::Separator();
        item(langMenuCartAutoReset(), SC_CART_AUTORESET, true, p->cartridge.autoReset != 0,
             []() { actionToggleCartAutoReset(); });
    }

    /* History, grouped by system as on Windows. */
    ImGui::Separator();
    if (p->filehistory.cartridge[slot][0][0] == 0) {
        ImGui::MenuItem(langMenuNoRecentFiles(), NULL, false, false);
        return;
    }
    const char* lastSystem = NULL;
    static const char* const systems[] = { "MSX", "Sega SG-1000[+]", "Coleco", "SVI-318/328" };
    for (const char* sys : systems) {
        for (int i = 0; i < p->filehistory.count && p->filehistory.cartridge[slot][i][0]; i++) {
            RomType t = p->filehistory.cartridgeType[slot][i];
            if (strcmp(systemOfRomType(t), sys) != 0) continue;
            if (lastSystem != sys) {
                if (lastSystem != NULL) ImGui::Separator();
                ImGui::TextDisabled("%s", sys);
                lastSystem = sys;
            }
            std::string label = std::string(clean(p->filehistory.cartridge[slot][i])) + "##h" + std::to_string(i);
            item(label.c_str(), NO_SC, [slot, i]() {
                Properties* q = props();
                insertCartridge(q, slot, q->filehistory.cartridge[slot][i], NULL,
                                q->filehistory.cartridgeType[slot][i], 0);
            });
        }
    }
}

/* ---- Disks ---------------------------------------------------------------- */

static std::vector<std::string> zipDiskImages(const char* zip)
{
    static const char* const exts[] = { ".dsk", ".di1", ".di2", ".360", ".720", ".Sf7" };
    std::vector<std::string> names;
    if (zip == NULL || !*zip || !isFileExtension((char*)zip, (char*)".zip")) return names;
    for (const char* ext : exts) {
        int count = 0;
        char* list = zipGetFileList(zip, ext, &count);
        const char* q = list;
        for (int i = 0; list && i < count; i++) {
            names.push_back(q);
            q += strlen(q) + 1;
        }
        free(list);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
    return names;
}

/* Inner listings are cached per zip, as menus are built every frame. */
static const std::vector<std::string>& cachedZipDisks(const char* zip)
{
    static std::vector<std::pair<std::string, std::vector<std::string>>> cache;
    for (auto& e : cache) if (e.first == zip) return e.second;
    if (cache.size() > 64) cache.clear();
    cache.emplace_back(zip, zipDiskImages(zip));
    return cache.back().second;
}

static void menuDisk(int drive)
{
    Properties* p = props();

    item(langMenuInsert(), drive ? SC_DISKB_INSERT : SC_DISKA_INSERT, [drive]() { actionDiskInsert(drive); });
    item(langMenuDiskDirInsert(), drive ? SC_DISKB_DIR_INSERT : SC_DISKA_DIR_INSERT,
         [drive]() { actionDiskInsertDir(drive); });
    item(langMenuDiskInsertNew(), NO_SC, [drive]() { actionDiskInsertNew(drive); });
    item(ejectLabel(p->media.disks[drive].fileName, "disk").c_str(),
         drive ? SC_DISKB_REMOVE : SC_DISKA_REMOVE, p->media.disks[drive].fileName[0] != 0, false,
         [drive]() { actionDiskRemove(drive); });
    if (drive == 0) {
        ImGui::Separator();
        item(langMenuDiskAutoStart(), SC_DISK_AUTORESET, true, p->diskdrive.autostartA != 0,
             []() { actionToggleDiskAutoReset(); });
    }
    ImGui::Separator();
    if (p->filehistory.diskdrive[drive][0][0] == 0) {
        ImGui::MenuItem(langMenuNoRecentFiles(), NULL, false, false);
        return;
    }
    for (int i = 0; i < p->filehistory.count && p->filehistory.diskdrive[drive][i][0]; i++) {
        const char* path = p->filehistory.diskdrive[drive][i];
        std::string label = std::string(clean(path)) + "##h" + std::to_string(i);
        const auto& inner = cachedZipDisks(path);
        if (inner.size() >= 2) {
            if (ImGui::BeginMenu(label.c_str())) {
                for (size_t j = 0; j < inner.size(); j++) {
                    std::string zip = path, name = inner[j];
                    item((name + "##z" + std::to_string(j)).c_str(), NO_SC, [drive, zip, name]() {
                        insertDiskette(props(), drive, zip.c_str(), name.c_str(), 0);
                    });
                }
                ImGui::EndMenu();
            }
        }
        else {
            item(label.c_str(), NO_SC, [drive, i]() {
                Properties* q = props();
                insertDiskette(q, drive, q->filehistory.diskdrive[drive][i], NULL, 0);
            });
        }
    }
}

/* ---- Hard disks ------------------------------------------------------------ */

static void menuIdeHd(int diskNo, bool showCdrom)
{
    Properties* p = props();
    item(langMenuInsert(), NO_SC, [diskNo]() { actionHarddiskInsert(diskNo); });
    if (showCdrom) item(langMenuDiskDirInsertCdrom(), NO_SC, [diskNo]() { actionHarddiskInsertCdrom(diskNo); });
    item(langMenuDiskInsertNew(), NO_SC, [diskNo]() { actionHarddiskInsertNew(diskNo); });
    ImGui::Separator();
    item(ejectLabel(p->media.disks[diskNo].fileName, "hd").c_str(), NO_SC,
         p->media.disks[diskNo].fileName[0] != 0, false, [diskNo]() { actionHarddiskRemove(diskNo); });
}

static void hdSub(const std::string& label, int diskNo, bool cdrom)
{
    if (ImGui::BeginMenu(label.c_str())) {
        menuIdeHd(diskNo, cdrom);
        ImGui::EndMenu();
    }
}

static void menuHarddisk()
{
    Properties* p = props();
    bool hasHd = false;

    for (int i = 0; i < MAX_HD_COUNT; i++) {
        std::string n = std::to_string(i);
        switch (boardGetHdType(i)) {
        case HD_SUNRISEIDE:
            hasHd = true;
            hdSub("IDE" + n + "  - Sunrise Primary", diskGetHdDriveId(i, 0), false);
            hdSub("IDE" + n + "  - Sunrise Secondary", diskGetHdDriveId(i, 1), false);
            break;
        case HD_BEERIDE:
            hasHd = true;
            hdSub("IDE" + n + " Beer", diskGetHdDriveId(i, 0), false);
            break;
        case HD_NOWIND:
            hasHd = true;
            for (int j = 0; j < 4; j++) hdSub("Disk " + std::to_string(j + 1), diskGetHdDriveId(i, j), false);
            ImGui::Separator();
            if (ImGui::BeginMenu(langPropSettings())) {
                item("Enable DOS2", NO_SC, true, p->nowind.enableDos2 != 0,
                     []() { props()->nowind.enableDos2 ^= 1; });
                item("Enable Phantom Drives", NO_SC, true, p->nowind.enablePhantomDrives != 0,
                     []() { props()->nowind.enablePhantomDrives ^= 1; });
                item("Enable Other Diskroms", NO_SC, true, p->nowind.enableOtherDiskRoms != 0,
                     []() { props()->nowind.enableOtherDiskRoms ^= 1; });
                ImGui::EndMenu();
            }
            break;
        case HD_RSIDE:
            hasHd = true;
            hdSub("IDE" + n + " RS IDE", diskGetHdDriveId(i, 0), false);
            break;
        case HD_GIDE:
            hasHd = true;
            hdSub("IDE" + n + " GIDE", diskGetHdDriveId(i, 0), false);
            break;
        case HD_MEGASCSI:
        case HD_WAVESCSI:
            hasHd = true;
            for (int j = 0; j < 8; j++) {
                hdSub("SCSI" + n + (boardGetHdType(i) == HD_MEGASCSI ? " MEGA" : " WAVE") +
                      "-SCSI #" + std::to_string(j), diskGetHdDriveId(i, j), true);
            }
            break;
        case HD_GOUDASCSI:
            hasHd = true;
            for (int j = 0; j < 8; j++) {
                hdSub("SCSI" + n + " Gouda SCSI #" + std::to_string(j), diskGetHdDriveId(i, j), true);
            }
            break;
        case HD_MFRSD:
            hasHd = true;
            hdSub("SD" + n + " - MFR SCC+ SD slot 1", diskGetHdDriveId(i, 0), false);
            hdSub("SD" + n + " - MFR SCC+ SD slot 2", diskGetHdDriveId(i, 1), false);
            break;
        default:
            break;
        }
    }
    if (!hasHd) {
        ImGui::MenuItem(langMenuFileHarddiskNoPresent(), NULL, false, false);
        return;
    }
    bool any = false;
    for (int i = 0; i < MAX_HD_COUNT && !any; i++) {
        for (int j = 0; j < MAX_DRIVES_PER_HD && !any; j++) {
            if (p->media.disks[diskGetHdDriveId(i, j)].fileName[0]) any = true;
        }
    }
    ImGui::Separator();
    item(langMenuFileHarddiskRemoveAll(), NO_SC, any, false, []() { actionHarddiskRemoveAll(); });
}

/* ---- Cassette, printer ----------------------------------------------------- */

static void menuCassette()
{
    Properties* p = props();
    bool hasTape = p->media.tapes[0].fileName[0] != 0;

    item(langMenuInsert(), SC_CAS_INSERT, []() { actionCasInsert(); });
    item(langMenuCasInsertNew(), NO_SC, []() { actionCasInsertNew(); });
    item(ejectLabel(p->media.tapes[0].fileName, "cas").c_str(), SC_CAS_REMOVE, hasTape, false,
         []() { actionCasRemove(); });
    ImGui::Separator();
    item(langMenuCasRewindAfterInsert(), SC_CAS_AUTOREWIND, true, p->cassette.rewindAfterInsert != 0,
         []() { actionToggleCasAutoRewind(); });
    item(langMenuCasSaveMonitor(), NO_SC, true, p->cassette.saveMonitor != 0,
         []() { actionToggleCasSaveMonitor(); });
    ImGui::Separator();
    item(langMenuCasUseReadOnly(), SC_CAS_READONLY, true, p->cassette.readOnly != 0,
         []() { actionCasToggleReadonly(); });
    item(langMenuCasSaveAs(), SC_CAS_SAVE, hasTape && !tapeIsSignalOnly(), false, []() { actionCasSave(); });
    ImGui::Separator();
    item(langMenuCasSetPosition(), SC_CAS_SET_POS, hasTape, false, []() { actionCasSetPosition(); });
    item(langMenuCasRewind(), SC_CAS_REWIND, hasTape, false, []() { actionCasRewind(); });
    ImGui::Separator();
    if (p->filehistory.cassette[0][0][0] == 0) {
        ImGui::MenuItem(langMenuNoRecentFiles(), NULL, false, false);
        return;
    }
    for (int i = 0; i < p->filehistory.count && p->filehistory.cassette[0][i][0]; i++) {
        std::string label = std::string(clean(p->filehistory.cassette[0][i])) + "##h" + std::to_string(i);
        item(label.c_str(), NO_SC, [i]() {
            Properties* q = props();
            if (q->cassette.rewindAfterInsert) tapeRewindNextInsert();
            insertCassette(q, 0, q->filehistory.cassette[0][i], NULL, 0);
        });
    }
}

static void menuPrinter()
{
    item(langMenuPrnFormfeed(), SC_PRN_FORMFEED, []() { actionPrinterForceFormFeed(); });
}

/* ---- Top level menus ------------------------------------------------------- */

static void menuVideoCapture()
{
    int logVideo = boardCaptureIsRecording() ? 1 : boardCaptureIsPlaying() ? 2 : 0;
    if (logVideo == 1) {
        item(langMenuVideoStop(), SC_VIDEO_CAP_STOP, []() { actionVideoCaptureStop(); });
    }
    else {
        item(logVideo == 2 ? langMenuVideoRecAppend() : langMenuVideoRecord(), SC_VIDEO_CAP_REC,
             []() { actionVideoCaptureRec(); });
    }
    ImGui::Separator();
    item(langMenuVideoLoad(), SC_VIDEO_CAP_LOAD, []() { actionVideoCaptureLoad(); });
    item(langMenuVideoPlay(), SC_VIDEO_CAP_PLAY, []() { actionVideoCapturePlay(); });
}

static void menuFile()
{
    Properties* p = props();
    int state = emulatorGetState();
    bool stopped = state == EMU_STOPPED;
    bool enableSpecial = boardGetType() == BOARD_MSX;
    std::string label;

    label = std::string(langMenuFileCart()) + " 1";
    if (ImGui::BeginMenu(label.c_str())) { menuCart(0, enableSpecial); ImGui::EndMenu(); }
    label = std::string(langMenuFileCart()) + " 2";
    if (ImGui::BeginMenu(label.c_str())) { menuCart(1, enableSpecial); ImGui::EndMenu(); }
    ImGui::Separator();
    label = std::string(langMenuFileDisk()) + " A";
    if (ImGui::BeginMenu(label.c_str())) { menuDisk(0); ImGui::EndMenu(); }
    label = std::string(langMenuFileDisk()) + " B";
    if (ImGui::BeginMenu(label.c_str())) { menuDisk(1); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuFileCas())) { menuCassette(); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuFileHarddisk())) { menuHarddisk(); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::BeginMenu(langMenuFilePrn())) { menuPrinter(); ImGui::EndMenu(); }
    ImGui::Separator();

    item(langMenuFileLoadState(), SC_STATE_LOAD, []() { actionLoadState(); });
    item(langMenuFileSaveState(), SC_STATE_SAVE, !stopped, false, []() { actionSaveState(); });
    ImGui::Separator();
    item(langMenuFileQLoadState(), SC_STATE_QUICK_LOAD,
         fileExist(p->filehistory.quicksave, NULL) != 0, false, []() { actionQuickLoadState(); });
    item(langMenuFileQSaveState(), SC_STATE_QUICK_SAVE, !stopped, false, []() { actionQuickSaveState(); });
    ImGui::Separator();

    bool logSound = mixerIsLogging(platformGetMixer()) != 0;
    item(logSound ? langMenuFileStopAudio() : langMenuFileCaptureAudio(), SC_WAV_CAPTURE, true, logSound,
         []() { actionToggleWaveCapture(); });
    item(langMenuFileScreenShot(), SC_SCREEN_CAPTURE, []() { actionScreenCapture(); });
    if (ImGui::BeginMenu(langMenuFileCaptureVideo())) { menuVideoCapture(); ImGui::EndMenu(); }
    ImGui::Separator();
    item(langMenuFileExit(), SC_QUIT, []() { actionQuit(); });
}

static void menuRun()
{
    int state = emulatorGetState();
    item(state == EMU_RUNNING ? langMenuRunPause() : langMenuRunRun(), SC_RUN_PAUSE,
         []() { actionEmuTogglePause(); });
    item(langMenuRunStop(), SC_STOP, state != EMU_STOPPED, false, []() { actionEmuStop(); });
    ImGui::Separator();
    item(langMenuRunHardReset(), SC_RESET_HARD, []() { actionEmuResetHard(); });
    item(langMenuRunSoftReset(), SC_RESET_SOFT, []() { actionEmuResetSoft(); });
    item(langMenuRunCleanReset(), SC_RESET_CLEAN, []() { actionEmuResetClean(); });
}

static void menuReset()
{
    item(langMenuRunHardReset(), SC_RESET_HARD, []() { actionEmuResetHard(); });
    item(langMenuRunSoftReset(), SC_RESET_SOFT, []() { actionEmuResetSoft(); });
    item(langMenuRunCleanReset(), SC_RESET_CLEAN, []() { actionEmuResetClean(); });
}

static void menuZoom()
{
    static void (*const sizes[])() = {
        actionWindowSize1x, actionWindowSize2x, actionWindowSize3x, actionWindowSize4x,
        actionWindowSize5x, actionWindowSize6x, actionWindowSize7x, actionWindowSize8x,
    };
    const char* labels[] = {
        langMenuZoom1x(), langMenuZoom2x(), langMenuZoom3x(), langMenuZoom4x(),
        langMenuZoom5x(), langMenuZoom6x(), langMenuZoom7x(), langMenuZoom8x(),
    };
    Properties* p = props();
    for (int i = 0; i < 8; i++) {
        auto fn = sizes[i];
        item(labels[i], (ShortcutId)(SC_SIZE_1X + i), true, p->video.windowSize == P_VIDEO_SIZEX1 + i,
             [fn]() { fn(); });
    }
    item(langMenuZoomFullscreen(), SC_FULLSCREEN_TOGGLE, true,
         p->video.windowSize == P_VIDEO_SIZEFULLSCREEN, []() { actionFullscreenToggle(); });
}

static void menuJoyPort(int port)
{
    JoystickPortType current = joystickPortGetType(port);
    int count = joystickPortGetTypeCount();
    for (int t = 0; t < count; t++) {
        if (!joystickPortTypeEnabled(port, (JoystickPortType)t)) continue;
        std::string label = std::string(joystickPortGetDescription((JoystickPortType)t, 1)) +
                            "##p" + std::to_string(t);
        item(label.c_str(), NO_SC, true, current == t, [port, t]() {
            joystickPortSetType(port, (JoystickPortType)t);
        });
    }
}

static void menuVideoConnect()
{
    Properties* p = props();
    int count = videoManagerGetCount();
    if (count == 0) ImGui::MenuItem(langMenuVideoSourceDefault(), NULL, false, false);
    for (int i = 0; i < count; i++) {
        std::string label = std::string(videoManagerGetName(i)) + "##v" + std::to_string(i);
        item(label.c_str(), NO_SC, true, videoManagerIsActive(i) != 0, [i]() {
            videoManagerSetActive(videoManagerGetActive() == i ? -1 : i);
        });
    }
    ImGui::Separator();
    item(langMenuVideoChipAutodetect(), NO_SC, count > 1, p->video.detectActiveMonitor != 0, []() {
        Properties* q = props();
        q->video.detectActiveMonitor = !q->video.detectActiveMonitor;
        boardSetVideoAutodetect(q->video.detectActiveMonitor);
    });
}

static void menuOptions()
{
    if (ImGui::BeginMenu(langMenuVideoSource())) { menuVideoConnect(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langPropControls())) {
        if (ImGui::BeginMenu("Port 1")) { menuJoyPort(0); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Port 2")) { menuJoyPort(1); ImGui::EndMenu(); }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    item(langMenuPropsEmulation(), SC_PROP_EMULATION, []() { actionPropShowEmulation(); });
    item(langMenuPropsVideo(), SC_PROP_VIDEO, []() { actionPropShowVideo(); });
    item(langMenuPropsSound(), SC_PROP_AUDIO, []() { actionPropShowAudio(); });
    item(langMenuPropsPorts(), SC_PROP_PORTS, []() { actionPropShowPorts(); });
    item(langMenuPropsFile(), SC_PROP_SETTINGS, []() { actionPropShowSettings(); });
    ImGui::Separator();
    item(langMenuPropsLanguage(), SC_LANGUAGE, []() { actionOptionsShowLanguage(); });
}

static void menuTools()
{
    item(langMenuToolsShortcuts(), SC_TOOL_SHORTCUT_EDITOR, []() { actionToolsShowShorcutEditor(); });
    item(langMenuToolsMixer(), SC_TOOL_MIXER, []() { actionToolsShowMixer(); });
}

static void menuHelp()
{
    item(langMenuHelpHelp(), SC_HELP, []() { actionHelpShowHelp(); });
    ImGui::Separator();
    item(langMenuHelpAbout(), SC_ABOUT, []() { actionHelpShowAbout(); });
}

void drawMainMenuBar()
{
    if (menuBarHeight() <= 0) return;
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(langMenuFile()))    { menuFile();    ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuRun()))     { menuRun();     ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuWindow()))  { menuZoom();    ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuOptions())) { menuOptions(); ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuTools()))   { menuTools();   ImGui::EndMenu(); }
    if (ImGui::BeginMenu(langMenuHelp()))    { menuHelp();    ImGui::EndMenu(); }

    /* Machine and state on the right, as a status line. */
    Properties* p = props();
    int state = emulatorGetState();
    std::string status = p->emulation.machineName;
    if (state == EMU_PAUSED)  status += "  ||";
    if (state == EMU_STOPPED) status += "  []";
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
    bool special = boardGetType() == BOARD_MSX;
    switch (activePopup) {
    case FRONTEND_MENU_FILE:          menuFile(); break;
    case FRONTEND_MENU_RUN:           menuRun(); break;
    case FRONTEND_MENU_RESET:         menuReset(); break;
    case FRONTEND_MENU_CART1:         menuCart(0, special); break;
    case FRONTEND_MENU_CART2:         menuCart(1, special); break;
    case FRONTEND_MENU_SPECIAL_CART1: menuCartSpecial(0); break;
    case FRONTEND_MENU_SPECIAL_CART2: menuCartSpecial(1); break;
    case FRONTEND_MENU_DISKA:         menuDisk(0); break;
    case FRONTEND_MENU_DISKB:         menuDisk(1); break;
    case FRONTEND_MENU_HARDDISK:      menuHarddisk(); break;
    case FRONTEND_MENU_CASSETTE:      menuCassette(); break;
    case FRONTEND_MENU_PRINTER:       menuPrinter(); break;
    case FRONTEND_MENU_JOYPORT1:      menuJoyPort(0); break;
    case FRONTEND_MENU_JOYPORT2:      menuJoyPort(1); break;
    case FRONTEND_MENU_ZOOM:          menuZoom(); break;
    case FRONTEND_MENU_OPTIONS:       menuOptions(); break;
    case FRONTEND_MENU_TOOLS:         menuTools(); break;
    case FRONTEND_MENU_HELP:          menuHelp(); break;
    }
    ImGui::EndPopup();
}

} // namespace app
