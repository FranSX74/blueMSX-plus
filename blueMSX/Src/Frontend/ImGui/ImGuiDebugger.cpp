/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: debugger window
**
** The same debugger as QblueMSX's (Frontend/Common/DebuggerCore does the
** work) drawn as one ImGui window over the emulated screen: menus and a
** toolbar, the disassembly on the left, registers / flags / stack /
** callstack on the right, and tabs below for breakpoints, memory,
** peripheral registers and I/O ports. Position and size live in imgui.ini;
** the options share debugger.ini with the Qt frontend.
**
** While the window has the focus it keeps the keyboard: the debugger keys
** (F5, F10, F11...) never reach the MSX or the emulator's shortcuts.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ImGuiApp.h"
#include "DebuggerCore.h"

extern "C" {
#include "Language.h"
#include "PlatformPaths.h"
}

#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace app {

namespace {

using dbg::text;

/* ---- State --------------------------------------------------------------- */

std::unique_ptr<dbg::Session> session;
std::atomic<bool> coreChanged{ true };
bool windowOpen = false;
bool focusNext = false;
bool hasFocus = false;

std::vector<dbg::Session::Line> lines;
int cursorLine = -1;
int scrollToLine = -1;          /* applied inside the disassembly child */

bool asmFlags = true;
bool showRight = true, showBottom = true;
float bottomHeight = 0;         /* 0: not laid out yet */

int memView = 0, periView = 0, ioView = 0;
int hexCursor = -1;
bool hexLowNibble = false;
int hexScrollTo = -1;
int bpSelected = -1;
int bottomTab = -1;             /* request to show a tab: 1 = memory */

std::string lastSearch;
std::string status;             /* transient status bar text */
Uint64 statusTime = 0;

/* Prompts, opened from anywhere and drawn inside the window. */
enum Prompt { PROMPT_NONE, PROMPT_BREAKPOINT, PROMPT_GOTO, PROMPT_FIND, PROMPT_WATCH, PROMPT_CLOSE, PROMPT_SYMBOLS };
Prompt promptPending = PROMPT_NONE;
char promptBuf[256];
int watchType = 0, watchCondition = 0, watchSize = 0;
char watchValue[16];
std::string symbolsFile;        /* waiting for "replace old symbols?" */

const ImU32 kRed = IM_COL32(230, 60, 60, 255);

struct Mono {
    Mono() { ImGui::PushFont(monoFont, 0.0f); }
    ~Mono() { ImGui::PopFont(); }
};

std::string settingsPath()
{
    return std::string(platformPaths()->configDir) + "/debugger.ini";
}

/* debugger.ini is QSettings' [General] section: keep the Qt keys we do not
** use (geometry and dock state) as they are. */
void loadSettings()
{
    std::ifstream in(settingsPath());
    std::string line;
    bool vram = false;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        bool on = v == "true";
        if (k == "showSymbols") session->symbols.shown = on;
        else if (k == "asmFlags") asmFlags = on;
        else if (k == "checkVram") vram = on;
    }
    if (vram) session->vramCheck(true);
}

void saveSettings()
{
    if (!session) return;
    std::map<std::string, std::string> mine = {
        { "showSymbols", session->symbols.shown ? "true" : "false" },
        { "asmFlags", asmFlags ? "true" : "false" },
        { "checkVram", session->vramCheck() ? "true" : "false" },
    };
    std::vector<std::string> out;
    bool general = false;
    {
        std::ifstream in(settingsPath());
        std::string line;
        while (std::getline(in, line)) {
            if (line == "[General]") general = true;
            size_t eq = line.find('=');
            if (eq != std::string::npos && mine.count(line.substr(0, eq))) continue;
            out.push_back(line);
        }
    }
    if (!general) out.insert(out.begin(), "[General]");
    size_t at = 0;
    while (at < out.size() && out[at] != "[General]") at++;
    at++;
    for (const auto& kv : mine) out.insert(out.begin() + at++, kv.first + "=" + kv.second);
    std::ofstream o(settingsPath());
    for (const std::string& l : out) o << l << "\n";
}

void setStatus(const std::string& s)
{
    status = s;
    statusTime = SDL_GetTicks();
}

bool paused() { return session && session->state() == dbg::PAUSED; }
bool stale() { return !paused(); }

int lineOf(int address)
{
    int lo = 0, hi = (int)lines.size() - 1, best = hi;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lines[mid].address >= address) {
            best = mid;
            hi = mid - 1;
        }
        else lo = mid + 1;
    }
    while (best + 1 < (int)lines.size() && lines[best].label) best++;
    return best < 0 ? 0 : best;
}

int cursorAddress()
{
    return cursorLine >= 0 && cursorLine < (int)lines.size() ? lines[cursorLine].address : -1;
}

void showAddress(int address, bool moveCursor)
{
    if (lines.empty()) return;
    int line = lineOf(address);
    scrollToLine = line;
    if (moveCursor) cursorLine = line;
}

void rebuildListing(bool followPc)
{
    int keep = cursorAddress();
    lines = session->hasSnapshot() ? session->listing() : std::vector<dbg::Session::Line>();
    cursorLine = keep >= 0 && !lines.empty() ? lineOf(keep) : -1;
    if (followPc && paused()) showAddress(session->pc, false);
}

void coreUpdate()
{
    if (!coreChanged.exchange(false)) return;
    bool changed = session->update();
    if (!changed) return;
    rebuildListing(true);
    if (paused() && !windowOpen && !session->breakpoints().empty()) {
        windowOpen = true;          /* one of our breakpoints stopped the machine */
        focusNext = true;
    }
}

bool armed()
{
    for (const dbg::Breakpoint& bp : session->breakpoints()) {
        if (bp.enabled) return true;
    }
    return false;
}

/* ---- Files ----------------------------------------------------------------- */

void loadSymbols()
{
    defer([]() {
        FrontendFileRequest req = {};
        req.kind = FRONTEND_FILE_SAVE_GENERIC;
        req.title = text("Open Symbol File", "Abrir archivo de símbolos");
        req.filters = text("Symbol files|*.sym;*.SYM", "Archivos de símbolos|*.sym;*.SYM");
        char path[1024];
        if (!chooseFile(&req, path, sizeof(path))) return;
        if (!session->symbols.empty()) {
            symbolsFile = path;
            promptPending = PROMPT_SYMBOLS;
            return;
        }
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        session->symbols.append(ss.str());
        session->symbols.shown = true;
        rebuildListing(false);
        setStatus(std::to_string(session->symbols.size()) + text(" symbols", " símbolos"));
    });
}

void finishSymbols(bool replace)
{
    if (replace) session->symbols.clear();
    std::ifstream in(symbolsFile);
    std::stringstream ss;
    ss << in.rdbuf();
    session->symbols.append(ss.str());
    session->symbols.shown = true;
    rebuildListing(false);
    setStatus(std::to_string(session->symbols.size()) + text(" symbols", " símbolos"));
}

void saveFile(bool listing)
{
    defer([listing]() {
        FrontendFileRequest req = {};
        req.kind = FRONTEND_FILE_SAVE_GENERIC;
        req.save = 1;
        req.allowCreate = 1;
        req.title = listing ? text("Save Disassembly", "Guardar desensamblado") : text("Save Memory", "Guardar memoria");
        req.filters = listing ? text("Assembly|*.asm", "Ensamblador|*.asm") : text("Binary|*.bin", "Binario|*.bin");
        req.defaultName = listing ? "disassembly.asm" : "memory.bin";
        char path[1024];
        if (!chooseFile(&req, path, sizeof(path))) return;
        std::string file = path;
        const char* ext = listing ? ".asm" : ".bin";
        size_t slash = file.find_last_of('/');
        if (file.find('.', slash == std::string::npos ? 0 : slash) == std::string::npos) file += ext;
        bool ok = listing ? session->saveListing(file) : session->saveMemory(memView, file);
        setStatus(ok ? file : text("Could not write the file.", "No se pudo escribir el archivo."));
    });
}

/* ---- Toolbar pictures ------------------------------------------------------ */

enum Glyph { G_PLAY, G_PAUSE, G_STOP, G_RESTART, G_NEXT, G_BACK, G_INTO, G_OVER, G_OUT, G_RUNTO, G_BP, G_BP_OFF, G_BP_CLEAR };

void drawGlyph(ImDrawList* dl, ImVec2 o, float s, int g, bool enabled)
{
    auto col = [enabled](int r, int gr, int b) {
        if (enabled) return IM_COL32(r, gr, b, 255);
        int grey = (r * 30 + gr * 59 + b * 11) / 100;
        return IM_COL32(grey, grey, grey, 110);
    };
    auto P = [o, s](float x, float y) { return ImVec2(o.x + x * s / 32, o.y + y * s / 32); };
    float w = 3.2f * s / 32;
    ImU32 blue = col(40, 110, 200), green = col(30, 150, 50), red = col(210, 40, 40);
    ImU32 yellow = col(240, 190, 0), dark = col(60, 60, 60);
    auto head = [&](ImVec2 tip, ImVec2 dir, ImU32 c) {
        ImVec2 n(-dir.y, dir.x);
        float k = 8 * s / 32, m = 6 * s / 32;
        dl->AddTriangleFilled(tip, ImVec2(tip.x - dir.x * k + n.x * m, tip.y - dir.y * k + n.y * m),
                              ImVec2(tip.x - dir.x * k - n.x * m, tip.y - dir.y * k - n.y * m), c);
    };
    auto dot = [&](float x, float y) { dl->AddCircleFilled(P(x, y), 3.2f * s / 32, dark); };
    auto arc = [&](float cx, float cy, float r, float a0, float a1) {
        dl->PathArcTo(P(cx, cy), r * s / 32, a0, a1, 24);
        dl->PathStroke(blue, 0, w);
    };
    const float pi = 3.14159265f;
    switch (g) {
    case G_PLAY:  dl->AddTriangleFilled(P(8, 5), P(27, 16), P(8, 27), green); break;
    case G_PAUSE: dl->AddRectFilled(P(7, 6), P(13, 26), blue); dl->AddRectFilled(P(19, 6), P(25, 26), blue); break;
    case G_STOP:  dl->AddRectFilled(P(7, 7), P(25, 25), red); break;
    case G_RESTART: arc(16, 16, 10, -pi * 0.45f, pi * 1.2f); head(P(21, 6), ImVec2(1, 0), blue); break;
    case G_NEXT: {
        ImVec2 pts[7] = { P(4, 11), P(16, 11), P(16, 5), P(28, 16), P(16, 27), P(16, 21), P(4, 21) };
        dl->AddConvexPolyFilled(pts + 1, 5, yellow);
        dl->AddRectFilled(P(4, 11), P(16.5f, 21), yellow);
        break;
    }
    case G_BACK: arc(17, 13, 9, -pi * 0.5f, pi); head(P(8, 21), ImVec2(0, 1), blue); dot(17, 27); break;
    case G_INTO: dl->AddLine(P(16, 3), P(16, 16), blue, w); head(P(16, 22), ImVec2(0, 1), blue); dot(16, 27); break;
    case G_OVER: arc(16, 16, 10, pi, pi * 2); head(P(26, 22), ImVec2(0, 1), blue); dot(16, 26); break;
    case G_OUT:  dl->AddLine(P(16, 27), P(16, 14), blue, w); head(P(16, 6), ImVec2(0, -1), blue); dot(16, 29); break;
    case G_RUNTO:
        dl->AddLine(P(4, 16), P(18, 16), blue, w);
        head(P(25, 16), ImVec2(1, 0), blue);
        dl->AddLine(P(28, 6), P(28, 26), dark, 2.5f * s / 32);
        break;
    default: {
        ImVec2 c = P(16, 16);
        float r = 11 * s / 32;
        if (g != G_BP_OFF) dl->AddCircleFilled(c, r, red);
        dl->AddCircle(c, r, col(160, 0, 0), 0, 2 * s / 32);
        if (g == G_BP_CLEAR) {
            ImU32 white = col(255, 255, 255);
            dl->AddLine(P(11, 11), P(21, 21), white, 3 * s / 32);
            dl->AddLine(P(21, 11), P(11, 21), white, 3 * s / 32);
        }
        break;
    }
    }
}

/* ---- Actions: menus, toolbar and keys share one table ---------------------- */

struct Action {
    const char* en;
    const char* es;
    const char* keys;           /* shown in the menu */
    ImGuiKeyChord chord;        /* 0: none */
    bool repeat;
    int glyph;                  /* -1: none */
    std::function<bool()> enabled;
    std::function<void()> run;
    std::function<bool()> checked;  /* null: not a toggle */
    const char* label() const { return text(en, es); }
};

enum {
    A_LOAD_SYM, A_SAVE_ASM, A_SAVE_MEM, A_CLOSE,
    A_CONTINUE, A_BREAK, A_STOP, A_RESTART, A_STEP_BACK, A_STEP_INTO, A_STEP_OVER, A_STEP_OUT,
    A_RUN_TO, A_SHOW_NEXT, A_SYMBOLS, A_FIND, A_FIND_NEXT, A_GOTO, A_ADD_BP, A_ADD_WP,
    A_TOGGLE_BP, A_ENABLE_BP, A_REMOVE_ALL, A_ENABLE_ALL, A_DISABLE_ALL, A_ASM_FLAGS, A_VRAM,
    A_COUNT
};

std::vector<Action> actions;

void findNext(bool ask);

void buildActions()
{
    auto st = []() { return session->state(); };
    auto always = []() { return true; };
    auto isPaused = []() { return paused(); };
    auto snap = []() { return session->hasSnapshot(); };
    auto notStopped = [st]() { return st() != dbg::STOPPED; };
    auto anyBp = []() { return !session->breakpoints().empty(); };
    actions.resize(A_COUNT);
    actions[A_LOAD_SYM]  = { "Load Symbol File...", "Cargar archivo de símbolos...", "", 0, false, -1, always, loadSymbols, nullptr };
    actions[A_SAVE_ASM]  = { "Save Disassembly...", "Guardar desensamblado...", "", 0, false, -1, snap, []() { saveFile(true); }, nullptr };
    actions[A_SAVE_MEM]  = { "Save Memory...", "Guardar memoria...", "", 0, false, -1,
                             []() { return session->hasSnapshot() && memView < (int)session->memories.size(); },
                             []() { saveFile(false); }, nullptr };
    actions[A_CLOSE]     = { "Close", "Cerrar", "Ctrl+W", ImGuiMod_Ctrl | ImGuiKey_W, false, -1, always,
                             []() { if (armed()) promptPending = PROMPT_CLOSE; else windowOpen = false; }, nullptr };
    actions[A_CONTINUE]  = { "Start/Continue", "Iniciar/Continuar", "F5", ImGuiKey_F5, false, G_PLAY,
                             [st]() { return st() != dbg::RUNNING; }, []() { session->run(); }, nullptr };
    actions[A_BREAK]     = { "Break All", "Pausar", "Ctrl+Alt+Pause", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_Pause, false, G_PAUSE,
                             [st]() { return st() == dbg::RUNNING; }, []() { session->pause(); }, nullptr };
    actions[A_STOP]      = { "Stop Debugging", "Detener", "Shift+F5", ImGuiMod_Shift | ImGuiKey_F5, false, G_STOP,
                             notStopped, []() { session->stop(); }, nullptr };
    actions[A_RESTART]   = { "Restart", "Reiniciar", "Ctrl+Shift+F5", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F5, false, G_RESTART,
                             notStopped, []() { session->restart(); }, nullptr };
    actions[A_STEP_BACK] = { "Step Back", "Paso atrás", "Ctrl+F11", ImGuiMod_Ctrl | ImGuiKey_F11, true, G_BACK,
                             isPaused, []() { session->stepBack(); }, nullptr };
    actions[A_STEP_INTO] = { "Step Into", "Paso a paso", "F11", ImGuiKey_F11, true, G_INTO,
                             isPaused, []() { session->stepInto(); }, nullptr };
    actions[A_STEP_OVER] = { "Step Over", "Saltar llamada", "F10", ImGuiKey_F10, true, G_OVER,
                             []() { return paused() && session->hasSnapshot(); }, []() { session->stepOver(); }, nullptr };
    actions[A_STEP_OUT]  = { "Step Out", "Salir de la rutina", "Shift+F11", ImGuiMod_Shift | ImGuiKey_F11, false, G_OUT,
                             []() { return paused() && session->returnAddress() >= 0; }, []() { session->stepOut(); }, nullptr };
    actions[A_RUN_TO]    = { "Run To Cursor", "Ejecutar hasta el cursor", "Shift+F10", ImGuiMod_Shift | ImGuiKey_F10, false, G_RUNTO,
                             []() { return paused() && cursorAddress() >= 0; },
                             []() { session->runTo((uint16_t)cursorAddress()); }, nullptr };
    actions[A_SHOW_NEXT] = { "Show Next Statement", "Mostrar la siguiente instrucción", "Home", ImGuiKey_Home, false, G_NEXT,
                             snap, []() { showAddress(session->pc, true); }, nullptr };
    actions[A_SYMBOLS]   = { "Show Symbol Information", "Mostrar símbolos", "F8", ImGuiKey_F8, false, -1, always,
                             []() { session->symbols.shown = !session->symbols.shown; rebuildListing(false); saveSettings(); },
                             []() { return session->symbols.shown; } };
    actions[A_FIND]      = { "Find...", "Buscar...", "Ctrl+F", ImGuiMod_Ctrl | ImGuiKey_F, false, -1, snap,
                             []() { findNext(true); }, nullptr };
    actions[A_FIND_NEXT] = { "Find Next", "Buscar siguiente", "F3", ImGuiKey_F3, true, -1,
                             []() { return session->hasSnapshot() && !lastSearch.empty(); }, []() { findNext(false); }, nullptr };
    actions[A_GOTO]      = { "Go To...", "Ir a...", "Ctrl+G", ImGuiMod_Ctrl | ImGuiKey_G, false, -1, snap,
                             []() { promptBuf[0] = 0; promptPending = PROMPT_GOTO; }, nullptr };
    actions[A_ADD_BP]    = { "Add New Breakpoint...", "Añadir punto de ruptura...", "Ctrl+B", ImGuiMod_Ctrl | ImGuiKey_B, false, -1, notStopped,
                             []() {
                                 int a = cursorAddress();
                                 if (a >= 0) snprintf(promptBuf, sizeof(promptBuf), "#%04X", a);
                                 else promptBuf[0] = 0;
                                 promptPending = PROMPT_BREAKPOINT;
                             }, nullptr };
    actions[A_ADD_WP]    = { "Add New Watchpoint...", "Añadir watchpoint...", "Ctrl+Shift+W", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W, false, -1, notStopped,
                             []() { promptBuf[0] = 0; watchValue[0] = 0; promptPending = PROMPT_WATCH; }, nullptr };
    actions[A_TOGGLE_BP] = { "Set/Remove Breakpoint", "Poner/quitar punto de ruptura", "F9", ImGuiKey_F9, false, G_BP,
                             [st]() { return st() != dbg::STOPPED && cursorAddress() >= 0; },
                             []() { session->toggleCode(cursorAddress()); }, nullptr };
    actions[A_ENABLE_BP] = { "Enable/Disable Breakpoint", "Activar/desactivar punto de ruptura", "Shift+F9", ImGuiMod_Shift | ImGuiKey_F9, false, G_BP_OFF,
                             [st]() { return st() != dbg::STOPPED && cursorAddress() >= 0 && session->findCode(cursorAddress()) >= 0; },
                             []() { session->toggleCodeEnabled(cursorAddress()); }, nullptr };
    actions[A_REMOVE_ALL] = { "Remove All Breakpoints", "Quitar todos los puntos de ruptura", "Ctrl+Shift+F9", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F9, false, G_BP_CLEAR,
                              anyBp, []() { session->removeAll(); }, nullptr };
    actions[A_ENABLE_ALL] = { "Enable All Breakpoints", "Activar todos los puntos de ruptura", "", 0, false, -1, anyBp,
                              []() { session->enableAll(true); }, nullptr };
    actions[A_DISABLE_ALL] = { "Disable All Breakpoints", "Desactivar todos los puntos de ruptura", "", 0, false, -1, anyBp,
                               []() { session->enableAll(false); }, nullptr };
    actions[A_ASM_FLAGS] = { "Show Assembly Flags", "Banderas como en ensamblador", "Ctrl+M", ImGuiMod_Ctrl | ImGuiKey_M, false, -1, always,
                             []() { asmFlags = !asmFlags; saveSettings(); }, []() { return asmFlags; } };
    actions[A_VRAM]      = { "Break on too fast VRAM access", "Parar en accesos a VRAM demasiado rápidos", "Ctrl+V", ImGuiMod_Ctrl | ImGuiKey_V, false, -1, always,
                             []() { session->vramCheck(!session->vramCheck()); saveSettings(); },
                             []() { return session->vramCheck(); } };
}

void menuItem(int id)
{
    const Action& a = actions[id];
    bool checked = a.checked ? a.checked() : false;
    if (ImGui::MenuItem(a.label(), *a.keys ? a.keys : NULL, checked, a.enabled())) a.run();
}

bool toolButton(int id)
{
    const Action& a = actions[id];
    float s = ImGui::GetFrameHeight();
    bool enabled = a.enabled();
    ImGui::PushID(id);
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = ImGui::Button("##tool", ImVec2(s, s));
    if (!enabled) ImGui::EndDisabled();
    ImVec2 min = ImGui::GetItemRectMin();
    drawGlyph(ImGui::GetWindowDrawList(), ImVec2(min.x + s * 0.12f, min.y + s * 0.12f), s * 0.76f, a.glyph, enabled);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s%s%s%s", a.label(), *a.keys ? " (" : "", a.keys, *a.keys ? ")" : "");
    }
    ImGui::PopID();
    if (pressed && enabled) a.run();
    return pressed;
}

void handleKeys()
{
    if (!hasFocus || ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    ImGuiKeyChord mods = ImGui::GetIO().KeyMods;
    for (const Action& a : actions) {
        if (a.chord == 0) continue;
        ImGuiKey key = (ImGuiKey)(a.chord & ~ImGuiMod_Mask_);
        if ((a.chord & ImGuiMod_Mask_) != mods) continue;
        if (ImGui::IsKeyPressed(key, a.repeat) && a.enabled()) {
            a.run();
            return;
        }
    }
}

/* ---- Hex fields: edit a value, written when the field is left ---------------- */

bool hexField(const char* id, uint32_t value, int digits, bool changed, bool editable, uint32_t* out)
{
    static std::map<ImGuiID, std::string> buffers;
    char shown[16];
    snprintf(shown, sizeof(shown), "%0*X", digits, value);
    if (!editable) {
        if (changed) ImGui::TextColored(ImColor(kRed), "%s", shown);
        else ImGui::TextUnformatted(shown);
        return false;
    }
    ImGuiID gid = ImGui::GetID(id);
    std::string& b = buffers[gid];
    char buf[16];
    if (ImGui::GetActiveID() != gid) b = shown;
    snprintf(buf, sizeof(buf), "%s", b.c_str());
    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText(id, buf, digits + 1, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_AutoSelectAll |
                                          ImGuiInputTextFlags_CharsUppercase);
    ImGui::PopStyleColor(changed ? 2 : 1);
    b = buf;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *out = (uint32_t)strtoul(buf, NULL, 16);
        return true;
    }
    return false;
}

/* ---- Panels ------------------------------------------------------------------- */

void drawDisassembly()
{
    Mono mono;
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0));
    ImGui::BeginChild("##disasm", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeight() + 2;
    float cw = ImGui::CalcTextSize("0").x;
    float gutter = lh + 4;
    if (lines.empty()) {
        ImGui::SetCursorPosX(gutter);
        ImGui::TextDisabled("%s", text("Disassembly unavailable.", "Desensamblado no disponible."));
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    if (scrollToLine >= 0) {
        float h = ImGui::GetWindowHeight();
        float y = scrollToLine * lh, top = ImGui::GetScrollY();
        if (y < top || y + lh > top + h) ImGui::SetScrollY(std::max(0.0f, y - h / 3));
        scrollToLine = -1;
    }
    bool isStale = stale();
    ImU32 textCol = ImGui::GetColorU32(isStale ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImU32 labelCol = IM_COL32(60, 150, 230, 255);

    ImGuiListClipper clipper;
    clipper.Begin((int)lines.size(), lh);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const dbg::Session::Line& l = lines[i];
            ImVec2 p = ImGui::GetCursorScreenPos();
            float width = std::max(ImGui::GetContentRegionAvail().x, cw * 48);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##l", ImVec2(width, lh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            bool rclicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            bool dbl = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            bool isPc = !l.label && session->hasSnapshot() && l.address == session->pc;
            if (i == cursorLine) dl->AddRectFilled(ImVec2(p.x + gutter, p.y), ImVec2(p.x + width, p.y + lh), ImGui::GetColorU32(ImGuiCol_Header));
            else if (isPc) dl->AddRectFilled(ImVec2(p.x + gutter, p.y), ImVec2(p.x + width, p.y + lh), IM_COL32(255, 220, 0, isStale ? 40 : 90));
            float ty = p.y + 1;
            if (!l.label) {
                int bp = session->findCode(l.address);
                if (bp >= 0) {
                    ImVec2 c(p.x + lh / 2 + 1, p.y + lh / 2);
                    if (session->breakpoints()[bp].enabled) dl->AddCircleFilled(c, lh / 2 - 2, IM_COL32(220, 30, 30, 255));
                    dl->AddCircle(c, lh / 2 - 2, IM_COL32(180, 0, 0, 255), 0, 1.5f);
                }
                if (isPc) {
                    float cy = p.y + lh / 2;
                    dl->AddTriangleFilled(ImVec2(p.x + gutter - lh / 2 - 1, cy - lh / 4), ImVec2(p.x + gutter - 2, cy),
                                          ImVec2(p.x + gutter - lh / 2 - 1, cy + lh / 4), IM_COL32(255, 200, 0, 255));
                }
                char addr[8];
                snprintf(addr, sizeof(addr), "%04X:", l.address);
                dl->AddText(ImVec2(p.x + gutter + cw, ty), dimCol, addr);
                dl->AddText(ImVec2(p.x + gutter + 7 * cw, ty), dimCol, l.bytes.c_str());
                dl->AddText(ImVec2(p.x + gutter + 20 * cw, ty), textCol, l.text.c_str());
            }
            else {
                dl->AddText(ImVec2(p.x + gutter + cw, ty), labelCol, l.text.c_str());
            }
            if (clicked) {
                if (ImGui::GetIO().MousePos.x < p.x + gutter && !l.label) session->toggleCode(l.address);
                else cursorLine = i;
            }
            if (dbl && !l.label && ImGui::GetIO().MousePos.x >= p.x + gutter) session->toggleCode(l.address);
            if (rclicked) {
                cursorLine = i;
                ImGui::OpenPopup("##disasmmenu");
            }
        }
    }
    if (ImGui::BeginPopup("##disasmmenu")) {
        menuItem(A_TOGGLE_BP);
        menuItem(A_ENABLE_BP);
        menuItem(A_RUN_TO);
        ImGui::Separator();
        menuItem(A_SHOW_NEXT);
        menuItem(A_GOTO);
        ImGui::EndPopup();
    }

    /* Cursor keys while the listing has the focus. */
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
        int page = std::max(1, (int)(ImGui::GetWindowHeight() / lh) - 1);
        int move = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move = -page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move = page;
        if (move != 0) {
            int line = cursorLine < 0 ? (int)(ImGui::GetScrollY() / lh) : cursorLine;
            cursorLine = std::max(0, std::min((int)lines.size() - 1, line + move));
            scrollToLine = cursorLine;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void drawRegisters()
{
    const auto& regs = session->cpu;
    if (!session->hasSnapshot() || regs.empty()) {
        ImGui::TextDisabled("%s", text("No registers.", "Sin registros."));
        return;
    }
    int rows = ((int)regs.size() + 1) / 2;
    Mono mono;
    if (ImGui::BeginTable("##regs", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        for (int r = 0; r < rows; r++) {
            ImGui::TableNextRow();
            for (int half = 0; half < 2; half++) {
                int i = r + half * rows;
                ImGui::TableNextColumn();
                if (i >= (int)regs.size()) {
                    ImGui::TableNextColumn();
                    continue;
                }
                const dbg::Register& reg = regs[i];
                ImGui::TextUnformatted(reg.name.c_str());
                ImGui::TableNextColumn();
                bool editable = paused() && reg.name != "CLKH" && reg.name != "CLKL" && reg.name != "CNT";
                char id[32];
                snprintf(id, sizeof(id), "##r%d", i);
                uint32_t v;
                if (hexField(id, reg.value, std::max(1, reg.width / 4), reg.changed, editable, &v)) {
                    session->writeCpu(i, v);
                    rebuildListing(false);
                }
            }
        }
        ImGui::EndTable();
    }
    std::vector<dbg::Session::Flag> flags = session->flags(asmFlags);
    float avail = ImGui::GetContentRegionAvail().x;
    float w = (avail - ImGui::GetStyle().ItemSpacing.x * (flags.size() - 1)) / flags.size();
    if (!paused()) ImGui::BeginDisabled();
    for (int i = 0; i < (int)flags.size(); i++) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        if (flags[i].on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
        if (ImGui::Button(flags[i].name.c_str(), ImVec2(w, 0))) {
            session->toggleFlag(i, asmFlags);
            rebuildListing(false);
        }
        if (flags[i].on) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (!paused()) ImGui::EndDisabled();
}

void drawStack()
{
    Mono mono;
    if (ImGui::BeginTable("##stack", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(text("Address", "Dirección"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Dirección ").x);
        ImGui::TableSetupColumn(text("Value", "Valor"));
        ImGui::TableHeadersRow();
        for (const auto& e : session->stackEntries()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%04X", e.address);
            ImGui::TableNextColumn();
            const char* label = session->symbols.find(e.value);
            char t[96];
            snprintf(t, sizeof(t), "%04X%s%s", e.value, label ? "  " : "", label ? label : "");
            if (e.changed && !stale()) ImGui::TextColored(ImColor(kRed), "%s", t);
            else ImGui::TextUnformatted(t);
        }
        ImGui::EndTable();
    }
}

void drawCallstack()
{
    Mono mono;
    if (ImGui::BeginTable("##calls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(text("Return", "Retorno"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Dirección ").x);
        ImGui::TableSetupColumn(text("Call", "Llamada"));
        ImGui::TableHeadersRow();
        int row = 0;
        for (const auto& e : session->callEntries()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const char* label = session->symbols.find(e.ret);
            char r[64];
            snprintf(r, sizeof(r), "%s", label ? label : "");
            if (!label) snprintf(r, sizeof(r), "%04X", e.ret);
            ImGui::PushID(row++);
            if (ImGui::Selectable(r, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                showAddress(e.from, true);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.text.c_str());
        }
        ImGui::EndTable();
    }
}

void drawBreakpoints()
{
    if (ImGui::Button(text("Add Breakpoint", "Añadir punto de ruptura"))) actions[A_ADD_BP].run();
    ImGui::SameLine();
    if (ImGui::Button(text("Add Watchpoint", "Añadir watchpoint"))) actions[A_ADD_WP].run();
    ImGui::SameLine();
    bool sel = bpSelected >= 0 && bpSelected < (int)session->breakpoints().size();
    if (!sel) ImGui::BeginDisabled();
    if (ImGui::Button(text("Remove", "Quitar"))) {
        session->remove(bpSelected);
        bpSelected = -1;
    }
    if (!sel) ImGui::EndDisabled();
    ImGui::SameLine();
    bool any = !session->breakpoints().empty();
    if (!any) ImGui::BeginDisabled();
    if (ImGui::Button(text("Enable All", "Activar todos"))) session->enableAll(true);
    ImGui::SameLine();
    if (ImGui::Button(text("Disable All", "Desactivar todos"))) session->enableAll(false);
    ImGui::SameLine();
    if (ImGui::Button(text("Remove All", "Quitar todos"))) session->removeAll();
    if (!any) ImGui::EndDisabled();

    if (ImGui::BeginTable("##bps", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(text("Type", "Tipo"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("VRAM  ").x);
        ImGui::TableSetupColumn(text("Address", "Dirección"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Dirección   ").x * 1.5f);
        ImGui::TableSetupColumn(text("Condition", "Condición"));
        ImGui::TableSetupColumn(text("Size", "Tamaño"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Tamaño ").x);
        ImGui::TableHeadersRow();
        const auto& list = session->breakpoints();
        for (int i = 0; i < (int)list.size(); i++) {
            const dbg::Breakpoint bp = list[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableNextColumn();
            bool on = bp.enabled;
            if (ImGui::Checkbox("##on", &on)) session->setEnabled(i, on);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(bp.typeName(), bpSelected == i,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                bpSelected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && bp.type == dbg::Breakpoint::CODE) {
                    showAddress(bp.address, true);
                }
            }
            ImGui::TableNextColumn();
            const char* label = bp.type == dbg::Breakpoint::CODE || bp.type == dbg::Breakpoint::MEMORY
                              ? session->symbols.find((uint16_t)bp.address) : nullptr;
            if (label) ImGui::TextUnformatted(label);
            else ImGui::Text("#%0*X", bp.type == dbg::Breakpoint::VRAM ? 5 : bp.type == dbg::Breakpoint::IO ? 2 : 4, bp.address);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(bp.conditionText().c_str());
            ImGui::TableNextColumn();
            if (bp.type != dbg::Breakpoint::CODE) ImGui::Text("%d", bp.size);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

bool viewCombo(const char* id, int* index, const std::vector<std::string>& names)
{
    if (names.empty()) {
        ImGui::TextDisabled("%s", text("Unavailable.", "No disponible."));
        return false;
    }
    if (*index >= (int)names.size()) *index = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, names[*index].c_str())) {
        for (int i = 0; i < (int)names.size(); i++) {
            if (ImGui::Selectable(names[i].c_str(), i == *index)) *index = i;
        }
        ImGui::EndCombo();
    }
    return true;
}

void drawMemory()
{
    std::vector<std::string> names;
    for (const auto& v : session->memories) names.push_back(v.name);
    int before = memView;
    if (!viewCombo("##memview", &memView, names)) return;
    if (before != memView) hexCursor = -1;
    const dbg::MemoryView& v = session->memories[memView];
    bool editable = !v.writeProtected && paused();
    int size = (int)v.data.size();

    Mono mono;
    ImGui::BeginChild("##hex", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove);
    float cw = ImGui::CalcTextSize("0").x, lh = ImGui::GetTextLineHeightWithSpacing();
    int digits = v.start + size > 0x10000 ? 6 : 4;
    float avail = ImGui::GetContentRegionAvail().x / cw - digits - 2;
    int perRow = 8;
    while (perRow * 2 * 4 <= avail && perRow < 32) perRow *= 2;
    int rows = (size + perRow - 1) / perRow;
    float hexX = (digits + 2) * cw, asciiX = hexX + (perRow * 3 + 1) * cw;

    if (hexScrollTo >= 0) {
        float y = (hexScrollTo / perRow) * lh, top = ImGui::GetScrollY(), h = ImGui::GetWindowHeight();
        if (y < top || y + lh > top + h) ImGui::SetScrollY(y);
        hexScrollTo = -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 textCol = ImGui::GetColorU32(stale() ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImU32 sel = ImGui::GetColorU32(ImGuiCol_Header);
    ImGuiListClipper clipper;
    clipper.Begin(rows, lh);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(r);
            ImGui::InvisibleButton("##row", ImVec2(std::max(asciiX + perRow * cw, 1.0f), lh));
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            char t[16];
            snprintf(t, sizeof(t), "%0*X", digits, v.start + r * perRow);
            dl->AddText(ImVec2(p.x + cw / 2, p.y), dim, t);
            for (int i = 0; i < perRow; i++) {
                int o = r * perRow + i;
                if (o >= size) break;
                uint8_t b = v.data[o];
                bool changed = !stale() && o < (int)v.previous.size() && v.previous[o] != b;
                ImU32 c = changed ? kRed : textCol;
                if (o == hexCursor) {
                    dl->AddRectFilled(ImVec2(p.x + hexX + i * 3 * cw - 1, p.y), ImVec2(p.x + hexX + (i * 3 + 2) * cw + 1, p.y + lh), sel);
                    dl->AddRectFilled(ImVec2(p.x + asciiX + i * cw, p.y), ImVec2(p.x + asciiX + (i + 1) * cw, p.y + lh), sel);
                }
                snprintf(t, sizeof(t), "%02X", b);
                dl->AddText(ImVec2(p.x + hexX + i * 3 * cw, p.y), c, t);
                char a[2] = { (char)(b >= 32 && b < 127 ? b : '.'), 0 };
                dl->AddText(ImVec2(p.x + asciiX + i * cw, p.y), changed ? kRed : dim, a);
            }
            if (clicked) {
                float x = ImGui::GetIO().MousePos.x - p.x;
                int col = -1;
                if (x >= hexX && x < asciiX - cw) col = (int)((x - hexX) / (3 * cw));
                else if (x >= asciiX) col = (int)((x - asciiX) / cw);
                if (col >= 0 && col < perRow && r * perRow + col < size) {
                    hexCursor = r * perRow + col;
                    hexLowNibble = false;
                }
            }
        }
    }

    /* Keys: arrows move, hex digits type over the byte. */
    if (ImGui::IsWindowFocused() && hexCursor >= 0 && !ImGui::GetIO().WantTextInput) {
        int page = perRow * std::max(1, (int)(ImGui::GetWindowHeight() / lh) - 1);
        int move = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) move = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) move = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move = -perRow;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move = perRow;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move = -page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move = page;
        if (move) {
            hexCursor = std::max(0, std::min(size - 1, hexCursor + move));
            hexLowNibble = false;
            hexScrollTo = hexCursor;
        }
        else if (editable && ImGui::GetIO().KeyMods == 0) {
            int nibble = -1;
            for (int k = 0; k < 10; k++) {
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + k), false) || ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + k), false)) nibble = k;
            }
            for (int k = 0; k < 6; k++) {
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + k), false)) nibble = 10 + k;
            }
            if (nibble >= 0) {
                uint8_t b = v.data[hexCursor];
                b = hexLowNibble ? (uint8_t)((b & 0xf0) | nibble) : (uint8_t)((b & 0x0f) | (nibble << 4));
                int at = hexCursor, view = memView;
                bool advance = hexLowNibble;
                hexLowNibble = !hexLowNibble;
                session->writeMemory(view, at, { b });
                rebuildListing(false);
                if (advance && hexCursor < size - 1) hexCursor++;
                hexScrollTo = hexCursor;
            }
        }
    }
    ImGui::EndChild();
}

void drawPeripherals()
{
    std::vector<std::string> names;
    for (const auto& v : session->peripherals) names.push_back(v.name);
    if (!viewCombo("##periview", &periView, names)) return;
    const auto& regs = session->peripherals[periView].regs;
    if (ImGui::BeginTable("##peri", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(text("Register", "Registro"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Registro      ").x);
        ImGui::TableSetupColumn(text("Value", "Valor"));
        ImGui::TableHeadersRow();
        for (int k = 0; k < (int)regs.size(); k++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(regs[k].name.c_str());
            ImGui::TableNextColumn();
            char id[24];
            snprintf(id, sizeof(id), "##p%d", k);
            uint32_t v;
            if (hexField(id, regs[k].value, std::max(2, regs[k].width / 4), regs[k].changed, paused(), &v)) {
                session->writePeripheral(periView, k, v);
                return;     /* the snapshot was reread */
            }
        }
        ImGui::EndTable();
    }
}

void drawIoPorts()
{
    std::vector<std::string> names;
    for (const auto& v : session->ioPorts) names.push_back(v.name);
    if (!viewCombo("##ioview", &ioView, names)) return;
    const auto& ports = session->ioPorts[ioView].ports;
    static const char* dir[] = { "", "R", "W", "R/W" };
    if (ImGui::BeginTable("##io", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(text("Port", "Puerto"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Puerto  ").x);
        ImGui::TableSetupColumn("R/W", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("R/W  ").x);
        ImGui::TableSetupColumn(text("Value", "Valor"));
        ImGui::TableHeadersRow();
        for (int k = 0; k < (int)ports.size(); k++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("#%02X", ports[k].port);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dir[ports[k].direction & 3]);
            ImGui::TableNextColumn();
            char id[24];
            snprintf(id, sizeof(id), "##io%d", k);
            uint32_t v;
            if (hexField(id, ports[k].value, 2, ports[k].changed, paused() && (ports[k].direction & 2), &v)) {
                session->writeIoPort(ioView, k, v);
                return;
            }
        }
        ImGui::EndTable();
    }
}

/* ---- Prompts ------------------------------------------------------------------ */

void findNext(bool ask)
{
    if (ask || lastSearch.empty()) {
        snprintf(promptBuf, sizeof(promptBuf), "%s", lastSearch.c_str());
        promptPending = PROMPT_FIND;
        return;
    }
    int at = session->find(memView, lastSearch, hexCursor);
    if (at < 0) {
        setStatus(text("Not found.", "No encontrado."));
        return;
    }
    showBottom = true;
    bottomTab = 1;
    hexCursor = at;
    hexLowNibble = false;
    hexScrollTo = at;
}

bool inputLine(const char* label, bool focus)
{
    if (focus) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
    return ImGui::InputText(label, promptBuf, sizeof(promptBuf), ImGuiInputTextFlags_EnterReturnsTrue);
}

bool promptButtons(bool* ok)
{
    *ok = ImGui::Button(langDlgOK(), ImVec2(ImGui::GetFontSize() * 6, 0));
    ImGui::SameLine();
    bool cancel = ImGui::Button(langDlgCancel(), ImVec2(ImGui::GetFontSize() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    return cancel;
}

void drawPrompts()
{
    static const char* ids[] = { "", "##bp", "##goto", "##find", "##watch", "##close", "##symbols" };
    if (promptPending != PROMPT_NONE) {
        ImGui::OpenPopup(ids[promptPending]);
        promptPending = PROMPT_NONE;
    }
    ImGuiWindowFlags f = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    bool ok;

    if (ImGui::BeginPopupModal("##bp", NULL, f)) {
        ImGui::TextUnformatted(text("Add New Breakpoint", "Añadir punto de ruptura"));
        bool enter = inputLine(text("Address or Label", "Dirección o etiqueta"), ImGui::IsWindowAppearing());
        bool cancel = promptButtons(&ok);
        if (ok || enter) {
            int a = session->parseAddress(promptBuf);
            if (a < 0) setStatus(text("Not a valid address or label.", "Dirección o etiqueta no válida."));
            else if (session->findCode(a) < 0) session->toggleCode(a);
            ImGui::CloseCurrentPopup();
        }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##goto", NULL, f)) {
        ImGui::TextUnformatted(text("Go To Address", "Ir a la dirección"));
        bool enter = inputLine(text("Address or Label", "Dirección o etiqueta"), ImGui::IsWindowAppearing());
        bool cancel = promptButtons(&ok);
        if (ok || enter) {
            int a = session->parseAddress(promptBuf);
            if (a < 0) setStatus(text("Not a valid address or label.", "Dirección o etiqueta no válida."));
            else {
                showAddress(a, true);
                if (memView < (int)session->memories.size() && session->memories[memView].data.size() == 0x10000) {
                    hexCursor = a;
                    hexLowNibble = false;
                    hexScrollTo = a;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##find", NULL, f)) {
        ImGui::TextUnformatted(text("Find", "Buscar"));
        ImGui::TextDisabled("%s", text("\"text\" or hex bytes", "\"texto\" o bytes en hex"));
        bool enter = inputLine("##what", ImGui::IsWindowAppearing());
        bool cancel = promptButtons(&ok);
        if ((ok || enter) && promptBuf[0]) {
            lastSearch = promptBuf;
            ImGui::CloseCurrentPopup();
            findNext(false);
        }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##watch", NULL, f)) {
        ImGui::TextUnformatted(text("Add New Watchpoint", "Añadir watchpoint"));
        const char* types[] = { text("Memory (CPU)", "Memoria (CPU)"), "VRAM", text("I/O port", "Puerto de E/S") };
        const char* conds[] = { text("Any access", "Cualquier acceso"), "==", "!=", "<", ">" };
        const char* sizes[] = { "1", "2", "4" };
        float w = ImGui::GetFontSize() * 14;
        ImGui::SetNextItemWidth(w);
        ImGui::Combo(text("Type", "Tipo"), &watchType, types, 3);
        bool enter = false;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(w);
        enter |= ImGui::InputText(text("Address", "Dirección"), promptBuf, sizeof(promptBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(w);
        ImGui::Combo(text("Condition", "Condición"), &watchCondition, conds, 5);
        if (watchCondition == 0) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(w);
        enter |= ImGui::InputText(text("Value", "Valor"), watchValue, sizeof(watchValue),
                                  ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        if (watchCondition == 0) ImGui::EndDisabled();
        if (watchType == 2) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(w);
        ImGui::Combo(text("Size", "Tamaño"), &watchSize, sizes, 3);
        if (watchType == 2) ImGui::EndDisabled();
        bool cancel = promptButtons(&ok);
        if (ok || enter) {
            int a = session->parseAddress(promptBuf);
            if (a < 0) setStatus(text("Not a valid address or value.", "Dirección o valor no válidos."));
            else {
                dbg::Breakpoint bp;
                bp.type = (dbg::Breakpoint::Type)(watchType + 1);
                bp.address = a;
                bp.condition = (dbg::Breakpoint::Condition)watchCondition;
                bp.value = watchCondition == 0 ? 0 : (int)strtol(watchValue, NULL, 16);
                bp.size = watchType == 2 ? 1 : 1 << watchSize;
                bp.enabled = true;
                session->add(bp);
                bottomTab = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##close", NULL, f)) {
        ImGui::TextUnformatted(text("Breakpoints are still set. Remove them before closing?",
                                    "Aún hay puntos de ruptura. ¿Quitarlos antes de cerrar?"));
        float bw = ImGui::GetFontSize() * 6;
        if (ImGui::Button(langDlgYes(), ImVec2(bw, 0))) {
            session->removeAll();
            windowOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(langDlgNo(), ImVec2(bw, 0))) {
            windowOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(langDlgCancel(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##symbols", NULL, f)) {
        ImGui::TextUnformatted(text("Replace old symbols?", "¿Sustituir los símbolos anteriores?"));
        float bw = ImGui::GetFontSize() * 6;
        if (ImGui::Button(langDlgYes(), ImVec2(bw, 0))) {
            finishSymbols(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(langDlgNo(), ImVec2(bw, 0))) {
            finishSymbols(false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/* ---- The window ------------------------------------------------------------------ */

void drawMenus()
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu(text("File", "Archivo"))) {
        menuItem(A_LOAD_SYM);
        menuItem(A_SAVE_ASM);
        menuItem(A_SAVE_MEM);
        ImGui::Separator();
        menuItem(A_CLOSE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(text("Debug", "Depurar"))) {
        for (int id : { A_CONTINUE, A_BREAK, A_STOP, A_RESTART }) menuItem(id);
        ImGui::Separator();
        for (int id : { A_STEP_BACK, A_STEP_INTO, A_STEP_OVER, A_STEP_OUT, A_RUN_TO, A_SHOW_NEXT }) menuItem(id);
        ImGui::Separator();
        menuItem(A_SYMBOLS);
        ImGui::Separator();
        for (int id : { A_FIND, A_FIND_NEXT, A_GOTO }) menuItem(id);
        ImGui::Separator();
        for (int id : { A_ADD_BP, A_ADD_WP, A_TOGGLE_BP, A_ENABLE_BP, A_REMOVE_ALL, A_ENABLE_ALL, A_DISABLE_ALL }) menuItem(id);
        ImGui::Separator();
        menuItem(A_ASM_FLAGS);
        menuItem(A_VRAM);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(text("Window", "Ventana"))) {
        ImGui::MenuItem(text("Registers, Stack and Callstack", "Registros, pila y llamadas"), NULL, &showRight);
        ImGui::MenuItem(text("Breakpoints, Memory, Peripherals and I/O", "Puntos de ruptura, memoria, periféricos y E/S"), NULL, &showBottom);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void drawToolbar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    int groups[3][6] = {
        { A_CONTINUE, A_BREAK, A_STOP, A_RESTART, -1, -1 },
        { A_SHOW_NEXT, A_STEP_BACK, A_STEP_INTO, A_STEP_OVER, A_STEP_OUT, A_RUN_TO },
        { A_TOGGLE_BP, A_ENABLE_BP, A_REMOVE_ALL, -1, -1, -1 },
    };
    bool first = true;
    for (auto& g : groups) {
        if (!first) {
            ImGui::SameLine(0, 10);
        }
        for (int id : g) {
            if (id < 0) continue;
            if (!first) ImGui::SameLine();
            first = false;
            toolButton(id);
        }
    }
    ImGui::PopStyleVar();
}

/* A horizontal bar to drag; returns the vertical change. */
float splitter(const char* id, float height)
{
    ImGui::InvisibleButton(id, ImVec2(-FLT_MIN, height));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, (a.y + b.y) / 2), ImVec2(b.x, (a.y + b.y) / 2),
                                        ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_Separator));
    return ImGui::IsItemActive() ? ImGui::GetIO().MouseDelta.y : 0.0f;
}

void drawWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x - 20, fs * 62), std::min(io.DisplaySize.y - 40, fs * 42)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(fs * 30, io.DisplaySize.x), std::min(fs * 20, io.DisplaySize.y)),
                                        ImVec2(FLT_MAX, FLT_MAX));
    if (focusNext) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false);
        focusNext = false;
    }
    char title[128];
    snprintf(title, sizeof(title), "blueMSX+ - %s###debugger", text("Debugger", "Depurador"));
    bool open = true;
    bool visible = ImGui::Begin(title, &open, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) {
        if (armed()) promptPending = PROMPT_CLOSE;
        else windowOpen = false;
    }
    hasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    /* Keep it on screen when the main window shrinks (imgui.ini may hold a
    ** place from a bigger one). */
    {
        ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float top = menuBarHeight();
        ImVec2 maxSize(io.DisplaySize.x, io.DisplaySize.y - top);
        ImVec2 fit(std::min(size.x, maxSize.x), std::min(size.y, maxSize.y));
        ImVec2 at(std::max(0.0f, std::min(pos.x, io.DisplaySize.x - fit.x)),
                  std::max(top, std::min(pos.y, io.DisplaySize.y - fit.y)));
        if (fit.x != size.x || fit.y != size.y) ImGui::SetWindowSize(fit);
        if (at.x != pos.x || at.y != pos.y) ImGui::SetWindowPos(at);
    }
    if (!visible) {
        ImGui::End();
        return;
    }

    drawMenus();
    drawToolbar();
    handleKeys();

    float statusH = ImGui::GetTextLineHeightWithSpacing() + 2;
    float splitH = 6;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (bottomHeight <= 0) bottomHeight = avail.y * 0.36f;
    float minPane = fs * 5;
    if (showBottom) bottomHeight = std::max(minPane, std::min(bottomHeight, avail.y - statusH - splitH - minPane));
    float topH = avail.y - statusH - (showBottom ? bottomHeight + splitH : 0);

    ImGui::BeginChild("##top", ImVec2(0, topH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (showRight && ImGui::BeginTable("##split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                                                     ImGuiTableFlags_NoSavedSettings, ImVec2(0, -FLT_MIN))) {
        ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetContentRegionAvail().y);
        ImGui::TableNextColumn();
        drawDisassembly();
        ImGui::TableNextColumn();
        float h = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##regs", ImVec2(0, h * 0.40f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(text("CPU Registers", "Registros de la CPU"));
        ImGui::Separator();
        drawRegisters();
        ImGui::EndChild();
        ImGui::BeginChild("##stackc", ImVec2(0, h * 0.30f - ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(text("Stack", "Pila"));
        drawStack();
        ImGui::EndChild();
        ImGui::BeginChild("##callc", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(text("Callstack", "Pila de llamadas"));
        drawCallstack();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    else if (!showRight) {
        drawDisassembly();
    }
    ImGui::EndChild();

    if (showBottom) {
        bottomHeight -= splitter("##split", splitH);
        ImGui::BeginChild("##bottom", ImVec2(0, bottomHeight), ImGuiChildFlags_None);
        if (ImGui::BeginTabBar("##tabs")) {
            const char* names[4] = { text("Breakpoints", "Puntos de ruptura"), text("Memory", "Memoria"),
                                     text("Peripheral Registers", "Registros de periféricos"), text("I/O Ports", "Puertos de E/S") };
            for (int t = 0; t < 4; t++) {
                ImGuiTabItemFlags tf = bottomTab == t ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(names[t], NULL, tf)) {
                    switch (t) {
                    case 0: drawBreakpoints(); break;
                    case 1: drawMemory(); break;
                    case 2: drawPeripherals(); break;
                    case 3: drawIoPorts(); break;
                    }
                    ImGui::EndTabItem();
                }
            }
            bottomTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    /* Status bar */
    dbg::State s = session->state();
    const char* stateText = s == dbg::RUNNING ? text("Running", "En marcha")
                          : s == dbg::PAUSED ? text("Paused", "En pausa") : text("Stopped", "Detenido");
    if (!status.empty() && SDL_GetTicks() - statusTime < 4000) ImGui::Text("%s    %s", stateText, status.c_str());
    else ImGui::TextUnformatted(stateText);
    if (session->hasSnapshot()) {
        char pc[32];
        snprintf(pc, sizeof(pc), "PC %04X  SP %04X", session->pc, session->sp);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(pc).x);
        ImGui::TextUnformatted(pc);
    }

    drawPrompts();
    ImGui::End();
}

}

void openDebugger()
{
    if (!session) {
        session.reset(new dbg::Session([]() { coreChanged = true; }));
        buildActions();
        loadSettings();
        coreChanged = true;
    }
    windowOpen = true;
    focusNext = true;
}

void drawDebugger()
{
    hasFocus = false;
    if (!session) return;
    coreUpdate();
    if (!windowOpen) return;
    drawWindow();
}

bool debuggerWantsKeys()
{
    return windowOpen && hasFocus;
}

void closeDebugger()
{
    if (!session) return;
    saveSettings();
    session.reset();
}

} // namespace app
