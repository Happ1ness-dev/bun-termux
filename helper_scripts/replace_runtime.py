#!/data/data/com.termux/files/usr/bin/env python
"""
Replace the bun runtime in a bundled executable with a custom wrapper binary.

Supports both pre-1.3.12 (old-style append) and post-1.3.12 (ELF .bun section)
bundled binary formats. Auto-detects input format and matches it by default.

Credit: https://github.com/kaan-escober/bun-termux-loader/blob/master/build.py
"""

import struct
import sys
from pathlib import Path

ELF_MAGIC = b'\x7fELF'
BUN_MARKER = b'---- Bun! ----'

ELFCLASS64 = 2
ELFDATA2LSB = 1

EM_X86_64 = 62
EM_AARCH64 = 183

PT_LOAD = 1
PT_GNU_STACK = 0x6474E551

PF_R = 4

U64_SIZE = struct.calcsize('<Q')  # sizeof(uint64_t)


def log(msg):
    print(f"[+] {msg}")


def error(msg):
    print(f"[!] Error: {msg}", file=sys.stderr)


def page_size(e_machine):
    """Return page size for architecture. Matches bun's elf.zig pageSize()."""
    if e_machine == EM_AARCH64:
        return 0x10000  # 64KB
    if e_machine == EM_X86_64:
        return 0x1000  # 4KB
    raise ElfError(f"Unsupported architecture (e_machine={e_machine})")


def align_up(value, alignment):
    if alignment == 0:
        return value
    mask = alignment - 1
    return (value + mask) & ~mask


class ElfError(Exception):
    pass


class ElfParser:
    """Minimal 64-bit little-endian ELF parser."""

    def __init__(self, data):
        if len(data) < 64 or data[:4] != ELF_MAGIC:
            raise ElfError("Not a valid ELF file")
        if data[4] != ELFCLASS64:
            raise ElfError("Only 64-bit ELF supported")
        if data[5] != ELFDATA2LSB:
            raise ElfError("Only little-endian ELF supported")

        self.data = data
        self.e_phoff, self.e_shoff = struct.unpack('<QQ', data[32:48])
        self.e_phentsize, self.e_phnum = struct.unpack('<HH', data[54:58])
        self.e_shentsize, self.e_shnum = struct.unpack('<HH', data[58:62])
        self.e_shstrndx = struct.unpack('<H', data[62:64])[0]
        self.e_machine = struct.unpack('<H', data[18:20])[0]

    def read_phdr(self, index):
        off = self.e_phoff + index * self.e_phentsize
        if off + 56 > len(self.data):
            raise ElfError("Program header out of bounds")
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
            struct.unpack_from('<IIQQQQQQ', self.data, off)
        return {
            'p_type': p_type, 'p_flags': p_flags, 'p_offset': p_offset,
            'p_vaddr': p_vaddr, 'p_paddr': p_paddr, 'p_filesz': p_filesz,
            'p_memsz': p_memsz, 'p_align': p_align,
        }

    def read_shdr(self, index):
        off = self.e_shoff + index * self.e_shentsize
        if off + 64 > len(self.data):
            raise ElfError("Section header out of bounds")
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = \
            struct.unpack_from('<IIQQQQIIQQ', self.data, off)
        return {
            'sh_name': sh_name, 'sh_type': sh_type, 'sh_flags': sh_flags,
            'sh_addr': sh_addr, 'sh_offset': sh_offset, 'sh_size': sh_size,
            'sh_link': sh_link, 'sh_info': sh_info, 'sh_addralign': sh_addralign,
            'sh_entsize': sh_entsize,
        }

    def write_shdr(self, index, shdr):
        off = self.e_shoff + index * self.e_shentsize
        if off + 64 > len(self.data):
            raise ElfError("Section header out of bounds")
        struct.pack_into('<IIQQQQIIQQ', self.data, off,
            shdr['sh_name'], shdr['sh_type'], shdr['sh_flags'], shdr['sh_addr'],
            shdr['sh_offset'], shdr['sh_size'], shdr['sh_link'], shdr['sh_info'],
            shdr['sh_addralign'], shdr['sh_entsize'])

    def write_phdr(self, index, phdr):
        off = self.e_phoff + index * self.e_phentsize
        if off + 56 > len(self.data):
            raise ElfError("Program header out of bounds")
        struct.pack_into('<IIQQQQQQ', self.data, off,
            phdr['p_type'], phdr['p_flags'], phdr['p_offset'], phdr['p_vaddr'],
            phdr['p_paddr'], phdr['p_filesz'], phdr['p_memsz'], phdr['p_align'])

    def get_section_name(self, shdr):
        """Return section name string from .shstrtab."""
        if self.e_shnum == 0 or self.e_shstrndx >= self.e_shnum:
            return ""
        strtab_shdr = self.read_shdr(self.e_shstrndx)
        strtab_off = strtab_shdr['sh_offset']
        strtab_size = strtab_shdr['sh_size']
        name_off = shdr['sh_name']
        if name_off >= strtab_size:
            return ""
        end = self.data.find(b'\x00', strtab_off + name_off)
        if end == -1:
            return ""
        return self.data[strtab_off + name_off:end].decode('ascii', errors='replace')

    def find_section(self, name):
        for i in range(self.e_shnum):
            shdr = self.read_shdr(i)
            if self.get_section_name(shdr) == name:
                return i, shdr
        return None, None

    def find_phdr_by_type(self, p_type):
        for i in range(self.e_phnum):
            phdr = self.read_phdr(i)
            if phdr['p_type'] == p_type:
                return i, phdr
        return None, None

    def set_e_shoff(self, new_shoff):
        self.data[40:48] = struct.pack('<Q', new_shoff)
        self.e_shoff = new_shoff


def find_elf_end(data):
    elf = ElfParser(data)
    end = 0
    for i in range(elf.e_phnum):
        phdr = elf.read_phdr(i)
        if phdr['p_type'] == PT_LOAD:
            end = max(end, phdr['p_offset'] + phdr['p_filesz'])
    if elf.e_shoff > 0 and elf.e_shnum > 0:
        end = max(end, elf.e_shoff + elf.e_shentsize * elf.e_shnum)
    return end


def check_bun_marker(data):
    return BUN_MARKER in data[-256:]


def detect_format(data):
    try:
        elf = ElfParser(data)
        idx, shdr = elf.find_section('.bun')
        if idx is None or shdr['sh_size'] < U64_SIZE:
            raise ElfError("no .bun")
        bun_off = shdr['sh_offset']
        if bun_off + U64_SIZE > len(data):
            raise ElfError("truncated")
        payload_len = struct.unpack('<Q', data[bun_off:bun_off+U64_SIZE])[0]
        if 0 < payload_len <= shdr['sh_size'] - U64_SIZE:
            return 'new'
    except ElfError:
        pass

    if check_bun_marker(data):
        return 'old'

    return None


def extract_payload_old(data):
    """Extract payload from old-style bundled binary.
    Returns payload bytes (module graph, excluding the 8-byte total trailer)."""
    elf_end = find_elf_end(data)
    if len(data) <= elf_end + U64_SIZE:
        raise ValueError("Old-style binary too small to contain payload")
    return data[elf_end:len(data) - U64_SIZE]


def extract_payload_new(data):
    """Extract payload from new-style bundled binary.
    Returns payload bytes (module graph, excluding the 8-byte length header)."""
    elf = ElfParser(data)
    idx, shdr = elf.find_section('.bun')
    if idx is None:
        raise ElfError("No .bun section found")

    bun_off = shdr['sh_offset']
    if bun_off + U64_SIZE > len(data):
        raise ElfError(".bun section header extends past end of file")
    payload_len = struct.unpack('<Q', data[bun_off:bun_off+U64_SIZE])[0]
    if payload_len == 0:
        raise ElfError(".bun section has payload_len=0 (not a standalone binary)")
    if payload_len > shdr['sh_size'] - U64_SIZE:
        raise ElfError(f"Invalid payload length: {payload_len} > sh_size - {U64_SIZE}")

    return data[bun_off + U64_SIZE:bun_off + U64_SIZE + payload_len]


def build_old_style(wrapper_data, payload):
    """Build old-style output: wrapper + payload + 8-byte total."""
    output = bytearray(wrapper_data) + payload
    total = len(output) + U64_SIZE
    output += struct.pack('<Q', total)
    return bytes(output)


def build_new_style(wrapper_data, payload):
    """Build new-style output by replicating bun's writeBunSection in Python."""
    data = bytearray(wrapper_data)
    elf = ElfParser(data)

    bun_idx, bun_shdr = elf.find_section('.bun')
    if bun_idx is None:
        raise ElfError(
            "Wrapper has no .bun section. Rebuild wrapper with 'make' to support new-style binaries."
        )
    bun_section_offset = bun_shdr['sh_offset']

    gs_idx, _ = elf.find_phdr_by_type(PT_GNU_STACK)
    if gs_idx is None:
        raise ElfError("Wrapper has no PT_GNU_STACK segment")

    ps = page_size(elf.e_machine)

    max_vaddr_end = 0
    for i in range(elf.e_phnum):
        phdr = elf.read_phdr(i)
        if phdr['p_type'] != PT_LOAD:
            continue
        max_vaddr_end = max(max_vaddr_end, phdr['p_vaddr'] + phdr['p_memsz'])

    new_vaddr = align_up(max_vaddr_end, ps)
    new_file_offset = align_up(len(data), ps)

    header_size = U64_SIZE
    new_content_size = header_size + len(payload)
    aligned_new_size = align_up(new_content_size, ps)

    shdr_table_size = elf.e_shnum * elf.e_shentsize
    new_shdr_offset = new_file_offset + aligned_new_size
    total_new_size = new_shdr_offset + shdr_table_size

    old_file_size = len(data)

    data.extend(b'\x00' * (total_new_size - old_file_size))

    old_shdr_offset = elf.e_shoff
    data[new_shdr_offset:new_shdr_offset + shdr_table_size] = data[
        old_shdr_offset:old_shdr_offset + shdr_table_size
    ]

    elf.set_e_shoff(new_shdr_offset)

    # Write payload: [u64 len][data][zero padding]
    struct.pack_into('<Q', data, new_file_offset, len(payload))
    data[new_file_offset + header_size:new_file_offset + header_size + len(payload)] = payload

    # Write new_vaddr at ORIGINAL .bun location
    # This is what the runtime dereferences via &BUN_COMPILED.size
    struct.pack_into('<Q', data, bun_section_offset, new_vaddr)

    bun_shdr = elf.read_shdr(bun_idx)
    bun_shdr['sh_offset'] = new_file_offset
    bun_shdr['sh_size'] = new_content_size
    bun_shdr['sh_addr'] = new_vaddr
    elf.write_shdr(bun_idx, bun_shdr)

    new_phdr = {
        'p_type': PT_LOAD,
        'p_flags': PF_R,
        'p_offset': new_file_offset,
        'p_vaddr': new_vaddr,
        'p_paddr': new_vaddr,
        'p_filesz': aligned_new_size,
        'p_memsz': aligned_new_size,
        'p_align': ps,
    }
    elf.write_phdr(gs_idx, new_phdr)

    return bytes(data)


def replace_runtime(input_path, output_path=None, wrapper_path=None, force_format=None):
    input_file = Path(input_path).resolve()

    if output_path is not None:
        output_file = Path(output_path).resolve()
        backup_file = None
    elif input_file.suffix == '.bak':
        output_file = input_file.with_suffix('')
        if output_file.exists():
            error(f"Cannot restore from backup: {output_file} already exists")
            sys.exit(1)
        backup_file = None
    else:
        backup_file = input_file.with_suffix(input_file.suffix + '.bak')
        if backup_file.exists():
            error(f"Backup already exists: {backup_file}")
            sys.exit(1)
        output_file = input_file

    if wrapper_path is None:
        wrapper_path = "~/.bun/bin/bun"
    wrapper_file = Path(wrapper_path).expanduser().resolve()

    if not input_file.exists():
        error(f"Input file not found: {input_file}")
        sys.exit(1)

    if not wrapper_file.exists():
        error(f"Wrapper not found: {wrapper_file}")
        sys.exit(1)

    log(f"Reading input: {input_file.name}")
    with open(input_file, 'rb') as f:
        input_data = f.read()

    log(f"Reading wrapper: {wrapper_file.name}")
    with open(wrapper_file, 'rb') as f:
        wrapper_data = f.read()

    if len(wrapper_data) < 64 or wrapper_data[:4] != ELF_MAGIC:
        error("Wrapper is not a valid ELF binary")
        sys.exit(1)

    detected_format = detect_format(input_data)
    if detected_format is None:
        error("Input doesn't appear to be a Bun bundled binary (no .bun section or '---- Bun! ----' marker)")
        sys.exit(1)

    out_format = detected_format if force_format in (None, 'auto') else force_format

    log(f"Detected input format: {detected_format}")
    log(f"Output format: {out_format}")

    try:
        if detected_format == 'new':
            payload = extract_payload_new(input_data)
        else:
            payload = extract_payload_old(input_data)
    except (ElfError, ValueError) as e:
        error(f"Failed to extract payload: {e}")
        sys.exit(1)

    log(f"Extracted payload: {len(payload):,} bytes")

    try:
        if out_format == 'new':
            output_data = build_new_style(wrapper_data, payload)
            log(f"Built new-style ELF with embedded .bun section")
        else:
            output_data = build_old_style(wrapper_data, payload)
            log(f"Built old-style appended binary")
    except ElfError as e:
        error(f"Failed to build output: {e}")
        sys.exit(1)

    log(f"Output size: {len(output_data):,} bytes")

    if backup_file is not None:
        log(f"Creating backup: {backup_file.name}")
        input_file.rename(backup_file)

    log(f"Writing output: {output_file.name}")
    with open(output_file, 'wb') as f:
        f.write(output_data)

    output_file.chmod(0o755)

    backup_info = f" (backup at {backup_file.name})" if backup_file else ""
    log(f"Success!{backup_info}")


def main():
    args = sys.argv[1:]

    if '-h' in args or '--help' in args or len(args) < 1:
        print("""Usage: python replace_runtime.py <input> [output] [--wrapper <path>] [--format <auto|old|new>]

Arguments:
  input              Path to the bundled bun executable
  output             Path for the output file (optional)
                     If not specified, the original file is backed up with .bak extension
                     and the output overwrites the original file.

Options:
  --wrapper <path>   Path to the wrapper binary (default: ~/.bun/bin/bun)
  --format <fmt>     Output format: auto, old, or new (default: auto)
                     auto: match the input binary's format
                     old:  force old-style (append payload + size trailer)
                     new:  force new-style (ELF .bun section + PT_LOAD)

Examples:
  python replace_runtime.py ./myapp
    # Auto-detect format, backs up ./myapp to ./myapp.bak, outputs to ./myapp

  python replace_runtime.py ./myapp.bak
    # Restores from backup, outputs to ./myapp (original without .bak)

  python replace_runtime.py ./myapp --wrapper /path/to/custom/bun
    # Uses custom wrapper, backs up and outputs to ./myapp

  python replace_runtime.py ./myapp ./myapp-wrapped --format new
    # Forces new-style output even if input is old-style
""")
        sys.exit(0)

    input_path = args[0]
    output_path = None
    wrapper_path = None
    force_format = None

    i = 1
    while i < len(args):
        if args[i] == '--wrapper':
            if i + 1 >= len(args):
                error("--wrapper requires a path")
                sys.exit(1)
            wrapper_path = args[i + 1]
            i += 2
        elif args[i] == '--format':
            if i + 1 >= len(args):
                error("--format requires a value")
                sys.exit(1)
            force_format = args[i + 1]
            if force_format not in ('auto', 'old', 'new'):
                error(f"Invalid format: {force_format}. Use auto, old, or new.")
                sys.exit(1)
            i += 2
        elif output_path is None:
            output_path = args[i]
            i += 1
        else:
            error(f"Unexpected argument: {args[i]}")
            sys.exit(1)

    replace_runtime(input_path, output_path, wrapper_path, force_format)


if __name__ == '__main__':
    main()
