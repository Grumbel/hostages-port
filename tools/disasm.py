#!/usr/bin/env python3
"""Disassemble a program from the Hostages disk, relocated to the addresses
it runs at in the port (so addresses match --verbose/--trace output).

    python3 tools/disasm.py "Hostage (Infogrames).adf" apro1. > apro1.dis
    python3 tools/disasm.py "Hostage (Infogrames).adf" tatou. --base 0xd620

Stage 2 ("tatou.", loaded by the bootblock) allocates each hunk with
AllocMem(size + 8) and uses the address + 8; with the port's memory layout
both "apro1." and "it." get their code hunk at $3998 and the data hunk
directly after it. Needs m68k objdump (in the nix dev shell), or set
OBJDUMP.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
import hdisk
import hunks as hunkmod

PROGRAM_BASE = 0x3998


def layout(hs, base):
    addrs = []
    a = base
    for h in hs:
        addrs.append(a)
        a = ((a + h.alloc_size + 7) & ~7) + 8
    return addrs


def relocate(hs, addrs):
    end = max(a + h.alloc_size for h, a in zip(hs, addrs))
    img = bytearray(end - addrs[0])
    for h, a in zip(hs, addrs):
        off = a - addrs[0]
        img[off:off + len(h.data)] = h.data
        for target, offs in h.relocs.items():
            for o in offs:
                v = struct.unpack_from(">I", img, off + o)[0]
                struct.pack_into(">I", img, off + o, (v + addrs[target]) & 0xFFFFFFFF)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("adf")
    ap.add_argument("program")
    ap.add_argument("--base", type=lambda x: int(x, 0), default=PROGRAM_BASE)
    args = ap.parse_args()
    img = open(args.adf, "rb").read()
    files = {name: (off, size) for _, name, off, size in hdisk.read_dir(img)}
    off, size = files[args.program]
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "prog")
        open(path, "wb").write(img[off:off + size])
        hs = hunkmod.parse(path)
        addrs = layout(hs, args.base)
        flat = relocate(hs, addrs)
        bin_path = os.path.join(td, "flat.bin")
        open(bin_path, "wb").write(flat)
        objdump = os.environ.get("OBJDUMP", "m68k-unknown-linux-gnu-objdump")
        # Only the code hunk is disassembled; data follows as raw bytes.
        out = subprocess.run([objdump, "-b", "binary", "-m", "m68k:68000", "-D",
                              f"--adjust-vma={addrs[0]:#x}",
                              f"--stop-address={addrs[0] + hs[0].alloc_size:#x}", bin_path],
                             capture_output=True, text=True, check=True).stdout
    for h, a in zip(hs, addrs):
        print(f"; hunk {h.index} {h.kind} {a:#08x}-{a + h.alloc_size:#08x}")
    for line in out.splitlines():
        if line.startswith(" "):
            print(line)


if __name__ == "__main__":
    main()
