/*****************************************************************************
** blueMSX+ - Platform layer: start up, shut down and display
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "Platform.h"
#include "PlatformInternal.h"
#include "PlatformShortcuts.h"

#include "Actions.h"
#include "ArchEvent.h"
#include "ArchFile.h"
#include "ArchGlob.h"
#include "ArchMenu.h"
#include "ArchNotifications.h"
#include "ArchSound.h"
#include "AudioMixer.h"
#include "Board.h"
#include "Casette.h"
#include "CommandLine.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "FrameBuffer.h"
#include "JoystickPort.h"
#include "Language.h"
#include "LaunchFile.h"
#include "Machine.h"
#include "MediaDb.h"
#include "MidiIO.h"
#include "PrinterIO.h"
#include "Properties.h"
#include "SysRomLocator.h"
#include "UartIO.h"
#include "VideoRender.h"
#include "YM2413.h"
#include "Switches.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMDLINE_MAX 8192

static FrontendHooks hooks;
static Properties* properties = NULL;
static Video*      video = NULL;
static Mixer*      mixer = NULL;
static void*       displayAckEvent = NULL;
static SDL_AtomicInt pendingDisplay;

const FrontendHooks* platformHooks(void)          { return &hooks; }
struct Properties* platformGetProperties(void)    { return properties; }
struct Video*      platformGetVideo(void)         { return video; }
struct Mixer*      platformGetMixer(void)         { return mixer; }

void platformFrontendMouseCapture(int capture)
{
    if (hooks.mouseCapture) hooks.mouseCapture(capture);
}

void platformFrontendMessage(FrontendMessageKind kind, const char* text)
{
    if (hooks.message) hooks.message(kind, text);
    else fprintf(stderr, "blueMSX+: %s\n", text);
}

/* ---- Directories ---------------------------------------------------------- */

/* A writable folder below the configuration directory, created if missing. */
static char* userDir(char* out, const char* name)
{
    platformPathJoin(out, PLATFORM_PATH_MAX, platformPaths()->configDir, name);
    platformMakeDirs(out);
    return out;
}

/* Shipped defaults are named "*.default" so that a new release never
** overwrites edited copies: each one is copied once, without the suffix. */
static void seedTemplates(const char* srcDir, const char* dstDir)
{
    char pattern[PLATFORM_PATH_MAX];
    ArchGlob* glob;
    int i;

    platformPathJoin(pattern, sizeof(pattern), srcDir, "*.default");
    glob = archGlob(pattern, ARCH_GLOB_FILES);
    if (glob == NULL) return;
    for (i = 0; i < glob->count; i++) {
        const char* base = strrchr(glob->pathVector[i], '/');
        char dst[PLATFORM_PATH_MAX];
        FILE* in;
        FILE* out;
        size_t len;

        base = base ? base + 1 : glob->pathVector[i];
        len = strlen(base) - strlen(".default");
        snprintf(dst, sizeof(dst), "%s/%.*s", dstDir, (int)len, base);
        if (len == 0 || platformIsFile(dst)) continue;
        in = fopen(glob->pathVector[i], "rb");
        out = in ? fopen(dst, "wb") : NULL;
        if (in && out) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        }
        if (out) fclose(out);
        if (in) fclose(in);
    }
    archGlobFree(glob);
}

static void setupDirectories(void)
{
    const PlatformPaths* p = platformPaths();
    char dir[PLATFORM_PATH_MAX];
    char file[PLATFORM_PATH_MAX];
    int i;

    propertiesSetDirectory(p->configDir, p->configDir);

    /* Machines: the user's own tree first (writable, used by the machine
    ** editor), then the classic ~/.blueMSX one, then the shipped one. */
    machineSetDirectory(userDir(dir, "Machines"));
    if (p->legacyDir[0]) {
        sysRomAddMachineDir(platformPathJoin(dir, sizeof(dir), p->legacyDir, "Machines"));
    }
    if (p->dataDir[0]) {
        sysRomAddMachineDir(platformPathJoin(dir, sizeof(dir), p->dataDir, "Machines"));
    }

    /* System ROMs with any file name: the user's systemroms folders and
    ** openMSX's, matched by SHA1 or by name. */
    sysRomAddSearchDir(userDir(dir, "systemroms"));
    if (p->legacyDir[0]) {
        platformPathJoin(dir, sizeof(dir), p->legacyDir, "systemroms");
        if (platformIsDir(dir)) sysRomAddSearchDir(dir);
    }
    for (i = 0; i < 2; i++) {
        if (p->openMsxRomDirs[i][0]) sysRomAddSearchDir(p->openMsxRomDirs[i]);
    }
    sysRomSetCacheFile(platformPathJoin(file, sizeof(file), p->configDir, "sysromcache.txt"));

    /* Databases: a user copy wins over the shipped one. */
    platformPathJoin(dir, sizeof(dir), p->configDir, "Databases");
    if (!platformIsDir(dir) && p->dataDir[0]) {
        platformPathJoin(dir, sizeof(dir), p->dataDir, "Databases");
    }
    sysRomSetHashTable(platformPathJoin(file, sizeof(file), dir, "sysromsha1.txt"));
    mediaDbLoad(dir);
    mediaDbCreateRomdb();
    mediaDbCreateDiskdb();
    mediaDbCreateCasdb();

    actionSetAudioCaptureSetDirectory(userDir(dir, "Audio Capture"), "");
    actionSetVideoCaptureSetDirectory(userDir(dir, "Video Capture"), "");
    actionSetQuickSaveSetDirectory(userDir(dir, "QuickSave"), "");
    boardSetDirectory(userDir(dir, "SRAM"));
    tapeSetDirectory(userDir(dir, "Casinfo"), "");
    shortcutsSetDirectory(userDir(dir, "Shortcut Profiles"));
    if (p->dataDir[0]) {
        char src[PLATFORM_PATH_MAX];
        seedTemplates(platformPathJoin(src, sizeof(src), p->dataDir, "Shortcut Profiles"), dir);
    }
}

/* ---- Start and stop ------------------------------------------------------- */

static void buildCommandLine(int argc, char** argv, char* out, int outSize)
{
    int i;
    out[0] = 0;
    for (i = 1; i < argc; i++) {
        int quote = strchr(argv[i], ' ') != NULL && argv[i][0] != '"';
        size_t len = strlen(out);
        snprintf(out + len, outSize - len, "%s%s%s ", quote ? "\"" : "", argv[i], quote ? "\"" : "");
    }
}

int platformInit(int argc, char** argv, const FrontendHooks* frontendHooks)
{
    static char cmdLine[CMDLINE_MAX];
    int resetProperties;
    int i;

    memset(&hooks, 0, sizeof(hooks));
    if (frontendHooks != NULL) hooks = *frontendHooks;
    SDL_SetAtomicInt(&pendingDisplay, 0);

    buildCommandLine(argc, argv, cmdLine, sizeof(cmdLine));

    if (emuCheckHelpArgument(cmdLine)) {
        static char help[16384];
        emuCommandLineGetHelpText(help, sizeof(help));
        for (i = 0; help[i]; i++) if (help[i] != '\r') fputc(help[i], stdout);
        return 1;
    }

    if (platformPathsInit(argc > 0 ? argv[0] : NULL) != 0) {
        return -1;
    }
    setupDirectories();

    resetProperties = emuCheckResetArgument(cmdLine);
    properties = propCreate(resetProperties, 0, P_KBD_EUROPEAN, 0, "");
    if (resetProperties == 2) {
        propDestroy(properties);
        properties = NULL;
        return 1;
    }

    /* The legacy SDL display path syncs on emulated frames. Host vblank sync
    ** comes with the shared GL renderer. */
    properties->emulation.syncMethod = P_EMU_SYNCFRAMES;

    if (cmdLine[0] && !emuCheckSettingArguments(properties, cmdLine)) {
        platformFrontendMessage(FRONTEND_MESSAGE_ERROR, emuCommandLineGetError());
        return -1;
    }

    video = videoCreate();
    videoSetColors(video, properties->video.saturation, properties->video.brightness,
                   properties->video.contrast, properties->video.gamma);
    videoSetScanLines(video, properties->video.scanlinesEnable, properties->video.scanlinesPct);
    videoSetColorSaturation(video, properties->video.colorSaturationEnable,
                            properties->video.colorSaturationWidth);

    displayAckEvent = archEventCreate(0);
    platformInputInit();

    mixer = mixerCreate();
    emulatorInit(properties, mixer);
    actionInit(video, properties, mixer);
    langInit();
    tapeSetReadOnly(properties->cassette.readOnly);
    langSetLanguage(properties->language);

    joystickPortSetType(0, properties->joy1.typeId);
    joystickPortSetType(1, properties->joy2.typeId);

    printerIoSetType(properties->ports.Lpt.type, properties->ports.Lpt.fileName);
    uartIoSetType(properties->ports.Com.type, properties->ports.Com.fileName);
    midiIoSetMidiOutType(properties->sound.MidiOut.type, properties->sound.MidiOut.fileName);
    midiIoSetMidiInType(properties->sound.MidiIn.type, properties->sound.MidiIn.fileName);
    ykIoSetMidiInType(properties->sound.YkIn.type, properties->sound.YkIn.fileName);

    emulatorRestartSound();

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerSetChannelTypeVolume(mixer, i, properties->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(mixer, i, properties->sound.mixerChannel[i].pan);
        mixerEnableChannelType(mixer, i, properties->sound.mixerChannel[i].enable);
    }
    mixerSetMasterVolume(mixer, properties->sound.masterVolume);
    mixerEnableMaster(mixer, properties->sound.masterEnable);

    videoUpdateAll(video, properties);
    platformShortcutsLoad(properties->emulation.shortcutProfile);
    mediaDbSetDefaultRomType(properties->cartridge.defaultType);

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        if (properties->media.carts[i].fileName[0]) {
            insertCartridge(properties, i, properties->media.carts[i].fileName,
                            properties->media.carts[i].fileNameInZip,
                            properties->media.carts[i].type, -1);
        }
        updateExtendedRomName(i, properties->media.carts[i].fileName,
                              properties->media.carts[i].fileNameInZip);
    }
    for (i = 0; i < PROP_MAX_DISKS; i++) {
        if (properties->media.disks[i].fileName[0]) {
            insertDiskette(properties, i, properties->media.disks[i].fileName,
                           properties->media.disks[i].fileNameInZip, -1);
        }
        updateExtendedDiskName(i, properties->media.disks[i].fileName,
                               properties->media.disks[i].fileNameInZip);
    }
    for (i = 0; i < PROP_MAX_TAPES; i++) {
        if (properties->media.tapes[i].fileName[0]) {
            insertCassette(properties, i, properties->media.tapes[i].fileName,
                           properties->media.tapes[i].fileNameInZip, 0);
        }
        updateExtendedCasName(i, properties->media.tapes[i].fileName,
                              properties->media.tapes[i].fileNameInZip);
    }

    {
        Machine* machine = machineCreate(properties->emulation.machineName);
        if (machine != NULL) {
            boardSetMachine(machine);
            machineDestroy(machine);
        }
    }
    boardSetFdcTimingEnable(properties->emulation.enableFdcTiming);
    boardSetY8950Enable(properties->sound.chip.enableY8950);
    boardSetYm2413Enable(properties->sound.chip.enableYM2413);
    boardSetMoonsoundEnable(properties->sound.chip.enableMoonsound);
    boardSetVideoAutodetect(properties->video.detectActiveMonitor);

    i = emuTryStartWithArguments(properties, cmdLine, NULL);
    if (i < 0) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Command line: %s", emuCommandLineGetError());
        platformFrontendMessage(FRONTEND_MESSAGE_ERROR, msg);
        return -1;
    }
    if (i == 0) {
        emulatorStart(NULL);
    }
    return 0;
}

/* ---- Settings changed by a frontend dialog -------------------------------- */

static int soundNeedsRestart(const Properties* a, const Properties* b)
{
    return a->sound.bufSize              != b->sound.bufSize
        || a->sound.chip.enableY8950     != b->sound.chip.enableY8950
        || a->sound.chip.enableYM2413    != b->sound.chip.enableYM2413
        || a->sound.chip.enableMoonsound != b->sound.chip.enableMoonsound
        || a->sound.stereo               != b->sound.stereo;
}

void platformSaveProperties(void)
{
    if (properties != NULL) propSave(properties);
}

void platformApplyProperties(const Properties* old)
{
    Properties* p = properties;
    int i;

    if (p == NULL || old == NULL) return;

    /* An explicit change makes command line values permanent. */
    emuCommandLineDropOverrides();

    videoSetColors(video, p->video.saturation, p->video.brightness,
                   p->video.contrast, p->video.gamma);
    videoSetScanLines(video, p->video.scanlinesEnable, p->video.scanlinesPct);
    videoSetColorSaturation(video, p->video.colorSaturationEnable,
                            p->video.colorSaturationWidth);
    videoUpdateAll(video, p);
    mediaDbSetDefaultRomType(p->cartridge.defaultType);

    if (p->ports.Lpt.type != old->ports.Lpt.type ||
        strcmp(p->ports.Lpt.fileName, old->ports.Lpt.fileName) != 0) {
        printerIoSetType(p->ports.Lpt.type, p->ports.Lpt.fileName);
    }
    if (p->ports.Com.type != old->ports.Com.type ||
        strcmp(p->ports.Com.fileName, old->ports.Com.fileName) != 0) {
        uartIoSetType(p->ports.Com.type, p->ports.Com.fileName);
    }
    if (p->sound.MidiOut.type != old->sound.MidiOut.type ||
        strcmp(p->sound.MidiOut.fileName, old->sound.MidiOut.fileName) != 0) {
        midiIoSetMidiOutType(p->sound.MidiOut.type, p->sound.MidiOut.fileName);
    }
    if (p->sound.MidiIn.type != old->sound.MidiIn.type ||
        strcmp(p->sound.MidiIn.fileName, old->sound.MidiIn.fileName) != 0) {
        midiIoSetMidiInType(p->sound.MidiIn.type, p->sound.MidiIn.fileName);
    }

    boardSetFdcTimingEnable(p->emulation.enableFdcTiming);
    boardSetHddSdBoostEnable(p->emulation.enableHddSdBoost);
    boardSetCasBoostEnable(p->emulation.enableCasBoost);
    boardSetNoSpriteLimits(p->emulation.noSpriteLimits);
    boardSetVdpCmdSpeed(p->emulation.vdpCmdSpeed);
    boardSetVideoAutodetect(p->video.detectActiveMonitor);
    switchSetAudio(p->emulation.audioSwitch);
    switchSetFront(p->emulation.frontSwitch);
    switchSetPause(p->emulation.pauseSwitch);
    emulatorSetFrequency(p->emulation.speed, NULL);

    if (soundNeedsRestart(old, p)) {
        emulatorRestartSound();
    }
    if (old->sound.chip.enableY8950     != p->sound.chip.enableY8950 ||
        old->sound.chip.enableYM2413    != p->sound.chip.enableYM2413 ||
        old->sound.chip.enableMoonsound != p->sound.chip.enableMoonsound) {
        boardSetY8950Enable(p->sound.chip.enableY8950);
        boardSetYm2413Enable(p->sound.chip.enableYM2413);
        boardSetMoonsoundEnable(p->sound.chip.enableMoonsound);
    }

    /* OPLL analog filter: a preset overwrites the stored cutoffs, Custom keeps
    ** the slider value; the live YM2413 picks the change up at once. */
    if (old->sound.chip.ym2413AnalogFilterMode  != p->sound.chip.ym2413AnalogFilterMode ||
        old->sound.chip.ym2413AnalogFilterLpfHz != p->sound.chip.ym2413AnalogFilterLpfHz ||
        old->sound.chip.ym2413AnalogFilterHpfHz != p->sound.chip.ym2413AnalogFilterHpfHz) {
        int lpf = 0, hpf = 0;
        propertiesGetOpllFilterHz(p->sound.chip.ym2413AnalogFilterMode, &p->sound.chip, &lpf, &hpf);
        if (p->sound.chip.ym2413AnalogFilterMode != PROP_OPLL_FILTER_CUSTOM) {
            p->sound.chip.ym2413AnalogFilterLpfHz = lpf;
            p->sound.chip.ym2413AnalogFilterHpfHz = hpf;
        }
        ym2413AnalogFilterSet(lpf, hpf);
    }

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerSetChannelTypeVolume(mixer, i, p->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(mixer, i, p->sound.mixerChannel[i].pan);
        mixerEnableChannelType(mixer, i, p->sound.mixerChannel[i].enable);
    }
    mixerSetMasterVolume(mixer, p->sound.masterVolume);
    mixerEnableMaster(mixer, p->sound.masterEnable);

    if (p->language != old->language) {
        langSetLanguage(p->language);
    }
    if (strcmp(p->emulation.shortcutProfile, old->emulation.shortcutProfile) != 0) {
        platformShortcutsLoad(p->emulation.shortcutProfile);
    }

    if (p->video.windowSize != old->video.windowSize ||
        p->video.horizontalStretch != old->video.horizontalStretch ||
        p->video.verticalStretch != old->video.verticalStretch) {
        archUpdateWindow();
    }
    archUpdateMenu(0);
}

void platformShutdown(void)
{
    if (properties == NULL) {
        return;
    }
    emulatorStop();
    emulatorExit();

    /* Wake a frame wait that may still be pending. */
    if (displayAckEvent != NULL) archEventSet(displayAckEvent);

    propSave(properties);
    platformShortcutsFree();
    platformInputShutdown();
    archSoundDestroy();
    videoDestroy(video);
    mixerDestroy(mixer);
    propDestroy(properties);
    archEventDestroy(displayAckEvent);
    properties = NULL;
    video = NULL;
    mixer = NULL;
    displayAckEvent = NULL;
}

/* ---- Display -------------------------------------------------------------- */

static int lastMaxWidth = PLATFORM_SCREEN_WIDTH;

int platformRenderFrame(void* dst, int pitch, int zoom)
{
    FrameBuffer* frameBuffer;
    int borderWidth;

    frameBuffer = frameBufferFlipViewFrame(properties->emulation.syncMethod == P_EMU_SYNCTOVBLANKASYNC);
    if (frameBuffer == NULL) {
        frameBuffer = frameBufferGetWhiteNoiseFrame();
    }

    /* videoRender takes the unzoomed border in bytes and clears it itself. */
    lastMaxWidth = frameBuffer->maxWidth;
    borderWidth = (PLATFORM_SCREEN_WIDTH - frameBuffer->maxWidth) / 2;
    return videoRender(video, frameBuffer, 32, zoom, dst,
                       borderWidth > 0 ? borderWidth * 4 : 0, pitch, -1);
}

void platformScreenSourceRect(int zoom, int* x, int* y, int* w, int* h)
{
    int border = 0;
    int crop = 0;

    if (properties != NULL && properties->video.horizontalStretch &&
        lastMaxWidth > 0 && lastMaxWidth < PLATFORM_SCREEN_WIDTH) {
        border = (PLATFORM_SCREEN_WIDTH - lastMaxWidth) / 2;
    }
    if (properties != NULL && properties->video.verticalStretch) {
        crop = 7;
    }
    *x = border * zoom;
    *y = crop * zoom;
    *w = (PLATFORM_SCREEN_WIDTH - 2 * border) * zoom;
    *h = (PLATFORM_SCREEN_HEIGHT - 2 * crop) * zoom;
}

void platformDisplayDone(void)
{
    SDL_AddAtomicInt(&pendingDisplay, -1);
    archEventSet(displayAckEvent);
}

int archUpdateEmuDisplay(int synchronous)
{
    if (SDL_GetAtomicInt(&pendingDisplay) > 1 || hooks.displayUpdate == NULL) {
        return 1;
    }
    SDL_AddAtomicInt(&pendingDisplay, 1);
    hooks.displayUpdate();

    if (properties != NULL && properties->emulation.syncMethod == P_EMU_SYNCFRAMES) {
        archEventWait(displayAckEvent, 500);
    }
    return 1;
}

void archUpdateEmuDisplayConfig(void) {}
void archPumpEmuDisplay(void) {}
int  archGetFramesPerSecond(void) { return 60; }

void archUpdateWindow(void)
{
    if (hooks.windowUpdate) hooks.windowUpdate();
}

void archVideoOutputChange(void)
{
    if (hooks.windowUpdate) hooks.windowUpdate();
}
