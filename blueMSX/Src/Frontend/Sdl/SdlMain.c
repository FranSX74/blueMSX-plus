/*****************************************************************************
** blueMSX+ - minimal SDL3 frontend
**
** A window showing the emulated screen, with no menus: the keyboard
** shortcuts of the active profile work, and media files can be dropped on
** the window. bluemsx-plus-imgui is the full frontend.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "Platform.h"
#include "Properties.h"
#include "Actions.h"
#include "LaunchFile.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RENDER_ZOOM 2
#define TEX_WIDTH   (PLATFORM_SCREEN_WIDTH * RENDER_ZOOM)
#define TEX_HEIGHT  (PLATFORM_SCREEN_HEIGHT * RENDER_ZOOM)

enum { EV_DISPLAY, EV_WINDOW, EV_MESSAGE, EV_MOUSE_CAPTURE };

static SDL_Window*   window;
static SDL_Renderer* renderer;
static SDL_Texture*  texture;
static Uint32        userEvent;
static int           quitRequested;
static Uint32*       pixels;

static void pushUserEvent(int code, void* data1, intptr_t value)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = userEvent;
    ev.user.code = code;
    ev.user.data1 = data1;
    ev.user.data2 = (void*)value;
    SDL_PushEvent(&ev);
}

/* ---- Hooks (may run on the emulation thread) ------------------------------ */

static void hookDisplayUpdate(void)       { pushUserEvent(EV_DISPLAY, NULL, 0); }
static void hookWindowUpdate(void)        { pushUserEvent(EV_WINDOW, NULL, 0); }
static void hookMouseCapture(int capture) { pushUserEvent(EV_MOUSE_CAPTURE, NULL, capture); }

static void hookQuit(void)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
}

static void hookMessage(FrontendMessageKind kind, const char* text)
{
    fprintf(stderr, "blueMSX+: %s\n", text);
    if (kind == FRONTEND_MESSAGE_ERROR) {
        pushUserEvent(EV_MESSAGE, SDL_strdup(text), kind);
    }
}

/* ---- Window and display --------------------------------------------------- */

static void presentFrame(void)
{
    if (pixels == NULL || texture == NULL) return;
    platformRenderFrame(pixels, TEX_WIDTH * 4, RENDER_ZOOM);
    SDL_UpdateTexture(texture, NULL, pixels, TEX_WIDTH * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    {
        int x, y, w, h;
        SDL_FRect src;
        platformScreenSourceRect(RENDER_ZOOM, &x, &y, &w, &h);
        src.x = (float)x; src.y = (float)y; src.w = (float)w; src.h = (float)h;
        SDL_RenderTexture(renderer, texture, &src, NULL);
    }
    SDL_RenderPresent(renderer);
}

static void applyWindowSettings(void)
{
    Properties* p = platformGetProperties();
    int fullscreen = p != NULL && p->video.windowSize == P_VIDEO_SIZEFULLSCREEN;

    SDL_SetWindowFullscreen(window, fullscreen);
    if (!fullscreen && p != NULL) {
        int zoom = p->video.windowSize - P_VIDEO_SIZEX1 + 1;
        if (zoom < 1) zoom = 1;
        if (zoom > 8) zoom = 8;
        SDL_SetWindowSize(window, PLATFORM_SCREEN_WIDTH * zoom, PLATFORM_SCREEN_HEIGHT * zoom);
    }
}

/* ---- File chooser (blocking wrapper around SDL's asynchronous dialogs) ----- */

typedef struct {
    SDL_AtomicInt done;
    char* out;
    int   outSize;
    int   ok;
} DialogWait;

static void SDLCALL dialogDone(void* userdata, const char* const* files, int filter)
{
    DialogWait* w = (DialogWait*)userdata;
    if (files != NULL && files[0] != NULL) {
        SDL_strlcpy(w->out, files[0], w->outSize);
        w->ok = 1;
    }
    SDL_SetAtomicInt(&w->done, 1);
}

static int hookChooseFile(const FrontendFileRequest* req, char* out, int outSize)
{
    SDL_DialogFileFilter filters[8];
    char filterText[512];
    int nfilters = 0;
    DialogWait wait;
    char location[1024];

    /* "Label|pattern;pattern|Label|pattern" -> SDL filters ("rom;ri;mx1"). */
    if (req->filters != NULL) {
        char* save = NULL;
        char* tok;
        SDL_strlcpy(filterText, req->filters, sizeof(filterText));
        tok = SDL_strtok_r(filterText, "|", &save);
        while (tok != NULL && nfilters < 8) {
            char* pattern = SDL_strtok_r(NULL, "|", &save);
            char* r;
            char* w;
            if (pattern == NULL) break;
            /* strip "*." from each pattern */
            for (r = w = pattern; *r; r++) {
                if (*r == '*' && r[1] == '.') { r++; continue; }
                *w++ = *r;
            }
            *w = 0;
            filters[nfilters].name = tok;
            filters[nfilters].pattern = pattern;
            nfilters++;
            tok = SDL_strtok_r(NULL, "|", &save);
        }
    }

    location[0] = 0;
    if (req->defaultDir && *req->defaultDir) {
        SDL_snprintf(location, sizeof(location), "%s/%s", req->defaultDir,
                     req->defaultName ? req->defaultName : "");
    }

    SDL_SetAtomicInt(&wait.done, 0);
    wait.out = out;
    wait.outSize = outSize;
    wait.ok = 0;

    if (req->directory) {
        SDL_ShowOpenFolderDialog(dialogDone, &wait, window, location[0] ? location : NULL, false);
    }
    else if (req->save) {
        SDL_ShowSaveFileDialog(dialogDone, &wait, window, nfilters ? filters : NULL, nfilters,
                               location[0] ? location : NULL);
    }
    else {
        SDL_ShowOpenFileDialog(dialogDone, &wait, window, nfilters ? filters : NULL, nfilters,
                               location[0] ? location : NULL, false);
    }

    /* Keep the screen alive while the dialog is up; input goes nowhere. */
    while (!SDL_GetAtomicInt(&wait.done)) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 20)) {
            if (ev.type == userEvent && ev.user.code == EV_DISPLAY) {
                presentFrame();
                platformDisplayDone();
            }
            else if (ev.type == SDL_EVENT_QUIT) {
                quitRequested = 1;
            }
        }
    }
    return wait.ok;
}

/* ---- Main loop ------------------------------------------------------------ */

static void handleKey(const SDL_KeyboardEvent* key)
{
    if (!key->repeat) {
        platformKeyEvent(key->scancode, key->down);
    }
}

static void handleEvent(const SDL_Event* ev)
{
    platformHandleSdlEvent(ev);
    switch (ev->type) {
    case SDL_EVENT_QUIT:
        quitRequested = 1;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handleKey(&ev->key);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        platformKeyboardFocus(1);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        platformKeyboardFocus(0);
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
        SDL_RenderPresent(renderer);
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        float x = ev->motion.x, y = ev->motion.y;
        SDL_RenderCoordinatesFromWindow(renderer, ev->motion.x, ev->motion.y, &x, &y);
        platformMouseMotion((int)ev->motion.xrel, (int)ev->motion.yrel,
                            x / TEX_WIDTH, y / TEX_HEIGHT);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        platformMouseButton(ev->button.button == SDL_BUTTON_LEFT ? 1 :
                            ev->button.button == SDL_BUTTON_MIDDLE ? 2 : 3, ev->button.down);
        break;
    case SDL_EVENT_DROP_FILE:
        if (ev->drop.data != NULL) {
            tryLaunchUnknownFile(platformGetProperties(), ev->drop.data, 1);
        }
        break;
    default:
        if (ev->type == userEvent) {
            switch (ev->user.code) {
            case EV_DISPLAY:
                presentFrame();
                platformDisplayDone();
                break;
            case EV_WINDOW:
                applyWindowSettings();
                break;
            case EV_MOUSE_CAPTURE:
                SDL_SetWindowRelativeMouseMode(window, ev->user.data2 != NULL);
                break;
            case EV_MESSAGE:
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "blueMSX+",
                                         (const char*)ev->user.data1, window);
                SDL_free(ev->user.data1);
                break;
            }
        }
        break;
    }
}

int main(int argc, char** argv)
{
    FrontendHooks hooks;
    int rc;

    SDL_SetAppMetadata("blueMSX+", NULL, "org.bluemsx.plus");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "blueMSX+: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    userEvent = SDL_RegisterEvents(1);

    if (!SDL_CreateWindowAndRenderer("blueMSX+", TEX_WIDTH, TEX_HEIGHT,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        fprintf(stderr, "blueMSX+: cannot open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, TEX_WIDTH, TEX_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                SDL_TEXTUREACCESS_STREAMING, TEX_WIDTH, TEX_HEIGHT);
    pixels = (Uint32*)calloc(TEX_WIDTH * TEX_HEIGHT, sizeof(Uint32));

    SDL_zero(hooks);
    hooks.displayUpdate = hookDisplayUpdate;
    hooks.windowUpdate  = hookWindowUpdate;
    hooks.mouseCapture  = hookMouseCapture;
    hooks.quit          = hookQuit;
    hooks.message       = hookMessage;
    hooks.chooseFile    = hookChooseFile;

    rc = platformInit(argc, argv, &hooks);
    if (rc == 0) {
        applyWindowSettings();
        while (!quitRequested) {
            SDL_Event ev;
            if (SDL_WaitEvent(&ev)) {
                do {
                    handleEvent(&ev);
                } while (!quitRequested && SDL_PollEvent(&ev));
            }
        }
    }
    else {
        /* Show a start-up error that arrived before the loop. */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == userEvent && ev.user.code == EV_MESSAGE) handleEvent(&ev);
        }
    }

    platformShutdown();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    free(pixels);
    SDL_Quit();
    return rc < 0 ? 1 : 0;
}
