#!/usr/bin/env python3
import hashlib
import importlib.util
import pathlib
import struct
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rtbpf_pack", ROOT / "tools/rtbpf_pack.py")
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


def align(value, amount=8):
    return (value + amount - 1) & ~(amount - 1)


def elf_with_program(program: bytes) -> bytes:
    names = b"\0.shstrtab\0rtbpf/net_rx\0"
    names_off = packer.ELF_HEADER.size
    program_off = align(names_off + len(names))
    shoff = align(program_off + len(program))
    total = shoff + 3 * packer.SECTION_HEADER.size
    data = bytearray(total)
    ident = bytearray(16)
    ident[:6] = b"\x7fELF\x02\x01"
    header = packer.ELF_HEADER.pack(
        bytes(ident), packer.ET_REL, packer.EM_BPF, 1, 0, 0, shoff, 0,
        packer.ELF_HEADER.size, 0, 0, packer.SECTION_HEADER.size, 3, 1,
    )
    data[:len(header)] = header
    data[names_off:names_off + len(names)] = names
    data[program_off:program_off + len(program)] = program
    # section 0 is all zero
    packer.SECTION_HEADER.pack_into(
        data, shoff + packer.SECTION_HEADER.size,
        1, 3, 0, 0, names_off, len(names), 0, 0, 1, 0,
    )
    packer.SECTION_HEADER.pack_into(
        data, shoff + 2 * packer.SECTION_HEADER.size,
        11, packer.SHT_PROGBITS, 0x6, 0, program_off, len(program), 0, 0, 8, 8,
    )
    return bytes(data)


def insn(opcode, dst=0, src=0, offset=0, immediate=0):
    return struct.pack("<BBhi", opcode, (src << 4) | dst, offset, immediate)


def expect_error(data: bytes, phrase: str):
    try:
        packer.inspect_elf(data)
    except packer.PackError as exc:
        assert phrase in str(exc), (phrase, str(exc))
    else:
        raise AssertionError(f"expected PackError containing {phrase!r}")


def main():
    program = insn(0xB7, immediate=2) + insn(0x95)
    elf = elf_with_program(program)
    info = packer.inspect_elf(elf)
    assert info["instruction_count"] == 2
    assert info["sha256"] == hashlib.sha256(program).hexdigest()

    bundle = packer.make_bundle(info)
    fields = packer.BUNDLE_HEADER.unpack_from(bundle)
    assert fields[0] == packer.BUNDLE_MAGIC
    assert fields[1] == packer.BUNDLE_VERSION
    assert fields[4] == 2
    assert fields[5] == packer.BUNDLE_HEADER.size
    assert fields[6] == len(program)
    assert fields[8] == hashlib.sha256(program).digest()
    assert bundle[packer.BUNDLE_HEADER.size:] == program

    expect_error(elf_with_program(insn(0xFE)), "unsupported opcode")
    expect_error(elf_with_program(insn(0x05, offset=-1)), "back edge")
    expect_error(elf[:20], "truncated")

    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "program.o"
        output = pathlib.Path(tmp) / "program.rtbpf"
        path.write_bytes(elf)
        assert packer.main(["pack", str(path), "-o", str(output)]) == 0
        assert output.read_bytes() == bundle

    print("Packer tests: 14 checks, 0 failures")


if __name__ == "__main__":
    main()
