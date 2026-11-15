/*****************************************************************************
** blueMSX+ - SysRomLocator
**
** See SysRomLocator.h.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "SysRomLocator.h"
#include "MsxTypes.h"
#include "Sha1.h"
#include "ArchGlob.h"
#include "ArchEvent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_DIRS        16
#define MAX_PATH_LEN    1024
#define MAX_ROM_SIZE    (16 * 1024 * 1024)
#define MAX_SCAN_DEPTH  6

typedef struct {
    char*  path;
    char*  name;        /* lower case file name */
    long   size;
    long   mtime;
    char   sha1[41];    /* empty until hashed */
} IndexEntry;

typedef struct {
    char* relPath;      /* lower case, '/' separators */
    char* hashes;       /* tab separated lower case sha1 values */
} HashEntry;

static char* machineDirs[MAX_DIRS];
static int   machineDirCount = 0;
static char* searchDirs[MAX_DIRS];
static int   searchDirCount = 0;

static char  hashTableFile[MAX_PATH_LEN] = "";
static HashEntry* hashTable = NULL;
static int   hashTableCount = -1;      /* -1: not loaded yet */

static char  cacheFile[MAX_PATH_LEN] = "";
static IndexEntry* fileIndex = NULL;
static int   fileIndexCount = -1;      /* -1: not scanned yet */
static int   fileIndexCap = 0;
static int   fileIndexHashed = 0;      /* every entry has its sha1 */

static void* lock = NULL;

static void lockAcquire(void) { if (lock) archSemaphoreWait(lock, -1); }
static void lockRelease(void) { if (lock) archSemaphoreSignal(lock); }
static void lockInit(void)    { if (lock == NULL) lock = archSemaphoreCreate(1); }

static char* dupString(const char* s)
{
    size_t len = strlen(s) + 1;
    char* d = (char*)malloc(len);
    if (d != NULL) memcpy(d, s, len);
    return d;
}

static void normalizeRel(const char* in, char* out, int outSize)
{
    int i;
    for (i = 0; in[i] && i < outSize - 1; i++) {
        char c = in[i] == '\\' ? '/' : in[i];
        out[i] = (char)tolower((unsigned char)c);
    }
    out[i] = 0;
}

static int fileExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static void addDir(char** list, int* count, const char* dir)
{
    int i;
    if (dir == NULL || *dir == 0 || *count >= MAX_DIRS) return;
    for (i = 0; i < *count; i++) {
        if (strcmp(list[i], dir) == 0) return;
    }
    list[(*count)++] = dupString(dir);
}

void sysRomAddMachineDir(const char* dir)
{
    lockInit();
    addDir(machineDirs, &machineDirCount, dir);
}

int sysRomMachineDirCount(void)
{
    return machineDirCount;
}

const char* sysRomMachineDir(int index)
{
    return index >= 0 && index < machineDirCount ? machineDirs[index] : NULL;
}

void sysRomAddSearchDir(const char* dir)
{
    lockInit();
    addDir(searchDirs, &searchDirCount, dir);
    sysRomRescan();
}

void sysRomSetHashTable(const char* fileName)
{
    lockInit();
    strncpy(hashTableFile, fileName ? fileName : "", sizeof(hashTableFile) - 1);
    hashTableCount = -1;
}

void sysRomSetCacheFile(const char* fileName)
{
    lockInit();
    strncpy(cacheFile, fileName ? fileName : "", sizeof(cacheFile) - 1);
}

/* ---- SHA1 table --------------------------------------------------------- */

static void loadHashTable(void)
{
    char line[2048];
    FILE* f;
    int cap = 0;

    hashTableCount = 0;
    if (hashTableFile[0] == 0 || (f = fopen(hashTableFile, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        char rel[MAX_PATH_LEN];
        char* tab;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (line[0] == '#' || (tab = strchr(line, '\t')) == NULL) continue;
        *tab = 0;
        if (hashTableCount == cap) {
            HashEntry* grown;
            cap = cap ? cap * 2 : 256;
            grown = (HashEntry*)realloc(hashTable, cap * sizeof(HashEntry));
            if (grown == NULL) break;
            hashTable = grown;
        }
        normalizeRel(line, rel, sizeof(rel));
        hashTable[hashTableCount].relPath = dupString(rel);
        hashTable[hashTableCount].hashes  = dupString(tab + 1);
        hashTableCount++;
    }
    fclose(f);
}

static const char* hashesFor(const char* relPath)
{
    char rel[MAX_PATH_LEN];
    int i;
    if (hashTableCount < 0) loadHashTable();
    normalizeRel(relPath, rel, sizeof(rel));
    for (i = 0; i < hashTableCount; i++) {
        if (strcmp(hashTable[i].relPath, rel) == 0) return hashTable[i].hashes;
    }
    return NULL;
}

/* ---- File index ----------------------------------------------------------- */

static int skipByExtension(const char* name)
{
    static const char* skip[] = {
        ".txt", ".md", ".xml", ".htm", ".html", ".pdf", ".png", ".jpg",
        ".jpeg", ".gif", ".ini", ".zip", ".7z", ".gz", ".sh", ".py", NULL
    };
    const char* ext = strrchr(name, '.');
    int i;
    if (ext == NULL) return strcmp(name, "readme") == 0;
    for (i = 0; skip[i]; i++) {
        if (strcmp(ext, skip[i]) == 0) return 1;
    }
    return 0;
}

static void indexAdd(const char* path)
{
    struct stat st;
    const char* base;
    char lowerName[256];

    if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > MAX_ROM_SIZE) return;

    base = strrchr(path, '/');
    if (base == NULL) base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    normalizeRel(base, lowerName, sizeof(lowerName));
    if (skipByExtension(lowerName)) return;

    if (fileIndexCount == fileIndexCap) {
        IndexEntry* grown;
        fileIndexCap = fileIndexCap ? fileIndexCap * 2 : 512;
        grown = (IndexEntry*)realloc(fileIndex, fileIndexCap * sizeof(IndexEntry));
        if (grown == NULL) return;
        fileIndex = grown;
    }
    fileIndex[fileIndexCount].path    = dupString(path);
    fileIndex[fileIndexCount].name    = dupString(lowerName);
    fileIndex[fileIndexCount].size    = (long)st.st_size;
    fileIndex[fileIndexCount].mtime   = (long)st.st_mtime;
    fileIndex[fileIndexCount].sha1[0] = 0;
    fileIndexCount++;
}

static void scanDir(const char* dir, int depth)
{
    char pattern[MAX_PATH_LEN];
    ArchGlob* glob;
    int i;

    if (depth > MAX_SCAN_DEPTH) return;

    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    glob = archGlob(pattern, ARCH_GLOB_FILES);
    if (glob != NULL) {
        for (i = 0; i < glob->count; i++) indexAdd(glob->pathVector[i]);
        archGlobFree(glob);
    }
    glob = archGlob(pattern, ARCH_GLOB_DIRS);
    if (glob != NULL) {
        for (i = 0; i < glob->count; i++) scanDir(glob->pathVector[i], depth + 1);
        archGlobFree(glob);
    }
}

static void loadCache(void)
{
    char line[MAX_PATH_LEN + 128];
    FILE* f;
    if (cacheFile[0] == 0 || (f = fopen(cacheFile, "r")) == NULL) return;
    while (fgets(line, sizeof(line), f)) {
        char sha1[41];
        long size, mtime;
        int pos = 0, i;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (sscanf(line, "%40s %ld %ld %n", sha1, &size, &mtime, &pos) != 3 || pos == 0) continue;
        for (i = 0; i < fileIndexCount; i++) {
            IndexEntry* e = &fileIndex[i];
            if (e->sha1[0] == 0 && e->size == size && e->mtime == mtime &&
                strcmp(e->path, line + pos) == 0) {
                strcpy(e->sha1, sha1);
                break;
            }
        }
    }
    fclose(f);
}

static void saveCache(void)
{
    FILE* f;
    int i;
    if (cacheFile[0] == 0 || (f = fopen(cacheFile, "w")) == NULL) return;
    fprintf(f, "# blueMSX+ system ROM hash cache: sha1 size mtime path\n");
    for (i = 0; i < fileIndexCount; i++) {
        if (fileIndex[i].sha1[0]) {
            fprintf(f, "%s %ld %ld %s\n", fileIndex[i].sha1, fileIndex[i].size,
                    fileIndex[i].mtime, fileIndex[i].path);
        }
    }
    fclose(f);
}

static void buildIndex(void)
{
    int i;
    fileIndexCount = 0;
    fileIndexHashed = 0;
    for (i = 0; i < searchDirCount; i++) scanDir(searchDirs[i], 0);
    loadCache();
}

static void hashAll(void)
{
    int i, changed = 0;
    for (i = 0; i < fileIndexCount; i++) {
        IndexEntry* e = &fileIndex[i];
        FILE* f;
        UInt8* data;
        if (e->sha1[0]) continue;
        if ((f = fopen(e->path, "rb")) == NULL) continue;
        data = (UInt8*)malloc(e->size);
        if (data != NULL && fread(data, 1, e->size, f) == (size_t)e->size) {
            calcSha1Hex(data, (unsigned)e->size, e->sha1);
            changed = 1;
        }
        free(data);
        fclose(f);
    }
    fileIndexHashed = 1;
    if (changed) saveCache();
}

void sysRomRescan(void)
{
    int i;
    lockAcquire();
    for (i = 0; i < fileIndexCount; i++) {
        free(fileIndex[i].path);
        free(fileIndex[i].name);
    }
    fileIndexCount = -1;
    fileIndexHashed = 0;
    lockRelease();
}

/* ---- Resolution ----------------------------------------------------------- */

static int findInSearchDirs(const char* relPath, char* out, int outSize)
{
    const char* hashes;
    const char* base;
    char lowerName[256];
    int i;

    if (searchDirCount == 0) return 0;
    if (fileIndexCount < 0) buildIndex();

    hashes = hashesFor(relPath);
    if (hashes != NULL) {
        for (i = 0; i < fileIndexCount; i++) {
            if (fileIndex[i].sha1[0] && strstr(hashes, fileIndex[i].sha1)) {
                snprintf(out, outSize, "%s", fileIndex[i].path);
                return 1;
            }
        }
        if (!fileIndexHashed) {
            hashAll();
            for (i = 0; i < fileIndexCount; i++) {
                if (fileIndex[i].sha1[0] && strstr(hashes, fileIndex[i].sha1)) {
                    snprintf(out, outSize, "%s", fileIndex[i].path);
                    return 1;
                }
            }
        }
    }

    base = strrchr(relPath, '/');
    if (base == NULL) base = strrchr(relPath, '\\');
    base = base ? base + 1 : relPath;
    normalizeRel(base, lowerName, sizeof(lowerName));
    for (i = 0; i < fileIndexCount; i++) {
        if (strcmp(fileIndex[i].name, lowerName) == 0) {
            snprintf(out, outSize, "%s", fileIndex[i].path);
            return 1;
        }
    }
    return 0;
}

static int resolved(const char* relPath, const char* out, int found)
{
    if (getenv("BLUEMSX_DEBUG_ROMS") != NULL) {
        fprintf(stderr, "sysrom: %s -> %s\n", relPath, found ? out : "(not found)");
    }
    return found;
}

int sysRomResolve(const char* primaryDir, const char* relPath, char* out, int outSize)
{
    char candidate[MAX_PATH_LEN];
    int i, found = 0;

    snprintf(candidate, sizeof(candidate), "%s/%s", primaryDir, relPath);
    if (fileExists(candidate) || (machineDirCount == 0 && searchDirCount == 0)) {
        snprintf(out, outSize, "%s", candidate);
        return resolved(relPath, out, fileExists(candidate));
    }

    for (i = 0; i < machineDirCount; i++) {
        snprintf(out, outSize, "%s/%s", machineDirs[i], relPath);
        if (fileExists(out)) return resolved(relPath, out, 1);
    }

    lockAcquire();
    found = findInSearchDirs(relPath, out, outSize);
    lockRelease();

    if (!found) {
        snprintf(out, outSize, "%s", candidate);
    }
    return resolved(relPath, out, found);
}
