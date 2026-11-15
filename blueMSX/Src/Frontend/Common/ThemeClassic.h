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

#ifdef __cplusplus
}
#endif

#endif
