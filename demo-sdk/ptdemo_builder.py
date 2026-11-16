#!/usr/bin/env python3
"""
Polyend Tracker CE demo builder (.ptdemo)

Compiles one freestanding C file into a position-independent Cortex-M4F
Thumb-2 binary (runs on both the original Tracker and Tracker+/Mini) and
prepends the 128-byte PtDemoHeader from tracker_demo.h.

Metadata comes from comments in the source:
    // PTDEMO_NAME: Oldskool
    // PTDEMO_AUTHOR: someone
    // PTDEMO_SCRATCH: 8192          (bytes of working RAM, max 393216)
    // PTDEMO_FLAGS: IMX_ONLY        (optional)
    // PTDEMO_MOD: tune.mod          (optional music, path relative to the source;
                                       appended to the .ptdemo and played by the firmware)

Usage:
    python3 ptdemo_builder.py demos/oldskool.c -o Demos/oldskool.ptdemo
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

PTDEMO_MAGIC = 0x4F4D4450  # "PDMO"
PTDEMO_FORMAT_VERSION = 1
HEADER_SIZE = 128
API_VERSION = 2
MOD_TAGS = (b"M.K.", b"M!K!", b"FLT4", b"4CHN")
MAX_MOD_PATTERNS = 100  # what the firmware's module memory holds
EXIT_ABSENT = 0xFFFFFFFF
MAX_CODE_MK66 = 32 * 1024
MAX_CODE_IMX = 128 * 1024
MAX_SCRATCH = 384 * 1024

FLAG_MAP = {"IMX_ONLY": 0x0001}

LINKER_SCRIPT = """
SECTIONS
{
    . = 0;
    .text : {
        *(.text*)
        *(.rodata*)
    }
    . = ALIGN(4);
    .data : {
        *(.data*)
    }
    . = ALIGN(4);
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    __bss_end = .;
    .got : { *(.got*) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
"""


def run(cmd):
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if res.returncode != 0:
        sys.exit(f"{' '.join(cmd[:1])} failed:\n{res.stderr.decode()}")
    return res.stdout.decode()


def parse_meta(source):
    meta = {}
    for line in source.splitlines():
        m = re.match(r"^\s*//\s*PTDEMO_(\w+)\s*:\s*(.*)$", line)
        if m:
            meta[m.group(1).upper()] = m.group(2).strip()
    return meta


def read_mod(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 1084:
        sys.exit(f"{path}: too short to be a .mod")
    tag = data[1080:1084]
    if tag not in MOD_TAGS:
        sys.exit(f"{path}: not a 4-channel ProTracker module (tag {tag!r})")
    song_length = data[950]
    patterns = max(data[952:952 + 128][:max(song_length, 1)]) + 1
    if patterns > MAX_MOD_PATTERNS:
        sys.exit(f"{path}: uses {patterns} patterns, the firmware holds {MAX_MOD_PATTERNS}")
    return data


def build(c_file, out_file, toolchain):
    with open(c_file) as f:
        source = f.read()
    meta = parse_meta(source)

    name = meta.get("NAME", os.path.splitext(os.path.basename(c_file))[0])
    author = meta.get("AUTHOR", "")
    scratch = int(meta.get("SCRATCH", "0"), 0)
    flags = 0
    for token in re.split(r"[|,\s]+", meta.get("FLAGS", "")):
        if token:
            if token.upper() not in FLAG_MAP:
                sys.exit(f"Unknown flag {token}")
            flags |= FLAG_MAP[token.upper()]
    if scratch > MAX_SCRATCH:
        sys.exit(f"PTDEMO_SCRATCH {scratch} is over the {MAX_SCRATCH} byte limit")

    mod = b""
    if meta.get("MOD"):
        mod = read_mod(os.path.join(os.path.dirname(os.path.abspath(c_file)), meta["MOD"]))
    # API 2 brought the music: a demo that has or drives a module needs it.
    uses_music = bool(mod) or re.search(r"\bmod_(state|play|stop|load)\b", source)
    required_api = 2 if uses_music else 1

    sdk_dir = os.path.dirname(os.path.abspath(__file__))

    with tempfile.TemporaryDirectory() as tmp:
        ld = os.path.join(tmp, "demo.ld")
        obj = os.path.join(tmp, "demo.o")
        elf = os.path.join(tmp, "demo.elf")
        binf = os.path.join(tmp, "demo.bin")
        with open(ld, "w") as f:
            f.write(LINKER_SCRIPT)

        run([
            toolchain + "gcc", "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
            "-O2", "-std=gnu11", "-Wall", "-Wextra",
            "-ffreestanding", "-fno-builtin", "-fno-tree-loop-distribute-patterns",
            "-fPIC", "-mpic-data-is-text-relative", "-fvisibility=hidden",
            "-fno-common", "-fno-asynchronous-unwind-tables",
            "-nostdlib", f"-I{sdk_dir}", "-c", c_file, "-o", obj,
        ])

        # Anything that needs patching at load time can't work: there is no
        # relocation step, the code runs wherever the loader puts it.
        relocs = run([toolchain + "readelf", "-r", "-W", obj])
        bad = [l for l in relocs.splitlines()
               if re.search(r"R_ARM_(ABS32|GOT_BREL|GOT32|GLOB_DAT|JUMP_SLOT|TARGET1)", l)]
        if bad:
            sys.exit("Absolute or GOT relocations found - a pointer in initialized data, or a "
                     "non-static global? Make it static / compute it at run time:\n  " + "\n  ".join(bad))

        run([toolchain + "ld", "-T", ld, "-e", "demo_init", "--no-undefined", obj, "-o", elf])

        sections = run([toolchain + "readelf", "-S", "-W", elf])
        m = re.search(r"\.got\s+\S+\s+\S+\s+\S+\s+([0-9a-f]+)", sections)
        if m and int(m.group(1), 16) != 0:
            sys.exit("The binary has a GOT; keep every global static.")

        undefined = run([toolchain + "nm", "-u", elf]).split()
        if undefined:
            sys.exit("Undefined symbols (no libc - use the host API): " + " ".join(undefined))

        symbols = {}
        for line in run([toolchain + "nm", elf]).splitlines():
            parts = line.split()
            if len(parts) == 3:
                symbols[parts[2]] = int(parts[0], 16)

        for required in ("demo_init", "demo_frame"):
            if required not in symbols:
                sys.exit(f"{required}() is missing")

        run([toolchain + "objcopy", "-O", "binary", "-R", ".bss", "-R", ".got", elf, binf])
        with open(binf, "rb") as f:
            code = f.read()

    bss_size = symbols["__bss_end"] - symbols["__bss_start"]
    code_size = len(code)
    if code_size != symbols["__bss_start"]:
        # objcopy pads to the last loaded byte; bss must follow it exactly
        code = code.ljust(symbols["__bss_start"], b"\0")[: symbols["__bss_start"]]
        code_size = len(code)

    total = code_size + bss_size
    limit = MAX_CODE_IMX if flags & FLAG_MAP["IMX_ONLY"] else MAX_CODE_MK66
    if total > limit:
        if limit == MAX_CODE_MK66 and total <= MAX_CODE_IMX:
            sys.exit(f"Code+bss is {total} bytes: over 32 KB, add '// PTDEMO_FLAGS: IMX_ONLY'")
        sys.exit(f"Code+bss is {total} bytes, over the {limit} byte limit")

    code = code.ljust((len(code) + 3) & ~3, b"\0")  # keep the module 4-byte aligned
    mod_offset = HEADER_SIZE + len(code) if mod else 0

    header = struct.pack(
        "<IHH32s16sIIIIIIIIII32s",
        PTDEMO_MAGIC,
        PTDEMO_FORMAT_VERSION,
        flags,
        name.encode("ascii", "replace")[:31].ljust(32, b"\0"),
        author.encode("ascii", "replace")[:15].ljust(16, b"\0"),
        scratch,
        HEADER_SIZE,
        code_size,
        bss_size,
        symbols["demo_init"] & ~1,
        symbols["demo_frame"] & ~1,
        (symbols["demo_exit"] & ~1) if "demo_exit" in symbols else EXIT_ABSENT,
        mod_offset,
        len(mod),
        required_api,
        b"\0" * 32,
    )
    assert len(header) == HEADER_SIZE

    os.makedirs(os.path.dirname(os.path.abspath(out_file)), exist_ok=True)
    with open(out_file, "wb") as f:
        f.write(header + code + mod)

    music = f"  music {len(mod)} B" if mod else ""
    print(f"{out_file}: '{name}'  code {code_size} B  bss {bss_size} B  scratch {scratch} B{music}"
          f"  API {required_api}  {'IMX only' if flags & 1 else 'all Trackers'}")


def main():
    ap = argparse.ArgumentParser(description="Build a Polyend Tracker CE .ptdemo")
    ap.add_argument("source", help="demo C file")
    ap.add_argument("-o", "--output", help="output .ptdemo (default: Demos/<name>.ptdemo)")
    ap.add_argument("--toolchain", default="arm-none-eabi-", help="toolchain prefix")
    args = ap.parse_args()
    out = args.output or os.path.join(
        "Demos", os.path.splitext(os.path.basename(args.source))[0] + ".ptdemo")
    build(args.source, out, args.toolchain)


if __name__ == "__main__":
    main()
