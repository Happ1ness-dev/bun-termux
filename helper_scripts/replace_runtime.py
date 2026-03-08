#!/data/data/com.termux/files/usr/bin/env python
"""
Replace the bun runtime in a bundled executable with a custom wrapper binary.

Credit: https://github.com/kaan-escober/bun-termux-loader/blob/master/build.py
"""

import struct
import sys
import os
from pathlib import Path

ELF_MAGIC = b'\x7fELF'
BUN_MARKER = b'---- Bun! ----'

def log(msg):
    print(f"[+] {msg}")

def error(msg):
    print(f"[!] Error: {msg}", file=sys.stderr)

def find_elf_end(data: bytes) -> int:
    if len(data) < 64 or data[:4] != ELF_MAGIC:
        raise ValueError("Not a valid ELF file")
    if data[4] != 2:
        raise ValueError("Only 64-bit ELF supported")

    e_phoff, e_shoff = struct.unpack('<QQ', data[32:48])
    e_phentsize, e_phnum = struct.unpack('<HH', data[54:58])
    e_shentsize, e_shnum = struct.unpack('<HH', data[58:62])

    end = 0
    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        p_type = struct.unpack('<I', data[ph:ph+4])[0]
        if p_type == 1:  # PT_LOAD
            p_offset, p_filesz = struct.unpack('<QQ', data[ph+8:ph+16] + data[ph+32:ph+40])
            seg_end = p_offset + p_filesz
            if seg_end > end:
                end = seg_end

    if e_shoff > 0 and e_shnum > 0:
        sh_end = e_shoff + e_shentsize * e_shnum
        if sh_end > end:
            end = sh_end

    return end

def check_bun_marker(data: bytes) -> bool:
    return BUN_MARKER in data[-256:]

def replace_runtime(input_path: str, output_path: str = None, wrapper_path: str = None):
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
        wrapper_path = os.path.expanduser("~/.bun/bin/bun")
    wrapper_file = Path(wrapper_path).resolve()

    if not input_file.exists():
        error(f"Input file not found: {input_file}")
        sys.exit(1)

    if not wrapper_file.exists():
        error(f"Wrapper not found: {wrapper_file}")
        sys.exit(1)

    log(f"Reading input: {input_file.name}")
    with open(input_file, 'rb') as f:
        input_data = f.read()

    if not check_bun_marker(input_data):
        error("Input doesn't appear to be a Bun bundled binary (missing '---- Bun! ----' marker)")
        sys.exit(1)

    log(f"Reading wrapper: {wrapper_file.name}")
    with open(wrapper_file, 'rb') as f:
        wrapper_data = f.read()

    if len(wrapper_data) < 64 or wrapper_data[:4] != ELF_MAGIC:
        error("Wrapper is not a valid ELF binary")
        sys.exit(1)

    try:
        original_elf_end = find_elf_end(input_data)
    except ValueError as e:
        error(f"Failed to parse input ELF: {e}")
        sys.exit(1)

    log(f"Original runtime size: {original_elf_end:,} bytes")
    log(f"Wrapper size: {len(wrapper_data):,} bytes")

    embedded_data = input_data[original_elf_end:]
    output_data = bytearray(wrapper_data) + embedded_data
    output_data[-8:] = struct.pack('<Q', len(output_data))

    if backup_file is not None:
        log(f"Creating backup: {backup_file.name}")
        input_file.rename(backup_file)

    log(f"Writing output: {output_file.name}")
    with open(output_file, 'wb') as f:
        f.write(output_data)

    os.chmod(output_file, 0o755)

    backup_info = f" (backup at {backup_file.name})" if backup_file else ""
    log(f"Success! Output size: {len(output_data):,} bytes{backup_info}")

def main():
    args = sys.argv[1:]

    if '-h' in args or '--help' in args or len(args) < 1:
        print("""Usage: python replace_runtime.py <input> [output] [--wrapper <path>]

Arguments:
  input              Path to the bundled bun executable
  output             Path for the output file (optional)
                     If not specified, the original file is backed up with .bak extension
                     and the output overwrites the original file.

Options:
  --wrapper <path>   Path to the wrapper binary (default: ~/.bun/bin/bun)

Examples:
  python replace_runtime.py ./myapp
    # Backs up ./myapp to ./myapp.bak, outputs to ./myapp

  python replace_runtime.py ./myapp.bak
    # Restores from backup, outputs to ./myapp (original without .bak)

  python replace_runtime.py ./myapp --wrapper /path/to/custom/bun
    # Uses custom wrapper, backs up and outputs to ./myapp

  python replace_runtime.py ./myapp ./myapp-wrapped --wrapper /path/to/custom/bun
    # Uses custom wrapper, outputs to ./myapp-wrapped (no backup)
""")
        sys.exit(0 if '-h' in args or '--help' in args else 1)

    input_path = args[0]
    output_path = None
    wrapper_path = None
    
    i = 1
    while i < len(args):
        if args[i] == '--wrapper' and i + 1 < len(args):
            wrapper_path = args[i + 1]
            i += 2
        elif output_path is None:
            output_path = args[i]
            i += 1
        else:
            error(f"Unexpected argument: {args[i]}")
            sys.exit(1)

    replace_runtime(input_path, output_path, wrapper_path)

if __name__ == '__main__':
    main()
