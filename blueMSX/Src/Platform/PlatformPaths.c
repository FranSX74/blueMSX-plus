/*****************************************************************************
** blueMSX+ - Platform layer: file locations
**
** See PlatformPaths.h.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "PlatformPaths.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static PlatformPaths paths;

const PlatformPaths* platformPaths(void)
{
    return &paths;
}

char* platformPathJoin(char* out, int outSize, const char* dir, const char* name)
{
    size_t len = strlen(dir);
    snprintf(out, outSize, "%s%s%s", dir, (len > 0 && dir[len - 1] == '/') ? "" : "/", name);
    return out;
}

int platformIsDir(const char* path)
{
    struct stat st;
    return path != NULL && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int platformIsFile(const char* path)
{
    struct stat st;
    return path != NULL && *path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int platformMakeDirs(const char* dir)
{
    char buf[PLATFORM_PATH_MAX];
    char* p;

    if (platformIsDir(dir)) return 0;
    snprintf(buf, sizeof(buf), "%s", dir);
    for (p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static const char* homeDir(void)
{
    const char* home = getenv("HOME");
    return home != NULL && *home ? home : ".";
}

static void exeDir(const char* argv0, char* out, int outSize)
{
    char buf[PLATFORM_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    char* slash;

    if (n > 0) {
        buf[n] = 0;
    }
    else if (argv0 == NULL || realpath(argv0, buf) == NULL) {
        snprintf(out, outSize, ".");
        return;
    }
    slash = strrchr(buf, '/');
    if (slash != NULL) *slash = 0;
    snprintf(out, outSize, "%s", buf);
}

static void findDataDir(const char* argv0)
{
    char exe[PLATFORM_PATH_MAX];
    char candidate[PLATFORM_PATH_MAX];
    char probe[PLATFORM_PATH_MAX];
    const char* env = getenv("BLUEMSX_DATA_DIR");

    paths.dataDir[0] = 0;
    if (env != NULL && *env) {
        snprintf(paths.dataDir, sizeof(paths.dataDir), "%s", env);
        return;
    }

    exeDir(argv0, exe, sizeof(exe));

    /* Installed (<prefix>/bin + <prefix>/share/bluemsx) or the build tree,
    ** which mirrors that layout. */
    snprintf(candidate, sizeof(candidate), "%s/../share/bluemsx", exe);
    if (platformIsDir(platformPathJoin(probe, sizeof(probe), candidate, "Machines"))) {
        if (realpath(candidate, paths.dataDir) == NULL) {
            snprintf(paths.dataDir, sizeof(paths.dataDir), "%s", candidate);
        }
        return;
    }

    /* Portable: data next to the executable. */
    if (platformIsDir(platformPathJoin(probe, sizeof(probe), exe, "Machines"))) {
        snprintf(paths.dataDir, sizeof(paths.dataDir), "%s", exe);
        return;
    }

#ifdef BLUEMSX_INSTALL_DATADIR
    snprintf(paths.dataDir, sizeof(paths.dataDir), "%s", BLUEMSX_INSTALL_DATADIR);
#endif
}

static void findConfigDir(void)
{
    char xdg[PLATFORM_PATH_MAX];
    char classic[PLATFORM_PATH_MAX];
    const char* env = getenv("BLUEMSX_CONFIG_DIR");
    const char* xdgHome = getenv("XDG_CONFIG_HOME");

    if (xdgHome != NULL && *xdgHome == '/') {
        snprintf(xdg, sizeof(xdg), "%s/bluemsx", xdgHome);
    }
    else {
        snprintf(xdg, sizeof(xdg), "%s/.config/bluemsx", homeDir());
    }
    snprintf(classic, sizeof(classic), "%s/.blueMSX", homeDir());

    paths.legacyDir[0] = 0;
    if (env != NULL && *env) {
        snprintf(paths.configDir, sizeof(paths.configDir), "%s", env);
    }
    else if (!platformIsDir(xdg) && platformIsDir(classic)) {
        /* Only the classic folder exists: keep using it. */
        snprintf(paths.configDir, sizeof(paths.configDir), "%s", classic);
        return;
    }
    else {
        snprintf(paths.configDir, sizeof(paths.configDir), "%s", xdg);
    }

    if (strcmp(paths.configDir, classic) != 0 && platformIsDir(classic)) {
        snprintf(paths.legacyDir, sizeof(paths.legacyDir), "%s", classic);
    }
}

static void findOpenMsxDirs(void)
{
    const char* userData = getenv("OPENMSX_USER_DATA");
    char dir[PLATFORM_PATH_MAX];
    int n = 0;

    paths.openMsxRomDirs[0][0] = 0;
    paths.openMsxRomDirs[1][0] = 0;

    if (userData != NULL && *userData) {
        snprintf(dir, sizeof(dir), "%s/systemroms", userData);
        if (platformIsDir(dir)) {
            snprintf(paths.openMsxRomDirs[n++], PLATFORM_PATH_MAX, "%s", dir);
        }
    }
    snprintf(dir, sizeof(dir), "%s/.openMSX/share/systemroms", homeDir());
    if (platformIsDir(dir) && (n == 0 || strcmp(dir, paths.openMsxRomDirs[0]) != 0)) {
        snprintf(paths.openMsxRomDirs[n++], PLATFORM_PATH_MAX, "%s", dir);
    }
}

int platformPathsInit(const char* argv0)
{
    memset(&paths, 0, sizeof(paths));
    findConfigDir();
    findDataDir(argv0);
    findOpenMsxDirs();

    if (platformMakeDirs(paths.configDir) != 0) {
        fprintf(stderr, "blueMSX+: cannot create %s: %s\n", paths.configDir, strerror(errno));
        return -1;
    }
    return 0;
}
