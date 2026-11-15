/*****************************************************************************
** blueMSX+ - Platform layer: user interface hooks
**
** The arch* calls below need a user interface. They are turned into
** FrontendHooks calls; without a frontend implementation they do nothing.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "Platform.h"
#include "PlatformInternal.h"

#include "ArchDialog.h"
#include "ArchFile.h"
#include "ArchMenu.h"
#include "ArchNotifications.h"
#include "Properties.h"
#include "MediaDb.h"
#include "Language.h"
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

/* ---- Menus ---------------------------------------------------------------- */

static void showMenu(FrontendMenu menu, int x, int y)
{
    if (platformHooks()->showMenu) platformHooks()->showMenu(menu, x, y);
}

void archUpdateMenu(int show)
{
    if (platformHooks()->menuUpdate) platformHooks()->menuUpdate(show);
}

void archShowMenuSpecialCart1(int x, int y) { showMenu(FRONTEND_MENU_SPECIAL_CART1, x, y); }
void archShowMenuSpecialCart2(int x, int y) { showMenu(FRONTEND_MENU_SPECIAL_CART2, x, y); }
void archShowMenuReset(int x, int y)        { showMenu(FRONTEND_MENU_RESET, x, y); }
void archShowMenuHelp(int x, int y)         { showMenu(FRONTEND_MENU_HELP, x, y); }
void archShowMenuRun(int x, int y)          { showMenu(FRONTEND_MENU_RUN, x, y); }
void archShowMenuFile(int x, int y)         { showMenu(FRONTEND_MENU_FILE, x, y); }
void archShowMenuCart1(int x, int y)        { showMenu(FRONTEND_MENU_CART1, x, y); }
void archShowMenuCart2(int x, int y)        { showMenu(FRONTEND_MENU_CART2, x, y); }
void archShowMenuHarddisk(int x, int y)     { showMenu(FRONTEND_MENU_HARDDISK, x, y); }
void archShowMenuDiskA(int x, int y)        { showMenu(FRONTEND_MENU_DISKA, x, y); }
void archShowMenuDiskB(int x, int y)        { showMenu(FRONTEND_MENU_DISKB, x, y); }
void archShowMenuCassette(int x, int y)     { showMenu(FRONTEND_MENU_CASSETTE, x, y); }
void archShowMenuPrinter(int x, int y)      { showMenu(FRONTEND_MENU_PRINTER, x, y); }
void archShowMenuZoom(int x, int y)         { showMenu(FRONTEND_MENU_ZOOM, x, y); }
void archShowMenuOptions(int x, int y)      { showMenu(FRONTEND_MENU_OPTIONS, x, y); }
void archShowMenuTools(int x, int y)        { showMenu(FRONTEND_MENU_TOOLS, x, y); }
void archShowMenuJoyPort1(int x, int y)     { showMenu(FRONTEND_MENU_JOYPORT1, x, y); }
void archShowMenuJoyPort2(int x, int y)     { showMenu(FRONTEND_MENU_JOYPORT2, x, y); }
int  archMenuStripHeight(void)
{
    const FrontendHooks* h = platformHooks();
    return h->menuStripHeight ? h->menuStripHeight() : 0;
}
void archSetDpiOverride(unsigned int dpi)   {}

/* ---- Dialogs and window --------------------------------------------------- */

static void showDialog(FrontendDialog dialog, int arg)
{
    if (platformHooks()->showDialog) platformHooks()->showDialog(dialog, arg);
}

static void windowCommand(FrontendWindowCommand command)
{
    if (platformHooks()->windowCommand) platformHooks()->windowCommand(command);
}

void archShowPropertiesDialog(PropPage page) { showDialog(FRONTEND_DIALOG_PROPERTIES, (int)page); }
void archShowLanguageDialog(void)   { showDialog(FRONTEND_DIALOG_LANGUAGE, 0); }
void archShowHelpDialog(void)       { showDialog(FRONTEND_DIALOG_HELP, 0); }
void archShowAboutDialog(void)      { showDialog(FRONTEND_DIALOG_ABOUT, 0); }
void archShowCassettePosDialog(void){ showDialog(FRONTEND_DIALOG_CASSETTE_POS, 0); }
void archShowShortcutsEditor(void)  { showDialog(FRONTEND_DIALOG_SHORTCUTS, 0); }
void archShowKeyboardEditor(void)   { showDialog(FRONTEND_DIALOG_KEYBOARD, 0); }
void archShowMixer(void)            { showDialog(FRONTEND_DIALOG_MIXER, 0); }
void archShowDebugger(void)         { showDialog(FRONTEND_DIALOG_DEBUGGER, 0); }
void archShowTrainer(void)          { showDialog(FRONTEND_DIALOG_TRAINER, 0); }
void archShowMachineEditor(void)    { showDialog(FRONTEND_DIALOG_MACHINE_EDITOR, 0); }

void archShowNoRomInZipDialog(void)  { platformFrontendMessage(FRONTEND_MESSAGE_WARNING, "The zip file holds no ROM image."); }
void archShowNoDiskInZipDialog(void) { platformFrontendMessage(FRONTEND_MESSAGE_WARNING, "The zip file holds no disk image."); }
void archShowNoCasInZipDialog(void)  { platformFrontendMessage(FRONTEND_MESSAGE_WARNING, "The zip file holds no cassette image."); }

void archShowStartEmuFailDialog(const char* machineName)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "The emulation could not start with machine \"%s\". "
             "Its system ROMs may be missing.", machineName ? machineName : "");
    platformFrontendMessage(FRONTEND_MESSAGE_ERROR, msg);
}

void archShowDirAsDskOverflowDialog(int skippedCount, int skippedBytes)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "%d file(s) (%d bytes) did not fit on the disk image.",
             skippedCount, skippedBytes);
    platformFrontendMessage(FRONTEND_MESSAGE_WARNING, msg);
}

void archMaximizeWindow(void)     { windowCommand(FRONTEND_WINDOW_MAXIMIZE); }
void archMinimizeWindow(void)     { windowCommand(FRONTEND_WINDOW_MINIMIZE); }
void archMinimizeMainWindow(void) { windowCommand(FRONTEND_WINDOW_MINIMIZE); }
void archCloseWindow(void)        { windowCommand(FRONTEND_WINDOW_CLOSE); }

void archQuit(void)
{
    if (platformHooks()->quit) platformHooks()->quit();
}

/* ---- Notifications -------------------------------------------------------- */

void archEmulationStartNotification(void)
{
    if (platformHooks()->emulationState) platformHooks()->emulationState(1);
}

void archEmulationStopNotification(void)
{
    if (platformHooks()->emulationState) platformHooks()->emulationState(0);
}

void archEmulationStartFailure(void)
{
    platformFrontendMessage(FRONTEND_MESSAGE_ERROR, "The emulation failed to start.");
}

void archEmuSuspendSignal(void) {}
void archDiskQuickChangeNotify(void) {}

void archReplayMissing(const char* fileName)
{
    char msg[600];
    snprintf(msg, sizeof(msg), "Replay file not found: %s", fileName);
    platformFrontendMessage(FRONTEND_MESSAGE_WARNING, msg);
}

void archReplaySaveFailure(const char* fileName)
{
    char msg[600];
    snprintf(msg, sizeof(msg), "Could not save the replay: %s", fileName);
    platformFrontendMessage(FRONTEND_MESSAGE_ERROR, msg);
}

void archCaptureToastInfo(const char* message)
{
    if (message) platformFrontendMessage(FRONTEND_MESSAGE_INFO, message);
}

void archCaptureToastSaved(const char* savedPath)
{
    char msg[600];
    if (savedPath == NULL) return;
    snprintf(msg, sizeof(msg), "Saved %s", savedPath);
    platformFrontendMessage(FRONTEND_MESSAGE_INFO, msg);
}

/* Theme (skin) and child window hooks belong to the classic Win32 UI. */
void  archThemeSetNext(void) {}
void  archThemeUpdate(struct Theme* theme) {}
void* archWindowCreate(struct Theme* theme, int childWindow) { return NULL; }
void  archWindowApplyOwnership(void* hwnd) {}
void  archWindowApplyOwnershipAll(void) {}
void  archWindowStartMove(void) {}
void  archWindowMove(void) {}
void  archWindowEndMove(void) {}

/* ---- File choosers -------------------------------------------------------- */

#define ROM_FILTER  "ROM images|*.rom;*.ri;*.mx1;*.mx2;*.col;*.sg;*.sc;*.zip|All files|*"
#define DISK_FILTER "Disk images|*.dsk;*.di1;*.di2;*.360;*.720;*.sf7;*.zip|All files|*"
#define HD_FILTER   "Hard disk images|*.dsk;*.hdd;*.img;*.ide;*.zip|All files|*"
#define CAS_FILTER  "Cassette images|*.cas;*.wav;*.tsx;*.zip|All files|*"
#define STA_FILTER  "Saved states|*.sta|All files|*"
#define CAP_FILTER  "Replays|*.cap|All files|*"

static char chosen[1024];

static char* choose(FrontendFileKind kind, int index, int save, int directory,
                    int allowCreate, const char* title, const char* defaultDir,
                    const char* defaultName, const char* filters, char* rememberDir)
{
    FrontendFileRequest request;

    if (platformHooks()->chooseFile == NULL) {
        return NULL;
    }
    memset(&request, 0, sizeof(request));
    request.kind        = kind;
    request.index       = index;
    request.save        = save;
    request.directory   = directory;
    request.allowCreate = allowCreate;
    request.title       = title;
    request.defaultDir  = defaultDir;
    request.defaultName = defaultName;
    request.filters     = filters;

    if (!platformHooks()->chooseFile(&request, chosen, sizeof(chosen)) || chosen[0] == 0) {
        return NULL;
    }
    if (rememberDir != NULL) {
        char* slash;
        snprintf(rememberDir, PROP_MAXPATH, "%s", chosen);
        slash = strrchr(rememberDir, '/');
        if (slash != NULL && !directory) *slash = 0;
    }
    return chosen;
}

char* archFilenameGetOpenRom(Properties* properties, int cartSlot, RomType* romType)
{
    if (romType) *romType = ROM_UNKNOWN;
    return choose(FRONTEND_FILE_OPEN_ROM, cartSlot, 0, 0, 0, "Insert cartridge",
                  properties->cartridge.defDir, NULL, ROM_FILTER, properties->cartridge.defDir);
}

char* archFilenameGetOpenDisk(Properties* properties, int drive, int allowCreate)
{
    return choose(FRONTEND_FILE_OPEN_DISK, drive, 0, 0, allowCreate, "Insert disk",
                  properties->diskdrive.defDir, NULL, DISK_FILTER, properties->diskdrive.defDir);
}

char* archFilenameGetOpenHarddisk(Properties* properties, int drive, int allowCreate)
{
    return choose(FRONTEND_FILE_OPEN_HARDDISK, drive, 0, 0, allowCreate, "Insert hard disk",
                  properties->diskdrive.defHdDir, NULL, HD_FILTER, properties->diskdrive.defHdDir);
}

char* archDirnameGetOpenDisk(Properties* properties, int drive)
{
    return choose(FRONTEND_FILE_OPEN_DISK_DIR, drive, 0, 1, 0, "Insert directory as disk",
                  properties->diskdrive.defDir, NULL, NULL, properties->diskdrive.defDir);
}

char* archDirnameGetOpenDiskWithFormat(Properties* properties, int drive, int* outMsxFormat)
{
    if (outMsxFormat) *outMsxFormat = 0;
    return archDirnameGetOpenDisk(properties, drive);
}

char* archFilenameGetOpenCas(Properties* properties)
{
    return choose(FRONTEND_FILE_OPEN_CAS, 0, 0, 0, 0, "Insert cassette",
                  properties->cassette.defDir, NULL, CAS_FILTER, properties->cassette.defDir);
}

char* archFilenameGetNewCas(Properties* properties)
{
    return choose(FRONTEND_FILE_NEW_CAS, 0, 1, 0, 1, "New cassette image",
                  properties->cassette.defDir, "new.cas", CAS_FILTER, properties->cassette.defDir);
}

char* archFilenameGetSaveCas(Properties* properties, int* type)
{
    if (type) *type = 0;
    return choose(FRONTEND_FILE_SAVE_CAS, 0, 1, 0, 1, "Save cassette",
                  properties->cassette.defDir, NULL, CAS_FILTER, properties->cassette.defDir);
}

char* archFilenameGetOpenState(Properties* properties)
{
    return choose(FRONTEND_FILE_OPEN_STATE, 0, 0, 0, 0, "Load state",
                  NULL, NULL, STA_FILTER, NULL);
}

char* archFilenameGetSaveState(Properties* properties)
{
    return choose(FRONTEND_FILE_SAVE_STATE, 0, 1, 0, 1, "Save state",
                  NULL, NULL, STA_FILTER, NULL);
}

char* archFilenameGetOpenCapture(Properties* properties)
{
    return choose(FRONTEND_FILE_OPEN_CAPTURE, 0, 0, 0, 0, "Load replay",
                  NULL, NULL, CAP_FILTER, NULL);
}

char* archFilenameGetSaveCapture(Properties* properties, const char* title, const char* defaultDir,
                                 const char* defaultName, const char* extension,
                                 const char* fileTypeLabel)
{
    char filters[256];
    snprintf(filters, sizeof(filters), "%s|*%s|All files|*",
             fileTypeLabel ? fileTypeLabel : "Files", extension ? extension : "");
    return choose(FRONTEND_FILE_SAVE_CAPTURE, 0, 1, 0, 1, title, defaultDir, defaultName,
                  filters, NULL);
}

char* archFileSave(char* title, char* extensionList, char* defaultDir, char* extensions,
                   int* selectedExtension, char* defExt)
{
    if (selectedExtension) *selectedExtension = 0;
    return choose(FRONTEND_FILE_SAVE_GENERIC, 0, 1, 0, 1, title, defaultDir, NULL, NULL, NULL);
}

/* ---- One file out of a zip ------------------------------------------------ */

static char* chooseInZip(const char* title, const char* description, const char* zipName,
                         const char* fileList, int count, const char* current, int* autostart)
{
    static char chosen[512];
    const char** items;
    FrontendListRequest req;
    const char* p = fileList;
    int i;
    int index;

    if (platformHooks()->chooseFromList == NULL || fileList == NULL || count <= 0) {
        return NULL;
    }
    items = (const char**)calloc(count, sizeof(char*));
    if (items == NULL) return NULL;

    memset(&req, 0, sizeof(req));
    req.title = title;
    req.description = description;
    req.zipName = zipName;
    req.items = items;
    req.count = count;
    req.selected = -1;
    req.autostart = autostart ? *autostart : -1;
    for (i = 0; i < count; i++) {
        items[i] = p;
        if (current && strcmp(current, p) == 0) req.selected = i;
        p += strlen(p) + 1;
    }

    index = platformHooks()->chooseFromList(&req);
    if (index >= 0 && index < count) {
        snprintf(chosen, sizeof(chosen), "%s", items[index]);
        if (autostart && *autostart != -1) *autostart = req.autostart;
    }
    free(items);
    return index >= 0 && index < count ? chosen : NULL;
}

char* archFilenameGetOpenRomZip(Properties* properties, int cartSlot, const char* fname,
                                const char* fileList, int count, int* autostart, int* romType)
{
    char* name = chooseInZip(langDlgLoadRom(), langDlgLoadRomDesc(), fname, fileList, count,
                             properties->media.carts[cartSlot ? 1 : 0].fileNameInZip, autostart);
    if (name && romType) *romType = ROM_UNKNOWN;
    return name;
}

char* archFilenameGetOpenDiskZip(Properties* properties, int drive, const char* fname,
                                 const char* fileList, int count, int* autostart)
{
    return chooseInZip(langDlgLoadDsk(), langDlgLoadDskDesc(), fname, fileList, count,
                       properties->media.disks[drive ? 1 : 0].fileNameInZip, autostart);
}

char* archFilenameGetOpenCasZip(Properties* properties, const char* fname,
                                const char* fileList, int count, int* autostart)
{
    return chooseInZip(langDlgLoadCas(), langDlgLoadCasDesc(), fname, fileList, count,
                       properties->media.tapes[0].fileNameInZip, autostart);
}

char* archFilenameGetOpenAnyZip(Properties* properties, const char* fname,
                                const char* fileList, int count, int* autostart, int* romType)
{
    char* name = chooseInZip(langDlgLoadRomDskCas(), langDlgLoadRomDskCasDesc(), fname,
                             fileList, count, NULL, autostart);
    if (name && romType) *romType = ROM_UNKNOWN;
    return name;
}
