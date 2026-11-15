/*****************************************************************************
** blueMSX+ - Platform layer: threads, events, timers and files
**
** Threads, synchronisation and timers use SDL3; files and globbing use POSIX.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "ArchThread.h"
#include "ArchEvent.h"
#include "ArchTimer.h"
#include "ArchFile.h"
#include "ArchGlob.h"

#include <SDL3/SDL.h>

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Threads -------------------------------------------------------------- */

static int threadEntry(void* data)
{
    void (*entryPoint)() = (void (*)())data;
    entryPoint();
    return 0;
}

void* archThreadCreate(void (*entryPoint)(), int priority)
{
    return SDL_CreateThread(threadEntry, "bluemsx-emu", (void*)entryPoint);
}

void* archThreadCreateEx(void (*entryPoint)(), int priority, int stacksize)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Thread* thread;
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void*)threadEntry);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "bluemsx-emu");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, (void*)entryPoint);
    if (stacksize > 0) {
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, stacksize);
    }
    thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    return thread;
}

void archThreadJoin(void* thread, int timeout)
{
    if (thread != NULL) {
        SDL_WaitThread((SDL_Thread*)thread, NULL);
    }
}

void archThreadDestroy(void* thread)
{
    /* Threads cannot be killed; the emulation thread ends on its own once
    ** told to, and archThreadJoin has normally reaped it already. */
}

void archThreadSleep(int milliseconds)
{
    SDL_Delay(milliseconds);
}

void* archThreadBeginEmulationProfile(int enable) { return NULL; }
void  archThreadEndEmulationProfile(void* handle) {}

/* ---- Semaphores and auto-reset events ------------------------------------ */

void* archSemaphoreCreate(int initCount)
{
    return SDL_CreateSemaphore(initCount);
}

void archSemaphoreDestroy(void* semaphore)
{
    SDL_DestroySemaphore((SDL_Semaphore*)semaphore);
}

void archSemaphoreSignal(void* semaphore)
{
    SDL_SignalSemaphore((SDL_Semaphore*)semaphore);
}

void archSemaphoreWait(void* semaphore, int timeout)
{
    SDL_WaitSemaphoreTimeout((SDL_Semaphore*)semaphore, timeout < 0 ? -1 : timeout);
}

typedef struct {
    SDL_Mutex*     mutex;
    SDL_Condition* cond;
    int            state;
} Event;

void* archEventCreate(int initState)
{
    Event* e = (Event*)calloc(1, sizeof(Event));
    e->mutex = SDL_CreateMutex();
    e->cond  = SDL_CreateCondition();
    e->state = initState ? 1 : 0;
    return e;
}

void archEventDestroy(void* event)
{
    Event* e = (Event*)event;
    if (e == NULL) return;
    SDL_DestroyCondition(e->cond);
    SDL_DestroyMutex(e->mutex);
    free(e);
}

void archEventSet(void* event)
{
    Event* e = (Event*)event;
    SDL_LockMutex(e->mutex);
    e->state = 1;
    SDL_SignalCondition(e->cond);
    SDL_UnlockMutex(e->mutex);
}

void archEventWait(void* event, int timeout)
{
    Event* e = (Event*)event;
    SDL_LockMutex(e->mutex);
    if (timeout < 0) {
        while (!e->state) SDL_WaitCondition(e->cond, e->mutex);
    }
    else {
        Uint64 deadline = SDL_GetTicks() + timeout;
        while (!e->state) {
            Uint64 now = SDL_GetTicks();
            if (now >= deadline || !SDL_WaitConditionTimeout(e->cond, e->mutex, (Sint32)(deadline - now))) {
                break;
            }
        }
    }
    e->state = 0;
    SDL_UnlockMutex(e->mutex);
}

/* ---- Timers --------------------------------------------------------------- */

static int (*timerCallback)(void*) = NULL;
static SDL_TimerID timerId = 0;

static Uint64 timerTick(void* userdata, SDL_TimerID id, Uint64 interval)
{
    if (timerCallback != NULL) {
        timerCallback(userdata);
    }
    return interval;
}

void* archCreateTimer(int period, int (*callback)(void*))
{
    timerCallback = callback;
    timerId = SDL_AddTimerNS((Uint64)period * 1000000u, timerTick, NULL);
    return timerId ? (void*)callback : NULL;
}

void archTimerDestroy(void* timer)
{
    if (timerId) {
        SDL_RemoveTimer(timerId);
        timerId = 0;
    }
    timerCallback = NULL;
}

UInt32 archGetSystemUpTime(UInt32 frequency)
{
    Uint64 counter = SDL_GetPerformanceCounter();
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    return (UInt32)((counter / perfFreq) * frequency + (counter % perfFreq) * frequency / perfFreq);
}

UInt32 archGetHiresTimer(void)
{
    return (UInt32)SDL_GetPerformanceCounter();
}

/* ---- Files ---------------------------------------------------------------- */

int archCreateDirectory(const char* pathname)
{
    return mkdir(pathname, 0777);
}

const char* archGetCurrentDirectory(void)
{
    static char buf[1024];
    return getcwd(buf, sizeof(buf)) ? buf : ".";
}

void archSetCurrentDirectory(const char* pathname)
{
    if (chdir(pathname) != 0) {
        /* Nothing sensible to do; callers do not check either. */
    }
}

int archFileExists(const char* fileName)
{
    struct stat s;
    return stat(fileName, &s) == 0;
}

int archFileDelete(const char* fileName)
{
    return remove(fileName) == 0;
}

/* ---- Glob ----------------------------------------------------------------- */

ArchGlob* archGlob(const char* pattern, int flags)
{
    ArchGlob* globHandle;
    glob_t g;
    size_t i;

    if (glob(pattern, GLOB_MARK, NULL, &g) != 0) {
        return NULL;
    }

    globHandle = (ArchGlob*)calloc(1, sizeof(ArchGlob));
    for (i = 0; i < g.gl_pathc; i++) {
        const char* path = g.gl_pathv[i];
        size_t len = strlen(path);
        int isDir = len > 0 && path[len - 1] == '/';
        if ((isDir && (flags & ARCH_GLOB_DIRS)) || (!isDir && (flags & ARCH_GLOB_FILES))) {
            char* store = (char*)calloc(1, len + 1);
            memcpy(store, path, isDir ? len - 1 : len);
            globHandle->pathVector = (char**)realloc(globHandle->pathVector,
                                                     sizeof(char*) * (globHandle->count + 1));
            globHandle->pathVector[globHandle->count++] = store;
        }
    }
    globfree(&g);
    return globHandle;
}

void archGlobFree(ArchGlob* globHandle)
{
    int i;
    if (globHandle == NULL) {
        return;
    }
    for (i = 0; i < globHandle->count; i++) {
        free(globHandle->pathVector[i]);
    }
    free(globHandle->pathVector);
    free(globHandle);
}
