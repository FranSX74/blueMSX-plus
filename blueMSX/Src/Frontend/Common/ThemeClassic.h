/*****************************************************************************
** blueMSX+ - the built-in Classic and Classic Dark themes
******************************************************************************
*/
#ifndef THEME_CLASSIC_H
#define THEME_CLASSIC_H

#include "Theme.h"

#ifdef __cplusplus
extern "C" {
#endif

ThemeCollection* themeClassicCreate(void);
ThemeCollection* themeClassicCreateDark(void);
void themeClassicRebuild(ThemeCollection* tc);

/* Provided by the frontend: a font strip (frameCount glyphs drawn for the
** x2 page) redrawn for zoom z with a real typeface, or NULL to stretch the
** original bitmap. */
struct ArchBitmap;
struct ArchBitmap* archBitmapCreateGlyphFont(struct ArchBitmap* x2Font, int frameCount, int z);

#ifdef __cplusplus
}
#endif

#endif
