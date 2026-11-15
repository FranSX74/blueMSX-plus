/*****************************************************************************
** blueMSX+ - menus shared by the frontends
**
** The same menus as Win32Menu.c, built from the live emulator state.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "FrontendMenus.h"

extern "C" {
#include "Platform.h"
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
#include "Properties.h"
#include "VideoManager.h"
#include "ziphelper.h"
}

#include <algorithm>
#include <cstring>
#include <strings.h>

namespace fe {

static Properties* props() { return platformGetProperties(); }

static const int NO_SC = -1;

const char* baseName(const char* path)
{
    if (path == NULL) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void item(MenuBuilder& b, const std::string& label, int sc, std::function<void()> fn)
{
    b.item(label, sc, true, NO_CHECK, std::move(fn));
}

static std::string ejectLabel(const char* fileName)
{
    std::string s = langMenuEject();
    if (fileName && *fileName) {
        s += ": ";
        s += baseName(fileName);
    }
    return s;
}

/* ---- Cartridges ------------------------------------------------------------ */

struct SpecialCart { const char* label; const char* name; RomType type; };

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void specialItems(MenuBuilder& b, int slot, const SpecialCart* carts, int count)
{
    for (int i = 0; i < count; i++) {
        const SpecialCart c = carts[i];
        item(b, c.label, NO_SC, [slot, c]() {
            insertCartridge(props(), slot, c.name, NULL, c.type, 0);
        });
    }
}

static void specialSub(MenuBuilder& b, const char* label, int slot, const SpecialCart* carts, int count)
{
    if (b.beginMenu(label)) {
        specialItems(b, slot, carts, count);
        b.endMenu();
    }
}

static void menuCartSpecial(MenuBuilder& b, int slot)
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

    specialItems(b, slot, sound, N(sound));
    specialSub(b, langMenuCartEseSCC(), slot, eseScc, N(eseScc));
    b.separator();
    specialSub(b, langMenuCartIde(), slot, ide, N(ide));
    if (b.beginMenu(langMenuCartScsi())) {
        specialSub(b, langMenuCartMegaSCSI(), slot, megaScsi, N(megaScsi));
        specialSub(b, langMenuCartWaveSCSI(), slot, waveScsi, N(waveScsi));
        specialItems(b, slot, gouda, N(gouda));
        b.endMenu();
    }
    specialSub(b, "Nowind USB Disk Controller", slot, nowind, N(nowind));
    b.separator();
    specialItems(b, slot, fmpac, N(fmpac));
    b.separator();
    specialItems(b, slot, hbi55, N(hbi55));
    b.separator();
    specialItems(b, slot, nms1210, N(nms1210));
    b.separator();
    specialSub(b, langMenuCartExternalRam(), slot, extRam, N(extRam));
    b.separator();
    specialSub(b, langMenuCartMegaRam(), slot, megaRam, N(megaRam));
    b.separator();
    specialSub(b, langMenuCartEseRam(), slot, eseRam, N(eseRam));
    b.separator();
    specialSub(b, langMenuCartMegaFlashRom(), slot, flashRom, N(flashRom));
    b.separator();
    specialSub(b, langMenuCartFlashCart(), slot, flashCart, N(flashCart));
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

static void menuCart(MenuBuilder& b, int slot)
{
    Properties* p = props();
    bool enableSpecial = boardGetType() == BOARD_MSX;

    item(b, langMenuInsert(), slot ? SC_CART2_INSERT : SC_CART1_INSERT, [slot]() { actionCartInsert(slot); });
    if (enableSpecial && b.beginMenu(langMenuCartInsertSpecial())) {
        menuCartSpecial(b, slot);
        b.endMenu();
    }
    b.separator();
    b.item(ejectLabel(p->media.carts[slot].fileName), slot ? SC_CART2_REMOVE : SC_CART1_REMOVE,
           p->media.carts[slot].fileName[0] != 0, NO_CHECK, [slot]() { actionCartRemove(slot); });
    if (slot == 0) {
        b.separator();
        b.item(langMenuCartAutoReset(), SC_CART_AUTORESET, true, p->cartridge.autoReset != 0,
               []() { actionToggleCartAutoReset(); });
    }

    /* History, grouped by system as on Windows. */
    b.separator();
    if (p->filehistory.cartridge[slot][0][0] == 0) {
        b.item(langMenuNoRecentFiles(), NO_SC, false, NO_CHECK, []() {});
        return;
    }
    const char* lastSystem = NULL;
    static const char* const systems[] = { "MSX", "Sega SG-1000[+]", "Coleco", "SVI-318/328" };
    for (const char* sys : systems) {
        for (int i = 0; i < p->filehistory.count && p->filehistory.cartridge[slot][i][0]; i++) {
            RomType t = p->filehistory.cartridgeType[slot][i];
            if (strcmp(systemOfRomType(t), sys) != 0) continue;
            if (lastSystem != sys) {
                if (lastSystem != NULL) b.separator();
                b.caption(sys);
                lastSystem = sys;
            }
            item(b, baseName(p->filehistory.cartridge[slot][i]), NO_SC, [slot, i]() {
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
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& c) {
        return strcasecmp(a.c_str(), c.c_str()) < 0;
    });
    return names;
}

/* Inner listings are cached per zip: the ImGui menus are built every frame. */
static const std::vector<std::string>& cachedZipDisks(const char* zip)
{
    static std::vector<std::pair<std::string, std::vector<std::string>>> cache;
    for (auto& e : cache) if (e.first == zip) return e.second;
    if (cache.size() > 64) cache.clear();
    cache.emplace_back(zip, zipDiskImages(zip));
    return cache.back().second;
}

static void menuDisk(MenuBuilder& b, int drive)
{
    Properties* p = props();

    item(b, langMenuInsert(), drive ? SC_DISKB_INSERT : SC_DISKA_INSERT, [drive]() { actionDiskInsert(drive); });
    item(b, langMenuDiskDirInsert(), drive ? SC_DISKB_DIR_INSERT : SC_DISKA_DIR_INSERT,
         [drive]() { actionDiskInsertDir(drive); });
    item(b, langMenuDiskInsertNew(), NO_SC, [drive]() { actionDiskInsertNew(drive); });
    b.item(ejectLabel(p->media.disks[drive].fileName), drive ? SC_DISKB_REMOVE : SC_DISKA_REMOVE,
           p->media.disks[drive].fileName[0] != 0, NO_CHECK, [drive]() { actionDiskRemove(drive); });
    if (drive == 0) {
        b.separator();
        b.item(langMenuDiskAutoStart(), SC_DISK_AUTORESET, true, p->diskdrive.autostartA != 0,
               []() { actionToggleDiskAutoReset(); });
    }
    b.separator();
    if (p->filehistory.diskdrive[drive][0][0] == 0) {
        b.item(langMenuNoRecentFiles(), NO_SC, false, NO_CHECK, []() {});
        return;
    }
    for (int i = 0; i < p->filehistory.count && p->filehistory.diskdrive[drive][i][0]; i++) {
        const char* path = p->filehistory.diskdrive[drive][i];
        const auto& inner = cachedZipDisks(path);
        if (inner.size() >= 2) {
            if (b.beginMenu(baseName(path))) {
                for (const std::string& name : inner) {
                    std::string zip = path;
                    item(b, name, NO_SC, [drive, zip, name]() {
                        insertDiskette(props(), drive, zip.c_str(), name.c_str(), 0);
                    });
                }
                b.endMenu();
            }
        }
        else {
            item(b, baseName(path), NO_SC, [drive, i]() {
                Properties* q = props();
                insertDiskette(q, drive, q->filehistory.diskdrive[drive][i], NULL, 0);
            });
        }
    }
}

/* ---- Hard disks ------------------------------------------------------------ */

static void menuIdeHd(MenuBuilder& b, int diskNo, bool showCdrom)
{
    Properties* p = props();
    item(b, langMenuInsert(), NO_SC, [diskNo]() { actionHarddiskInsert(diskNo); });
    if (showCdrom) item(b, langMenuDiskDirInsertCdrom(), NO_SC, [diskNo]() { actionHarddiskInsertCdrom(diskNo); });
    item(b, langMenuDiskInsertNew(), NO_SC, [diskNo]() { actionHarddiskInsertNew(diskNo); });
    b.separator();
    b.item(ejectLabel(p->media.disks[diskNo].fileName), NO_SC, p->media.disks[diskNo].fileName[0] != 0,
           NO_CHECK, [diskNo]() { actionHarddiskRemove(diskNo); });
}

static void hdSub(MenuBuilder& b, const std::string& label, int diskNo, bool cdrom)
{
    if (b.beginMenu(label)) {
        menuIdeHd(b, diskNo, cdrom);
        b.endMenu();
    }
}

static void menuHarddisk(MenuBuilder& b)
{
    Properties* p = props();
    bool hasHd = false;

    for (int i = 0; i < MAX_HD_COUNT; i++) {
        std::string n = std::to_string(i);
        switch (boardGetHdType(i)) {
        case HD_SUNRISEIDE:
            hasHd = true;
            hdSub(b, "IDE" + n + "  - Sunrise Primary", diskGetHdDriveId(i, 0), false);
            hdSub(b, "IDE" + n + "  - Sunrise Secondary", diskGetHdDriveId(i, 1), false);
            break;
        case HD_BEERIDE:
            hasHd = true;
            hdSub(b, "IDE" + n + " Beer", diskGetHdDriveId(i, 0), false);
            break;
        case HD_NOWIND:
            hasHd = true;
            for (int j = 0; j < 4; j++) hdSub(b, "Disk " + std::to_string(j + 1), diskGetHdDriveId(i, j), false);
            b.separator();
            if (b.beginMenu(langPropSettings())) {
                b.item("Enable DOS2", NO_SC, true, p->nowind.enableDos2 != 0,
                       []() { props()->nowind.enableDos2 ^= 1; });
                b.item("Enable Phantom Drives", NO_SC, true, p->nowind.enablePhantomDrives != 0,
                       []() { props()->nowind.enablePhantomDrives ^= 1; });
                b.item("Enable Other Diskroms", NO_SC, true, p->nowind.enableOtherDiskRoms != 0,
                       []() { props()->nowind.enableOtherDiskRoms ^= 1; });
                b.endMenu();
            }
            break;
        case HD_RSIDE:
            hasHd = true;
            hdSub(b, "IDE" + n + " RS IDE", diskGetHdDriveId(i, 0), false);
            break;
        case HD_GIDE:
            hasHd = true;
            hdSub(b, "IDE" + n + " GIDE", diskGetHdDriveId(i, 0), false);
            break;
        case HD_MEGASCSI:
        case HD_WAVESCSI:
            hasHd = true;
            for (int j = 0; j < 8; j++) {
                hdSub(b, "SCSI" + n + (boardGetHdType(i) == HD_MEGASCSI ? " MEGA" : " WAVE") +
                      "-SCSI #" + std::to_string(j), diskGetHdDriveId(i, j), true);
            }
            break;
        case HD_GOUDASCSI:
            hasHd = true;
            for (int j = 0; j < 8; j++) {
                hdSub(b, "SCSI" + n + " Gouda SCSI #" + std::to_string(j), diskGetHdDriveId(i, j), true);
            }
            break;
        case HD_MFRSD:
            hasHd = true;
            hdSub(b, "SD" + n + " - MFR SCC+ SD slot 1", diskGetHdDriveId(i, 0), false);
            hdSub(b, "SD" + n + " - MFR SCC+ SD slot 2", diskGetHdDriveId(i, 1), false);
            break;
        default:
            break;
        }
    }
    if (!hasHd) {
        b.item(langMenuFileHarddiskNoPresent(), NO_SC, false, NO_CHECK, []() {});
        return;
    }
    bool any = false;
    for (int i = 0; i < MAX_HD_COUNT && !any; i++) {
        for (int j = 0; j < MAX_DRIVES_PER_HD && !any; j++) {
            if (p->media.disks[diskGetHdDriveId(i, j)].fileName[0]) any = true;
        }
    }
    b.separator();
    b.item(langMenuFileHarddiskRemoveAll(), NO_SC, any, NO_CHECK, []() { actionHarddiskRemoveAll(); });
}

/* ---- Cassette, printer ----------------------------------------------------- */

static void menuCassette(MenuBuilder& b)
{
    Properties* p = props();
    bool hasTape = p->media.tapes[0].fileName[0] != 0;

    item(b, langMenuInsert(), SC_CAS_INSERT, []() { actionCasInsert(); });
    item(b, langMenuCasInsertNew(), NO_SC, []() { actionCasInsertNew(); });
    b.item(ejectLabel(p->media.tapes[0].fileName), SC_CAS_REMOVE, hasTape, NO_CHECK,
           []() { actionCasRemove(); });
    b.separator();
    b.item(langMenuCasRewindAfterInsert(), SC_CAS_AUTOREWIND, true, p->cassette.rewindAfterInsert != 0,
           []() { actionToggleCasAutoRewind(); });
    b.item(langMenuCasSaveMonitor(), NO_SC, true, p->cassette.saveMonitor != 0,
           []() { actionToggleCasSaveMonitor(); });
    b.separator();
    b.item(langMenuCasUseReadOnly(), SC_CAS_READONLY, true, p->cassette.readOnly != 0,
           []() { actionCasToggleReadonly(); });
    b.item(langMenuCasSaveAs(), SC_CAS_SAVE, hasTape && !tapeIsSignalOnly(), NO_CHECK, []() { actionCasSave(); });
    b.separator();
    b.item(langMenuCasSetPosition(), SC_CAS_SET_POS, hasTape, NO_CHECK, []() { actionCasSetPosition(); });
    b.item(langMenuCasRewind(), SC_CAS_REWIND, hasTape, NO_CHECK, []() { actionCasRewind(); });
    b.separator();
    if (p->filehistory.cassette[0][0][0] == 0) {
        b.item(langMenuNoRecentFiles(), NO_SC, false, NO_CHECK, []() {});
        return;
    }
    for (int i = 0; i < p->filehistory.count && p->filehistory.cassette[0][i][0]; i++) {
        item(b, baseName(p->filehistory.cassette[0][i]), NO_SC, [i]() {
            Properties* q = props();
            if (q->cassette.rewindAfterInsert) tapeRewindNextInsert();
            insertCassette(q, 0, q->filehistory.cassette[0][i], NULL, 0);
        });
    }
}

static void menuPrinter(MenuBuilder& b)
{
    item(b, langMenuPrnFormfeed(), SC_PRN_FORMFEED, []() { actionPrinterForceFormFeed(); });
}

/* ---- Top level menus ------------------------------------------------------- */

static void menuVideoCapture(MenuBuilder& b)
{
    int logVideo = boardCaptureIsRecording() ? 1 : boardCaptureIsPlaying() ? 2 : 0;
    if (logVideo == 1) {
        item(b, langMenuVideoStop(), SC_VIDEO_CAP_STOP, []() { actionVideoCaptureStop(); });
    }
    else {
        item(b, logVideo == 2 ? langMenuVideoRecAppend() : langMenuVideoRecord(), SC_VIDEO_CAP_REC,
             []() { actionVideoCaptureRec(); });
    }
    b.separator();
    item(b, langMenuVideoLoad(), SC_VIDEO_CAP_LOAD, []() { actionVideoCaptureLoad(); });
    item(b, langMenuVideoPlay(), SC_VIDEO_CAP_PLAY, []() { actionVideoCapturePlay(); });
}

static void sub(MenuBuilder& b, const std::string& label, const std::function<void()>& fill)
{
    if (b.beginMenu(label)) {
        fill();
        b.endMenu();
    }
}

static void menuFile(MenuBuilder& b)
{
    Properties* p = props();
    bool stopped = emulatorGetState() == EMU_STOPPED;

    sub(b, std::string(langMenuFileCart()) + " 1", [&]() { menuCart(b, 0); });
    sub(b, std::string(langMenuFileCart()) + " 2", [&]() { menuCart(b, 1); });
    b.separator();
    sub(b, std::string(langMenuFileDisk()) + " A", [&]() { menuDisk(b, 0); });
    sub(b, std::string(langMenuFileDisk()) + " B", [&]() { menuDisk(b, 1); });
    b.separator();
    sub(b, langMenuFileCas(), [&]() { menuCassette(b); });
    b.separator();
    sub(b, langMenuFileHarddisk(), [&]() { menuHarddisk(b); });
    b.separator();
    sub(b, langMenuFilePrn(), [&]() { menuPrinter(b); });
    b.separator();

    item(b, langMenuFileLoadState(), SC_STATE_LOAD, []() { actionLoadState(); });
    b.item(langMenuFileSaveState(), SC_STATE_SAVE, !stopped, NO_CHECK, []() { actionSaveState(); });
    b.separator();
    b.item(langMenuFileQLoadState(), SC_STATE_QUICK_LOAD, fileExist(p->filehistory.quicksave, NULL) != 0,
           NO_CHECK, []() { actionQuickLoadState(); });
    b.item(langMenuFileQSaveState(), SC_STATE_QUICK_SAVE, !stopped, NO_CHECK, []() { actionQuickSaveState(); });
    b.separator();

    bool logSound = mixerIsLogging(platformGetMixer()) != 0;
    b.item(logSound ? langMenuFileStopAudio() : langMenuFileCaptureAudio(), SC_WAV_CAPTURE, true,
           logSound ? 1 : NO_CHECK, []() { actionToggleWaveCapture(); });
    if (platformRecorderAvailable()) {
        if (archRecordVideoIsActive()) {
            b.item(langMenuFileStopRecordVideo(), SC_RECORD_VIDEO_STOP, true, 1,
                   []() { actionRecordVideoStop(); });
        }
        else {
            item(b, langMenuFileRecordVideo(), SC_RECORD_VIDEO_START, []() { actionRecordVideoStart(); });
        }
    }
    item(b, langMenuFileScreenShot(), SC_SCREEN_CAPTURE, []() { actionScreenCapture(); });
    sub(b, langMenuFileCaptureVideo(), [&]() { menuVideoCapture(b); });
    b.separator();
    item(b, langMenuFileExit(), SC_QUIT, []() { actionQuit(); });
}

static void menuReset(MenuBuilder& b)
{
    item(b, langMenuRunHardReset(), SC_RESET_HARD, []() { actionEmuResetHard(); });
    item(b, langMenuRunSoftReset(), SC_RESET_SOFT, []() { actionEmuResetSoft(); });
    item(b, langMenuRunCleanReset(), SC_RESET_CLEAN, []() { actionEmuResetClean(); });
}

static void menuRun(MenuBuilder& b)
{
    int state = emulatorGetState();
    item(b, state == EMU_RUNNING ? langMenuRunPause() : langMenuRunRun(), SC_RUN_PAUSE,
         []() { actionEmuTogglePause(); });
    b.item(langMenuRunStop(), SC_STOP, state != EMU_STOPPED, NO_CHECK, []() { actionEmuStop(); });
    b.separator();
    menuReset(b);
}

static void menuZoom(MenuBuilder& b)
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
        b.item(labels[i], SC_SIZE_1X + i, true, p->video.windowSize == P_VIDEO_SIZEX1 + i, [fn]() { fn(); });
    }
    b.item(langMenuZoomFullscreen(), SC_FULLSCREEN_TOGGLE, true,
           p->video.windowSize == P_VIDEO_SIZEFULLSCREEN, []() { actionFullscreenToggle(); });
}

static void menuJoyPort(MenuBuilder& b, int port)
{
    JoystickPortType current = joystickPortGetType(port);
    int count = joystickPortGetTypeCount();
    for (int t = 0; t < count; t++) {
        if (!joystickPortTypeEnabled(port, (JoystickPortType)t)) continue;
        b.item(joystickPortGetDescription((JoystickPortType)t, 1), NO_SC, true, current == t, [port, t]() {
            joystickPortSetType(port, (JoystickPortType)t);
        });
    }
}

static void menuVideoConnect(MenuBuilder& b)
{
    Properties* p = props();
    int count = videoManagerGetCount();
    if (count == 0) b.item(langMenuVideoSourceDefault(), NO_SC, false, NO_CHECK, []() {});
    for (int i = 0; i < count; i++) {
        b.item(videoManagerGetName(i), NO_SC, true, videoManagerIsActive(i) != 0, [i]() {
            videoManagerSetActive(videoManagerGetActive() == i ? -1 : i);
        });
    }
    b.separator();
    b.item(langMenuVideoChipAutodetect(), NO_SC, count > 1, p->video.detectActiveMonitor != 0, []() {
        Properties* q = props();
        q->video.detectActiveMonitor = !q->video.detectActiveMonitor;
        boardSetVideoAutodetect(q->video.detectActiveMonitor);
    });
}

static void menuOptions(MenuBuilder& b)
{
    sub(b, langMenuVideoSource(), [&]() { menuVideoConnect(b); });
    sub(b, langPropControls(), [&]() {
        sub(b, "Port 1", [&]() { menuJoyPort(b, 0); });
        sub(b, "Port 2", [&]() { menuJoyPort(b, 1); });
    });
    b.separator();
    item(b, langMenuPropsEmulation(), SC_PROP_EMULATION, []() { actionPropShowEmulation(); });
    item(b, langMenuPropsVideo(), SC_PROP_VIDEO, []() { actionPropShowVideo(); });
    item(b, langMenuPropsSound(), SC_PROP_AUDIO, []() { actionPropShowAudio(); });
    item(b, langMenuPropsPorts(), SC_PROP_PORTS, []() { actionPropShowPorts(); });
    item(b, langMenuPropsFile(), SC_PROP_SETTINGS, []() { actionPropShowSettings(); });
    b.separator();
    item(b, langMenuPropsLanguage(), SC_LANGUAGE, []() { actionOptionsShowLanguage(); });
}

static void menuTools(MenuBuilder& b)
{
    item(b, langMenuToolsShortcuts(), SC_TOOL_SHORTCUT_EDITOR, []() { actionToolsShowShorcutEditor(); });
    item(b, langMenuToolsMixer(), SC_TOOL_MIXER, []() { actionToolsShowMixer(); });
}

static void menuHelp(MenuBuilder& b)
{
    item(b, langMenuHelpHelp(), SC_HELP, []() { actionHelpShowHelp(); });
    b.separator();
    item(b, langMenuHelpAbout(), SC_ABOUT, []() { actionHelpShowAbout(); });
}

std::vector<MainMenu> mainMenus()
{
    return {
        { langMenuFile(),    FRONTEND_MENU_FILE },
        { langMenuRun(),     FRONTEND_MENU_RUN },
        { langMenuWindow(),  FRONTEND_MENU_ZOOM },
        { langMenuOptions(), FRONTEND_MENU_OPTIONS },
        { langMenuTools(),   FRONTEND_MENU_TOOLS },
        { langMenuHelp(),    FRONTEND_MENU_HELP },
    };
}

void buildMenu(FrontendMenu menu, MenuBuilder& b)
{
    switch (menu) {
    case FRONTEND_MENU_FILE:          menuFile(b); break;
    case FRONTEND_MENU_RUN:           menuRun(b); break;
    case FRONTEND_MENU_RESET:         menuReset(b); break;
    case FRONTEND_MENU_CART1:         menuCart(b, 0); break;
    case FRONTEND_MENU_CART2:         menuCart(b, 1); break;
    case FRONTEND_MENU_SPECIAL_CART1: menuCartSpecial(b, 0); break;
    case FRONTEND_MENU_SPECIAL_CART2: menuCartSpecial(b, 1); break;
    case FRONTEND_MENU_DISKA:         menuDisk(b, 0); break;
    case FRONTEND_MENU_DISKB:         menuDisk(b, 1); break;
    case FRONTEND_MENU_HARDDISK:      menuHarddisk(b); break;
    case FRONTEND_MENU_CASSETTE:      menuCassette(b); break;
    case FRONTEND_MENU_PRINTER:       menuPrinter(b); break;
    case FRONTEND_MENU_JOYPORT1:      menuJoyPort(b, 0); break;
    case FRONTEND_MENU_JOYPORT2:      menuJoyPort(b, 1); break;
    case FRONTEND_MENU_ZOOM:          menuZoom(b); break;
    case FRONTEND_MENU_OPTIONS:       menuOptions(b); break;
    case FRONTEND_MENU_TOOLS:         menuTools(b); break;
    case FRONTEND_MENU_HELP:          menuHelp(b); break;
    }
}

std::string machineStatus()
{
    Properties* p = props();
    int state = emulatorGetState();
    std::string status = p != NULL ? p->emulation.machineName : "";
    if (state == EMU_PAUSED)  status += "  ||";
    if (state == EMU_STOPPED) status += "  []";
    return status;
}

/* ---- Shortcut descriptions --------------------------------------------------- */

const char* shortcutDescription(int sc)
{
    ShortcutId id = (ShortcutId)sc;
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

}
