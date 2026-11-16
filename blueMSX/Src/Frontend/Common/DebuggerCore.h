/*****************************************************************************
** blueMSX+ native port - debugger logic shared by the frontends
**
** Everything the Windows SimpleDebugger plugin does that is not drawing:
** the Z80 disassembler and its symbol files, the breakpoint and watchpoint
** list, step over / step out / run to cursor, and a copy of what the core's
** debug snapshot holds (CPU registers, the visible 64 KB, callstack, every
** memory block, peripheral registers and I/O ports) with the previous stop's
** values beside it so a frontend can colour what changed.
**
** Threads: the core reports state changes on the emulation thread; the
** `notify` callback is all that runs there, and the frontend must bring the
** call to its UI thread before touching a Session.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#ifndef DEBUGGER_CORE_H
#define DEBUGGER_CORE_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct BlueDebugger;
struct DbgSnapshot;

namespace dbg {

enum State { STOPPED, PAUSED, RUNNING };

/* Labels from .sym files (tniASM, sjasm, pasmo...): "label: equ 1234h",
** "label equ $1234", "1234h label"... */
class Symbols {
public:
    void clear() { table.clear(); }
    void append(const std::string& text);
    bool empty() const { return table.empty(); }
    size_t size() const { return table.size(); }

    bool shown = false;
    const char* find(uint16_t address) const;              /* when shown */
    bool rfind(const std::string& name, uint16_t* address) const;
    std::string toString(uint16_t address) const;          /* label or #1234 */

private:
    std::map<uint16_t, std::string> table;
};

/* One Z80/R800 instruction at `pc` of a 64 KB image; returns its length.
** The text is padded like the Windows debugger's (mnemonic column). */
int disassemble(const Symbols& symbols, const uint8_t* memory, uint16_t pc, std::string& text);

struct Breakpoint {
    enum Type { CODE, MEMORY, VRAM, IO };
    enum Condition { ANY, EQUALS, NOT_EQUALS, LESS_THAN, GREATER_THAN };    /* = DbgWatchpointCondition */
    Type      type = CODE;
    int       address = 0;
    Condition condition = ANY;
    int       value = 0;
    int       size = 1;
    bool      enabled = true;

    const char* typeName() const;
    std::string conditionText() const;      /* "== #12", "" for ANY */
};

struct Register {
    std::string name;
    int         width;      /* bits */
    uint32_t    value;
    bool        changed;
};

struct MemoryView {
    std::string          name;              /* "device: block" */
    uint32_t             start = 0;
    bool                 writeProtected = false;
    std::vector<uint8_t> data;
    std::vector<uint8_t> previous;          /* same size, last stop */
    void*                block = nullptr;   /* DbgMemoryBlock in the snapshot */
};

struct RegisterView {
    std::string           name;
    std::vector<Register> regs;
    void*                 bank = nullptr;   /* DbgRegisterBank */
};

struct IoPort {
    uint16_t port;
    int      direction;                     /* 1 read, 2 write, 3 both */
    uint8_t  value;
    bool     changed;
};

struct IoView {
    std::string         name;
    std::vector<IoPort> ports;
    void*               ports_ = nullptr;   /* DbgIoPorts */
};

class Session {
public:
    explicit Session(std::function<void()> notify);
    ~Session();

    /* UI thread, after `notify`: reads the core state, and a snapshot when
    ** paused. Returns true when the state changed or the machine stopped
    ** again (a step), i.e. when the views must be refreshed. */
    bool update();
    State state() const { return current; }
    bool hasSnapshot() const { return valid; }

    /* ---- What the last stop showed ------------------------------------ */
    std::vector<Register>     cpu;          /* AF BC DE HL AF' ... PC I R IM IFF1 IFF2 ... */
    uint16_t                  pc = 0, sp = 0;
    bool                      interrupts = false;
    std::vector<uint8_t>      memory;       /* 64 KB as the CPU sees it */
    std::vector<uint8_t>      memoryBefore; /* the stop before */
    std::vector<uint32_t>     callstack;    /* return addresses, oldest first */
    std::vector<MemoryView>   memories;
    std::vector<RegisterView> peripherals;
    std::vector<IoView>       ioPorts;
    int                       cpuIndex(const char* name) const;

    /* ---- Edits (paused only); the snapshot is reread afterwards -------- */
    bool writeCpu(int index, uint32_t value);
    bool writeMemory(int view, uint32_t offset, const std::vector<uint8_t>& bytes);
    bool writePeripheral(int view, int index, uint32_t value);
    bool writeIoPort(int view, int index, uint32_t value);

    /* ---- Control ------------------------------------------------------- */
    void run();
    void pause();
    void stop();
    void restart();
    void stepInto();
    void stepBack();
    void stepOver();
    bool stepOut();                 /* false with an empty callstack */
    void runTo(uint16_t address);
    int  returnAddress() const;     /* for step out, -1 none */

    /* ---- Breakpoints and watchpoints ----------------------------------- */
    const std::vector<Breakpoint>& breakpoints() const { return list; }
    void add(const Breakpoint& bp);             /* replaces same type+address */
    void remove(int index);
    void setEnabled(int index, bool enabled);
    int  findCode(int address) const;           /* index, -1 */
    void toggleCode(int address);               /* F9 */
    void toggleCodeEnabled(int address);        /* Shift+F9 */
    void removeAll();
    void enableAll(bool enabled);
    void vramCheck(bool on);
    bool vramCheck() const { return vramChecking; }

    /* ---- Symbols, listing, search -------------------------------------- */
    Symbols symbols;
    struct Line {
        int         address;
        bool        label;          /* a "name:" line before the instruction */
        std::string bytes;          /* "3e 01" */
        std::string text;
    };
    /* The whole 64 KB from the current memory: decoded from 0 so the
    ** instruction at PC starts a line. */
    std::vector<Line> listing() const;
    bool saveListing(const std::string& file) const;
    bool saveMemory(int view, const std::string& file) const;
    /* "text", or hex bytes "3e 01" / "#3e01"; from `from` + 1, wrapping. */
    int  find(int view, const std::string& what, int from) const;
    /* A label, "#1234", "$1234", "1234h" or plain hex; -1 if neither. */
    int  parseAddress(const std::string& text) const;

    /* Events from the core beyond the state (BASIC "set breakpoint" calls,
    ** trace output). */
    std::vector<std::string> trace;

private:
    static void onStart(void* ref);
    static void onStop(void* ref);
    static void onPause(void* ref);
    static void onResume(void* ref);
    static void onReset(void* ref);
    static void onTrace(void* ref, const char* text);
    static void onSetBp(void* ref, uint16_t slot, uint16_t page, uint16_t address);

    void applyAll();
    void setInCore(const Breakpoint& bp, bool on);
    void clearRunTo();
    void readSnapshot(bool newStop = true);

    std::function<void()> notify;
    BlueDebugger*         debugger = nullptr;
    DbgSnapshot*          snapshot = nullptr;
    State                 current = STOPPED;
    bool                  valid = false;
    std::vector<Breakpoint> list;
    int                   runTo_ = -1;
    bool                  vramChecking = false;

    /* Filled on the emulation thread, read on the UI one. */
    struct Pending;
    Pending*              pending;
    unsigned              seenPauses = 0, seenStarts = 0;
    struct Base;                            /* values at the last stop */
    Base*                 base;
};

}

#endif
