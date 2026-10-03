#!/usr/bin/env python3
"""Find Unity's IL2CPP icall wrappers for set_targetFrameRate and set_vSyncCount
in an x86_64 GameAssembly.dylib, the same way the library does at runtime:
locate the binding-name string, then the wrapper whose lazy cache lookup loads
that string. Prints each wrapper's RVA; exit status 1 if one is missing.

    tools/find_wrappers.py GameAssembly.dylib
"""
import struct
import sys

BINDINGS = (
    "UnityEngine.Application::set_targetFrameRate(System.Int32)",
    "UnityEngine.QualitySettings::set_vSyncCount(System.Int32)",
)
# mov rax,[rip+cache]; test rax,rax; je +2; jmp rax; push rbp; mov rbp,rsp;
# push rbx; push rax; lea rax,[rip+name]   (bytes 3..6 are the cache displacement)
PREFIX = bytes.fromhex("488B05000000004885C07402FFE05548 89E55350488D05".replace(" ", ""))


def segments(data):
    magic, cputype, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF or cputype != 0x01000007:
        raise SystemExit("not an x86_64 Mach-O image")
    offset, sections = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, offset)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = struct.unpack_from("<I", data, offset + 64)[0]
            for i in range(nsects):
                base = offset + 72 + 80 * i
                sect = data[base:base + 16].rstrip(b"\0").decode()
                seg = data[base + 16:base + 32].rstrip(b"\0").decode()
                addr, sz, foff = struct.unpack_from("<QQI", data, base + 32)
                sections.append((seg, sect, addr, sz, foff))
        offset += size
    return sections


def find(data, sections):
    # IL2CPP puts generated code in its own "il2cpp" section next to __text.
    code = [s for s in sections if s[0] == "__TEXT" and s[1] in ("__text", "il2cpp")]
    strings = [s for s in sections if s[1] in ("__cstring", "__const") and s[0] in ("__TEXT", "__DATA_CONST")]
    found = {}
    for name in BINDINGS:
        needle = name.encode() + b"\0"
        vmaddr = None
        for _, _, addr, size, foff in strings:
            at = data.find(needle, foff, foff + size)
            # Must be a whole string, not the tail of a longer one.
            while at != -1 and data[at - 1] != 0:
                at = data.find(needle, at + 1, foff + size)
            if at != -1:
                vmaddr = addr + (at - foff)
                break
        if vmaddr is None:
            found[name] = None
            continue
        hits = []
        lea = b"\x48\x8D\x05"
        for _, _, taddr, tsize, tfoff in code:
            at = data.find(lea, tfoff, tfoff + tsize)
            while at != -1:
                disp = struct.unpack_from("<i", data, at + 3)[0]
                if taddr + (at - tfoff) + 7 + disp == vmaddr:
                    start = at - 20
                    chunk = data[start:start + 23]
                    if chunk[:3] == PREFIX[:3] and chunk[7:] == PREFIX[7:]:
                        hits.append(taddr + (start - tfoff))
                at = data.find(lea, at + 1, tfoff + tsize)
        found[name] = hits
    return found


def main():
    data = open(sys.argv[1], "rb").read()
    status = 0
    for name, hits in find(data, segments(data)).items():
        if not hits:
            print(f"MISSING wrapper: {name}" + (" (string not found)" if hits is None else ""))
            status = 1
        else:
            print(f"wrapper {name}: " + ", ".join(f"RVA 0x{h:X}" for h in hits))
    return status


if __name__ == "__main__":
    sys.exit(main())
