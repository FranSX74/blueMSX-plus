/*****************************************************************************
** QblueMSX - Qt frontend: host keys to USB HID usage codes
**
** The platform layer takes keys as USB HID usages (the SDL_Scancode
** numbering). On Linux, under both X11 and Wayland, Qt reports the XKB
** keycode, which is the evdev code + 8, so the physical key is known
** whatever the layout. Elsewhere, or when no native code is given, the
** Qt::Key is mapped instead.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"

#include <QKeyEvent>

namespace qbm {

/* evdev code -> HID usage (0 = none), as in SDL's scancodes_linux.h. */
static const unsigned char evdevToHid[128] = {
    /*   0 */ 0,   41,  30,  31,  32,  33,  34,  35,
    /*   8 */ 36,  37,  38,  39,  45,  46,  42,  43,
    /*  16 */ 20,  26,  8,   21,  23,  28,  24,  12,
    /*  24 */ 18,  19,  47,  48,  40,  224, 4,   22,
    /*  32 */ 7,   9,   10,  11,  13,  14,  15,  51,
    /*  40 */ 52,  53,  225, 49,  29,  27,  6,   25,
    /*  48 */ 5,   17,  16,  54,  55,  56,  229, 85,
    /*  56 */ 226, 44,  57,  58,  59,  60,  61,  62,
    /*  64 */ 63,  64,  65,  66,  67,  83,  71,  95,
    /*  72 */ 96,  97,  86,  92,  93,  94,  87,  89,
    /*  80 */ 90,  91,  98,  99,  0,   148, 100, 68,
    /*  88 */ 69,  135, 146, 147, 138, 136, 139, 140,
    /*  96 */ 88,  228, 84,  70,  230, 0,   74,  82,
    /* 104 */ 75,  80,  79,  77,  81,  78,  73,  76,
    /* 112 */ 0,   127, 129, 128, 0,   103, 0,   72,
    /* 120 */ 0,   133, 144, 145, 137, 227, 231, 101,
};

static int fromQtKey(const QKeyEvent* e)
{
    int k = e->key();
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return 4 + (k - Qt::Key_A);
    if (k >= Qt::Key_1 && k <= Qt::Key_9) return 30 + (k - Qt::Key_1);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F12) return 58 + (k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_0:          return 39;
    case Qt::Key_Return:     return 40;
    case Qt::Key_Enter:      return 88;
    case Qt::Key_Escape:     return 41;
    case Qt::Key_Backspace:  return 42;
    case Qt::Key_Tab:        return 43;
    case Qt::Key_Space:      return 44;
    case Qt::Key_Minus:      return 45;
    case Qt::Key_Equal:      return 46;
    case Qt::Key_BracketLeft:  return 47;
    case Qt::Key_BracketRight: return 48;
    case Qt::Key_Backslash:  return 49;
    case Qt::Key_Semicolon:  return 51;
    case Qt::Key_Apostrophe: return 52;
    case Qt::Key_QuoteLeft:  return 53;
    case Qt::Key_Comma:      return 54;
    case Qt::Key_Period:     return 55;
    case Qt::Key_Slash:      return 56;
    case Qt::Key_CapsLock:   return 57;
    case Qt::Key_Print:      return 70;
    case Qt::Key_ScrollLock: return 71;
    case Qt::Key_Pause:      return 72;
    case Qt::Key_Insert:     return 73;
    case Qt::Key_Home:       return 74;
    case Qt::Key_PageUp:     return 75;
    case Qt::Key_Delete:     return 76;
    case Qt::Key_End:        return 77;
    case Qt::Key_PageDown:   return 78;
    case Qt::Key_Right:      return 79;
    case Qt::Key_Left:       return 80;
    case Qt::Key_Down:       return 81;
    case Qt::Key_Up:         return 82;
    case Qt::Key_NumLock:    return 83;
    case Qt::Key_Control:    return 224;
    case Qt::Key_Shift:      return 225;
    case Qt::Key_Alt:        return 226;
    case Qt::Key_Meta:       return 227;
    case Qt::Key_AltGr:      return 230;
    default:                 return 0;
    }
}

int hidFromKeyEvent(const QKeyEvent* e)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    quint32 native = e->nativeScanCode();
    if (native >= 8 && native - 8 < sizeof(evdevToHid)) {
        int hid = evdevToHid[native - 8];
        if (hid != 0) return hid;
    }
#endif
    return fromQtKey(e);
}

}
