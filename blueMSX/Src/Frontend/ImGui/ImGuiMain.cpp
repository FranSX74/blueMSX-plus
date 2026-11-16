/*****************************************************************************
** blueMSX+ - Dear ImGui frontend
**
** The emulated screen fills the window below a menu bar that mirrors the
** Windows blueMSX+ menus; settings, file choosers and tools are ImGui
** windows. Menu texts come from blueMSX's own language tables.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ImGuiApp.h"

extern "C" {
#include "PlatformPaths.h"
#include "PlatformShortcuts.h"
#include "LaunchFile.h"
#include "Language.h"
#include "Actions.h"
}

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace app {

SDL_Window*   window;
SDL_Renderer* renderer;

static SDL_Texture* screenTex;
static Uint32*      pixels;
static Uint32       userEvent;
static bool         quitRequested;
static bool         mouseCaptured;
static bool         keyboardToMsx = true;
static ImVec2       screenMin, screenMax;     /* emulated screen, in window points */
static Uint64       lastMouseMove;
ImFont* monoFont = NULL;

static std::vector<std::function<void()>> deferred;
static std::string  iniPath;

enum { EV_DISPLAY, EV_WINDOW, EV_MESSAGE, EV_MOUSE_CAPTURE };

#define RENDER_ZOOM 2
#define TEX_WIDTH   (PLATFORM_SCREEN_WIDTH * RENDER_ZOOM)
#define TEX_HEIGHT  (PLATFORM_SCREEN_HEIGHT * RENDER_ZOOM)

void defer(std::function<void()> fn)
{
    deferred.push_back(std::move(fn));
}

static void runDeferred()
{
    /* A deferred call may defer more work: take the list first. */
    std::vector<std::function<void()>> work;
    work.swap(deferred);
    for (auto& fn : work) fn();
}

const char* clean(const char* path)
{
    if (path == NULL) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---- Hooks (any thread) --------------------------------------------------- */

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

static void hookDisplayUpdate()       { pushUserEvent(EV_DISPLAY, NULL, 0); }
static void hookWindowUpdate()        { pushUserEvent(EV_WINDOW, NULL, 0); }
static void hookMouseCapture(int on)  { pushUserEvent(EV_MOUSE_CAPTURE, NULL, on); }
static void hookMenuUpdate(int)       {}
static void hookEmulationState(int)   {}

static void hookQuit()
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
}

static void hookMessage(FrontendMessageKind kind, const char* text)
{
    fprintf(stderr, "blueMSX+: %s\n", text);
    pushUserEvent(EV_MESSAGE, SDL_strdup(text), kind);
}

/* ---- Hooks (UI thread) ---------------------------------------------------- */

static void hookShowMenu(FrontendMenu menu, int, int) { requestPopupMenu(menu); }
static void hookShowDialog(FrontendDialog dialog, int arg) { openDialog(dialog, arg); }

static void hookWindowCommand(FrontendWindowCommand cmd)
{
    switch (cmd) {
    case FRONTEND_WINDOW_MINIMIZE: SDL_MinimizeWindow(window); break;
    case FRONTEND_WINDOW_MAXIMIZE: SDL_MaximizeWindow(window); break;
    case FRONTEND_WINDOW_CLOSE:    quitRequested = true; break;
    default: break;              /* skin commands: ImGui has no skins */
    }
}

static int hookChooseFile(const FrontendFileRequest* req, char* out, int outSize)
{
    return chooseFile(req, out, outSize);
}

static int hookChooseFromList(FrontendListRequest* req)
{
    return chooseFromList(req);
}

/* ---- Window --------------------------------------------------------------- */

static bool isFullscreen()
{
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
}

/* The bar height, valid before the first frame too. */
static float barHeight()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return style.FontSizeBase * style.FontScaleDpi + style.FramePadding.y * 2.0f;
}

float menuBarHeight()
{
    if (!isFullscreen()) return barHeight();
    /* Fullscreen: the bar shows while the mouse is near the top edge. */
    float y = ImGui::GetIO().MousePos.y;
    bool near = y >= 0 && y < barHeight() * 1.5f;
    bool open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    return (near || open) && !mouseCaptured ? barHeight() : 0.0f;
}

static void applyWindowSettings()
{
    Properties* p = platformGetProperties();
    bool fullscreen = p != NULL && p->video.windowSize == P_VIDEO_SIZEFULLSCREEN;

    SDL_SetWindowFullscreen(window, fullscreen);
    if (!fullscreen && p != NULL) {
        int zoom = p->video.windowSize - P_VIDEO_SIZEX1 + 1;
        if (zoom < 1) zoom = 1;
        if (zoom > 8) zoom = 8;
        SDL_RestoreWindow(window);
        SDL_SetWindowSize(window, PLATFORM_SCREEN_WIDTH * zoom,
                          PLATFORM_SCREEN_HEIGHT * zoom + (int)barHeight());
    }
}

/* ---- Fonts and style ------------------------------------------------------ */

static const char* firstFile(const char* const* paths)
{
    for (; *paths; paths++) {
        if (platformIsFile(*paths)) return *paths;
    }
    return NULL;
}

static void setupFonts(float scale)
{
    static const char* const latin[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        NULL
    };
    static const char* const cjk[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        NULL
    };
    static const char* const mono[] = {
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        NULL
    };
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    const char* main = firstFile(latin);
    Properties* p = platformGetProperties();

    style.FontSizeBase = 15.0f;
    style.FontScaleDpi = scale;
    if (main == NULL || io.Fonts->AddFontFromFileTTF(main) == NULL) {
        io.Fonts->AddFontDefault();
        return;
    }
    /* Japanese, Korean and Chinese menus need a CJK font. */
    if (p != NULL && (p->language == EMU_LANG_JAPANESE || p->language == EMU_LANG_KOREAN ||
                      p->language == EMU_LANG_CHINESESIMP || p->language == EMU_LANG_CHINESETRAD)) {
        const char* f = firstFile(cjk);
        if (f != NULL) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(f, 0.0f, &cfg);
        }
    }
    /* Fixed width, for the debugger's listings: added last, so the main
    ** font stays the default and the CJK one merges into it. */
    const char* m = firstFile(mono);
    monoFont = m != NULL ? io.Fonts->AddFontFromFileTTF(m) : NULL;
    if (monoFont == NULL) monoFont = io.Fonts->AddFontDefault();
}

static void setupStyle(float scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    /* blueMSX blue accents */
    ImVec4* c = style.Colors;
    c[ImGuiCol_Header]         = ImVec4(0.18f, 0.35f, 0.62f, 0.70f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.22f, 0.45f, 0.80f, 0.85f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.25f, 0.50f, 0.90f, 1.00f);
    c[ImGuiCol_MenuBarBg]      = ImVec4(0.11f, 0.13f, 0.18f, 1.00f);
    /* The game shows through translucent windows; keep them readable. */
    c[ImGuiCol_WindowBg].w     = 1.00f;
    c[ImGuiCol_PopupBg].w      = 0.98f;
    style.ScaleAllSizes(scale);
}

/* ---- Display -------------------------------------------------------------- */

static void uploadFrame()
{
    platformRenderFrame(pixels, TEX_WIDTH * 4, RENDER_ZOOM);
    SDL_UpdateTexture(screenTex, NULL, pixels, TEX_WIDTH * 4);
    platformDisplayDone();
}

static void drawScreen()
{
    ImGuiIO& io = ImGui::GetIO();
    float top = menuBarHeight();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y - top;
    float sw = w, sh = w * 3.0f / 4.0f;
    int sx, sy, sxw, syh;

    if (sh > h) {
        sh = h; sw = h * 4.0f / 3.0f;
    }
    screenMin = ImVec2((w - sw) * 0.5f, top + (h - sh) * 0.5f);
    screenMax = ImVec2(screenMin.x + sw, screenMin.y + sh);
    platformScreenSourceRect(RENDER_ZOOM, &sx, &sy, &sxw, &syh);
    ImVec2 uv0((float)sx / TEX_WIDTH, (float)sy / TEX_HEIGHT);
    ImVec2 uv1((float)(sx + sxw) / TEX_WIDTH, (float)(sy + syh) / TEX_HEIGHT);
    ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)screenTex, screenMin, screenMax, uv0, uv1);
}

static void testRun();
static void testSnapshot();

static void renderAndPresent()
{
    ImGui::Render();
    /* ImGui works in window points; scale to pixels on HiDPI and fractional
    ** scaling. */
    ImGuiIO& io = ImGui::GetIO();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    testSnapshot();
    SDL_RenderPresent(renderer);
    testRun();
}

static void newFrame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

/* ---- Events --------------------------------------------------------------- */

/* Events every loop handles: the emulator's own, quitting, gamepads. */
static bool handleCommonEvent(const SDL_Event& ev)
{
    platformHandleSdlEvent(&ev);
    if (ev.type == SDL_EVENT_QUIT) {
        quitRequested = true;
        return true;
    }
    if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        quitRequested = true;
        return true;
    }
    if (ev.type != userEvent) return false;
    switch (ev.user.code) {
    case EV_DISPLAY:
        uploadFrame();
        break;
    case EV_WINDOW:
        applyWindowSettings();
        break;
    case EV_MOUSE_CAPTURE:
        mouseCaptured = ev.user.data2 != NULL;
        SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
        break;
    case EV_MESSAGE:
        showMessage((FrontendMessageKind)(intptr_t)ev.user.data2, (const char*)ev.user.data1);
        SDL_free(ev.user.data1);
        break;
    }
    return true;
}

static void setKeyboardToMsx(bool on)
{
    if (on == keyboardToMsx) return;
    keyboardToMsx = on;
    /* Release whatever the MSX thinks is held. */
    platformKeyboardFocus(0);
    platformKeyboardFocus(1);
}

static void handleKey(const SDL_KeyboardEvent& key)
{
    ImGuiIO& io = ImGui::GetIO();
    if (key.repeat) return;
    if (key.down && shortcutEditorWantsKey(key.scancode, platformShortcutMods())) return;
    if (debuggerWantsKeys()) {
        /* The debugger's own keys; only let go of what the MSX may hold. */
        if (!key.down) platformShortcutKey(key.scancode, 0);
        return;
    }

    if (keyboardToMsx) {
        platformKeyEvent(key.scancode, key.down);
    }
    else if (!io.WantTextInput) {
        /* A window has focus: shortcuts still work, the MSX gets nothing. */
        platformShortcutKey(key.scancode, key.down);
    }
    else if (!key.down) {
        platformShortcutKey(key.scancode, 0);   /* keep modifiers in step */
    }
}

static void handleEvent(const SDL_Event& ev)
{
    ImGuiIO& io = ImGui::GetIO();
    bool mouseEvent = ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                      ev.type == SDL_EVENT_MOUSE_BUTTON_UP || ev.type == SDL_EVENT_MOUSE_WHEEL;

    if (!(mouseCaptured && mouseEvent)) {
        ImGui_ImplSDL3_ProcessEvent(&ev);
    }
    if (handleCommonEvent(ev)) return;

    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handleKey(ev.key);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        platformKeyboardFocus(1);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        platformKeyboardFocus(0);
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        lastMouseMove = SDL_GetTicks();
        if (mouseCaptured || !io.WantCaptureMouse) {
            float sw = screenMax.x - screenMin.x, sh = screenMax.y - screenMin.y;
            platformMouseMotion((int)ev.motion.xrel, (int)ev.motion.yrel,
                                sw > 0 ? (ev.motion.x - screenMin.x) / sw : 0,
                                sh > 0 ? (ev.motion.y - screenMin.y) / sh : 0);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (mouseCaptured || !io.WantCaptureMouse) {
            platformMouseButton(ev.button.button == SDL_BUTTON_LEFT ? 1 :
                                ev.button.button == SDL_BUTTON_MIDDLE ? 2 : 3, ev.button.down);
        }
        break;
    case SDL_EVENT_DROP_FILE:
        if (ev.drop.data != NULL) {
            std::string file = ev.drop.data;
            defer([file]() { tryLaunchUnknownFile(platformGetProperties(), file.c_str(), 1); });
        }
        break;
    default:
        break;
    }
}

int runModal(const std::function<int()>& draw)
{
    int result = -2;
    bool wasToMsx = keyboardToMsx;

    setKeyboardToMsx(false);
    platformShortcutsReleaseAll();
    while (result == -2) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            handleCommonEvent(ev);
        }
        if (quitRequested) {
            result = -1;
            break;
        }
        newFrame();
        drawScreen();
        drawNotifications();
        result = draw();
        renderAndPresent();
    }
    /* The frame after a modal must not see the click that closed it. */
    setKeyboardToMsx(wasToMsx);
    return result;
}

/* ---- Scripted input for automated checks --------------------------------------
** BLUEMSX_TEST="2.0 move 40 10; 2.2 click; 3 snap /tmp/a.bmp; 3.5 key F12; 5 quit"
** Times are seconds from start-up; snap saves the window as BMP;
** "dialog N ARG" opens a FrontendDialog (PropPage ARG for 0). */

struct TestStep { double t; std::string cmd, a, b; };
static std::vector<TestStep> testSteps;
static size_t testNext;
static float testX, testY;
static std::string pendingSnap;

static void testLoad()
{
    const char* env = getenv("BLUEMSX_TEST");
    if (env == NULL) return;
    std::string s = env;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(';', pos);
        if (end == std::string::npos) end = s.size();
        char cmd[32] = "", a[512] = "", b[64] = "";
        double t = 0;
        if (sscanf(s.substr(pos, end - pos).c_str(), "%lf %31s %511s %63s", &t, cmd, a, b) >= 2) {
            testSteps.push_back(TestStep{ t, cmd, a, b });
        }
        pos = end + 1;
    }
}

static void testPush(SDL_Event ev)
{
    SDL_PushEvent(&ev);
}

static void testRun()
{
    double now = SDL_GetTicks() / 1000.0;
    while (testNext < testSteps.size() && testSteps[testNext].t <= now) {
        const TestStep& st = testSteps[testNext++];
        SDL_Event ev;
        SDL_zero(ev);
        Uint32 wid = SDL_GetWindowID(window);
        if (st.cmd == "move") {
            testX = (float)atof(st.a.c_str());
            testY = (float)atof(st.b.c_str());
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.windowID = wid;
            ev.motion.x = testX;
            ev.motion.y = testY;
            testPush(ev);
        }
        else if (st.cmd == "click" || st.cmd == "rclick") {
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.windowID = wid;
            ev.button.button = st.cmd == "click" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            ev.button.down = true;
            ev.button.clicks = 1;
            ev.button.x = testX;
            ev.button.y = testY;
            testPush(ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.down = false;
            testPush(ev);
        }
        else if (st.cmd == "key") {
            /* "F10", or with modifiers: "Ctrl+Shift+F9". */
            std::string name = st.a;
            SDL_Keymod mod = SDL_KMOD_NONE;
            for (;;) {
                size_t plus = name.find('+');
                if (plus == std::string::npos || plus + 1 >= name.size()) break;
                std::string m = name.substr(0, plus);
                if (m == "Ctrl") mod |= SDL_KMOD_LCTRL;
                else if (m == "Shift") mod |= SDL_KMOD_LSHIFT;
                else if (m == "Alt") mod |= SDL_KMOD_LALT;
                name = name.substr(plus + 1);
            }
            SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.windowID = wid;
            ev.key.mod = mod;
            ev.key.scancode = sc;
            ev.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
            ev.key.down = true;
            testPush(ev);
            ev.type = SDL_EVENT_KEY_UP;
            ev.key.down = false;
            ev.key.mod = SDL_KMOD_NONE;     /* the modifiers are let go too */
            testPush(ev);
        }
        else if (st.cmd == "text") {
            static std::string keep;
            keep = st.a;
            ev.type = SDL_EVENT_TEXT_INPUT;
            ev.text.windowID = wid;
            ev.text.text = keep.c_str();
            testPush(ev);
        }
        else if (st.cmd == "dialog") {
            openDialog((FrontendDialog)atoi(st.a.c_str()), atoi(st.b.c_str()));
        }
        else if (st.cmd == "snap") {
            pendingSnap = st.a;
        }
        else if (st.cmd == "quit") {
            quitRequested = true;
        }
    }
}

static void testSnapshot()
{
    if (pendingSnap.empty()) return;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, NULL);
    if (shot != NULL) {
        SDL_SaveBMP(shot, pendingSnap.c_str());
        SDL_DestroySurface(shot);
    }
    pendingSnap.clear();
}

/* ---- Main ----------------------------------------------------------------- */

static void frame()
{
    newFrame();

    drawScreen();
    drawMainMenuBar();
    drawPopupMenus();
    drawDialogs();
    drawNotifications();

    /* The MSX gets the keyboard unless an ImGui window wants it. */
    setKeyboardToMsx(!ImGui::GetIO().WantCaptureKeyboard && !debuggerWantsKeys());

    /* Fullscreen: hide an idle mouse pointer. */
    if (isFullscreen() && !mouseCaptured && SDL_GetTicks() - lastMouseMove > 2000 &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    renderAndPresent();
    runDeferred();
}

} // namespace app

using namespace app;

int main(int argc, char** argv)
{
    FrontendHooks hooks;
    int rc;

    SDL_SetAppMetadata("blueMSX-ImGui", NULL, "org.bluemsx.imgui");
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "blueMSX+: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    userEvent = SDL_RegisterEvents(1);

    if (!SDL_CreateWindowAndRenderer("blueMSX-ImGui", TEX_WIDTH, TEX_HEIGHT + 24,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                     SDL_WINDOW_HIDDEN, &window, &renderer)) {
        fprintf(stderr, "blueMSX+: cannot open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    screenTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, TEX_WIDTH, TEX_HEIGHT);
    SDL_SetTextureScaleMode(screenTex, SDL_SCALEMODE_PIXELART);
    pixels = (Uint32*)calloc(TEX_WIDTH * TEX_HEIGHT, sizeof(Uint32));

    SDL_zero(hooks);
    hooks.displayUpdate  = hookDisplayUpdate;
    hooks.windowUpdate   = hookWindowUpdate;
    hooks.menuUpdate     = hookMenuUpdate;
    hooks.emulationState = hookEmulationState;
    hooks.message        = hookMessage;
    hooks.quit           = hookQuit;
    hooks.mouseCapture   = hookMouseCapture;
    hooks.showMenu       = hookShowMenu;
    hooks.showDialog     = hookShowDialog;
    hooks.windowCommand  = hookWindowCommand;
    hooks.chooseFile     = hookChooseFile;
    hooks.chooseFromList = hookChooseFromList;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;   /* arrows belong to the MSX */
    io.IniFilename = NULL;

    rc = platformInit(argc, argv, &hooks);
    if (rc == 0) {
        float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
        if (scale <= 0) scale = 1.0f;

        iniPath = std::string(platformPaths()->configDir) + "/imgui.ini";
        io.IniFilename = iniPath.c_str();
        setupStyle(scale);
        setupFonts(scale);
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        applyWindowSettings();
        SDL_ShowWindow(window);
        testLoad();
        while (!quitRequested) {
            SDL_Event ev;
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_WaitEventTimeout(NULL, 50);
            }
            while (SDL_PollEvent(&ev)) {
                handleEvent(ev);
            }
            runDeferred();
            if (quitRequested) break;
            frame();
        }
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    else {
        /* Report a start-up error that arrived before the loop. */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == userEvent && ev.user.code == EV_MESSAGE) {
                if ((intptr_t)ev.user.data2 == FRONTEND_MESSAGE_ERROR) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "blueMSX-ImGui",
                                             (const char*)ev.user.data1, NULL);
                }
                SDL_free(ev.user.data1);
            }
        }
    }

    closeDebugger();
    platformShutdown();
    ImGui::DestroyContext();

    SDL_DestroyTexture(screenTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    free(pixels);
    SDL_Quit();
    return rc < 0 ? 1 : 0;
}
