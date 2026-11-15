/*****************************************************************************
** blueMSX+ - the built-in Classic and Classic Dark themes, portable version
**
** The theme layout is Win32ThemeClassic.c's, unchanged; only the bitmaps
** are read from files (share/bluemsx/Classic/classic and classic_dark) instead of Windows
** resources. Keep the two in step when the layout changes.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ThemeClassic.h"
#include "Theme.h"
#include "Actions.h"
#include "ArchBitmap.h"
#include "ArchMenu.h"
#include "ArchText.h"
#include "FileHistory.h"
#include "Properties.h"
#include "PlatformPaths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    IDB_CLASSIC_NONE = 0,
    IDB_CLASSIC_BG,
    IDB_CLASSIC_BGSMALL,
    IDB_CLASSIC_FONT,
    IDB_CLASSIC_FONT2,
    IDB_CLASSIC_DISKA,
    IDB_CLASSIC_DISKB,
    IDB_CLASSIC_CAS,
    IDB_CLASSIC_CAPS,
    IDB_CLASSIC_KANA,
    IDB_CLASSIC_FS,
    IDB_CLASSIC_AS,
    IDB_CLASSIC_FPS,
    IDB_CLASSIC_FREQ,
    IDB_CLASSIC_RESET,
    IDB_CLASSIC_PLAY,
    IDB_CLASSIC_PAUSE,
    IDB_CLASSIC_STOP,
    IDB_CLASSIC_CART1,
    IDB_CLASSIC_CART2,
    IDB_CLASSIC_BTDISKA,
    IDB_CLASSIC_BTDISKB,
    IDB_CLASSIC_BTCAS,
    IDB_CLASSIC_BTSIZE,
    IDB_CLASSIC_BTOPTIONS,
    IDB_CLASSIC_BTHELP,
    IDB_CLASSIC_BG_DARK,
    IDB_CLASSIC_BGSMALL_DARK,
    IDB_CLASSIC_FONT_DARK,
    IDB_CLASSIC_FONT2_DARK,
    IDB_CLASSIC_DISKA_DARK,
    IDB_CLASSIC_DISKB_DARK,
    IDB_CLASSIC_CAS_DARK,
    IDB_CLASSIC_CAPS_DARK,
    IDB_CLASSIC_KANA_DARK,
    IDB_CLASSIC_FS_DARK,
    IDB_CLASSIC_AS_DARK,
    IDB_CLASSIC_FPS_DARK,
    IDB_CLASSIC_FREQ_DARK,
    IDB_CLASSIC_RESET_DARK,
    IDB_CLASSIC_PLAY_DARK,
    IDB_CLASSIC_PAUSE_DARK,
    IDB_CLASSIC_STOP_DARK,
    IDB_CLASSIC_CART1_DARK,
    IDB_CLASSIC_CART2_DARK,
    IDB_CLASSIC_BTDISKA_DARK,
    IDB_CLASSIC_BTDISKB_DARK,
    IDB_CLASSIC_BTCAS_DARK,
    IDB_CLASSIC_BTSIZE_DARK,
    IDB_CLASSIC_BTOPTIONS_DARK,
    IDB_CLASSIC_BTHELP_DARK,
    IDB_CLASSIC_COUNT
};

static const char* const classicFiles[IDB_CLASSIC_COUNT] = {
    [IDB_CLASSIC_BG] = "classic/background.bmp",
    [IDB_CLASSIC_BGSMALL] = "classic/backgroundsmall.bmp",
    [IDB_CLASSIC_FONT] = "classic/font.bmp",
    [IDB_CLASSIC_FONT2] = "classic/font2.bmp",
    [IDB_CLASSIC_DISKA] = "classic/ledDiskA.bmp",
    [IDB_CLASSIC_DISKB] = "classic/ledDiskB.bmp",
    [IDB_CLASSIC_CAS] = "classic/ledCas.bmp",
    [IDB_CLASSIC_CAPS] = "classic/ledCaps.bmp",
    [IDB_CLASSIC_KANA] = "classic/ledKana.bmp",
    [IDB_CLASSIC_FS] = "classic/ledFs.bmp",
    [IDB_CLASSIC_AS] = "classic/ledAs.bmp",
    [IDB_CLASSIC_FPS] = "classic/fpsUnit.bmp",
    [IDB_CLASSIC_FREQ] = "classic/freqUnit.bmp",
    [IDB_CLASSIC_RESET] = "classic/buttonReset.bmp",
    [IDB_CLASSIC_PLAY] = "classic/buttonPlay.bmp",
    [IDB_CLASSIC_PAUSE] = "classic/buttonPause.bmp",
    [IDB_CLASSIC_STOP] = "classic/buttonStop.bmp",
    [IDB_CLASSIC_CART1] = "classic/buttonCart1.bmp",
    [IDB_CLASSIC_CART2] = "classic/buttonCart2.bmp",
    [IDB_CLASSIC_BTDISKA] = "classic/buttonDiskA.bmp",
    [IDB_CLASSIC_BTDISKB] = "classic/buttonDiskB.bmp",
    [IDB_CLASSIC_BTCAS] = "classic/buttonCas.bmp",
    [IDB_CLASSIC_BTSIZE] = "classic/buttonSize.bmp",
    [IDB_CLASSIC_BTOPTIONS] = "classic/buttonOptions.bmp",
    [IDB_CLASSIC_BTHELP] = "classic/buttonHelp.bmp",
    [IDB_CLASSIC_BG_DARK] = "classic_dark/background.bmp",
    [IDB_CLASSIC_BGSMALL_DARK] = "classic_dark/backgroundsmall.bmp",
    [IDB_CLASSIC_FONT_DARK] = "classic_dark/font.bmp",
    [IDB_CLASSIC_FONT2_DARK] = "classic_dark/font2.bmp",
    [IDB_CLASSIC_DISKA_DARK] = "classic_dark/ledDiskA.bmp",
    [IDB_CLASSIC_DISKB_DARK] = "classic_dark/ledDiskB.bmp",
    [IDB_CLASSIC_CAS_DARK] = "classic_dark/ledCas.bmp",
    [IDB_CLASSIC_CAPS_DARK] = "classic_dark/ledCaps.bmp",
    [IDB_CLASSIC_KANA_DARK] = "classic_dark/ledKana.bmp",
    [IDB_CLASSIC_FS_DARK] = "classic_dark/ledFs.bmp",
    [IDB_CLASSIC_AS_DARK] = "classic_dark/ledAs.bmp",
    [IDB_CLASSIC_FPS_DARK] = "classic_dark/fpsUnit.bmp",
    [IDB_CLASSIC_FREQ_DARK] = "classic_dark/freqUnit.bmp",
    [IDB_CLASSIC_RESET_DARK] = "classic_dark/buttonReset.bmp",
    [IDB_CLASSIC_PLAY_DARK] = "classic_dark/buttonPlay.bmp",
    [IDB_CLASSIC_PAUSE_DARK] = "classic_dark/buttonPause.bmp",
    [IDB_CLASSIC_STOP_DARK] = "classic_dark/buttonStop.bmp",
    [IDB_CLASSIC_CART1_DARK] = "classic_dark/buttonCart1.bmp",
    [IDB_CLASSIC_CART2_DARK] = "classic_dark/buttonCart2.bmp",
    [IDB_CLASSIC_BTDISKA_DARK] = "classic_dark/buttonDiskA.bmp",
    [IDB_CLASSIC_BTDISKB_DARK] = "classic_dark/buttonDiskB.bmp",
    [IDB_CLASSIC_BTCAS_DARK] = "classic_dark/buttonCas.bmp",
    [IDB_CLASSIC_BTSIZE_DARK] = "classic_dark/buttonSize.bmp",
    [IDB_CLASSIC_BTOPTIONS_DARK] = "classic_dark/buttonOptions.bmp",
    [IDB_CLASSIC_BTHELP_DARK] = "classic_dark/buttonHelp.bmp",
};

static ArchBitmap* classicBitmapFromId(int id)
{
    const PlatformPaths* paths = platformPaths();
    char dir[PLATFORM_PATH_MAX];
    char file[PLATFORM_PATH_MAX];

    if (id <= 0 || id >= IDB_CLASSIC_COUNT || classicFiles[id] == NULL) return NULL;
    platformPathJoin(dir, sizeof(dir), paths->dataDir, "Classic");
    platformPathJoin(file, sizeof(file), dir, classicFiles[id]);
    return archBitmapCreateFromFile(file);
}

/* Resolve the _DARK resource ID paired with a Classic light bitmap.
   Returns 0 when no _DARK pair exists. */
static int classicDarkIdFor(int lightId)
{
    switch (lightId) {
    case IDB_CLASSIC_BG:        return IDB_CLASSIC_BG_DARK;
    case IDB_CLASSIC_FONT:      return IDB_CLASSIC_FONT_DARK;
    case IDB_CLASSIC_DISKA:     return IDB_CLASSIC_DISKA_DARK;
    case IDB_CLASSIC_DISKB:     return IDB_CLASSIC_DISKB_DARK;
    case IDB_CLASSIC_CAS:       return IDB_CLASSIC_CAS_DARK;
    case IDB_CLASSIC_CAPS:      return IDB_CLASSIC_CAPS_DARK;
    case IDB_CLASSIC_KANA:      return IDB_CLASSIC_KANA_DARK;
    case IDB_CLASSIC_FS:        return IDB_CLASSIC_FS_DARK;
    case IDB_CLASSIC_AS:        return IDB_CLASSIC_AS_DARK;
    case IDB_CLASSIC_RESET:     return IDB_CLASSIC_RESET_DARK;
    case IDB_CLASSIC_PLAY:      return IDB_CLASSIC_PLAY_DARK;
    case IDB_CLASSIC_PAUSE:     return IDB_CLASSIC_PAUSE_DARK;
    case IDB_CLASSIC_STOP:      return IDB_CLASSIC_STOP_DARK;
    case IDB_CLASSIC_CART1:     return IDB_CLASSIC_CART1_DARK;
    case IDB_CLASSIC_CART2:     return IDB_CLASSIC_CART2_DARK;
    case IDB_CLASSIC_BTDISKA:   return IDB_CLASSIC_BTDISKA_DARK;
    case IDB_CLASSIC_BTDISKB:   return IDB_CLASSIC_BTDISKB_DARK;
    case IDB_CLASSIC_BTCAS:     return IDB_CLASSIC_BTCAS_DARK;
    case IDB_CLASSIC_BTSIZE:    return IDB_CLASSIC_BTSIZE_DARK;
    case IDB_CLASSIC_BTOPTIONS: return IDB_CLASSIC_BTOPTIONS_DARK;
    case IDB_CLASSIC_BTHELP:    return IDB_CLASSIC_BTHELP_DARK;
    case IDB_CLASSIC_FONT2:     return IDB_CLASSIC_FONT2_DARK;
    case IDB_CLASSIC_BGSMALL:   return IDB_CLASSIC_BGSMALL_DARK;
    case IDB_CLASSIC_FPS:       return IDB_CLASSIC_FPS_DARK;
    case IDB_CLASSIC_FREQ:      return IDB_CLASSIC_FREQ_DARK;
    default:                    return 0;
    }
}

/* Set during theme construction (single-threaded) so loadClassicResBitmap
   knows which resource set to load. */
static int s_classicBuildDark = 0;

/* Menu strip palette for themePageCreate; dark values match the
   per-pixel transform applied to the classic_dark bitmaps. */
static unsigned long classicMenuBgColor(void)
{
    return s_classicBuildDark ? archRGB(45, 46, 46)
                              : archRGB(219, 221, 224);
}
static unsigned long classicMenuFocusColor(void)
{
    return s_classicBuildDark ? archRGB(64, 64, 110)
                              : archRGB(128, 128, 255);
}
static unsigned long classicMenuTextColor(void)
{
    return s_classicBuildDark ? archRGB(220, 220, 224)
                              : archRGB(0, 0, 0);
}

/* Load a Classic bitmap, preferring the _DARK variant during a dark
   build.  Missing _DARK entries fall back to the light artwork so the
   dark set can be filled in incrementally. */
static ArchBitmap* loadClassicResBitmap(int id)
{
    if (s_classicBuildDark) {
        int darkId = classicDarkIdFor(id);
        if (darkId != 0) {
            ArchBitmap* darkBm = classicBitmapFromId(darkId);
            if (darkBm != NULL) return darkBm;
        }
    }
    return classicBitmapFromId(id);
}

/* Design-y of the divider band's top row in the Classic background
   bitmap; anchoring here avoids odd-zoom rounding wobble. */
#define CLASSIC_BG_DIVIDER_Y 19

static ThemePage* themeCreateSmall()
{
    /* vy = vertical offset to push the whole theme down so the
       bitmap's divider top (design-y CLASSIC_BG_DIVIDER_Y) lands at
       screen-y = menuH (just under the menu strip). */
    const int vy = archMenuStripHeight() - CLASSIC_BG_DIVIDER_Y;
    ThemePage* theme = themePageCreate("small",
                                   328,                 // Width
                                   318 + vy,            // Height (extended for shifted content)
                                   4,                   // Emu pos x
                                   51 + vy,             // Emu pos y
                                   320,                 // Emu width
                               240,                 // Emu height
                               0, 
                               0, 
                               328,                 // Menu width (= page width, no right gap)
                               classicMenuBgColor(),
                               classicMenuFocusColor(),
                               classicMenuTextColor(),
                               0,
                               0,
                               NULL);

    /* Background drawn at (0, vy) so the bitmap divider (design-y
       CLASSIC_BG_DIVIDER_Y) sits right below the menu strip. */
    themePageAddImage(theme, activeImageCreate(0, vy, 1, loadClassicResBitmap(IDB_CLASSIC_BGSMALL), 1),    THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(283, 278 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);

    themePageAddImage(theme, activeImageCreate(204, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_DISKA), 2),  THEME_TRIGGER_IMG_DISKA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(222, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_DISKB), 2),  THEME_TRIGGER_IMG_DISKB, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(240, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_CAS), 2),    THEME_TRIGGER_IMG_CAS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(269, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_CAPS), 2),   THEME_TRIGGER_IMG_CAPS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(284, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_KANA), 2),   THEME_TRIGGER_IMG_KANA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(299, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FS), 2),     THEME_TRIGGER_IMG_FS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(314, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_AS), 2),     THEME_TRIGGER_IMG_AS, THEME_TRIGGER_NONE);

    themePageAddImage(theme, activeImageCreate(68, 298 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FPS), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(177, 298 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FREQ), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(143, 299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 5, 0, 0, 1),  THEME_TRIGGER_TEXT_FREQ, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(55,  299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 2, 0, 0, 1),  THEME_TRIGGER_TEXT_FPS, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(96, 299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 5, 0, 0, 0),   THEME_TRIGGER_TEXT_CPU, THEME_TRIGGER_NONE);

    themePageAddDualButton(theme, activeDualButtonCreate(8, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_RESET),
                                              (ButtonEvent)actionEmuResetHard, 0, 0, 
                                              (ButtonEvent)actionMenuReset, 8, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(52, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_CART1),
                                              (ButtonEvent)actionCartInsert1, 0, 0, 
                                              (ButtonEvent)actionMenuCart1, 52, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(90, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_CART2),
                                              (ButtonEvent)actionCartInsert2, 0, 0, 
                                              (ButtonEvent)actionMenuCart2, 90, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(128, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTDISKA),
                                              (ButtonEvent)actionDiskInsertA, 0, 0, 
                                              (ButtonEvent)actionMenuDiskA, 128, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(166, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTDISKB),
                                              (ButtonEvent)actionDiskInsertB, 0, 0, 
                                              (ButtonEvent)actionMenuDiskB, 166, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(204, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTCAS),
                                              (ButtonEvent)actionCasInsert, 0, 0, 
                                              (ButtonEvent)actionMenuCassette, 204, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(251, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTSIZE),
                                              (ButtonEvent)actionWindowSize2x, 0, 0, 
                                              (ButtonEvent)actionMenuZoom, 251, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(289, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTOPTIONS),
                                              (ButtonEvent)actionPropShowEmulation, 0, 0, 
                                              (ButtonEvent)actionMenuOptions, 289, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    return theme;
}


/* NN-stretch the X2 bitmap to (z/2)x; frameCount keeps per-frame width
   integer so odd zooms don't garble sprite-sheet glyphs. */
static ArchBitmap* loadClassicBitmap(int z, int idX2, int frameCount)
{
    ArchBitmap* base = loadClassicResBitmap(idX2);
    ArchBitmap* scaled;
    int origW, origH, dstW, dstH;

    if (base != NULL && (idX2 == IDB_CLASSIC_FONT || idX2 == IDB_CLASSIC_FONT2) && frameCount > 1) {
        /* The frontend redraws the status-line font with a real typeface at
        ** the screen's resolution; a stretched 6-pixel font is hard to read. */
        ArchBitmap* font = archBitmapCreateGlyphFont(base, frameCount, z);
        if (font != NULL) {
            archBitmapDestroy(base);
            return font;
        }
    }
    if (z == 2 || base == NULL) {
        return base;
    }
    origW = archBitmapGetWidth(base);
    origH = archBitmapGetHeight(base);

    if (frameCount > 1 && origW % frameCount == 0) {
        int frameW    = origW / frameCount;
        int dstFrameW = (frameW * z + 1) / 2;   /* round-half-up, matches SC() */
        dstW          = dstFrameW * frameCount;
    } else {
        dstW = (origW * z + 1) / 2;
    }
    dstH = (origH * z + 1) / 2;

    scaled = archBitmapCreateScaledCopy(base, dstW, dstH);
    if (scaled == NULL) {
        return base;  /* keep the unscaled bitmap rather than returning NULL */
    }
    archBitmapDestroy(base);
    return scaled;
}

/* Data-driven layout for x2..x8 from x2 base coords scaled round-half-up.
   topShift compensates for the bitmap's scaled top empty band so the
   button row lands flush against the actual Win32 menu strip height. */
static ThemePage* themeCreateZoom(int z)
{
    ThemePage* theme;
    const int menuH = archMenuStripHeight();

    /* SC: scale x2 base coord by zoom factor, round half up. */
    #define SC(v) (((v) * (z) + 1) / 2)
    /* topShift puts the scaled divider top at screen-y = menuH so the
       full divider band shows just below the menu strip; anchoring on
       SC(19) avoids a 1-px wobble at odd zooms. */
    const int topShift = SC(CLASSIC_BG_DIVIDER_Y) - menuH;
    /* SCY: like SC but for Y, with the topShift compensation applied. */
    #define SCY(v) (SC(v) - topShift)
    /* BMP: X2 bitmap NN-stretched to (z/2)x; `frames` aligns per-frame
       width with activeImage's expectation. */
    #define BMP(name, frames) loadClassicBitmap((z), IDB_CLASSIC_##name, (frames))

    theme = themePageCreate("zoom",
                            SC(648),               // Width
                            SC(558) - topShift,    // Height (top empty band cropped)
                            SC(4),                 // Emu pos x
                            SCY(51),               // Emu pos y
                            320 * z,               // Emu width
                            240 * z,               // Emu height
                            0,                     // Menu pos x
                            0,                     // Menu pos y
                            SC(648),               // Menu width (= page width, no right gap)
                            classicMenuBgColor(),
                            classicMenuFocusColor(),
                            classicMenuTextColor(),
                            0,
                            0,
                            NULL);

    /* Background -- drawn at y = -topShift so its scaled top empty band is
       clipped against the client top edge. */
    themePageAddImage(theme, activeImageCreate(0, -topShift, 1, BMP(BG, 1), 1),
                      THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    /* Frame count = 256 = one cell per glyph so non-integer zooms don't
       scramble the version / build / status / FPS text. */
    themePageAddText(theme, activeTextCreate(SC(538), SCY(515), 256, BMP(FONT2, 256), 0, 256, 5, 0, 0, 0),
                     THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(606), SCY(515), 256, BMP(FONT2, 256), 0, 256, 5, 0, 0, 0),
                     THEME_TRIGGER_TEXT_BUILDNUMBER, THEME_TRIGGER_NONE);

    /* Status row icons. */
    themePageAddImage(theme, activeImageCreate(SC(439), SCY(540), 2, BMP(DISKA, 2), 2), THEME_TRIGGER_IMG_DISKA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(457), SCY(540), 2, BMP(DISKB, 2), 2), THEME_TRIGGER_IMG_DISKB, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(475), SCY(540), 2, BMP(CAS,   2), 2), THEME_TRIGGER_IMG_CAS,   THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(530), SCY(542), 2, BMP(CAPS,  2), 2), THEME_TRIGGER_IMG_CAPS,  THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(571), SCY(542), 2, BMP(KANA,  2), 2), THEME_TRIGGER_IMG_KANA,  THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(602), SCY(542), 2, BMP(FS,    2), 2), THEME_TRIGGER_IMG_FS,    THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(633), SCY(542), 2, BMP(AS,    2), 2), THEME_TRIGGER_IMG_AS,    THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(238), SCY(538), 2, BMP(FPS,   2), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(342), SCY(538), 2, BMP(FREQ,  2), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);

    /* Status row text. */
    themePageAddText(theme, activeTextCreate(SC(370), SCY(539), 256, BMP(FONT, 256), 0, 256, 9, 0, 0, 0), THEME_TRIGGER_TEXT_SCREEN, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(310), SCY(539), 256, BMP(FONT, 256), 0, 256, 5, 0, 0, 1), THEME_TRIGGER_TEXT_FREQ,   THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(225), SCY(539), 256, BMP(FONT, 256), 0, 256, 2, 0, 0, 1), THEME_TRIGGER_TEXT_FPS,    THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(268), SCY(539), 256, BMP(FONT, 256), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_CPU,    THEME_TRIGGER_NONE);

    /* Top button row -- buttons at fixed client y=22 (= SCY(22)) regardless
       of zoom; popup-menu offset y=47 base scaled and shifted likewise. */
    themePageAddDualButton(theme, activeDualButtonCreate(SC(8), SCY(22), 5, 0, BMP(RESET, 5),
                                          (ButtonEvent)actionEmuResetHard, 0, 0, 
                                          (ButtonEvent)actionMenuReset, SC(8), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(52), SCY(22), 4, 0, BMP(PLAY, 4),
                                          (ButtonEvent)actionEmuTogglePause, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_NOT_RUNNING, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(78), SCY(22), 4, 0, BMP(PAUSE, 4),
                                          (ButtonEvent)actionEmuTogglePause, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_RUNNING, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(104), SCY(22), 4, 0, BMP(STOP, 4),
                                          (ButtonEvent)actionEmuStop, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(136), SCY(22), 5, 0, BMP(CART1, 5),
                                          (ButtonEvent)actionCartInsert1, 0, 0, 
                                          (ButtonEvent)actionMenuCart1, SC(136), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(174), SCY(22), 5, 0, BMP(CART2, 5),
                                          (ButtonEvent)actionCartInsert2, 0, 0, 
                                          (ButtonEvent)actionMenuCart2, SC(174), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(212), SCY(22), 5, 0, BMP(BTDISKA, 5),
                                          (ButtonEvent)actionDiskInsertA, 0, 0, 
                                          (ButtonEvent)actionMenuDiskA, SC(212), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(250), SCY(22), 5, 0, BMP(BTDISKB, 5),
                                          (ButtonEvent)actionDiskInsertB, 0, 0, 
                                          (ButtonEvent)actionMenuDiskB, SC(250), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(288), SCY(22), 5, 0, BMP(BTCAS, 5),
                                          (ButtonEvent)actionCasInsert, 0, 0, 
                                          (ButtonEvent)actionMenuCassette, SC(288), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(335), SCY(22), 5, 0, BMP(BTSIZE, 5),
                                          (ButtonEvent)actionWindowSize1x, 0, 0, 
                                          (ButtonEvent)actionMenuZoom, SC(335), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(373), SCY(22), 5, 0, BMP(BTOPTIONS, 5),
                                          (ButtonEvent)actionPropShowEmulation, 0, 0, 
                                          (ButtonEvent)actionMenuOptions, SC(373), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(420), SCY(22), 4, 0, BMP(BTHELP, 4),
                                          (ButtonEvent)actionHelpShowHelp, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    #undef SC
    #undef SCY
    #undef BMP
    return theme;
}

static ThemePage* themeCreateFullscreen() 
{
    ThemePage* theme = themePageCreate("fullscreen",
                               640,                 // Width
                               480,                 // Hidth
                               0,                   // Emu pos x
                               0,                   // Emu pos y
                               640,                 // Emu width
                               480,                 // Emu height
                               0, 
                               0, 
                               640,
                               classicMenuBgColor(),
                               classicMenuFocusColor(),
                               classicMenuTextColor(),
                               0,
                               0,
                               NULL);

    themePageAddImage(theme, activeImageCreate(-4, -51, 1, loadClassicResBitmap(IDB_CLASSIC_BG), 1), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(534, 463, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(602, 463, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_BUILDNUMBER, THEME_TRIGGER_NONE);

    return theme;
}

static void themeClassicPopulate(ThemeCollection* tc)
{
    int z;

    /* x1 has a distinct compact button layout (Play/Pause/Stop/Help omitted)
       and is generated by its own dedicated function. */
    tc->zoom[1] = themeCreate("little");
    themeAddPage(tc->zoom[1], themeCreateSmall());

    /* x2..x8 share the same layout, generated from a single x2 base spec
       scaled by the zoom factor. */
    for (z = 2; z < THEME_ZOOM_COUNT; z++) {
        tc->zoom[z] = themeCreate("zoom");
        themeAddPage(tc->zoom[z], themeCreateZoom(z));
    }

    tc->fullscreen = themeCreate("fullscreen");
    themeAddPage(tc->fullscreen, themeCreateFullscreen());
}

ThemeCollection* themeClassicCreate() 
{
    ThemeCollection* themeCollection = themeCollectionCreate();
    strcpy(themeCollection->name, "Classic");
    s_classicBuildDark = 0;
    themeClassicPopulate(themeCollection);
    return themeCollection;
}

ThemeCollection* themeClassicCreateDark()
{
    ThemeCollection* themeCollection = themeCollectionCreate();
    strcpy(themeCollection->name, "Classic Dark");
    s_classicBuildDark = 1;
    themeClassicPopulate(themeCollection);
    s_classicBuildDark = 0;
    return themeCollection;
}

/* Called from WM_DPICHANGED: re-runs theme creation so the embedded
   archMenuStripHeight() lookups pick up the new DPI. */
void themeClassicRebuild(ThemeCollection* tc)
{
    int z;
    int prevDark;
    if (tc == NULL) return;
    
    for (z = 1; z < THEME_ZOOM_COUNT; z++) {
        if (tc->zoom[z]) {
            themeDestroy(tc->zoom[z]);
            tc->zoom[z] = NULL;
        }
    }
    if (tc->fullscreen) {
        themeDestroy(tc->fullscreen);
        tc->fullscreen = NULL;
    }

    prevDark = s_classicBuildDark;
    s_classicBuildDark = (strcmp(tc->name, "Classic Dark") == 0) ? 1 : 0;
    themeClassicPopulate(tc);
    s_classicBuildDark = prevDark;
}
