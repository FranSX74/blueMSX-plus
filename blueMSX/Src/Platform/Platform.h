/*****************************************************************************
** blueMSX+ - Platform layer: API for frontends
**
** A frontend calls platformInit() with its hooks, runs its own event loop,
** forwards input with the platformKey/Mouse functions, draws frames with
** platformRenderFrame() when told to, and calls platformShutdown() at exit.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef PLATFORM_H
#define PLATFORM_H

#include "Frontend.h"
#include "PlatformPaths.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Properties;
struct Video;
struct Mixer;

/* Width and height of the emulated screen at zoom 1. */
#define PLATFORM_SCREEN_WIDTH  320
#define PLATFORM_SCREEN_HEIGHT 240

/* Sets up paths, settings, sound and the emulator, applies the command line
** and starts the emulation. Returns 0 on success, 1 when the program should
** exit normally (e.g. /help), -1 on error (message already reported). */
int  platformInit(int argc, char** argv, const FrontendHooks* hooks);

/* Stops the emulation and frees everything platformInit created. */
void platformShutdown(void);

struct Properties* platformGetProperties(void);

/* Applies settings a frontend dialog changed in platformGetProperties()
** (video, sound, mixer, ports, switches, language, shortcut profile). `old`
** is a copy taken before the change. Settings are saved at exit, or now with
** platformSaveProperties. */
void platformApplyProperties(const struct Properties* old);
void platformSaveProperties(void);
struct Video*      platformGetVideo(void);
struct Mixer*      platformGetMixer(void);

/* ---- Display ------------------------------------------------------------- */

/* Renders the latest emulated frame as 32 bit XRGB8888 pixels into `dst`,
** which must hold (320 * zoom) x (240 * zoom) pixels with `pitch` bytes per
** line. Returns the zoom actually used. UI thread only. */
int  platformRenderFrame(void* dst, int pitch, int zoom);

/* The part of the rendered frame to show, in pixels of the zoomed frame:
** with horizontal stretch the side borders are cropped away, with vertical
** stretch 7 lines top and bottom, as the Win32 frontend does. The caller
** scales that rectangle to its 4:3 screen area. */
void platformScreenSourceRect(int zoom, int* x, int* y, int* w, int* h);

/* Whether the last rendered frame was interlaced (480i or the VDP's IL
** raster), where scanlines are left out as on a real set. */
int  platformLastFrameInterlaced(void);

/* The frontend draws the scanlines on the GPU: the software renderer leaves
** them out. */
void platformSetGpuScanlines(int on);

/* ---- Video recording (PlatformRecorder.cpp, FFmpeg) ---------------------- */

#define PLATFORM_PIXELS_BGRA 0      /* B, G, R, X bytes (XRGB8888 little endian) */
#define PLATFORM_PIXELS_RGBA 1

/* 1 when the build can record video (FFmpeg). */
int  platformRecorderAvailable(void);
/* 1 while File > Record video is on. */
int  platformRecorderActive(void);
/* A frontend that shows the frame through a shader passes the processed
** picture itself with platformRecorderAddFrame (on); otherwise the software
** frame is recorded as it is rendered. */
void platformRecorderSetPostRender(int on);
int  platformRecorderPostRender(void);
/* Records the frame being shown (UI thread). `pitch` may be negative for a
** bottom-up image (pass the address of the top row). */
void platformRecorderAddFrame(const void* pixels, int width, int height, int pitch, int format);

/* Replay to video (File > Video capture > Create video clip). The frontend
** shows FRONTEND_DIALOG_RENDER_VIDEO while it runs: progress 0..1000,
** Done once the replay has ended, then Stop (cancelled: the user gave up). */
int  platformReplayRenderStart(const char* capFile, const char* mp4File);
int  platformReplayRendering(void);
int  platformReplayRenderProgress(void);
int  platformReplayRenderDone(void);
void platformReplayRenderStop(int cancelled);
/* Shader frontends, after the recordFrame hook (UI thread): take each frame
** waiting, shade it at any size and give the result back. */
int  platformRecorderTakeFrame(const void** pixels, int* width, int* height, int* pitch);
void platformRecorderGiveFrame(const void* pixels, int width, int height, int pitch, int format);

/* Tells the emulator the frame requested by displayUpdate was presented. */
void platformDisplayDone(void);

/* ---- MIDI (ALSA sequencer) ---------------------------------------------- */

/* A device for sound.MidiOut/MidiIn/YkIn.name (type P_MIDI_HOST): `id` is
** what the properties keep, `name` what the user sees. "virtual" is our own
** port left unconnected. */
typedef struct {
    char id[256];
    char name[256];
} PlatformMidiPort;

int  platformMidiAvailable(void);
/* Synth/output ports when `output`, else input sources; returns how many
** exist (may exceed `max`). */
int  platformMidiListPorts(int output, PlatformMidiPort* ports, int max);
void platformMidiSetMt32ToGm(int enable);
void platformMidiSetChannelFilter(int channel);     /* 0 all, 1..16 */

/* ---- Input --------------------------------------------------------------- */

/* Keys are USB HID usage codes, the numbering of SDL_Scancode. */
void platformKeyEvent(int scancode, int pressed);
void platformKeyboardFocus(int focused);

/* Gamepad hot-plug, buttons and axes: pass every SDL event (UI thread).
** Other events are ignored. */
union SDL_Event;
void platformHandleSdlEvent(const union SDL_Event* event);

/* Relative motion in host pixels, and the absolute position normalised to
** 0..1 over the emulated screen (for light guns and the like). */
void platformMouseMotion(int dx, int dy, float x, float y);
void platformMouseButton(int button, int pressed);  /* 1 left, 2 middle, 3 right */

#ifdef __cplusplus
}
#endif

#endif
