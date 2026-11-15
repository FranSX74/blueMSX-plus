/*
** Stubs for arch* hooks added by blueMSX+ that the legacy SDL frontend
** never implemented (toasts, MP4 recording, profiling, extra dialogs).
** Each one is a no-op so the core links; real versions are future work.
*/
#include <stdio.h>
#include <stddef.h>
#include "Properties.h"

void  archCaptureToastInfo(const char* message) { if (message) fprintf(stderr, "[blueMSX+] %s\n", message); }
void  archCaptureToastSaved(const char* savedPath) { if (savedPath) fprintf(stderr, "[blueMSX+] saved %s\n", savedPath); }
void  archEmuSuspendSignal(void) {}
char* archFilenameGetNewCas(Properties* properties) { return NULL; }
char* archFilenameGetSaveCapture(Properties* properties, const char* title, const char* defaultDir,
                                 const char* defaultName, const char* extension, const char* fileTypeLabel) { return NULL; }
int   archRecordVideoIsActive(void) { return 0; }
void  archRecordVideoStart(const char* overrideFilename) {}
void  archRecordVideoStop(void) {}
void  archReplayMissing(const char* fileName) { fprintf(stderr, "[blueMSX+] replay missing: %s\n", fileName); }
void  archReplaySaveFailure(const char* fileName) { fprintf(stderr, "[blueMSX+] replay save failed: %s\n", fileName); }
void  archScreenCaptureAs(void) {}
void  archShowDirAsDskOverflowDialog(int skippedCount, int skippedBytes) {}
void* archThreadBeginEmulationProfile(int enable) { return NULL; }
void  archThreadEndEmulationProfile(void* handle) {}
