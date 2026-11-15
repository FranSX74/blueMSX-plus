/*****************************************************************************
** blueMSX+ - Platform layer: file locations
**
** Writable settings and user data live in one configuration directory:
**   $BLUEMSX_CONFIG_DIR, if set; otherwise
**   $XDG_CONFIG_HOME/bluemsx (~/.config/bluemsx), or ~/.blueMSX when only that
**   one exists (the classic location, kept working).
** Both ~/.config/bluemsx and ~/.blueMSX are searched for machines and ROMs.
**
** Read-only data (Machines, Databases, ...) comes from the data directory:
**   $BLUEMSX_DATA_DIR, if set; otherwise <exe>/../share/bluemsx, or the
**   executable's own directory when it holds a Machines folder (portable).
**
** System ROMs are also looked up in openMSX's folder:
**   $OPENMSX_USER_DATA/systemroms and ~/.openMSX/share/systemroms.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef PLATFORM_PATHS_H
#define PLATFORM_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_PATH_MAX 512

typedef struct {
    char configDir[PLATFORM_PATH_MAX];      /* writable, settings and user data */
    char legacyDir[PLATFORM_PATH_MAX];      /* the other user dir, read only use; may be empty */
    char dataDir[PLATFORM_PATH_MAX];        /* shipped read-only data; may be empty */
    char openMsxRomDirs[2][PLATFORM_PATH_MAX];  /* openMSX systemroms folders; may be empty */
} PlatformPaths;

/* Works the directories out (creating configDir). Returns 0 on success. */
int  platformPathsInit(const char* argv0);
const PlatformPaths* platformPaths(void);

/* Joins `dir` and `name` into `out`. Returns `out`. */
char* platformPathJoin(char* out, int outSize, const char* dir, const char* name);

/* Creates `dir` and its missing parents. Returns 0 on success. */
int  platformMakeDirs(const char* dir);

int  platformIsDir(const char* path);
int  platformIsFile(const char* path);

#ifdef __cplusplus
}
#endif

#endif
