/*****************************************************************************
** blueMSX+ - SysRomLocator
**
** Finds machine system ROMs outside the machine's own folder.
**
** Machine configs name their ROMs by path ("Machines/<machine>/<file>").
** Besides the primary machines directory, the locator looks in:
**   1. extra machine roots (same relative path), e.g. a read-only shared
**      install below the user's writable one;
**   2. search directories, recursively, first by SHA1 (from a table that maps
**      the relative path to the known dumps) and then by file name. This lets
**      a ROM collection with its own naming, such as openMSX's systemroms
**      folder, serve blueMSX machines.
**
** With nothing registered the behaviour is the historical one: the path is
** simply expanded below the primary machines directory.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef SYS_ROM_LOCATOR_H
#define SYS_ROM_LOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Extra directories holding "<machine>/config.ini" trees, searched after the
** primary machines directory in the order added. */
void sysRomAddMachineDir(const char* dir);
int  sysRomMachineDirCount(void);
const char* sysRomMachineDir(int index);

/* Directories searched recursively for ROM files by SHA1 and by name. */
void sysRomAddSearchDir(const char* dir);

/* Tab separated "<path below Machines/>\t<sha1>[\t<sha1>...]" table. */
void sysRomSetHashTable(const char* fileName);

/* Optional file where the SHA1 of every file in the search directories is
** remembered (keyed by path, size and mtime) so they are hashed only once. */
void sysRomSetCacheFile(const char* fileName);

/* Resolves `relPath` (the part after "Machines/") into `out`.
** Returns 1 when an existing file was found. When nothing is found `out`
** holds "<primaryDir>/<relPath>" and 0 is returned. */
int sysRomResolve(const char* primaryDir, const char* relPath, char* out, int outSize);

/* Drops the file index so the search directories are rescanned on next use. */
void sysRomRescan(void);

#ifdef __cplusplus
}
#endif

#endif
