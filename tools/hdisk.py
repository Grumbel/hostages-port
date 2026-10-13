#!/usr/bin/env python3
"""Reader for the Hostages (Infogrames) custom disk format.

The disk has no AmigaDOS file system.  Layout (byte offsets):
  $000  bootblock: hunk loader that reads the executable at $800 via
        trackdisk.device (stage 2, "tatou.")
  $400  directory: 50 entries of 20 bytes
        char name[12]; u32 offset; u32 size   (big endian, offset in bytes)
  ...   file data, each file starts on a 512-byte boundary

    python3 tools/hdisk.py list    "Hostage (Infogrames).adf"
    python3 tools/hdisk.py extract "Hostage (Infogrames).adf" outdir
"""
import os
import struct
import sys

DIR_OFFSET = 0x400
DIR_ENTRIES = 50
ENTRY_SIZE = 20


def read_dir(img):
    files = []
    for i in range(DIR_ENTRIES):
        e = img[DIR_OFFSET + i * ENTRY_SIZE:DIR_OFFSET + (i + 1) * ENTRY_SIZE]
        if not any(e):
            continue
        name = e[:12].split(b"\0")[0].decode("latin-1")
        off, size = struct.unpack(">II", e[12:20])
        files.append((i, name, off, size))
    return files


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    img = open(path, "rb").read()
    files = read_dir(img)
    if cmd == "list":
        for i, name, off, size in files:
            print(f"{i:2} {name:12} offset={off:#07x} size={size:6}")
    elif cmd == "extract":
        out = sys.argv[3]
        os.makedirs(out, exist_ok=True)
        for i, name, off, size in files:
            with open(os.path.join(out, name), "wb") as f:
                f.write(img[off:off + size])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
