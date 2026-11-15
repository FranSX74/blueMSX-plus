/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Sdl/bluemsxlite.c,v $
**
** $Revision: 1.29 $
**
** $Date: 2008-04-18 04:09:54 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
/* SDL2 port: rendering through SDL_Renderer, no legacy OpenGL path */

#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>

#include "CommandLine.h"
#include "Properties.h"
#include "ArchFile.h"
#include "VideoRender.h"
#include "AudioMixer.h"
#include "Casette.h"
#include "PrinterIO.h"
#include "UartIO.h"
#include "MidiIO.h"
#include "Machine.h"
#include "Board.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Actions.h"
#include "Language.h"
#include "LaunchFile.h"
#include "ArchEvent.h"
#include "ArchSound.h"
#include "ArchNotifications.h"
#include "JoystickPort.h"
#include "SdlShortcuts.h"
#include "SdlMouse.h"
#ifdef ENABLE_OPENGL
#include <SDL/SDL_opengl.h>
#endif

void keyboardSetDirectory(char* directory);
void keyboardInit();
void keyboardSetFocus(int handle, int focus);
void keyboardUpdate();
int keyboardGetModifiers();
static void handleEvent(SDL_Event* event);

static Properties* properties;
static Video* video;
static Mixer* mixer;

static Properties* properties;
static Video* video;
static Mixer* mixer;
static Shortcuts* shortcuts;
static int doQuit = 0;

static int pendingDisplayEvents = 0;
static void* dpyUpdateAckEvent = NULL;

static SDL_Surface *surface;
static int   bitDepth;
static int   zoom = 1;
static char* displayData[2] = { NULL, NULL };
static int   curDisplayData = 0;
static int   displayPitch = 0;
#ifdef ENABLE_OPENGL
static GLfloat texCoordX;
static GLfloat texCoordY;
static GLuint textureId;
#endif

#define WIDTH  320
#define HEIGHT 240

#define EVENT_UPDATE_DISPLAY 2
#define EVENT_UPDATE_WINDOW  3

static SDL_Window*   sdlWindow   = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Texture*  sdlTexture  = NULL;
static int texWidth = 0, texHeight = 0;

void createSdlSurface(int width, int height, int fullscreen)
{
    bitDepth = 32;
    if (sdlWindow == NULL) {
        sdlWindow = SDL_CreateWindow("blueMSX+ (Linux/SDL2)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (sdlWindow == NULL) return;
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (sdlRenderer == NULL) sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
    }
    else {
        SDL_SetWindowSize(sdlWindow, width, height);
    }
    SDL_SetWindowFullscreen(sdlWindow, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_RenderSetLogicalSize(sdlRenderer, width, height);

    if (sdlTexture == NULL || texWidth != width || texHeight != height) {
        if (sdlTexture) SDL_DestroyTexture(sdlTexture);
        sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        texWidth = width; texHeight = height;
        free(displayData[0]);
        displayPitch = width * 4;
        displayData[0] = (char*)calloc(1, displayPitch * height);
        curDisplayData = 0;
    }
    surface = (SDL_Surface*)sdlTexture; /* non-NULL marker for the legacy code paths */
}

int createSdlWindow()
{
    const char *title = "blueMSXlite";
    int fullscreen = properties->video.windowSize == P_VIDEO_SIZEFULLSCREEN;
    int width;
    int height;

    if (fullscreen) {
        zoom = properties->video.fullscreen.width / WIDTH;
        bitDepth = properties->video.fullscreen.bitDepth;
    }
    else {
        if (properties->video.windowSize == P_VIDEO_SIZEX1) {
            zoom = 1;
        }
        else {
            zoom = 2;
        }
        bitDepth = 32;
    }

    width  = zoom * WIDTH;
    height = zoom * HEIGHT;

    surface = NULL;

#ifdef ENABLE_OPENGL
    if (properties->video.driver != P_VIDEO_DRVGDI) {
        createSdlGlSurface(width, height, fullscreen);
        if (surface == NULL) {
            properties->video.driver = P_VIDEO_DRVGDI;
        }
    }
    // Invert 24 bit RGB in GL mode
    videoSetRgbMode(video, properties->video.driver != P_VIDEO_DRVGDI);
#endif
    if (surface == NULL) {
        createSdlSurface(width, height, fullscreen);
    }


    // Update mouse capture region
    sdlMouseSetCaptureRect(0, 0, width, height);
    
    return 1;
}

static int isLineDirty(int y, int lines) {
    int width = zoom * WIDTH * bitDepth / 8 / sizeof(UInt32);

    UInt32* d0 = (UInt32*)(displayData[0]) + width * y;
    UInt32* d1 = (UInt32*)(displayData[1]) + width * y;
    UInt32  cmp = 0;
    UInt32  i = lines * width / 8;

    while (i-- && !cmp) {
        cmp |= d0[0] - d1[0];
        cmp |= d0[1] - d1[1];
        cmp |= d0[2] - d1[2];
        cmp |= d0[3] - d1[3];
        cmp |= d0[4] - d1[4];
        cmp |= d0[5] - d1[5];
        cmp |= d0[6] - d1[6];
        cmp |= d0[7] - d1[7];
        d0 += 8;
        d1 += 8;
    }
    return cmp;
}

int updateEmuDisplay(int updateAll) 
{
    FrameBuffer* frameBuffer;
    int bytesPerPixel = bitDepth / 8;
    char* dpyData  = displayData[curDisplayData];
    int width  = zoom * WIDTH;
    int height = zoom * HEIGHT;
    int borderWidth;

    frameBuffer = frameBufferFlipViewFrame(properties->emulation.syncMethod == P_EMU_SYNCTOVBLANKASYNC);
    if (frameBuffer == NULL) {
        frameBuffer = frameBufferGetWhiteNoiseFrame();
    }

    borderWidth = (320 - frameBuffer->maxWidth) * zoom / 2;

#ifdef ENABLE_OPENGL
    if (properties->video.driver != P_VIDEO_DRVGDI) {
        const int linesPerBlock = 4;
        GLfloat coordX = texCoordX;
        GLfloat coordY = texCoordY;
        int y; 
        int isDirty = 0; 

        if (properties->video.horizontalStretch) {
            coordX = texCoordX * (width - 2 * borderWidth) / width;
            borderWidth = 0;
        }

        videoRender(video, frameBuffer, bitDepth, zoom,
                    dpyData + borderWidth * bytesPerPixel, 0, displayPitch, -1);

        if (borderWidth > 0) {
            int h = height;
            while (h--) {
                memset(dpyData, 0, borderWidth * bytesPerPixel);
                memset(dpyData + (width - borderWidth) * bytesPerPixel, 0, borderWidth * bytesPerPixel);
                dpyData += displayPitch;
            }
        }

        updateAll |= properties->video.driver == P_VIDEO_DRVDIRECTX;

        for (y = 0; y < height; y += linesPerBlock) {
            if (updateAll || isLineDirty(y, linesPerBlock)) {
                if (!isDirty) {
                    glEnable(GL_TEXTURE_2D);
                    glEnable(GL_ASYNC_TEX_IMAGE_SGIX);
	                glBindTexture(GL_TEXTURE_2D, textureId);

	                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                    isDirty = 1;
                }

                if (bitDepth == 16) {
		            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, width, linesPerBlock,
		                            GL_RGB, GL_UNSIGNED_SHORT_5_6_5, displayData[curDisplayData] + y * displayPitch);
	            } 
                else {
		            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, width, linesPerBlock,
		                            GL_RGBA, GL_UNSIGNED_BYTE, displayData[curDisplayData] + y * displayPitch);
	            }
            }
        }

        if (isDirty) {
            glBegin(GL_QUADS);
	        glTexCoord2f(0,      coordY); glVertex2i(0,     height);
	        glTexCoord2f(coordX, coordY); glVertex2i(width, height);
	        glTexCoord2f(coordX, 0     ); glVertex2i(width, 0     );
	        glTexCoord2f(0,      0     ); glVertex2i(0,     0     );
	        glEnd();
            glDisable(GL_TEXTURE_2D);
            
	        SDL_GL_SwapBuffers();
        }

        curDisplayData ^= 1;

        return 0;
    }
#endif

    /* blueMSX+ videoRender takes the unzoomed border in bytes and clears it itself */
    videoRender(video, frameBuffer, bitDepth, zoom, dpyData,
                borderWidth > 0 ? borderWidth / zoom * bytesPerPixel : 0, displayPitch, -1);


    SDL_UpdateTexture(sdlTexture, NULL, displayData[curDisplayData], displayPitch);
    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);

    return 0; 
}

int  archUpdateEmuDisplay(int syncMode) 
{
    SDL_Event event;

    if (pendingDisplayEvents > 1) {
        return 1;
    }

    pendingDisplayEvents++;

    event.type = SDL_USEREVENT;
    event.user.code = EVENT_UPDATE_DISPLAY;
    event.user.data1 = NULL;
    event.user.data2 = NULL;
    SDL_PushEvent(&event);

#ifndef SINGLE_THREADED
    if (properties->emulation.syncMethod == P_EMU_SYNCFRAMES) {
        archEventWait(dpyUpdateAckEvent, 500);
    }
#endif
    return 1;
}

void archUpdateWindow() 
{
    SDL_Event event;

    event.type = SDL_USEREVENT;
    event.user.code = EVENT_UPDATE_WINDOW;
    event.user.data1 = NULL;
    event.user.data2 = NULL;
    SDL_PushEvent(&event);
}

#ifdef SINGLE_THREADED
int archPollEvent()
{
    SDL_Event event;

    while(SDL_PollEvent(&event)) {
        if( event.type == SDL_QUIT ) {
            doQuit = 1;
        }
        else {
            handleEvent(&event);
        }
    }
    return doQuit;
}
#endif

void archEmulationStartNotification() 
{
}

void archEmulationStopNotification() 
{
#ifdef RUN_EMU_ONCE_ONLY
    doQuit = 1;
#endif
}

void archEmulationStartFailure() 
{
#ifdef RUN_EMU_ONCE_ONLY
    doQuit = 1;
#endif
}

void archTrap(UInt8 value)
{
}

void setDefaultPaths(const char* rootDir)
{   
    char buffer[512];  

    propertiesSetDirectory(rootDir, rootDir);

    sprintf(buffer, "%s/Machines", rootDir);
    machineSetDirectory(buffer);

    sprintf(buffer, "%s/Audio Capture", rootDir);
    archCreateDirectory(buffer);
    actionSetAudioCaptureSetDirectory(buffer, "");

    sprintf(buffer, "%s/Video Capture", rootDir);
    archCreateDirectory(buffer);
    actionSetVideoCaptureSetDirectory(buffer, "");

    sprintf(buffer, "%s/QuickSave", rootDir);
    archCreateDirectory(buffer);
    actionSetQuickSaveSetDirectory(buffer, "");

    sprintf(buffer, "%s/SRAM", rootDir);
    archCreateDirectory(buffer);
    boardSetDirectory(buffer);

    sprintf(buffer, "%s/Casinfo", rootDir);
    archCreateDirectory(buffer);
    tapeSetDirectory(buffer, "");

    sprintf(buffer, "%s/Databases", rootDir);
    archCreateDirectory(buffer);
    mediaDbLoad(buffer);
    
    sprintf(buffer, "%s/Keyboard Config", rootDir);
    archCreateDirectory(buffer);
    keyboardSetDirectory(buffer);

    sprintf(buffer, "%s/Shortcut Profiles", rootDir);
    archCreateDirectory(buffer);
    shortcutsSetDirectory(buffer);
}

static void handleEvent(SDL_Event* event) 
{
    switch (event->type) {
    case SDL_USEREVENT:
        switch (event->user.code) {
        case EVENT_UPDATE_DISPLAY:
            updateEmuDisplay(0);
            archEventSet(dpyUpdateAckEvent);
            pendingDisplayEvents--;
            break;
        case EVENT_UPDATE_WINDOW:
            if (!createSdlWindow()) {
                exit(0);
            }
            break;
        }
        break;
    case SDL_WINDOWEVENT:
        switch (event->window.event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED: keyboardSetFocus(1, 1); break;
        case SDL_WINDOWEVENT_FOCUS_LOST:   keyboardSetFocus(1, 0); break;
        case SDL_WINDOWEVENT_ENTER:        sdlMouseSetFocus(1); break;
        case SDL_WINDOWEVENT_LEAVE:        sdlMouseSetFocus(0); break;
        case SDL_WINDOWEVENT_EXPOSED:      updateEmuDisplay(1); break;
        }
        break;
    case SDL_KEYDOWN:
        shortcutCheckDown(shortcuts, HOTKEY_TYPE_KEYBOARD, keyboardGetModifiers(), event->key.keysym.scancode);
        break;
    case SDL_KEYUP:
        shortcutCheckUp(shortcuts, HOTKEY_TYPE_KEYBOARD, keyboardGetModifiers(), event->key.keysym.scancode);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        sdlMouseButton(event->button.button, event->button.state);
        break;
    case SDL_MOUSEMOTION:
        sdlMouseMove(event->motion.x, event->motion.y);
        break;
    }
}

int main(int argc, char **argv)
{
    SDL_Event event;
    char szLine[8192] = "";
    int resetProperties;
    char path[512] = "";
    int i;

    SDL_Init( SDL_INIT_EVERYTHING );

    for (i = 1; i < argc; i++) {
        if (strchr(argv[i], ' ') != NULL && argv[i][0] != '\"') {
            strcat(szLine, "\"");
            strcat(szLine, argv[i]);
            strcat(szLine, "\"");
        }
        else {
            strcat(szLine, argv[i]);
        }
        strcat(szLine, " ");
    }

    setDefaultPaths(archGetCurrentDirectory());

    resetProperties = emuCheckResetArgument(szLine);
    strcat(path, archGetCurrentDirectory());
    strcat(path, DIR_SEPARATOR "bluemsx.ini");
    properties = propCreate(resetProperties, 0, P_KBD_EUROPEAN, 0, "");
    
    properties->emulation.syncMethod = P_EMU_SYNCFRAMES;    
//    properties->emulation.syncMethod = P_EMU_SYNCTOVBLANKASYNC;

    if (resetProperties == 2) {
        propDestroy(properties);
        return 0;
    }
    
    video = videoCreate();
    videoSetColors(video, properties->video.saturation, properties->video.brightness, 
                  properties->video.contrast, properties->video.gamma);
    videoSetScanLines(video, properties->video.scanlinesEnable, properties->video.scanlinesPct);
    videoSetColorSaturation(video, properties->video.colorSaturationEnable, properties->video.colorSaturationWidth);
    
    bitDepth = 32;
    if (!createSdlWindow()) {
        return 0;
    }
    
    dpyUpdateAckEvent = archEventCreate(0);

    keyboardInit();

    mixer = mixerCreate();
    
    emulatorInit(properties, mixer);
    actionInit(video, properties, mixer);
    langInit();
    tapeSetReadOnly(properties->cassette.readOnly);
    
    langSetLanguage(properties->language);
    
    joystickPortSetType(0, properties->joy1.typeId);
    joystickPortSetType(1, properties->joy2.typeId);

    printerIoSetType(properties->ports.Lpt.type, properties->ports.Lpt.fileName);
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
    
    shortcuts = shortcutsCreate();

    mediaDbSetDefaultRomType(properties->cartridge.defaultType);

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        if (properties->media.carts[i].fileName[0]) insertCartridge(properties, i, properties->media.carts[i].fileName, properties->media.carts[i].fileNameInZip, properties->media.carts[i].type, -1);
        updateExtendedRomName(i, properties->media.carts[i].fileName, properties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        if (properties->media.disks[i].fileName[0]) insertDiskette(properties, i, properties->media.disks[i].fileName, properties->media.disks[i].fileNameInZip, -1);
        updateExtendedDiskName(i, properties->media.disks[i].fileName, properties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        if (properties->media.tapes[i].fileName[0]) insertCassette(properties, i, properties->media.tapes[i].fileName, properties->media.tapes[i].fileNameInZip, 0);
        updateExtendedCasName(i, properties->media.tapes[i].fileName, properties->media.tapes[i].fileNameInZip);
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

    i = emuTryStartWithArguments(properties, szLine, NULL);
    if (i < 0) {
        printf("Failed to parse command line: %s\n", emuCommandLineGetError());
        return 0;
    }
    
    if (i == 0) {
        emulatorStart(NULL);
    }

    //While the user hasn't quit
    while(!doQuit) {
        SDL_WaitEvent(&event);
        do {
            if( event.type == SDL_QUIT ) {
                doQuit = 1;
            }
            else {
                handleEvent(&event);
            }
        } while(SDL_PollEvent(&event));
    }

	// Stop the emulation thread before tearing down what it uses.
	emulatorStop();
	emulatorExit();

	if (SDL_WasInit(SDL_INIT_EVERYTHING)) {
		SDL_Quit(); 
	}

    videoDestroy(video);
    propDestroy(properties);
    archSoundDestroy();
    mixerDestroy(mixer);
    
    return 0;
}
