/*****************************************************************************
** blueMSX+ - Dear ImGui frontend: file chooser
**
** An in-window browser (works in fullscreen and with the keyboard), with a
** button that hands over to the desktop's own dialog (xdg portal).
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
#include "Language.h"
}

#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <strings.h>

namespace fs = std::filesystem;

namespace app {

struct Filter {
    std::string label;
    std::vector<std::string> exts;      /* lower case, without the dot; empty = all */
};

struct Entry {
    std::string name;
    bool dir;
    uintmax_t size;
};

static std::string lastDir;
static bool showHidden;

static std::vector<Filter> parseFilters(const char* text)
{
    std::vector<Filter> out;
    if (text != NULL) {
        std::string s = text;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t bar = s.find('|', pos);
            if (bar == std::string::npos) break;
            size_t end = s.find('|', bar + 1);
            if (end == std::string::npos) end = s.size();
            Filter f;
            f.label = s.substr(pos, bar - pos);
            std::string pats = s.substr(bar + 1, end - bar - 1);
            size_t p = 0;
            bool all = false;
            while (p <= pats.size()) {
                size_t semi = pats.find(';', p);
                if (semi == std::string::npos) semi = pats.size();
                std::string pat = pats.substr(p, semi - p);
                if (pat == "*" || pat == "*.*") all = true;
                else if (pat.rfind("*.", 0) == 0) {
                    std::string ext = pat.substr(2);
                    for (auto& c : ext) c = (char)tolower((unsigned char)c);
                    f.exts.push_back(ext);
                }
                p = semi + 1;
            }
            if (all) f.exts.clear();
            out.push_back(f);
            pos = end + 1;
        }
    }
    if (out.empty()) out.push_back(Filter{ "*", {} });
    return out;
}

static bool matches(const Filter& f, const std::string& name)
{
    if (f.exts.empty()) return true;
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return std::find(f.exts.begin(), f.exts.end(), ext) != f.exts.end();
}

static std::vector<Entry> listDir(const std::string& dir, const Filter& filter, bool dirsOnly,
                                  std::string& error)
{
    std::vector<Entry> out;
    std::error_code ec;
    error.clear();
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        error = ec.message();
        return out;
    }
    for (const auto& de : it) {
        std::string name = de.path().filename().string();
        if (!showHidden && !name.empty() && name[0] == '.') continue;
        std::error_code e2;
        bool isDir = de.is_directory(e2);
        if (!isDir && (dirsOnly || !matches(filter, name))) continue;
        uintmax_t size = isDir ? 0 : de.file_size(e2);
        out.push_back(Entry{ name, isDir, e2 ? 0 : size });
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

static std::string sizeText(uintmax_t n)
{
    char buf[32];
    if (n < 1024) snprintf(buf, sizeof(buf), "%ju B", n);
    else if (n < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f kB", n / 1024.0);
    else snprintf(buf, sizeof(buf), "%.1f MB", n / (1024.0 * 1024.0));
    return buf;
}

/* ---- The desktop's dialog ---------------------------------------------------- */

struct NativeWait {
    SDL_AtomicInt done;
    std::string result;
    bool ok;
};

static void SDLCALL nativeDone(void* userdata, const char* const* files, int)
{
    NativeWait* w = (NativeWait*)userdata;
    if (files != NULL && files[0] != NULL) {
        w->result = files[0];
        w->ok = true;
    }
    SDL_SetAtomicInt(&w->done, 1);
}

static int nativeChoose(const FrontendFileRequest* req, const std::vector<Filter>& filters,
                        const std::string& dir, std::string& out)
{
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> sdlFilters;
    for (const auto& f : filters) {
        std::string pat;
        for (const auto& e : f.exts) pat += (pat.empty() ? "" : ";") + e;
        patterns.push_back(f.exts.empty() ? "*" : pat);
    }
    for (size_t i = 0; i < filters.size(); i++) {
        sdlFilters.push_back(SDL_DialogFileFilter{ filters[i].label.c_str(), patterns[i].c_str() });
    }
    std::string location = dir;
    if (req->save && req->defaultName && *req->defaultName) location += "/" + std::string(req->defaultName);

    NativeWait wait;
    SDL_SetAtomicInt(&wait.done, 0);
    wait.ok = false;
    if (req->directory) {
        SDL_ShowOpenFolderDialog(nativeDone, &wait, window, location.c_str(), false);
    }
    else if (req->save) {
        SDL_ShowSaveFileDialog(nativeDone, &wait, window, sdlFilters.data(), (int)sdlFilters.size(),
                               location.c_str());
    }
    else {
        SDL_ShowOpenFileDialog(nativeDone, &wait, window, sdlFilters.data(), (int)sdlFilters.size(),
                               location.c_str(), false);
    }
    runModal([&]() { return SDL_GetAtomicInt(&wait.done) ? 0 : -2; });
    out = wait.result;
    return wait.ok ? 1 : 0;
}

/* ---- The in-window browser --------------------------------------------------- */

int chooseFile(const FrontendFileRequest* req, char* out, int outSize)
{
    std::vector<Filter> filters = parseFilters(req->filters);
    int filterIndex = 0;
    std::string dir;
    std::string error;
    std::vector<Entry> entries;
    int selected = -1;
    char nameBuf[512] = "";
    char pathBuf[1024] = "";
    bool refresh = true;
    bool first = true;
    bool confirmOverwrite = false;
    std::string result;
    const char* home = getenv("HOME");

    if (req->defaultDir && *req->defaultDir && platformIsDir(req->defaultDir)) dir = req->defaultDir;
    else if (!lastDir.empty() && platformIsDir(lastDir.c_str())) dir = lastDir;
    else dir = home ? home : "/";
    if (req->defaultName) snprintf(nameBuf, sizeof(nameBuf), "%s", req->defaultName);

    auto go = [&](const std::string& d) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(d), ec);
        dir = ec ? d : p.string();
        selected = -1;
        refresh = true;
    };
    auto finish = [&](const std::string& path) {
        result = path;
        lastDir = req->directory ? path : fs::path(path).parent_path().string();
        return 1;
    };
    auto acceptName = [&]() -> int {
        if (req->directory) {
            if (selected >= 0 && entries[selected].dir) return finish((fs::path(dir) / entries[selected].name).string());
            return finish(dir);
        }
        std::string name = nameBuf;
        if (name.empty()) return -2;
        fs::path p = name[0] == '/' ? fs::path(name) : fs::path(dir) / name;
        if (platformIsDir(p.string().c_str())) { go(p.string()); nameBuf[0] = 0; return -2; }
        if (req->save) {
            const Filter& f = filters[filterIndex];
            if (!f.exts.empty() && !matches(f, p.filename().string())) p += "." + f.exts[0];
            if (platformIsFile(p.string().c_str()) && !confirmOverwrite) {
                ImGui::OpenPopup("##overwrite");
                return -2;
            }
            return finish(p.string());
        }
        if (!platformIsFile(p.string().c_str()) && !req->allowCreate) return -2;
        return finish(p.string());
    };

    const char* title = req->title && *req->title ? req->title : (req->save ? langDlgSaveAs() : langDlgOpen());
    std::string popupId = std::string(title) + "###filechooser";

    auto browser = [&]() -> int {
        ImGuiIO& io = ImGui::GetIO();
        if (first) {
            ImGui::OpenPopup(popupId.c_str());
            first = false;
        }
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        bool open = true;
        if (!ImGui::BeginPopupModal(popupId.c_str(), &open)) return 0;
        int r = -2;

        if (refresh) {
            entries = listDir(dir, filters[filterIndex], req->directory != 0, error);
            snprintf(pathBuf, sizeof(pathBuf), "%s", dir.c_str());
            refresh = false;
        }

        /* Path bar */
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) go(fs::path(dir).parent_path().string());
        ImGui::SameLine();
        if (ImGui::Button("~") && home) go(home);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (platformIsDir(pathBuf)) go(pathBuf);
            else if (platformIsFile(pathBuf)) { snprintf(nameBuf, sizeof(nameBuf), "%s", pathBuf); r = acceptName(); }
        }

        /* Places */
        float footer = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##places", ImVec2(ImGui::GetFontSize() * 10, -footer), ImGuiChildFlags_Borders);
        struct Place { const char* label; std::string path; };
        std::vector<Place> places;
        if (home) {
            places.push_back({ "Home", home });
            for (const char* sub : { "Desktop", "Documents", "Downloads" }) {
                std::string p = std::string(home) + "/" + sub;
                if (platformIsDir(p.c_str())) places.push_back({ sub, p });
            }
        }
        places.push_back({ "blueMSX+", platformPaths()->configDir });
        if (platformPaths()->legacyDir[0]) places.push_back({ "~/.blueMSX", platformPaths()->legacyDir });
        places.push_back({ "/", "/" });
        for (const auto& pl : places) {
            if (ImGui::Selectable(pl.label, dir == pl.path)) go(pl.path);
        }
        ImGui::EndChild();
        ImGui::SameLine();

        /* Listing */
        ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        if (!error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", error.c_str());
        if (ImGui::BeginTable("##files", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
            for (int i = 0; i < (int)entries.size(); i++) {
                const Entry& e = entries[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                std::string label = (e.dir ? "[" + e.name + "]" : e.name) + "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected == i,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected = i;
                    if (!e.dir) snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (e.dir) go((fs::path(dir) / e.name).string());
                        else r = acceptName();
                    }
                }
                ImGui::TableNextColumn();
                if (!e.dir) ImGui::TextDisabled("%s", sizeText(e.size).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        /* File name, filter and buttons */
        if (!req->directory) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                r = acceptName();
            }
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##filter", filters[filterIndex].label.c_str())) {
            for (int i = 0; i < (int)filters.size(); i++) {
                if (ImGui::Selectable(filters[i].label.c_str(), i == filterIndex)) {
                    filterIndex = i;
                    refresh = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Show hidden files", &showHidden)) refresh = true;
        ImGui::SameLine();
        /* The desktop dialog runs a loop of its own: leave this one first. */
        if (ImGui::Button("System dialog...")) r = 3;
        float bw = ImGui::GetFontSize() * 6;
        ImGui::SameLine(ImGui::GetWindowWidth() - bw * 2 - ImGui::GetStyle().ItemSpacing.x * 2 -
                        ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(req->save ? langDlgSave() : langDlgOK(), ImVec2(bw, 0))) r = acceptName();
        ImGui::SameLine();
        if (ImGui::Button(langDlgCancel(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) r = 0;

        /* Overwrite confirmation */
        if (ImGui::BeginPopupModal("##overwrite", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s already exists. Replace it?", nameBuf);
            if (ImGui::Button(langDlgOK(), ImVec2(bw, 0))) {
                confirmOverwrite = true;
                ImGui::CloseCurrentPopup();
                r = acceptName();
            }
            ImGui::SameLine();
            if (ImGui::Button(langDlgCancel(), ImVec2(bw, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!open) r = 0;
        if (r != -2) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return r;
    };

    int rc;
    for (;;) {
        rc = runModal(browser);
        if (rc != 3) break;
        std::string chosen;
        if (nativeChoose(req, filters, dir, chosen)) {
            finish(chosen);
            rc = 1;
            break;
        }
        first = true;
    }

    if (rc == 1 && !result.empty()) {
        snprintf(out, outSize, "%s", result.c_str());
        return 1;
    }
    return 0;
}

} // namespace app
