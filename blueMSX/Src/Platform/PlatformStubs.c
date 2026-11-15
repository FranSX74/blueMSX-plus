/*****************************************************************************
** blueMSX+ - Platform layer: devices not implemented yet on this platform
**
** Each entry behaves like "device absent". Real versions (ALSA MIDI, serial
** ports, TAP networking, printing, FFmpeg recording, screenshots) belong to
** later steps of the Linux port.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "MsxTypes.h"
#include "ArchCdrom.h"
#include "ArchEth.h"
#include "ArchMidi.h"
#include "ArchNotifications.h"
#include "ArchPrinter.h"
#include "ArchUart.h"
#include "ArchVideoIn.h"
#include "ScsiDefs.h"

#include <stdlib.h>

/* CD-ROM */
struct ArchCdrom { int dummy; };
ArchCdrom* archCdromCreate(CdromXferCompCb xferCompCb, void* ref) { return NULL; }
void  archCdromDestroy(ArchCdrom* cdrom) {}
void  archCdromHwReset(ArchCdrom* cdrom) {}
void  archCdromBusReset(ArchCdrom* cdrom) {}
void  archCdromDisconnect(ArchCdrom* cdrom) {}
void  archCdromLoadState(ArchCdrom* cdrom) {}
void  archCdromSaveState(ArchCdrom* cdrom) {}
int   archCdromExecCmd(ArchCdrom* cdrom, const UInt8* cdb, UInt8* buffer, int bufferSize) { return 0; }
int   archCdromIsXferComplete(ArchCdrom* cdrom, int* transferLength) { *transferLength = 0; return 1; }
UInt8 archCdromGetStatusCode(ArchCdrom* cdrom) { return SCSIST_CHECK_CONDITION; }
int   archCdromGetSenseKeyCode(ArchCdrom* cdrom) { return SENSE_INVALID_COMMAND_CODE; }
void* archCdromBufferMalloc(size_t size) { return calloc(1, size); }
void  archCdromBufferFree(void* ptr) { free(ptr); }

/* Ethernet */
void archEthGetMacAddress(UInt8* macAddress) {}
void archEthCreate(void) {}
void archEthDestroy(void) {}
int  archEthSendPacket(UInt8* buffer, UInt32 length) { return 0; }
int  archEthRecvPacket(UInt8** buffer, UInt32* length) { return 0; }

/* MIDI */
void archMidiEnable(int enable) {}
int  archMidiGetNoteOn(void) { return 0; }
void archMidiUpdateVolume(int left, int right) {}
void archMidiLoadState(void) {}
void archMidiSaveState(void) {}
ArchMidi* archMidiInCreate(int device, ArchMidiInCb cb, void* ref) { return NULL; }
void archMidiInDestroy(ArchMidi* archMidi) {}
int  archMidiInGetNoteOn(ArchMidi* archMidi, int note) { return 0; }
ArchMidi* archMidiOutCreate(int device) { return NULL; }
void archMidiOutDestroy(ArchMidi* archMidi) {}
void archMidiOutTransmit(ArchMidi* archMidi, UInt8 value) {}

/* Serial port */
void archUartTransmit(UInt8 value) {}
int  archUartCreate(void (*archUartReceiveCallback)(UInt8)) { return 1; }
void archUartDestroy(void) {}

/* Printer */
void archPrinterWrite(UInt8 value) {}
int  archPrinterCreate(void) { return 1; }
void archPrinterDestroy(void) {}
void archForceFormFeed(void) {}
void archPrinterSetMsxFont(void* romData, UInt32 size) {}

/* Video in (digitizer) */
int     archVideoInIsVideoConnected(void) { return 0; }
UInt16* archVideoInBufferGet(int width, int height) { return NULL; }

/* Screenshots and recording */
void* archScreenCapture(ScreenCaptureType type, int* bitmapSize, int onlyBmp) { *bitmapSize = 0; return NULL; }
void  archScreenCaptureAs(void) {}
int   archScreenCaptureToFile(ScreenCaptureType type, const char* fname) { return 0; }
void  archVideoCaptureSave(void) {}

/* Debug hook */
void archTrap(UInt8 value) {}

/* Profiling counters a theme can show (THEME_TRIGGER_TEXT_TIMER*); the core
** does not time anything on this platform. */
unsigned long long int rdtsc_get_timer(int timer) { return 0; }
