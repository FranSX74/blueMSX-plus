#!/usr/bin/env python3
"""Build blueMSX/ReleaseFiles/Databases/sysromsha1.txt.

blueMSX machine configs name their system ROMs by file name only
("Machines/MSX2 - Philips NMS-8250/8250bios.rom"), while openMSX machine
descriptions carry the SHA1 of every ROM at a given slot position.  This
script pairs both by machine name and slot layout so blueMSX+ can locate the
ROMs inside an openMSX systemroms folder whatever their file names are.

Usage: gen-sysrom-sha1.py <openMSX share/machines dir> [output file]

Output: one line per ROM, tab separated:
    <path below Machines/>	<sha1>[	<sha1> ...]
Only SHA1 values are taken from openMSX (no ROM data), so the table is
plain metadata.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINES = os.path.join(HERE, "..", "blueMSX", "ReleaseFiles", "Machines")
DEFAULT_OUT = os.path.join(HERE, "..", "blueMSX", "ReleaseFiles", "Databases", "sysromsha1.txt")

PREFIX = re.compile(r"^(MSX|MSX2|MSX2\+|MSXturboR|turboR|COL|SVI|SEGA|SG|SC|SF)\s+-\s+", re.I)
SLOT_LINE = re.compile(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+"([^"]*)"')


# blueMSX machine name (without generation prefix) -> openMSX machine file.
# Only unambiguous equivalences: a wrong pairing would load another model's ROM.
ALIASES = {
    "Philips VG-8020-00": "Philips_VG_8020",
    "Philips VG-8235": "Philips_VG_8235-00",
    "Sharp Epcom HotBit 1.1": "Sharp_HB-8000_1.1",
    "Sharp Epcom HotBit 1.2": "Sharp_HB-8000_1.2",
    "Sanyo Wavy MPC-10": "Sanyo_MPC-10",
    "Sanyo Wavy MPC-25FD": "Sanyo_MPC-25FD",
    "Sanyo Wavy PHC-23": "Sanyo_PHC-23",
    "Yamaha CX5M-1": "Yamaha_CX5M",
    "Yamaha CX5M-2": "Yamaha_CX5MII",
    "Yamaha CX5M-128": "Yamaha_CX5MII-128",
    "Yamaha CX7M": "Yamaha_CX7M-128",
    "Yamaha YIS503M": "Yamaha_YIS-503",
    "Yamaha YIS503IIR Estonian": "Yamaha_YIS-503IIR",
    "Toshiba HX-20": "Toshiba_HX-20E",
    "Olympia PHC-28": "Olympia_PHC-28",
    "Spectravideo SVI-738 Swedish": "Spectravideo_SVI-738_SE",
    "Bit Corporation Dina": "Bit_Corporation_Dina",
}


def norm(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def parse_int(text):
    text = (text or "0").strip()
    return int(text, 16) if text.lower().startswith("0x") else int(text)


def openmsx_roms(path):
    """Yield (ps, ss, base, size, tag_or_id, [sha1...]) for every ROM in an openMSX machine."""
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        return []
    out = []

    def walk(elem, ps, ss):
        for child in elem:
            tag = child.tag
            nps, nss = ps, ss
            if tag == "primary":
                nps, nss = child.get("slot"), "0"
            elif tag == "secondary":
                nss = child.get("slot")
            rom = child.find("rom")
            if rom is not None and tag not in ("primary", "secondary"):
                sha1s = [s.text.strip().lower() for s in rom.findall("sha1") if s.text]
                mem = child.find("mem")
                base = parse_int(mem.get("base")) if mem is not None else None
                size = parse_int(mem.get("size")) if mem is not None else None
                ident = (tag + " " + (child.get("id") or "")).lower()
                if sha1s:
                    out.append((nps, nss, base, size, ident, sha1s))
            walk(child, nps, nss)

    devices = root.find("devices")
    if devices is not None:
        walk(devices, None, None)
    return out


def blue_slots(config):
    entries = []
    in_slots = False
    with open(config, encoding="latin-1") as f:
        for line in f:
            s = line.strip()
            if s.startswith("["):
                in_slots = s.lower() == "[slots]"
                continue
            if not in_slots:
                continue
            m = SLOT_LINE.match(s)
            if m and m.group(6).lower().startswith("machines/"):
                ps, ss, page, pages = (int(m.group(i)) for i in range(1, 5))
                entries.append((ps, ss, page, pages, m.group(6)[len("machines/"):].replace("\\", "/")))
    return entries


def pick(entry, roms):
    ps, ss, page, pages, rel = entry
    fname = os.path.basename(rel).lower()
    if pages == 0:
        # Not memory mapped (kanji font, etc.): match by keyword
        for key in ("kanji", "font", "jisyo", "sram"):
            if key in fname:
                cands = [r for r in roms if key in r[4]]
                if cands:
                    return cands[0]
        return None
    start, end = page * 0x2000, (page + pages) * 0x2000
    best, best_score = None, 0
    for r in roms:
        if r[0] != str(ps) or (r[1] or "0") != str(ss) or r[2] is None:
            continue
        rs, re_ = r[2], r[2] + (r[3] or 0)
        overlap = min(end, re_) - max(start, rs)
        if overlap <= 0:
            continue
        score = overlap + (0x100000 if rs == start else 0)
        if score > best_score:
            best, best_score = r, score
    return best


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    omsx_dir = sys.argv[1]
    out_file = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    omsx = {norm(os.path.splitext(f)[0]): os.path.join(omsx_dir, f)
            for f in os.listdir(omsx_dir) if f.endswith(".xml")}

    table = {}
    machines = matched_machines = roms_total = roms_matched = roms_in_paired = 0
    unmatched = []
    for name in sorted(os.listdir(MACHINES)):
        config = os.path.join(MACHINES, name, "config.ini")
        if not os.path.isfile(config):
            continue
        entries = blue_slots(config)
        if not entries:
            continue
        machines += 1
        short = re.sub(r"\s*\([a-z]\)$", "", PREFIX.sub("", name))
        xml = (omsx.get(norm(ALIASES[short])) if short in ALIASES else None) \
            or omsx.get(norm(short)) or omsx.get(norm(name))
        roms = openmsx_roms(xml) if xml else []
        if xml:
            matched_machines += 1
        else:
            unmatched.append(name)
        for e in entries:
            roms_total += 1
            roms_in_paired += 1 if roms else 0
            r = pick(e, roms) if roms else None
            if r:
                roms_matched += 1
                table.setdefault(e[4], [])
                for h in r[5]:
                    if h not in table[e[4]]:
                        table[e[4]].append(h)

    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.write("# blueMSX+ system ROM SHA1 table, generated by scripts/gen-sysrom-sha1.py\n")
        f.write("# from the openMSX machine descriptions. <path below Machines/>\\t<sha1>...\n")
        for rel in sorted(table):
            f.write(rel + "\t" + "\t".join(table[rel]) + "\n")

    print(f"machines with ROMs: {machines}, paired with openMSX: {matched_machines}")
    print(f"ROM entries: {roms_total} ({roms_in_paired} in paired machines), with SHA1: {roms_matched}, table lines: {len(table)}")
    if "-v" in sys.argv:
        print("unpaired machines:", *unmatched, sep="\n  ")


if __name__ == "__main__":
    main()
