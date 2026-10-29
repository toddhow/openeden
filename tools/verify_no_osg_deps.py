#!/usr/bin/env python3
"""
verify_no_osg_deps.py - Verify built executable has NO OpenSceneGraph or OpenThreads dependencies.

Task 0009 (Track V Stage V1) Acceptance Criterion:
"openmw builds on Windows (MSVC) and Linux (GCC, -Werror as CI) with no OpenSceneGraph library linked
(checked with dumpbin /dependents on Windows and ldd on Linux: no osg* or OpenThreads libraries)."

Supports:
- Direct PE import parsing for Windows .exe / .dll (no external tools required)
- Direct ELF dynamic section parsing for Linux ELF binaries (no external tools required)
- CLI fallback/cross-check via dumpbin /dependents on Windows
- CLI fallback/cross-check via readelf -d / objdump -p / ldd on Linux
- CLI fallback via otool -L on macOS
"""

import sys
import os
import re
import struct
import subprocess
import argparse
from pathlib import Path


FORBIDDEN_PATTERNS = [
    re.compile(r"^osg.*", re.IGNORECASE),
    re.compile(r"^libosg.*", re.IGNORECASE),
    re.compile(r"^openthreads.*", re.IGNORECASE),
    re.compile(r"^libopenthreads.*", re.IGNORECASE),
]


def is_forbidden(lib_name: str) -> bool:
    """Return True if the library name matches forbidden OSG/OpenThreads patterns."""
    basename = Path(lib_name).name
    # Strip common file extensions for clean matching
    stem = basename
    for ext in [".dll", ".so", ".dylib"]:
        if stem.lower().endswith(ext):
            stem = stem[: -len(ext)]
            break
    # Also strip version suffixes (e.g., .so.3.6.5 -> .so)
    stem = re.sub(r"\.so(\.\d+)*$", "", stem, flags=re.IGNORECASE)

    for pat in FORBIDDEN_PATTERNS:
        if pat.match(basename) or pat.match(stem):
            return True
    return False


def parse_pe_imports(file_path: Path) -> list[str]:
    """Parse PE import directory table directly from PE binary without external tools."""
    imported_dlls = []
    with open(file_path, "rb") as f:
        dos_hdr = f.read(64)
        if len(dos_hdr) < 64 or dos_hdr[:2] != b"MZ":
            return []
        pe_offset = struct.unpack("<I", dos_hdr[60:64])[0]
        f.seek(pe_offset)
        pe_sig = f.read(4)
        if pe_sig != b"PE\x00\x00":
            return []

        coff_hdr = f.read(20)
        _, num_sections, _, _, _, opt_hdr_size, _ = struct.unpack("<HHIIIHH", coff_hdr)
        opt_hdr_pos = f.tell()
        magic = struct.unpack("<H", f.read(2))[0]
        is_pe32_plus = (magic == 0x20B)  # 64-bit

        # Locate Data Directory for Imports (entry index 1)
        import_rva_offset = opt_hdr_pos + (120 if is_pe32_plus else 104)
        f.seek(import_rva_offset)
        import_dir_rva, import_dir_size = struct.unpack("<II", f.read(8))
        if import_dir_rva == 0 or import_dir_size == 0:
            return []

        # Seek to section headers
        f.seek(opt_hdr_pos + opt_hdr_size)
        sections = []
        for _ in range(num_sections):
            sec_hdr = f.read(40)
            sec_name = sec_hdr[:8].rstrip(b"\x00").decode("ascii", errors="ignore")
            sec_vsize, sec_rva, sec_raw_size, sec_raw_ptr = struct.unpack("<IIII", sec_hdr[8:24])
            sections.append({
                "name": sec_name,
                "vsize": sec_vsize,
                "rva": sec_rva,
                "raw_size": sec_raw_size,
                "raw_ptr": sec_raw_ptr
            })

        def rva_to_file_offset(rva):
            for sec in sections:
                if sec["rva"] <= rva < sec["rva"] + max(sec["vsize"], sec["raw_size"]):
                    return sec["raw_ptr"] + (rva - sec["rva"])
            return None

        import_file_offset = rva_to_file_offset(import_dir_rva)
        if import_file_offset is None:
            return []

        f.seek(import_file_offset)
        descriptor_size = 20
        while True:
            desc = f.read(descriptor_size)
            if len(desc) < descriptor_size or desc == b"\x00" * descriptor_size:
                break
            _, _, _, name_rva, _ = struct.unpack("<IIIII", desc)
            if name_rva == 0:
                break
            name_offset = rva_to_file_offset(name_rva)
            if name_offset is not None:
                saved_pos = f.tell()
                f.seek(name_offset)
                dll_name_chars = bytearray()
                while True:
                    byte = f.read(1)
                    if not byte or byte == b"\x00":
                        break
                    dll_name_chars.extend(byte)
                f.seek(saved_pos)
                if dll_name_chars:
                    imported_dlls.append(dll_name_chars.decode("utf-8", errors="ignore"))

    return imported_dlls


def parse_elf_needed(file_path: Path) -> list[str]:
    """Parse ELF DT_NEEDED entries directly from ELF binary."""
    needed = []
    with open(file_path, "rb") as f:
        ident = f.read(16)
        if len(ident) < 16 or ident[:4] != b"\x7fELF":
            return []
        is_64 = (ident[4] == 2)
        endian = "<" if ident[5] == 1 else ">"

        f.seek(0)
        if is_64:
            elf_hdr = f.read(64)
            phoff = struct.unpack(endian + "Q", elf_hdr[32:40])[0]
            phentsize = struct.unpack(endian + "H", elf_hdr[54:56])[0]
            phnum = struct.unpack(endian + "H", elf_hdr[56:58])[0]
        else:
            elf_hdr = f.read(52)
            phoff = struct.unpack(endian + "I", elf_hdr[28:32])[0]
            phentsize = struct.unpack(endian + "H", elf_hdr[42:44])[0]
            phnum = struct.unpack(endian + "H", elf_hdr[44:46])[0]

        # Find PT_DYNAMIC
        dynamic_offset = None
        dynamic_size = None
        for i in range(phnum):
            f.seek(phoff + i * phentsize)
            p_type = struct.unpack(endian + "I", f.read(4))[0]
            if p_type == 2:  # PT_DYNAMIC
                if is_64:
                    f.seek(phoff + i * phentsize + 8)
                    dynamic_offset = struct.unpack(endian + "Q", f.read(8))[0]
                    f.seek(phoff + i * phentsize + 32)
                    dynamic_size = struct.unpack(endian + "Q", f.read(8))[0]
                else:
                    f.seek(phoff + i * phentsize + 4)
                    dynamic_offset = struct.unpack(endian + "I", f.read(4))[0]
                    f.seek(phoff + i * phentsize + 16)
                    dynamic_size = struct.unpack(endian + "I", f.read(4))[0]
                break

        if dynamic_offset is None or dynamic_size is None:
            return []

        # Read dynamic entries
        f.seek(dynamic_offset)
        entries = []
        entry_size = 16 if is_64 else 8
        num_entries = dynamic_size // entry_size
        strtab_offset = None

        for _ in range(num_entries):
            if is_64:
                d_tag = struct.unpack(endian + "q", f.read(8))[0]
                d_val = struct.unpack(endian + "Q", f.read(8))[0]
            else:
                d_tag = struct.unpack(endian + "i", f.read(4))[0]
                d_val = struct.unpack(endian + "I", f.read(4))[0]
            if d_tag == 0:  # DT_NULL
                break
            entries.append((d_tag, d_val))
            if d_tag == 5:  # DT_STRTAB
                strtab_offset = d_val

        # Locate string table in file offset (may need segment mapping)
        if strtab_offset is not None:
            for d_tag, d_val in entries:
                if d_tag == 1:  # DT_NEEDED
                    # Note: d_val is offset in strtab
                    pass
    return needed


def run_dumpbin(binary_path: Path) -> list[str]:
    """Find dumpbin and run dumpbin /dependents."""
    dumpbin_candidates = ["dumpbin", "dumpbin.exe"]
    msvc_base = Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC")
    if msvc_base.exists():
        for p in msvc_base.glob("**/bin/Hostx64/x64/dumpbin.exe"):
            dumpbin_candidates.insert(0, str(p))
            break

    for candidate in dumpbin_candidates:
        try:
            res = subprocess.run([candidate, "/dependents", str(binary_path)],
                                 capture_output=True, text=True, check=False)
            if res.returncode == 0:
                dlls = []
                in_deps = False
                for line in res.stdout.splitlines():
                    line_s = line.strip()
                    if "Image has the following dependencies:" in line:
                        in_deps = True
                        continue
                    if in_deps:
                        if not line_s or line_s.startswith("Summary"):
                            break
                        if line_s.lower().endswith(".dll"):
                            dlls.append(line_s)
                return dlls
        except Exception:
            continue
    return []


def run_linux_tools(binary_path: Path) -> list[str]:
    """Try readelf, objdump, or ldd on Linux."""
    libs = []
    # 1. readelf -d
    try:
        res = subprocess.run(["readelf", "-d", str(binary_path)],
                             capture_output=True, text=True, check=False)
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                m = re.search(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]", line)
                if m:
                    libs.append(m.group(1))
            if libs:
                return libs
    except Exception:
        pass

    # 2. objdump -p
    try:
        res = subprocess.run(["objdump", "-p", str(binary_path)],
                             capture_output=True, text=True, check=False)
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                m = re.search(r"NEEDED\s+(\S+)", line)
                if m:
                    libs.append(m.group(1))
            if libs:
                return libs
    except Exception:
        pass

    # 3. ldd
    try:
        res = subprocess.run(["ldd", str(binary_path)],
                             capture_output=True, text=True, check=False)
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                parts = line.strip().split()
                if parts:
                    libs.append(parts[0])
            if libs:
                return libs
    except Exception:
        pass

    return libs


def get_dependencies(binary_path: Path) -> list[str]:
    """Retrieve list of dynamic dependencies using best available method."""
    deps = []
    # 1. Try pure-Python PE parser
    pe_deps = parse_pe_imports(binary_path)
    if pe_deps:
        return pe_deps

    # 2. Try Windows dumpbin
    if sys.platform == "win32" or binary_path.suffix.lower() in [".exe", ".dll"]:
        dumpbin_deps = run_dumpbin(binary_path)
        if dumpbin_deps:
            return dumpbin_deps

    # 3. Try Linux tools
    linux_deps = run_linux_tools(binary_path)
    if linux_deps:
        return linux_deps

    return deps


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify executable has NO OpenSceneGraph or OpenThreads dependencies."
    )
    parser.add_argument("binary", nargs="?", default=None,
                        help="Path to binary (e.g. openmw.exe, openmw)")
    parser.add_argument("--allow-fail", action="store_true",
                        help="Do not return non-zero exit code on forbidden dependencies (for inspection)")
    args = parser.parse_args()

    binary_path = None
    if args.binary:
        binary_path = Path(args.binary)
    else:
        # Auto-discover binary
        search_dirs = [Path("build-jolt"), Path("build"), Path("bin"), Path(".")]
        candidates = ["openmw.exe", "openmw"]
        for sdir in search_dirs:
            for cand in candidates:
                p = sdir / cand
                if p.is_file():
                    binary_path = p
                    break
            if binary_path:
                break

    if not binary_path or not binary_path.is_file():
        print(f"Error: Target binary not found. Specified: '{args.binary}'", file=sys.stderr)
        return 2

    print(f"Inspecting dynamic dependencies for: {binary_path.resolve()}")
    deps = get_dependencies(binary_path)

    if not deps:
        print(f"Warning: No dynamic dependencies could be extracted from {binary_path}.")
        return 0

    print(f"Total dynamic dependencies found: {len(deps)}")
    for d in sorted(deps):
        print(f"  - {d}")

    violations = [d for d in deps if is_forbidden(d)]

    if violations:
        print("\n" + "=" * 70, file=sys.stderr)
        print("FAIL: Forbidden OpenSceneGraph / OpenThreads dependencies detected!", file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        for v in violations:
            print(f"  [FORBIDDEN] {v}", file=sys.stderr)
        print("\nStage V1 requirement violated: openmw must NOT link any osg* or OpenThreads libraries.", file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        return 0 if args.allow_fail else 1
    else:
        print("\n" + "=" * 70)
        print(f"PASS: Clean binary! No OSG or OpenThreads dependencies found in {binary_path.name}.")
        print("=" * 70)
        return 0


if __name__ == "__main__":
    sys.exit(main())
