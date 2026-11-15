/*****************************************************************************
** blueMSX+ - Platform layer: frontend interface
**
** The platform layer (Src/Platform) implements every arch* hook the emulator
** core needs on Linux and other POSIX systems. The few that depend on the
** user interface (menus, dialogs, file pickers, window handling, messages)
** are forwarded to a frontend through this table, so the same platform code
** serves the SDL, Dear ImGui and Qt frontends.
**
** Threading: hooks marked [any] can be called from the emulation thread and
** must only queue work for the UI thread. The others are called from the UI
** thread (from actions the frontend itself triggered) and may block.
** Every hook is optional; a NULL entry means "not supported".
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef PLATFORM_FRONTEND_H
#define PLATFORM_FRONTEND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Menus the core asks to pop up (the classic blueMSX themed buttons). */
typedef enum {
    FRONTEND_MENU_FILE, FRONTEND_MENU_RUN, FRONTEND_MENU_RESET,
    FRONTEND_MENU_CART1, FRONTEND_MENU_CART2,
    FRONTEND_MENU_SPECIAL_CART1, FRONTEND_MENU_SPECIAL_CART2,
    FRONTEND_MENU_DISKA, FRONTEND_MENU_DISKB, FRONTEND_MENU_HARDDISK,
    FRONTEND_MENU_CASSETTE, FRONTEND_MENU_PRINTER,
    FRONTEND_MENU_JOYPORT1, FRONTEND_MENU_JOYPORT2,
    FRONTEND_MENU_ZOOM, FRONTEND_MENU_OPTIONS, FRONTEND_MENU_TOOLS,
    FRONTEND_MENU_HELP
} FrontendMenu;

typedef enum {
    FRONTEND_DIALOG_PROPERTIES,     /* arg: PropPage */
    FRONTEND_DIALOG_LANGUAGE,
    FRONTEND_DIALOG_HELP,
    FRONTEND_DIALOG_ABOUT,
    FRONTEND_DIALOG_CASSETTE_POS,
    FRONTEND_DIALOG_SHORTCUTS,
    FRONTEND_DIALOG_KEYBOARD,
    FRONTEND_DIALOG_MIXER,
    FRONTEND_DIALOG_DEBUGGER,
    FRONTEND_DIALOG_TRAINER,
    FRONTEND_DIALOG_MACHINE_EDITOR
} FrontendDialog;

typedef enum {
    FRONTEND_FILE_OPEN_ROM,         /* index: cartridge slot */
    FRONTEND_FILE_OPEN_DISK,        /* index: drive */
    FRONTEND_FILE_OPEN_HARDDISK,    /* index: drive */
    FRONTEND_FILE_OPEN_DISK_DIR,    /* index: drive; pick a directory */
    FRONTEND_FILE_OPEN_CAS,
    FRONTEND_FILE_NEW_CAS,
    FRONTEND_FILE_SAVE_CAS,
    FRONTEND_FILE_OPEN_STATE,
    FRONTEND_FILE_SAVE_STATE,
    FRONTEND_FILE_OPEN_CAPTURE,
    FRONTEND_FILE_SAVE_CAPTURE,
    FRONTEND_FILE_SAVE_GENERIC
} FrontendFileKind;

typedef struct {
    FrontendFileKind kind;
    int         index;          /* slot or drive, when it applies */
    int         save;           /* save dialog */
    int         directory;      /* choose a directory */
    int         allowCreate;    /* the user may name a file that does not exist */
    const char* title;
    const char* defaultDir;
    const char* defaultName;
    /* Filters as "Label|*.rom;*.ri;*.mx1|Label 2|*.zip" (may be NULL). */
    const char* filters;
} FrontendFileRequest;

/* Picking one file inside a zip that holds several. */
typedef struct {
    const char* title;
    const char* description;
    const char* zipName;
    const char* const* items;
    int  count;
    int  selected;      /* preselected item, or -1 */
    int  autostart;     /* in/out: "reset after insert"; -1 hides the option */
} FrontendListRequest;

typedef enum {
    FRONTEND_MESSAGE_INFO,          /* transient notice (toast) */
    FRONTEND_MESSAGE_WARNING,
    FRONTEND_MESSAGE_ERROR          /* modal error */
} FrontendMessageKind;

typedef enum {
    FRONTEND_WINDOW_MINIMIZE,
    FRONTEND_WINDOW_MAXIMIZE,
    FRONTEND_WINDOW_CLOSE
} FrontendWindowCommand;

typedef struct FrontendHooks {
    /* [any] A new emulated frame is ready: render it with platformRenderFrame
    ** on the UI thread, then call platformDisplayDone(). */
    void (*displayUpdate)(void);

    /* [any] Window size, zoom or fullscreen settings changed. */
    void (*windowUpdate)(void);

    /* [any] Menu state (checkmarks, media names) changed. */
    void (*menuUpdate)(int show);

    /* [any] Emulation started (1) or stopped (0). */
    void (*emulationState)(int running);

    /* [any] Notices and errors. */
    void (*message)(FrontendMessageKind kind, const char* text);

    /* [any] The emulator asks to quit. */
    void (*quit)(void);

    /* [any] Grab (1) or release (0) the mouse for an emulated MSX mouse. */
    void (*mouseCapture)(int capture);

    void (*showMenu)(FrontendMenu menu, int x, int y);
    void (*showDialog)(FrontendDialog dialog, int arg);
    void (*windowCommand)(FrontendWindowCommand command);

    /* Modal file chooser. Writes the chosen path into `out` and returns 1,
    ** or returns 0 when cancelled. */
    int  (*chooseFile)(const FrontendFileRequest* request, char* out, int outSize);

    /* Modal list chooser. Returns the chosen index, or -1 when cancelled;
    ** may update request->autostart. */
    int  (*chooseFromList)(FrontendListRequest* request);
} FrontendHooks;

#ifdef __cplusplus
}
#endif

#endif
