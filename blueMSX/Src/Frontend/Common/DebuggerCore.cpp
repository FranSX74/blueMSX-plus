/*****************************************************************************
** blueMSX+ native port - debugger logic shared by the frontends
**
** See DebuggerCore.h. The disassembler tables and the symbol file reader
** come from the Windows SimpleDebugger plugin (Disassembly.cpp,
** SymbolInfo.cpp); the stepping rules from its Breakpoints.cpp.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "DebuggerCore.h"

extern "C" {
#include "Debugger.h"
#include "Language.h"
}

#include <cstdio>
#include <cstring>
#include <mutex>

namespace dbg {

/* ---- Disassembler ------------------------------------------------------- */

static const char* mnemonicXxCb[256] =
{
	"#","#","#","#","#","#","rlc Y"  ,"#",
	"#","#","#","#","#","#","rrc Y"  ,"#",
	"#","#","#","#","#","#","rl Y"   ,"#",
	"#","#","#","#","#","#","rr Y"   ,"#",
	"#","#","#","#","#","#","sla Y"  ,"#",
	"#","#","#","#","#","#","sra Y"  ,"#",
	"#","#","#","#","#","#","sll Y"  ,"#",
	"#","#","#","#","#","#","srl Y"  ,"#",
	"#","#","#","#","#","#","bit 0,Y","#",
	"#","#","#","#","#","#","bit 1,Y","#",
	"#","#","#","#","#","#","bit 2,Y","#",
	"#","#","#","#","#","#","bit 3,Y","#",
	"#","#","#","#","#","#","bit 4,Y","#",
	"#","#","#","#","#","#","bit 5,Y","#",
	"#","#","#","#","#","#","bit 6,Y","#",
	"#","#","#","#","#","#","bit 7,Y","#",
	"#","#","#","#","#","#","res 0,Y","#",
	"#","#","#","#","#","#","res 1,Y","#",
	"#","#","#","#","#","#","res 2,Y","#",
	"#","#","#","#","#","#","res 3,Y","#",
	"#","#","#","#","#","#","res 4,Y","#",
	"#","#","#","#","#","#","res 5,Y","#",
	"#","#","#","#","#","#","res 6,Y","#",
	"#","#","#","#","#","#","res 7,Y","#",
	"#","#","#","#","#","#","set 0,Y","#",
	"#","#","#","#","#","#","set 1,Y","#",
	"#","#","#","#","#","#","set 2,Y","#",
	"#","#","#","#","#","#","set 3,Y","#",
	"#","#","#","#","#","#","set 4,Y","#",
	"#","#","#","#","#","#","set 5,Y","#",
	"#","#","#","#","#","#","set 6,Y","#",
	"#","#","#","#","#","#","set 7,Y","#"
};

static const char* mnemonicCb[256] =
{
	"rlc b"  ,"rlc c"  ,"rlc d"  ,"rlc e"  ,"rlc h"  ,"rlc l"  ,"rlc (hl)"  ,"rlc a"  ,
	"rrc b"  ,"rrc c"  ,"rrc d"  ,"rrc e"  ,"rrc h"  ,"rrc l"  ,"rrc (hl)"  ,"rrc a"  ,
	"rl b"   ,"rl c"   ,"rl d"   ,"rl e"   ,"rl h"   ,"rl l"   ,"rl (hl)"   ,"rl a"   ,
	"rr b"   ,"rr c"   ,"rr d"   ,"rr e"   ,"rr h"   ,"rr l"   ,"rr (hl)"   ,"rr a"   ,
	"sla b"  ,"sla c"  ,"sla d"  ,"sla e"  ,"sla h"  ,"sla l"  ,"sla (hl)"  ,"sla a"  ,
	"sra b"  ,"sra c"  ,"sra d"  ,"sra e"  ,"sra h"  ,"sra l"  ,"sra (hl)"  ,"sra a"  ,
	"sll b"  ,"sll c"  ,"sll d"  ,"sll e"  ,"sll h"  ,"sll l"  ,"sll (hl)"  ,"sll a"  ,
	"srl b"  ,"srl c"  ,"srl d"  ,"srl e"  ,"srl h"  ,"srl l"  ,"srl (hl)"  ,"srl a"  ,
	"bit 0,b","bit 0,c","bit 0,d","bit 0,e","bit 0,h","bit 0,l","bit 0,(hl)","bit 0,a",
	"bit 1,b","bit 1,c","bit 1,d","bit 1,e","bit 1,h","bit 1,l","bit 1,(hl)","bit 1,a",
	"bit 2,b","bit 2,c","bit 2,d","bit 2,e","bit 2,h","bit 2,l","bit 2,(hl)","bit 2,a",
	"bit 3,b","bit 3,c","bit 3,d","bit 3,e","bit 3,h","bit 3,l","bit 3,(hl)","bit 3,a",
	"bit 4,b","bit 4,c","bit 4,d","bit 4,e","bit 4,h","bit 4,l","bit 4,(hl)","bit 4,a",
	"bit 5,b","bit 5,c","bit 5,d","bit 5,e","bit 5,h","bit 5,l","bit 5,(hl)","bit 5,a",
	"bit 6,b","bit 6,c","bit 6,d","bit 6,e","bit 6,h","bit 6,l","bit 6,(hl)","bit 6,a",
	"bit 7,b","bit 7,c","bit 7,d","bit 7,e","bit 7,h","bit 7,l","bit 7,(hl)","bit 7,a",
	"res 0,b","res 0,c","res 0,d","res 0,e","res 0,h","res 0,l","res 0,(hl)","res 0,a",
	"res 1,b","res 1,c","res 1,d","res 1,e","res 1,h","res 1,l","res 1,(hl)","res 1,a",
	"res 2,b","res 2,c","res 2,d","res 2,e","res 2,h","res 2,l","res 2,(hl)","res 2,a",
	"res 3,b","res 3,c","res 3,d","res 3,e","res 3,h","res 3,l","res 3,(hl)","res 3,a",
	"res 4,b","res 4,c","res 4,d","res 4,e","res 4,h","res 4,l","res 4,(hl)","res 4,a",
	"res 5,b","res 5,c","res 5,d","res 5,e","res 5,h","res 5,l","res 5,(hl)","res 5,a",
	"res 6,b","res 6,c","res 6,d","res 6,e","res 6,h","res 6,l","res 6,(hl)","res 6,a",
	"res 7,b","res 7,c","res 7,d","res 7,e","res 7,h","res 7,l","res 7,(hl)","res 7,a",
	"set 0,b","set 0,c","set 0,d","set 0,e","set 0,h","set 0,l","set 0,(hl)","set 0,a",
	"set 1,b","set 1,c","set 1,d","set 1,e","set 1,h","set 1,l","set 1,(hl)","set 1,a",
	"set 2,b","set 2,c","set 2,d","set 2,e","set 2,h","set 2,l","set 2,(hl)","set 2,a",
	"set 3,b","set 3,c","set 3,d","set 3,e","set 3,h","set 3,l","set 3,(hl)","set 3,a",
	"set 4,b","set 4,c","set 4,d","set 4,e","set 4,h","set 4,l","set 4,(hl)","set 4,a",
	"set 5,b","set 5,c","set 5,d","set 5,e","set 5,h","set 5,l","set 5,(hl)","set 5,a",
	"set 6,b","set 6,c","set 6,d","set 6,e","set 6,h","set 6,l","set 6,(hl)","set 6,a",
	"set 7,b","set 7,c","set 7,d","set 7,e","set 7,h","set 7,l","set 7,(hl)","set 7,a"
};

static const char* mnemonicEd[256] =
{
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"in b,(c)","out (c),b","sbc hl,bc","ld (L),bc","neg","retn","im 0","ld i,a",
	"in c,(c)","out (c),c","adc hl,bc","ld bc,(L)","!"  ,"reti","!"   ,"ld r,a",
	"in d,(c)","out (c),d","sbc hl,de","ld (L),de","!"  ,"!"   ,"im 1","ld a,i",
	"in e,(c)","out (c),e","adc hl,de","ld de,(L)","!"  ,"!"   ,"im 2","ld a,r",
	"in h,(c)","out (c),h","sbc hl,hl","ld (L),hl","!"  ,"!"   ,"!"   ,"rrd"   ,
	"in l,(c)","out (c),l","adc hl,hl","ld hl,(L)","!"  ,"!"   ,"!"   ,"rld"   ,
	"in 0,(c)","out (c),0","sbc hl,sp","ld (L),sp","!"  ,"!"   ,"!"   ,"!"     ,
	"in a,(c)","out (c),a","adc hl,sp","ld sp,(L)","!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"!"        ,"!"        ,"!"        ,"!"  ,"!"   ,"!"   ,"!"     ,
	"ldi"     ,"cpi"      ,"ini"      ,"outi"     ,"!"  ,"!"   ,"!"   ,"!"     ,
	"ldd"     ,"cpd"      ,"ind"      ,"outd"     ,"!"  ,"!"   ,"!"   ,"!"     ,
	"ldir"    ,"cpir"     ,"inir"     ,"otir"     ,"!"  ,"!"   ,"!"   ,"!"     ,
	"lddr"    ,"cpdr"     ,"indr"     ,"otdr"     ,"!"  ,"!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,b","!"        ,"muluw hl,bc","!","!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,c","!"        ,"!"        ,"!",  "!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,d","!"        ,"muluw hl,de","!","!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,e","!"        ,"!"        ,"!",  "!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,h","!"        ,"muluw hl,hl","!","!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,l","!"        ,"!"        ,"!",  "!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,(hl)","!"     ,"muluw hl,sp","!","!"   ,"!"   ,"!"     ,
	"!"       ,"mulub a,a","!"        ,"!"        ,"!",  "!"   ,"!"   ,"!"
};

static const char* mnemonicXx[256] =
{
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"add I,bc","@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"add I,de","@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"ld I,W"  ,"ld (L),I","inc I"    ,"inc Ih"  ,"dec Ih"  ,"ld Ih,B","@"      ,
	"@"      ,"add I,I" ,"ld I,(L)","dec I"    ,"inc Il"  ,"dec Il"  ,"ld Il,B","@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"inc X"   ,"dec X"   ,"ld X,B" ,"@"      ,
	"@"      ,"add I,sp","@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"ld b,Ih" ,"ld b,Il" ,"ld b,X" ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"ld c,Ih" ,"ld c,Il" ,"ld c,X" ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"ld d,Ih" ,"ld d,Il" ,"ld d,X" ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"ld e,Ih" ,"ld e,Il" ,"ld e,X" ,"@"      ,
	"ld Ih,b","ld Ih,c" ,"ld Ih,d" ,"ld Ih,e"  ,"ld Ih,h" ,"ld Ih,l" ,"ld h,X" ,"ld Ih,a",
	"ld Il,b","ld Il,c" ,"ld Il,d" ,"ld Il,e"  ,"ld Il,h" ,"ld Il,l" ,"ld l,X" ,"ld Il,a",
	"ld X,b" ,"ld X,c"  ,"ld X,d"  ,"ld X,e"   ,"ld X,h"  ,"ld X,l"  ,"@"      ,"ld X,a" ,
	"@"      ,"@"       ,"@"       ,"@"        ,"ld a,Ih" ,"ld a,Il" ,"ld a,X" ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"add a,Ih","add a,Il","add a,X","@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"adc a,Ih","adc a,Il","adc a,X","@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"sub Ih"  ,"sub Il"  ,"sub X"  ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"sbc a,Ih","sbc a,Il","sbc a,X","@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"and Ih"  ,"and Il"  ,"and X"  ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"xor Ih"  ,"xor Il"  ,"xor X"  ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"or Ih"   ,"or Il"   ,"or X"   ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"cp Ih"   ,"cp Il"   ,"cp X"   ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"fd cb"    ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"pop I"   ,"@"       ,"ex (sp),I","@"       ,"push I"  ,"@"      ,"@"      ,
	"@"      ,"jp (I)"  ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"@"       ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"      ,
	"@"      ,"ld sp,I" ,"@"       ,"@"        ,"@"       ,"@"       ,"@"      ,"@"
};

static const char* mnemonicMain[256] =
{
	"nop"      ,"ld bc,W"  ,"ld (bc),a","inc bc"    ,"inc b"    ,"dec b"    ,"ld b,B"    ,"rlca"     ,
	"ex af,af'","add hl,bc","ld a,(bc)","dec bc"    ,"inc c"    ,"dec c"    ,"ld c,B"    ,"rrca"     ,
	"djnz R"   ,"ld de,W"  ,"ld (de),a","inc de"    ,"inc d"    ,"dec d"    ,"ld d,B"    ,"rla"      ,
	"jr R"     ,"add hl,de","ld a,(de)","dec de"    ,"inc e"    ,"dec e"    ,"ld e,B"    ,"rra"      ,
	"jr nz,R"  ,"ld hl,W"  ,"ld (L),hl","inc hl"    ,"inc h"    ,"dec h"    ,"ld h,B"    ,"daa"      ,
	"jr z,R"   ,"add hl,hl","ld hl,(L)","dec hl"    ,"inc l"    ,"dec l"    ,"ld l,B"    ,"cpl"      ,
	"jr nc,R"  ,"ld sp,W"  ,"ld (L),a" ,"inc sp"    ,"inc (hl)" ,"dec (hl)" ,"ld (hl),B" ,"scf"      ,
	"jr c,R"   ,"add hl,sp","ld a,(L)" ,"dec sp"    ,"inc a"    ,"dec a"    ,"ld a,B"    ,"ccf"      ,
	"ld b,b"   ,"ld b,c"   ,"ld b,d"   ,"ld b,e"    ,"ld b,h"   ,"ld b,l"   ,"ld b,(hl)" ,"ld b,a"   ,
	"ld c,b"   ,"ld c,c"   ,"ld c,d"   ,"ld c,e"    ,"ld c,h"   ,"ld c,l"   ,"ld c,(hl)" ,"ld c,a"   ,
	"ld d,b"   ,"ld d,c"   ,"ld d,d"   ,"ld d,e"    ,"ld d,h"   ,"ld d,l"   ,"ld d,(hl)" ,"ld d,a"   ,
	"ld e,b"   ,"ld e,c"   ,"ld e,d"   ,"ld e,e"    ,"ld e,h"   ,"ld e,l"   ,"ld e,(hl)" ,"ld e,a"   ,
	"ld h,b"   ,"ld h,c"   ,"ld h,d"   ,"ld h,e"    ,"ld h,h"   ,"ld h,l"   ,"ld h,(hl)" ,"ld h,a"   ,
	"ld l,b"   ,"ld l,c"   ,"ld l,d"   ,"ld l,e"    ,"ld l,h"   ,"ld l,l"   ,"ld l,(hl)" ,"ld l,a"   ,
	"ld (hl),b","ld (hl),c","ld (hl),d","ld (hl),e" ,"ld (hl),h","ld (hl),l","halt"      ,"ld (hl),a",
	"ld a,b"   ,"ld a,c"   ,"ld a,d"   ,"ld a,e"    ,"ld a,h"   ,"ld a,l"   ,"ld a,(hl)" ,"ld a,a"   ,
	"add a,b"  ,"add a,c"  ,"add a,d"  ,"add a,e"   ,"add a,h"  ,"add a,l"  ,"add a,(hl)","add a,a"  ,
	"adc a,b"  ,"adc a,c"  ,"adc a,d"  ,"adc a,e"   ,"adc a,h"  ,"adc a,l"  ,"adc a,(hl)","adc a,a"  ,
	"sub b"    ,"sub c"    ,"sub d"    ,"sub e"     ,"sub h"    ,"sub l"    ,"sub (hl)"  ,"sub a"    ,
	"sbc a,b"  ,"sbc a,c"  ,"sbc a,d"  ,"sbc a,e"   ,"sbc a,h"  ,"sbc a,l"  ,"sbc a,(hl)","sbc a,a"  ,
	"and b"    ,"and c"    ,"and d"    ,"and e"     ,"and h"    ,"and l"    ,"and (hl)"  ,"and a"    ,
	"xor b"    ,"xor c"    ,"xor d"    ,"xor e"     ,"xor h"    ,"xor l"    ,"xor (hl)"  ,"xor a"    ,
	"or b"     ,"or c"     ,"or d"     ,"or e"      ,"or h"     ,"or l"     ,"or (hl)"   ,"or a"     ,
	"cp b"     ,"cp c"     ,"cp d"     ,"cp e"      ,"cp h"     ,"cp l"     ,"cp (hl)"   ,"cp a"     ,
	"ret nz"   ,"pop bc"   ,"jp nz,L"  ,"jp L"      ,"call nz,L","push bc"  ,"add a,B"   ,"rst $"    ,
	"ret z"    ,"ret"      ,"jp z,L"   ,"cb"        ,"call z,L" ,"call L"   ,"adc a,B"   ,"rst $"    ,
	"ret nc"   ,"pop de"   ,"jp nc,L"  ,"out (B),a" ,"call nc,L","push de"  ,"sub B"     ,"rst $"    ,
	"ret c"    ,"exx"      ,"jp c,L"   ,"in a,(B)"  ,"call c,L" ,"dd"       ,"sbc a,B"   ,"rst $"    ,
	"ret po"   ,"pop hl"   ,"jp po,L"  ,"ex (sp),hl","call po,L","push hl"  ,"and B"     ,"rst $"    ,
	"ret pe"   ,"jp (hl)"  ,"jp pe,L"  ,"ex de,hl"  ,"call pe,L","ed"       ,"xor B"     ,"rst $"    ,
	"ret p"    ,"pop af"   ,"jp p,L"   ,"di"        ,"call p,L" ,"push af"  ,"or B"      ,"rst $"    ,
	"ret m"    ,"ld sp,hl" ,"jp m,L"   ,"ei"        ,"call m,L" ,"fd"       ,"cp B"      ,"rst $"
};


#define SIGN(val) (((val) & 128) ? '-' : '+')
#define ABS(val)  (((val) & 128) ? 256 - (val) : val)

static void pad(std::string& s, int column)
{
    int k = (int)s.size();
    s.append(k < column - 1 ? column - k : 1, ' ');
}

int disassemble(const Symbols& symbols, const uint8_t* memory, uint16_t PC, std::string& dest)
{
    const char* r = "INTERNAL PROGRAM ERROR";
    signed char offset = 0;
    const char* S;
    char buf[64];
    int pc = PC;
    uint8_t val0, val1 = 0, val2 = 0, val;
    auto at = [memory](int a) { return memory[a & 0xffff]; };

    dest.clear();
    val0 = at(pc++);

    switch (val0) {
    case 0xcb:
        val1 = at(pc++);
        S = mnemonicCb[val1];
        break;
    case 0xed:
        val1 = at(pc++);
        S = mnemonicEd[val1];
        break;
    case 0xdd:
    case 0xfd:
        r = val0 == 0xdd ? "ix" : "iy";
        val1 = at(pc++);
        if (val1 == 0xcb) {
            val2 = at(pc++);
            offset = (signed char)val2;
            S = mnemonicXxCb[at(pc++)];
        }
        else {
            S = mnemonicXx[val1];
        }
        break;
    default:
        S = mnemonicMain[val0];
        break;
    }

    for (int j = 0; S[j]; j++) {
        switch (S[j]) {
        case 'B':
            snprintf(buf, sizeof(buf), "#%02x", at(pc++));
            dest += buf;
            break;
        case 'R':
            dest += symbols.toString((uint16_t)(PC + 2 + (signed char)at(pc++)));
            break;
        case 'L':
        case 'W':
            val = at(pc++);
            dest += symbols.toString((uint16_t)(val + at(pc++) * 256));
            break;
        case 'X':
            val = at(pc++);
            snprintf(buf, sizeof(buf), "(%s%c#%02x)", r, SIGN(val), ABS(val));
            dest += buf;
            break;
        case 'Y':
            snprintf(buf, sizeof(buf), "(%s%c#%02x)", r, SIGN((uint8_t)offset), ABS((uint8_t)offset));
            dest += buf;
            break;
        case 'I':
            dest += r;
            break;
        case '!':
            snprintf(buf, sizeof(buf), "db     #ED, #%02x", val1);
            dest = buf;
            return 2;
        case '@':
            snprintf(buf, sizeof(buf), "db     #%02x", val0);
            dest = buf;
            return 1;
        case '#':
            snprintf(buf, sizeof(buf), "db     #%02x,#CB,#%02x", val0, val2);
            dest = buf;
            return 2;
        case '$': {
            uint16_t addr = val0 - 199;
            const char* symbol = symbols.find(addr);
            if (symbol == nullptr) {
                snprintf(buf, sizeof(buf), "%02xh", addr);
                dest += buf;
            }
            else {
                dest += symbol;
            }
            return 1;
        }
        case ' ':
            pad(dest, 7);
            break;
        default:
            dest += S[j];
            break;
        }
    }
    return pc - PC;
}

/* ---- Symbols ------------------------------------------------------------ */

const char* Symbols::find(uint16_t address) const
{
    if (!shown) return nullptr;
    auto i = table.find(address);
    return i == table.end() ? nullptr : i->second.c_str();
}

bool Symbols::rfind(const std::string& name, uint16_t* address) const
{
    for (const auto& s : table) {
        if (s.second == name) {
            *address = s.first;
            return true;
        }
    }
    return false;
}

std::string Symbols::toString(uint16_t address) const
{
    const char* name = find(address);
    if (name != nullptr) return name;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%04x", address);
    return buf;
}

static int hexLength(const char* t)
{
    int l = (int)strlen(t);
    if (l == 0) return 0;
    if (t[l - 1] == 'h' || t[l - 1] == 'H') l--;
    else if (t[0] == '$' || t[0] == '#') {
        t++;
        l--;
    }
    bool hex = l > 0;
    for (int i = 0; i < l; i++) hex &= isxdigit((unsigned char)t[i]) != 0;
    return hex ? (int)strlen(t) : 0;
}

void Symbols::append(const std::string& text)
{
    size_t index = 0;
    while (index <= text.size()) {
        size_t next = text.find('\n', index);
        std::string line = text.substr(index, next == std::string::npos ? std::string::npos : next - index);
        index = next == std::string::npos ? text.size() + 1 : next + 1;
        if (line.empty()) continue;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s", line.c_str());
        char* save = nullptr;
        char* t1 = strtok_r(buffer, "\r\n\t ", &save);
        char* t2 = strtok_r(nullptr, "\r\n\t ", &save);
        if (t2 != nullptr && (strcmp(t2, "equ") == 0 || strcmp(t2, "EQU") == 0)) {
            t2 = strtok_r(nullptr, "\r\n\t ", &save);
        }
        if (t1 == nullptr || t2 == nullptr) continue;

        char* addr = t2;
        char* label = t1;
        if (hexLength(t1) > hexLength(t2)) {
            addr = t1;
            label = t2;
        }
        size_t labelLen = strlen(label);
        if (labelLen > 0 && label[labelLen - 1] == ':') label[--labelLen] = 0;
        unsigned value;
        int count = sscanf(addr, "%xh", &value);
        if (count != 1) count = sscanf(addr, "$%x", &value);
        if (count != 1) count = sscanf(addr, "#%x", &value);
        if (count == 1 && labelLen > 0) table[(uint16_t)value] = label;
    }
}

/* ---- Breakpoints -------------------------------------------------------- */

const char* Breakpoint::typeName() const
{
    switch (type) {
    case CODE:   return "BP";
    case MEMORY: return "MEM";
    case VRAM:   return "VRAM";
    case IO:     return "IO";
    }
    return "";
}

std::string Breakpoint::conditionText() const
{
    static const char* ops[] = { "", "==", "!=", "<", ">" };
    if (type == CODE || condition == ANY) return "";
    char buf[32];
    snprintf(buf, sizeof(buf), size > 1 ? "%s #%04x" : "%s #%02x", ops[condition], value);
    return buf;
}

static DbgDeviceType deviceType(Breakpoint::Type type)
{
    switch (type) {
    case Breakpoint::MEMORY: return DBGTYPE_CPU;
    case Breakpoint::VRAM:   return DBGTYPE_VIDEO;
    case Breakpoint::IO:     return DBGTYPE_PORT;
    default:                 return DBGTYPE_UNKNOWN;
    }
}

/* ---- Session ------------------------------------------------------------ */

struct Session::Pending {
    std::mutex               lock;
    std::vector<std::string> trace;
    std::vector<uint16_t>    breakpoints;
    /* A step goes paused -> running -> paused before the UI looks, and a
    ** restart stopped -> running: count the events, the state alone hides them. */
    unsigned                 pauses = 0;
    unsigned                 starts = 0;
};

struct Session::Base {
    std::map<std::string, std::vector<uint32_t>> periph;
    std::map<std::string, std::vector<uint8_t>>  io;
    std::map<std::string, std::vector<uint8_t>>  mem;
    std::vector<uint32_t>                        cpu;
};

Session::Session(std::function<void()> notify) : notify(std::move(notify)), pending(new Pending), base(new Base)
{
    debugger = debuggerCreate(onStart, onStop, onPause, onResume, onReset, onTrace, onSetBp, this);
    current = (State)dbgGetState();
    if (current == PAUSED) readSnapshot();
}

Session::~Session()
{
    clearRunTo();
    debuggerDestroy(debugger);
    if (snapshot != nullptr) dbgSnapshotDestroy(snapshot);
    if (vramChecking) dbgEnableVramAccessCheck(0);
    delete pending;
    delete base;
}

void Session::onStart(void* ref)
{
    Session* s = static_cast<Session*>(ref);
    {
        std::lock_guard<std::mutex> l(s->pending->lock);
        s->pending->starts++;
    }
    s->notify();
}
void Session::onStop(void* ref)   { static_cast<Session*>(ref)->notify(); }
void Session::onPause(void* ref)
{
    Session* s = static_cast<Session*>(ref);
    {
        std::lock_guard<std::mutex> l(s->pending->lock);
        s->pending->pauses++;
    }
    s->notify();
}
void Session::onResume(void* ref) { static_cast<Session*>(ref)->notify(); }
void Session::onReset(void* ref)  { static_cast<Session*>(ref)->notify(); }

void Session::onTrace(void* ref, const char* text)
{
    Session* s = static_cast<Session*>(ref);
    {
        std::lock_guard<std::mutex> l(s->pending->lock);
        s->pending->trace.push_back(text);
    }
    s->notify();
}

void Session::onSetBp(void* ref, uint16_t slot, uint16_t page, uint16_t address)
{
    Session* s = static_cast<Session*>(ref);
    {
        std::lock_guard<std::mutex> l(s->pending->lock);
        s->pending->breakpoints.push_back(address);
    }
    s->notify();
}

bool Session::update()
{
    std::vector<uint16_t> bps;
    bool newPause, newStart;
    {
        std::lock_guard<std::mutex> l(pending->lock);
        trace.insert(trace.end(), pending->trace.begin(), pending->trace.end());
        pending->trace.clear();
        bps.swap(pending->breakpoints);
        newPause = pending->pauses != seenPauses;
        newStart = pending->starts != seenStarts;
        seenPauses = pending->pauses;
        seenStarts = pending->starts;
    }
    for (uint16_t a : bps) {
        if (findCode(a) < 0) {
            Breakpoint bp;
            bp.address = a;
            add(bp);
        }
    }

    State now = (State)dbgGetState();
    bool changed = now != current;
    if (now != STOPPED && ((changed && current == STOPPED) || newStart)) applyAll();     /* a new machine */
    current = now;

    if (now == PAUSED) {
        if (changed || newPause || !valid) {
            clearRunTo();
            readSnapshot();
            changed = true;
        }
    }
    else if (now == STOPPED) {
        valid = false;
    }
    return changed;
}

int Session::cpuIndex(const char* name) const
{
    for (size_t i = 0; i < cpu.size(); i++) {
        if (cpu[i].name == name) return (int)i;
    }
    return -1;
}

void Session::readSnapshot(bool newStop)
{
    if (snapshot != nullptr) dbgSnapshotDestroy(snapshot);
    snapshot = dbgSnapshotCreate();
    valid = false;
    if (snapshot == nullptr) return;

    /* What changed is measured against the last stop; an edit while paused
    ** rereads the snapshot without moving that base. */
    if (newStop) {
        base->periph.clear();
        base->io.clear();
        base->mem.clear();
        base->cpu.clear();
        for (const RegisterView& v : peripherals) {
            for (const Register& r : v.regs) base->periph[v.name].push_back(r.value);
        }
        for (const IoView& v : ioPorts) {
            for (const IoPort& p : v.ports) base->io[v.name].push_back(p.value);
        }
        for (MemoryView& v : memories) base->mem[v.name].swap(v.data);
        for (const Register& r : cpu) base->cpu.push_back(r.value);
        memoryBefore.swap(memory);
    }
    const auto& oldPeriph = base->periph;
    const auto& oldIo = base->io;
    const auto& oldMem = base->mem;
    const auto& oldCpu = base->cpu;

    memory.clear();
    memories.clear();
    peripherals.clear();
    ioPorts.clear();
    callstack.clear();
    bool haveCpu = false;

    int devices = dbgSnapshotGetDeviceCount(snapshot);
    for (int d = 0; d < devices; d++) {
        DbgDevice* device = const_cast<DbgDevice*>(dbgSnapshotGetDevice(snapshot, d));
        if (device == nullptr) continue;
        bool isCpu = device->type == DBGTYPE_CPU && !haveCpu;

        int blocks = dbgDeviceGetMemoryBlockCount(device);
        for (int m = 0; m < blocks; m++) {
            DbgMemoryBlock* b = const_cast<DbgMemoryBlock*>(dbgDeviceGetMemoryBlock(device, m));
            MemoryView v;
            v.name = blocks > 1 || strcmp(device->name, b->name) != 0
                   ? std::string(device->name) + ": " + b->name : std::string(device->name);
            v.start = b->startAddress;
            v.writeProtected = b->writeProtected != 0;
            v.data.assign(b->memory, b->memory + b->size);
            auto old = oldMem.find(v.name);
            v.previous = old != oldMem.end() && old->second.size() == v.data.size() ? old->second : v.data;
            if (old == oldMem.end()) base->mem[v.name] = v.data;     /* first stop */
            v.block = b;
            if (isCpu && m == 0 && b->size == 0x10000) memory = v.data;
            memories.push_back(std::move(v));
        }

        int banks = dbgDeviceGetRegisterBankCount(device);
        for (int r = 0; r < banks; r++) {
            DbgRegisterBank* bank = const_cast<DbgRegisterBank*>(dbgDeviceGetRegisterBank(device, r));
            std::vector<Register> regs;
            for (UInt32 k = 0; k < bank->count; k++) {
                regs.push_back(Register{ bank->reg[k].name, bank->reg[k].width, bank->reg[k].value, false });
            }
            if (isCpu && r == 0) {
                for (size_t k = 0; k < regs.size(); k++) {
                    regs[k].changed = k < oldCpu.size() && oldCpu[k] != regs[k].value;
                }
                cpu = regs;
                if (base->cpu.empty()) {
                    for (const Register& reg : regs) base->cpu.push_back(reg.value);
                }
                continue;
            }
            RegisterView v;
            v.name = banks > 1 ? std::string(device->name) + ": " + bank->name : std::string(device->name);
            auto old = oldPeriph.find(v.name);
            for (size_t k = 0; k < regs.size(); k++) {
                regs[k].changed = old != oldPeriph.end() && k < old->second.size() && old->second[k] != regs[k].value;
            }
            if (old == oldPeriph.end()) {
                for (const Register& reg : regs) base->periph[v.name].push_back(reg.value);
            }
            v.regs = regs;
            v.bank = bank;
            peripherals.push_back(std::move(v));
        }

        int groups = dbgDeviceGetIoPortsCount(device);
        for (int g = 0; g < groups; g++) {
            DbgIoPorts* ports = const_cast<DbgIoPorts*>(dbgDeviceGetIoPorts(device, g));
            IoView v;
            v.name = groups > 1 ? std::string(device->name) + ": " + ports->name : std::string(device->name);
            auto old = oldIo.find(v.name);
            for (UInt32 k = 0; k < ports->count; k++) {
                bool changed = old != oldIo.end() && k < old->second.size() && old->second[k] != ports->port[k].value;
                v.ports.push_back(IoPort{ ports->port[k].port, ports->port[k].direction, ports->port[k].value, changed });
            }
            if (old == oldIo.end()) {
                for (const IoPort& p : v.ports) base->io[v.name].push_back(p.value);
            }
            v.ports_ = ports;
            ioPorts.push_back(std::move(v));
        }

        if (isCpu) {
            haveCpu = true;
            const DbgCallstack* stack = dbgDeviceGetCallstack(device, 0);
            if (stack != nullptr) callstack.assign(stack->callstack, stack->callstack + stack->size);
        }
    }

    if (!haveCpu || memory.size() != 0x10000) return;
    if (memoryBefore.size() != memory.size()) memoryBefore = memory;
    int i = cpuIndex("PC");
    pc = i >= 0 ? (uint16_t)cpu[i].value : 0;
    i = cpuIndex("SP");
    sp = i >= 0 ? (uint16_t)cpu[i].value : 0;
    i = cpuIndex("IFF1");
    interrupts = i >= 0 && cpu[i].value != 0;
    valid = true;
}

/* ---- Edits -------------------------------------------------------------- */

static DbgRegisterBank* cpuBank(DbgSnapshot* snapshot)
{
    int devices = dbgSnapshotGetDeviceCount(snapshot);
    for (int d = 0; d < devices; d++) {
        DbgDevice* device = const_cast<DbgDevice*>(dbgSnapshotGetDevice(snapshot, d));
        if (device != nullptr && device->type == DBGTYPE_CPU && dbgDeviceGetRegisterBankCount(device) > 0) {
            return const_cast<DbgRegisterBank*>(dbgDeviceGetRegisterBank(device, 0));
        }
    }
    return nullptr;
}

bool Session::writeCpu(int index, uint32_t value)
{
    if (current != PAUSED || snapshot == nullptr) return false;
    DbgRegisterBank* bank = cpuBank(snapshot);
    bool ok = bank != nullptr && dbgDeviceWriteRegister(bank, index, value) != 0;
    readSnapshot(false);
    return ok;
}

bool Session::writeMemory(int view, uint32_t offset, const std::vector<uint8_t>& bytes)
{
    if (current != PAUSED || view < 0 || view >= (int)memories.size()) return false;
    DbgMemoryBlock* block = static_cast<DbgMemoryBlock*>(memories[view].block);
    bool ok = dbgDeviceWriteMemory(block, (void*)bytes.data(), (int)offset, (int)bytes.size()) != 0;
    readSnapshot(false);
    return ok;
}

bool Session::writePeripheral(int view, int index, uint32_t value)
{
    if (current != PAUSED || view < 0 || view >= (int)peripherals.size()) return false;
    bool ok = dbgDeviceWriteRegister(static_cast<DbgRegisterBank*>(peripherals[view].bank), index, value) != 0;
    readSnapshot(false);
    return ok;
}

bool Session::writeIoPort(int view, int index, uint32_t value)
{
    if (current != PAUSED || view < 0 || view >= (int)ioPorts.size()) return false;
    bool ok = dbgDeviceWriteIoPort(static_cast<DbgIoPorts*>(ioPorts[view].ports_), index, value) != 0;
    readSnapshot(false);
    return ok;
}

/* ---- Control ------------------------------------------------------------ */

void Session::run()      { dbgRun(); }
void Session::pause()    { dbgPause(); }
void Session::stop()     { clearRunTo(); dbgStop(); }
void Session::restart()  { clearRunTo(); dbgStop(); dbgRun(); }
void Session::stepInto() { dbgStep(); }
void Session::stepBack() { dbgStepBack(); }

void Session::runTo(uint16_t address)
{
    clearRunTo();
    runTo_ = address;
    if (findCode(address) < 0 || !list[findCode(address)].enabled) dbgSetBreakpoint(address);
    dbgRun();
}

void Session::stepOver()
{
    if (!valid) return;
    std::string text;
    int size = disassemble(symbols, memory.data(), pc, text);
    /* A call, rst or block instruction runs to the next one; halt too when
    ** an interrupt can end it (Breakpoints::setStepOverBreakpoint). */
    static const char* over[] = { "call", "ldir", "lddr", "cpir", "cpdr", "inir", "indr", "otir", "otdr", "rst" };
    bool step = true;
    for (const char* o : over) {
        if (text.compare(0, strlen(o), o) == 0) step = false;
    }
    if (interrupts && text.compare(0, 4, "halt") == 0) step = false;
    if (step) dbgStep();
    else runTo((uint16_t)(pc + size));
}

int Session::returnAddress() const
{
    return callstack.empty() ? -1 : (int)(callstack.back() & 0xffff);
}

bool Session::stepOut()
{
    int address = returnAddress();
    if (address < 0) return false;
    runTo((uint16_t)address);
    return true;
}

/* Run to cursor / step over leave a breakpoint the list does not show:
** take it out once the machine stops (or stops being debugged). */
void Session::clearRunTo()
{
    if (runTo_ < 0) return;
    int i = findCode(runTo_);
    if (i < 0 || !list[i].enabled) dbgClearBreakpoint((uint16_t)runTo_);
    runTo_ = -1;
}

/* ---- Breakpoint list ---------------------------------------------------- */

void Session::setInCore(const Breakpoint& bp, bool on)
{
    if (bp.type == Breakpoint::CODE) {
        if (on) dbgSetBreakpoint((uint16_t)bp.address);
        else if (bp.address != runTo_) dbgClearBreakpoint((uint16_t)bp.address);
    }
    else if (on) {
        dbgSetWatchpoint(deviceType(bp.type), bp.address, (DbgWatchpointCondition)bp.condition,
                         (UInt32)bp.value, bp.size);
    }
    else {
        dbgClearWatchpoint(deviceType(bp.type), bp.address);
    }
}

void Session::applyAll()
{
    for (const Breakpoint& bp : list) {
        if (bp.enabled) setInCore(bp, true);
    }
    if (vramChecking) {
        dbgEnableVramAccessCheck(0);
        dbgEnableVramAccessCheck(1);
    }
}

void Session::add(const Breakpoint& bp)
{
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].type == bp.type && list[i].address == bp.address) {
            remove((int)i);
            break;
        }
    }
    /* Sorted like the Windows list: breakpoints first, then by address. */
    auto pos = list.begin();
    while (pos != list.end() && (pos->type < bp.type || (pos->type == bp.type && pos->address < bp.address))) ++pos;
    pos = list.insert(pos, bp);
    if (bp.enabled) setInCore(bp, true);
}

void Session::remove(int index)
{
    if (index < 0 || index >= (int)list.size()) return;
    if (list[index].enabled) setInCore(list[index], false);
    list.erase(list.begin() + index);
}

void Session::setEnabled(int index, bool enabled)
{
    if (index < 0 || index >= (int)list.size() || list[index].enabled == enabled) return;
    list[index].enabled = enabled;
    setInCore(list[index], enabled);
}

int Session::findCode(int address) const
{
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].type == Breakpoint::CODE && list[i].address == address) return (int)i;
    }
    return -1;
}

void Session::toggleCode(int address)
{
    int i = findCode(address);
    if (i >= 0) {
        remove(i);
        return;
    }
    Breakpoint bp;
    bp.address = address & 0xffff;
    add(bp);
}

void Session::toggleCodeEnabled(int address)
{
    int i = findCode(address);
    if (i >= 0) setEnabled(i, !list[i].enabled);
}

void Session::removeAll()
{
    while (!list.empty()) remove((int)list.size() - 1);
}

void Session::enableAll(bool enabled)
{
    for (size_t i = 0; i < list.size(); i++) setEnabled((int)i, enabled);
}

void Session::vramCheck(bool on)
{
    if (on == vramChecking) return;
    vramChecking = on;
    dbgEnableVramAccessCheck(on ? 1 : 0);
}

/* ---- Listing, files, search --------------------------------------------- */

std::vector<Session::Line> Session::listing() const
{
    std::vector<Line> lines;
    if (memory.size() != 0x10000) return lines;
    lines.reserve(0x8000);
    int addr = 0;
    while (addr < 0x10000) {
        const char* label = symbols.find((uint16_t)addr);
        if (label != nullptr) lines.push_back(Line{ addr, true, "", std::string(label) + ":" });

        Line line;
        line.address = addr;
        line.label = false;
        int len = disassemble(symbols, memory.data(), (uint16_t)addr, line.text);
        /* Nothing before PC may swallow it: a byte that would is shown as data. */
        if (addr < pc && addr + len > pc) {
            char db[16];
            snprintf(db, sizeof(db), "db     #%02x", memory[addr]);
            line.text = db;
            len = 1;
        }
        if (addr + len > 0x10000) len = 0x10000 - addr;
        char hex[4];
        for (int i = 0; i < len && i < 4; i++) {
            snprintf(hex, sizeof(hex), "%02x", memory[addr + i]);
            if (i) line.bytes += ' ';
            line.bytes += hex;
        }
        lines.push_back(std::move(line));
        addr += len;
    }
    return lines;
}

bool Session::saveListing(const std::string& file) const
{
    if (!valid) return false;
    FILE* f = fopen(file.c_str(), "w");
    if (f == nullptr) return false;
    for (const Line& l : listing()) {
        if (l.label) fprintf(f, "%s\n", l.text.c_str());
        else fprintf(f, "%04X:   %s\n", l.address, l.text.c_str());
    }
    return fclose(f) == 0;
}

bool Session::saveMemory(int view, const std::string& file) const
{
    if (!valid || view < 0 || view >= (int)memories.size()) return false;
    FILE* f = fopen(file.c_str(), "wb");
    if (f == nullptr) return false;
    const std::vector<uint8_t>& d = memories[view].data;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    return fclose(f) == 0 && ok;
}

static std::vector<uint8_t> searchBytes(const std::string& what)
{
    std::vector<uint8_t> out;
    std::string s = what;
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        return std::vector<uint8_t>(s.begin() + 1, s.end() - 1);
    }
    std::string digits;
    for (char c : s) {
        if (c == ' ' || c == ',' || c == '#' || c == '$') {
            if (!digits.empty() && digits.size() % 2) digits.insert(digits.begin() + (digits.size() - 1), '0');
            continue;
        }
        if (!isxdigit((unsigned char)c)) return std::vector<uint8_t>(what.begin(), what.end());
        digits += c;
    }
    if (digits.empty()) return out;
    if (digits.size() % 2) digits.insert(digits.begin(), '0');
    for (size_t i = 0; i < digits.size(); i += 2) out.push_back((uint8_t)strtoul(digits.substr(i, 2).c_str(), nullptr, 16));
    return out;
}

int Session::find(int view, const std::string& what, int from) const
{
    if (view < 0 || view >= (int)memories.size()) return -1;
    std::vector<uint8_t> needle = searchBytes(what);
    const std::vector<uint8_t>& d = memories[view].data;
    int n = (int)needle.size(), size = (int)d.size();
    if (n == 0 || n > size) return -1;
    for (int k = 1; k <= size; k++) {
        int i = (from + k) % size;
        if (i + n <= size && memcmp(d.data() + i, needle.data(), n) == 0) return i;
    }
    return -1;
}

int Session::parseAddress(const std::string& text) const
{
    std::string s;
    for (char c : text) if (c != ' ') s += c;
    if (s.empty()) return -1;
    uint16_t a;
    if (symbols.rfind(s, &a)) return a;
    std::string h = s;
    if (h[0] == '#' || h[0] == '$') h = h.substr(1);
    else if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
    if (!h.empty() && (h.back() == 'h' || h.back() == 'H')) h.pop_back();
    if (h.empty() || h.size() > 4) return -1;
    for (char c : h) if (!isxdigit((unsigned char)c)) return -1;
    return (int)strtoul(h.c_str(), nullptr, 16);
}

}

namespace dbg {

const char* text(const char* english, const char* spanish)
{
    return langGetLanguage() == EMU_LANG_SPANISH ? spanish : english;
}

std::vector<Session::StackEntry> Session::stackEntries(int max) const
{
    std::vector<StackEntry> list;
    if (!valid) return list;
    for (int a = sp; a < 0x10000 && (int)list.size() < max; a += 2) {
        int hi = (a + 1) & 0xffff;
        uint16_t v = (uint16_t)(memory[a] | (memory[hi] << 8));
        uint16_t o = (uint16_t)(memoryBefore[a] | (memoryBefore[hi] << 8));
        list.push_back(StackEntry{ (uint16_t)a, v, v != o });
    }
    return list;
}

std::vector<Session::CallEntry> Session::callEntries() const
{
    std::vector<CallEntry> list;
    if (!valid) return list;
    for (int i = (int)callstack.size() - 1; i >= 0; i--) {
        CallEntry e;
        e.ret = (uint16_t)callstack[i];
        /* 3 bytes back for call / call cc, 1 for rst. */
        e.from = (uint16_t)(e.ret - 3);
        uint8_t op = memory[e.from];
        bool isCall = op == 0xcd || (op & 0xc7) == 0xc4;
        if (!isCall) e.from = (uint16_t)(e.ret - 1);
        bool isRst = (memory[e.from] & 0xc7) == 0xc7;
        e.interrupt = !isCall && !isRst;
        if (e.interrupt) {
            e.from = e.ret;
            e.text = text("(interrupt)", "(interrupción)");
        }
        else {
            std::string t;
            disassemble(symbols, memory.data(), e.from, t);
            /* Squeeze the mnemonic padding. */
            std::string squeezed;
            for (char c : t) {
                if (c == ' ' && !squeezed.empty() && squeezed.back() == ' ') continue;
                squeezed += c;
            }
            while (!squeezed.empty() && squeezed.back() == ' ') squeezed.pop_back();
            char addr[8];
            snprintf(addr, sizeof(addr), "%04X: ", e.from);
            e.text = addr + squeezed;
        }
        list.push_back(e);
    }
    return list;
}

std::vector<Session::Flag> Session::flags(bool asmStyle) const
{
    std::vector<Flag> list;
    int af = cpuIndex("AF");
    uint8_t f = valid && af >= 0 ? (uint8_t)cpu[af].value : 0;
    if (asmStyle) {
        int iff = cpuIndex("IFF1");
        bool ei = valid && iff >= 0 && cpu[iff].value != 0;
        static const char* names[5][2] = { { "NZ", "Z" }, { "NC", "C" }, { "PO", "PE" }, { "P", "M" }, { "DI", "EI" } };
        bool on[5] = { (f & 0x40) != 0, (f & 0x01) != 0, (f & 0x04) != 0, (f & 0x80) != 0, ei };
        for (int i = 0; i < 5; i++) list.push_back(Flag{ names[i][on[i] ? 1 : 0], on[i] });
    }
    else {
        static const char* names = "SZYHXPNC";
        for (int i = 0; i < 8; i++) list.push_back(Flag{ std::string(1, names[i]), ((f >> (7 - i)) & 1) != 0 });
    }
    return list;
}

void Session::toggleFlag(int index, bool asmStyle)
{
    int af = cpuIndex("AF");
    if (af < 0 || !valid) return;
    uint32_t v = cpu[af].value;
    if (!asmStyle) {
        writeCpu(af, v ^ (0x80 >> index));
        return;
    }
    static const int bits[4] = { 0x40, 0x01, 0x04, 0x80 };
    if (index < 4) {
        writeCpu(af, v ^ bits[index]);
        return;
    }
    int iff1 = cpuIndex("IFF1"), iff2 = cpuIndex("IFF2");
    bool ei = iff1 >= 0 && cpu[iff1].value != 0;
    if (iff1 >= 0) writeCpu(iff1, ei ? 0 : 1);
    if (iff2 >= 0) writeCpu(iff2, ei ? 0 : 1);
}

}
