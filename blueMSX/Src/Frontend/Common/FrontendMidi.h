/*****************************************************************************
** blueMSX+ native port - MIDI device choices shared by the frontends
**
** One list per selector (MIDI out, MIDI in, Yamaha keyboard): none, a file
** where the core supports it, our unconnected ALSA port and the ports the
** sequencer has now. A device saved in the properties but not present is
** kept in the list, marked, so opening the dialog does not lose it.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef FRONTEND_MIDI_H
#define FRONTEND_MIDI_H

extern "C" {
#include "Language.h"
#include "Platform.h"
#include "Properties.h"
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct MidiChoice {
    std::string label;
    int         type;       /* P_MIDI_NONE / P_MIDI_FILE / P_MIDI_HOST */
    std::string id;
};

static inline const char* midiText(const char* english, const char* spanish)
{
    return langGetLanguage() == EMU_LANG_SPANISH ? spanish : english;
}

/* `selected` gets the entry matching the current properties. */
static inline std::vector<MidiChoice> midiChoices(bool output, bool withFile, int type,
                                                  const char* id, int* selected)
{
    std::vector<MidiChoice> list;
    list.push_back({ langPropPortsNone(), P_MIDI_NONE, "" });
    if (withFile) list.push_back({ langPropPortsFile(), P_MIDI_FILE, "" });
    if (platformMidiAvailable()) {
        list.push_back({ midiText("blueMSX port (connect it yourself)",
                                  "Puerto de blueMSX (conectar a mano)"), P_MIDI_HOST, "virtual" });
        std::vector<PlatformMidiPort> ports(64);
        int n = platformMidiListPorts(output ? 1 : 0, ports.data(), (int)ports.size());
        if (n > (int)ports.size()) n = (int)ports.size();
        for (int i = 0; i < n; i++) list.push_back({ ports[i].name, P_MIDI_HOST, ports[i].id });
    }

    *selected = 0;
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].type == type && (type != P_MIDI_HOST || list[i].id == id)) {
            *selected = (int)i;
            return list;
        }
    }
    if (type == P_MIDI_HOST && id[0] != 0) {
        list.push_back({ std::string(id) + midiText(" (not present)", " (no está)"), P_MIDI_HOST, id });
        *selected = (int)list.size() - 1;
    }
    else if (type == P_MIDI_FILE && !withFile) {
        *selected = 0;
    }
    return list;
}

static inline void midiSelect(const MidiChoice& c, int* type, char* name, size_t nameSize,
                              char* desc, size_t descSize)
{
    *type = c.type;
    if (c.type == P_MIDI_HOST) {
        snprintf(name, nameSize, "%s", c.id.c_str());
        snprintf(desc, descSize, "%s", c.label.c_str());
    }
}

#endif
