#!/usr/bin/env python3
"""Minimal Phase-2 ELF inspector and canonical single-program bundle packer.

This deliberately supports only ELF64 little-endian EM_BPF objects and one
`rtbpf/net_rx` section without relocations. Full map/helper relocation and the
target decoder belong to the transactional-loader phase.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys

EM_BPF = 247
ET_REL = 1
SHT_PROGBITS = 1
SHT_RELA = 4
SHT_REL = 9
ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
BUNDLE_HEADER = struct.Struct("<8sHHIIIII32s")
BUNDLE_MAGIC = b"RTBPFNET"
BUNDLE_VERSION = 1
MAX_INSNS = 512
SECTION_NAME = "rtbpf/net_rx"

KNOWN_OPCODES = {
    0x07, 0x0F, 0xB7, 0xBF, 0x71, 0x69, 0x61, 0x79, 0x62, 0x63, 0x7B,
    0x05, 0x15, 0x55, 0x2D, 0x85, 0x95, 0xD0,
}
JUMP_OPCODES = {0x05, 0x15, 0x55, 0x2D}

class PackError(ValueError):
    pass


def checked_slice(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise PackError(f"{what} lies outside file")
    return data[offset:offset + size]


def c_string(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        raise PackError("section name offset outside string table")
    end = table.find(b"\0", offset)
    if end < 0:
        raise PackError("unterminated section name")
    try:
        return table[offset:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise PackError("non-ASCII section name") from exc


def inspect_elf(data: bytes) -> dict:
    if len(data) < ELF_HEADER.size:
        raise PackError("truncated ELF header")
    fields = ELF_HEADER.unpack_from(data)
    ident, e_type, machine, version, _entry, _phoff, shoff, _flags, ehsize, \
        _phentsize, _phnum, shentsize, shnum, shstrndx = fields
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
        raise PackError("requires ELF64 little-endian input")
    if e_type != ET_REL or machine != EM_BPF or version != 1:
        raise PackError("requires a relocatable EM_BPF ELF object")
    if ehsize != ELF_HEADER.size or shentsize != SECTION_HEADER.size:
        raise PackError("non-canonical ELF header sizes")
    if shnum == 0 or shnum > 256 or shstrndx >= shnum:
        raise PackError("invalid section table dimensions")
    checked_slice(data, shoff, shnum * shentsize, "section table")

    sections = []
    for index in range(shnum):
        raw = SECTION_HEADER.unpack_from(data, shoff + index * shentsize)
        sections.append({
            "name_offset": raw[0], "type": raw[1], "flags": raw[2],
            "offset": raw[4], "size": raw[5], "link": raw[6], "info": raw[7],
            "align": raw[8], "entry_size": raw[9], "index": index,
        })
    names_section = sections[shstrndx]
    names = checked_slice(data, names_section["offset"], names_section["size"],
                          "section-name string table")
    for section in sections:
        section["name"] = c_string(names, section["name_offset"])

    candidates = [s for s in sections if s["name"] == SECTION_NAME]
    if len(candidates) != 1:
        raise PackError(f"expected exactly one {SECTION_NAME!r} section")
    program = candidates[0]
    if program["type"] != SHT_PROGBITS:
        raise PackError("program section is not PROGBITS")
    for section in sections:
        if section["type"] in (SHT_REL, SHT_RELA) and section["info"] == program["index"]:
            raise PackError("Phase-2 packer does not yet accept program relocations")

    instructions = checked_slice(data, program["offset"], program["size"],
                                 "program section")
    if not instructions or len(instructions) % 8:
        raise PackError("instruction section must be a non-empty multiple of 8 bytes")
    count = len(instructions) // 8
    if count > MAX_INSNS:
        raise PackError("program exceeds instruction limit")

    for pc in range(count):
        opcode, dst_src, offset, immediate = struct.unpack_from("<BBhi", instructions, pc * 8)
        if opcode not in KNOWN_OPCODES:
            raise PackError(f"unsupported opcode 0x{opcode:02x} at instruction {pc}")
        if (dst_src & 0x0F) >= 11 or (dst_src >> 4) >= 11:
            raise PackError(f"invalid register at instruction {pc}")
        if opcode in JUMP_OPCODES:
            target = pc + 1 + offset
            if target < 0 or target >= count:
                raise PackError(f"jump outside program at instruction {pc}")
            if target <= pc:
                raise PackError(f"back edge at instruction {pc}")
        if opcode == 0x85 and immediate != 1:
            raise PackError(f"unsupported helper {immediate} at instruction {pc}")

    return {
        "section": SECTION_NAME,
        "instruction_count": count,
        "instruction_bytes": instructions,
        "sha256": hashlib.sha256(instructions).hexdigest(),
    }


def make_bundle(info: dict) -> bytes:
    instructions = info["instruction_bytes"]
    digest = hashlib.sha256(instructions).digest()
    header = BUNDLE_HEADER.pack(
        BUNDLE_MAGIC, BUNDLE_VERSION, BUNDLE_HEADER.size, 0,
        info["instruction_count"], BUNDLE_HEADER.size, len(instructions),
        info["instruction_count"], digest,
    )
    return header + instructions


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect_cmd = sub.add_parser("inspect", help="validate and describe an ELF object")
    inspect_cmd.add_argument("elf", type=pathlib.Path)
    pack_cmd = sub.add_parser("pack", help="emit a prototype .rtbpf bundle")
    pack_cmd.add_argument("elf", type=pathlib.Path)
    pack_cmd.add_argument("-o", "--output", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    try:
        info = inspect_elf(args.elf.read_bytes())
        if args.command == "inspect":
            printable = {k: v for k, v in info.items() if k != "instruction_bytes"}
            print(json.dumps(printable, indent=2, sort_keys=True))
        else:
            bundle = make_bundle(info)
            args.output.write_bytes(bundle)
            print(f"wrote {args.output} ({len(bundle)} bytes, "
                  f"{info['instruction_count']} instructions)")
        return 0
    except (OSError, PackError) as exc:
        print(f"rtbpf-pack: error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
