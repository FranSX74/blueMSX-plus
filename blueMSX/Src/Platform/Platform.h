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

/* Tells the emulator the frame requested by displayUpdate was presented. */
void platformDisplayDone(void);

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
