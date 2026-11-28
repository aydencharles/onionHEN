#!/usr/bin/env python3
"""Convert a standard x86-64 ELF64 shared object (linked with prx.ld) into a
PS5 ET_SCE_DYNAMIC OELF and PS5 Fake-SELF (.prx).

Supports PS5 native processes (EI_ABIVERSION=2, SELF magic 0xEEF51454) with
configurable PT_SCE_MODULE_PARAM SDK version fields.
"""

import argparse
import hashlib
import os
import struct
import sys

PAGE_SIZE = 0x4000
BLOCK_SIZE = 0x4000

ELF_MAGIC = b"\x7fELF"
ET_SCE_DYNAMIC = 0xFE18
EM_X86_64 = 0x3E
ELFOSABI_FREEBSD = 9
EI_ABIVERSION_PS5 = 2

PT_LOAD = 1
PT_DYNAMIC = 2
PT_NOTE = 4
PT_SCE_MODULE_PARAM = 0x61000002
PT_SCE_COMMENT = 0x6FFFFF00
PT_SCE_VERSION = 0x6FFFFF01

DT_NULL = 0
DT_NEEDED = 1
DT_PLTRELSZ = 2
DT_PLTGOT = 3
DT_HASH = 4
DT_STRTAB = 5
DT_SYMTAB = 6
DT_RELA = 7
DT_RELASZ = 8
DT_RELAENT = 9
DT_STRSZ = 10
DT_SYMENT = 11
DT_INIT = 12
DT_FINI = 13
DT_PLTREL = 20
DT_JMPREL = 23
DT_RELACOUNT = 0x6FFFFFF9

# PS5 (EI_ABIVERSION=2) SCE dynamic tags
DT_SCE_IMPORT_LIB_ATTR = 0x61000019
DT_SCE_HASHSZ = 0x6100003D
DT_SCE_SYMTABSZ = 0x6100003F
DT_SCE_ORIGINAL_FILENAME_1 = 0x61000041
DT_SCE_MODULE_INFO_1 = 0x61000043
DT_SCE_NEEDED_MODULE_1 = 0x61000045
DT_SCE_IMPORT_LIB_1 = 0x61000049

R_X86_64_RELATIVE = 8

SELF_MAGIC_PS5 = b"\x54\x14\xF5\xEE"
SELF_MAGIC_PS4 = b"\x4F\x15\x3D\x1D"
PTYPE_FAKE = 1
DEFAULT_PAID = 0x3100000000000002

NID_B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
NID_SUFFIX = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")


def align_up(val: int, align: int) -> int:
    return (val + align - 1) & ~(align - 1)


def compute_nid(symbol_name: str) -> str:
    digest = hashlib.sha1(symbol_name.encode("ascii") + NID_SUFFIX).digest()
    val = struct.unpack("<Q", digest[:8])[0]
    bits = (val << 2)
    return "".join(NID_B64[(bits >> (i * 6)) & 0x3F] for i in range(10, -1, -1))


def encode_b64_id(idx: int) -> str:
    if idx == 0:
        return NID_B64[0]
    chars = []
    while idx > 0:
        chars.append(NID_B64[idx & 0x3F])
        idx >>= 6
    return "".join(reversed(chars))


def elf_sysv_hash(name: bytes) -> int:
    h = 0
    for b in name:
        h = ((h << 4) + b) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


def build_sysv_hash(sym_names: list[bytes]) -> bytes:
    n = len(sym_names)
    nbucket = max(1, n)
    nchain = n
    buckets = [0] * nbucket
    chains = [0] * nchain
    for idx in range(1, n):
        h = elf_sysv_hash(sym_names[idx]) % nbucket
        chains[idx] = buckets[h]
        buckets[h] = idx
    return struct.pack(f"<II{nbucket}I{nchain}I", nbucket, nchain, *buckets, *chains)


def read_cstr(blob: bytes, offset: int) -> str:
    end = blob.index(b"\x00", offset)
    return blob[offset:end].decode("ascii", errors="replace")


def parse_input_elf(data: bytes):
    if data[:4] != ELF_MAGIC or data[4] != 2 or data[5] != 1:
        raise ValueError("Input must be a 64-bit little-endian ELF")
    (
        e_type,
        e_machine,
        e_version,
        e_entry,
        e_phoff,
        e_shoff,
        e_flags,
        e_ehsize,
        e_phentsize,
        e_phnum,
        e_shentsize,
        e_shnum,
        e_shstrndx,
    ) = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    if e_machine != EM_X86_64:
        raise ValueError(f"Expected x86-64 ELF, got e_machine=0x{e_machine:x}")

    phdrs = []
    for i in range(e_phnum):
        phdrs.append(
            struct.unpack_from("<IIQQQQQQ", data, e_phoff + i * e_phentsize)
        )

    shdrs = []
    for i in range(e_shnum):
        shdrs.append(
            struct.unpack_from("<IIQQQQIIQQ", data, e_shoff + i * e_shentsize)
        )
    shstrtab_off = shdrs[e_shstrndx][4]
    shstrtab_sz = shdrs[e_shstrndx][5]
    shstrtab = data[shstrtab_off : shstrtab_off + shstrtab_sz]

    sections = {}
    for sh in shdrs:
        name = read_cstr(shstrtab, sh[0])
        sections[name] = {
            "name": name,
            "type": sh[1],
            "flags": sh[2],
            "addr": sh[3],
            "offset": sh[4],
            "size": sh[5],
            "link": sh[6],
            "info": sh[7],
            "align": sh[8],
            "entsize": sh[9],
        }
    return phdrs, sections


def build_sce_version(module_stubs: list[str], ppr_sdk: int, sdk_unk: int) -> bytes:
    # Pack matching Sony PT_SCE_VERSION records:
    #   uint8_t zero, uint8_t record_len, uint8_t unk=0, uint8_t unk2=0, uint8_t name_type=8,
    #   ascii "<name>:", followed by two 8-byte version tuples (big-endian sdk + unk)
    ver_tuple = struct.pack(">II", ppr_sdk, sdk_unk)
    out = bytearray()
    names = ["crti"] + [f"{lib}_stub_weak" for lib in module_stubs]
    for name in names:
        label = (name + ":").encode("ascii")
        body = bytes([0x08]) + label + ver_tuple + ver_tuple
        rec_len = len(body) + 1
        out.extend(struct.pack("<BBH", 0, rec_len, 0))
        out.extend(body)
    return bytes(out)


def build_ps5_oelf(
    in_data: bytes,
    module_name: str,
    sdk_ver: int,
    ppr_sdk: int,
    sdk_unk: int,
) -> bytes:
    phdrs, sections = parse_input_elf(in_data)

    # Keep the real loadable segments (RX, R, RW); drop temporary flags=0 dyn segment.
    load_segs = []
    for p_type, p_flags, p_off, p_va, p_pa, p_fsz, p_msz, p_al in phdrs:
        if p_type == PT_LOAD and p_flags != 0:
            seg_bytes = bytearray(in_data[p_off : p_off + p_fsz])
            load_segs.append(
                {
                    "flags": p_flags,
                    "vaddr": p_va,
                    "filesz": p_fsz,
                    "memsz": p_msz,
                    "align": max(p_al, PAGE_SIZE),
                    "data": seg_bytes,
                }
            )
    if not load_segs:
        raise ValueError("No loadable PT_LOAD segments found in input ELF")

    # Locate and optionally patch .data.sce_module_param inside the RW segment.
    if ".data.sce_module_param" not in sections:
        raise ValueError("Missing .data.sce_module_param section (link crtprx.o)")
    mp_sec = sections[".data.sce_module_param"]
    if mp_sec["size"] != 0x20:
        raise ValueError(f"Expected 32-byte PS5 sce_module_param, got {mp_sec['size']}")
    mp_va = mp_sec["addr"]
    mp_patched = struct.pack(
        "<QIIIIII", 0x20, 0x3C13F4BF, 3, sdk_ver, ppr_sdk, sdk_unk, 0
    )

    rw_seg = None
    for seg in load_segs:
        if seg["vaddr"] <= mp_va < seg["vaddr"] + seg["filesz"]:
            rw_seg = seg
            rel = mp_va - seg["vaddr"]
            seg["data"][rel : rel + 0x20] = mp_patched
            break
    if rw_seg is None:
        raise ValueError(".data.sce_module_param does not fall inside any PT_LOAD")

    # Parse input .dynamic, .dynstr, .dynsym, .rela.plt, .rela.dyn
    dynstr = b""
    if ".dynstr" in sections:
        s = sections[".dynstr"]
        dynstr = in_data[s["offset"] : s["offset"] + s["size"]]

    needed_files = []
    init_va = None
    fini_va = None
    pltgot_va = 0
    if ".dynamic" in sections:
        s = sections[".dynamic"]
        for off in range(s["offset"], s["offset"] + s["size"], 16):
            tag, val = struct.unpack_from("<qQ", in_data, off)
            if tag == DT_NULL:
                break
            elif tag == DT_NEEDED:
                needed_files.append(read_cstr(dynstr, val))
            elif tag == DT_INIT:
                init_va = val
            elif tag == DT_FINI:
                fini_va = val
            elif tag == DT_PLTGOT:
                pltgot_va = val

    if not needed_files:
        needed_files = ["libkernel.prx"]

    # Derive module/library metadata for each DT_NEEDED PRX.
    # For a single-library PRX (libkernel.prx), lib_id=0 (#A), mod_id=1 (#B).
    needed_entries = []
    for idx, fname in enumerate(needed_files):
        base = os.path.splitext(os.path.basename(fname))[0]
        needed_entries.append(
            {
                "file": fname,
                "name": base,
                "mod_id": idx + 1,
                "lib_id": idx,
            }
        )

    # Parse symbols from .dynsym; convert undefined imports to PS5 NID format.
    # Default all imports to needed_entries[0] (libkernel) unless extended.
    orig_syms = []
    if ".dynsym" in sections:
        s = sections[".dynsym"]
        for off in range(s["offset"], s["offset"] + s["size"], 24):
            orig_syms.append(struct.unpack_from("<IBBHQQ", in_data, off))

    # Build new STRTAB and SYMTAB
    strtab = bytearray(b"\x00")
    sym_names_bytes = [b""]

    def add_str(text: str) -> int:
        pos = len(strtab)
        strtab.extend(text.encode("ascii") + b"\x00")
        return pos

    new_syms = [struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0)]
    sym_remap = {0: 0}
    default_lib = needed_entries[0]
    default_suffix = f"#{encode_b64_id(default_lib['lib_id'])}#{encode_b64_id(default_lib['mod_id'])}"

    for old_idx in range(1, len(orig_syms)):
        st_name, st_info, st_other, st_shndx, st_val, st_size = orig_syms[old_idx]
        raw_name = read_cstr(dynstr, st_name)
        if st_shndx == 0 and raw_name:
            nid_name = compute_nid(raw_name) + default_suffix
            new_name_off = add_str(nid_name)
            sym_names_bytes.append(nid_name.encode("ascii"))
            new_idx = len(new_syms)
            sym_remap[old_idx] = new_idx
            new_syms.append(
                struct.pack("<IBBHQQ", new_name_off, st_info, 0, 0, 0, 0)
            )

    for entry in needed_entries:
        entry["file_off"] = add_str(entry["file"])
        entry["name_off"] = add_str(entry["name"])

    mod_name_off = add_str(module_name)
    orig_fname_off = add_str(module_name + ".elf")

    # Remap .rela.plt (JMPREL) and .rela.dyn (RELA)
    def remap_relas(sec_name: str):
        out = bytearray()
        rel_count = 0
        if sec_name not in sections:
            return bytes(out), 0
        s = sections[sec_name]
        for off in range(s["offset"], s["offset"] + s["size"], 24):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", in_data, off)
            old_sym = r_info >> 32
            r_type = r_info & 0xFFFFFFFF
            if r_type == R_X86_64_RELATIVE:
                new_sym = 0
                rel_count += 1
            else:
                if old_sym not in sym_remap:
                    raise ValueError(
                        f"Relocation in {sec_name} references non-imported symbol index {old_sym}"
                    )
                new_sym = sym_remap[old_sym]
            out.extend(
                struct.pack("<QQq", r_offset, (new_sym << 32) | r_type, r_addend)
            )
        return bytes(out), rel_count

    jmprel_bytes, _ = remap_relas(".rela.plt")
    rela_bytes, rela_rel_count = remap_relas(".rela.dyn")

    symtab_bytes = b"".join(new_syms)
    hash_bytes = build_sysv_hash(sym_names_bytes)

    # Compute deterministic 20-byte GNU Build-ID fingerprint
    fp_hasher = hashlib.sha1()
    for seg in load_segs:
        fp_hasher.update(seg["data"])
    fp_hasher.update(module_name.encode("ascii"))
    fingerprint = fp_hasher.digest()
    note_bytes = struct.pack("<III", 4, 20, 3) + b"GNU\x00" + fingerprint

    # Compute dynlibdata VA (next page after max loaded segment end)
    max_va = max(seg["vaddr"] + seg["memsz"] for seg in load_segs)
    dyn_base_va = align_up(max_va, PAGE_SIZE)

    # Layout inside the flags=0 dynlibdata PT_LOAD segment
    dyn_buf = bytearray()

    def push_blob(blob: bytes, align: int = 8) -> int:
        pad = align_up(len(dyn_buf), align) - len(dyn_buf)
        if pad:
            dyn_buf.extend(b"\x00" * pad)
        va = dyn_base_va + len(dyn_buf)
        dyn_buf.extend(blob)
        return va

    strtab_va = push_blob(bytes(strtab), 8)
    symtab_va = push_blob(symtab_bytes, 8)
    jmprel_va = push_blob(jmprel_bytes, 8) if jmprel_bytes else 0
    rela_va = push_blob(rela_bytes, 8) if rela_bytes else 0
    hash_va = push_blob(hash_bytes, 8)
    note_va = push_blob(note_bytes, 4)

    # Build PT_DYNAMIC entries
    dyn_entries = []
    for entry in needed_entries:
        dyn_entries.append((DT_NEEDED, entry["file_off"]))
        dyn_entries.append(
            (
                DT_SCE_NEEDED_MODULE_1,
                (entry["mod_id"] << 48) | (0x0101 << 32) | entry["name_off"],
            )
        )
        dyn_entries.append(
            (
                DT_SCE_IMPORT_LIB_1,
                (entry["lib_id"] << 48) | (0x0001 << 32) | entry["name_off"],
            )
        )
        dyn_entries.append((DT_SCE_IMPORT_LIB_ATTR, (entry["lib_id"] << 48) | 0x9))

    dyn_entries.append((DT_SCE_MODULE_INFO_1, (0x0101 << 32) | mod_name_off))
    dyn_entries.append((DT_SCE_ORIGINAL_FILENAME_1, orig_fname_off))

    if rela_bytes:
        dyn_entries.append((DT_RELA, rela_va))
        dyn_entries.append((DT_RELASZ, len(rela_bytes)))
        dyn_entries.append((DT_RELAENT, 24))
        if rela_rel_count:
            dyn_entries.append((DT_RELACOUNT, rela_rel_count))

    if jmprel_bytes:
        dyn_entries.append((DT_JMPREL, jmprel_va))
        dyn_entries.append((DT_PLTRELSZ, len(jmprel_bytes)))
        if pltgot_va:
            dyn_entries.append((DT_PLTGOT, pltgot_va))
        dyn_entries.append((DT_PLTREL, 7))

    dyn_entries.append((DT_SYMTAB, symtab_va))
    dyn_entries.append((DT_SYMENT, 24))
    dyn_entries.append((DT_STRTAB, strtab_va))
    dyn_entries.append((DT_STRSZ, len(strtab)))
    dyn_entries.append((DT_HASH, hash_va))

    if init_va is not None:
        dyn_entries.append((DT_INIT, init_va))
    if fini_va is not None:
        dyn_entries.append((DT_FINI, fini_va))

    dyn_entries.append((DT_SCE_SYMTABSZ, len(symtab_bytes)))
    dyn_entries.append((DT_SCE_HASHSZ, len(hash_bytes)))
    dyn_entries.append((DT_NULL, 0))

    dynamic_bytes = b"".join(struct.pack("<QQ", t, v) for t, v in dyn_entries)
    dynamic_va = push_blob(dynamic_bytes, 8)

    # Patch GOT.PLT[0] to point to the new PT_DYNAMIC virtual address.
    if pltgot_va:
        for seg in load_segs:
            if seg["vaddr"] <= pltgot_va < seg["vaddr"] + seg["filesz"]:
                rel = pltgot_va - seg["vaddr"]
                seg["data"][rel : rel + 8] = struct.pack("<Q", dynamic_va)
                break

    # Build PT_SCE_COMMENT ("PATH" record) and PT_SCE_VERSION
    path_str = (module_name + ".elf").encode("ascii") + b"\x00"
    comment_pad = align_up(len(path_str), 4) - len(path_str)
    comment_bytes = (
        b"PATH"
        + struct.pack("<II", 4 + len(path_str) + comment_pad, len(path_str))
        + path_str
        + (b"\x00" * comment_pad)
    )
    version_bytes = build_sce_version(
        [e["name"] for e in needed_entries], ppr_sdk, sdk_unk
    )

    # Assemble output OELF file offsets (first segment at 0x4000)
    cur_off = PAGE_SIZE
    out_phdrs = []
    file_chunks = []

    mp_file_off = 0
    for seg in load_segs:
        cur_off = align_up(cur_off, PAGE_SIZE)
        seg["file_off"] = cur_off
        if seg is rw_seg:
            mp_file_off = cur_off + (mp_va - seg["vaddr"])
        out_phdrs.append(
            (
                PT_LOAD,
                seg["flags"],
                cur_off,
                seg["vaddr"],
                seg["vaddr"],
                len(seg["data"]),
                seg["memsz"],
                PAGE_SIZE,
            )
        )
        file_chunks.append((cur_off, bytes(seg["data"])))
        cur_off += len(seg["data"])

    # PT_SCE_MODULE_PARAM
    out_phdrs.append(
        (PT_SCE_MODULE_PARAM, 4, mp_file_off, mp_va, mp_va, 0x20, 0x20, 8)
    )

    # PT_LOAD (flags=0) dynlibdata
    cur_off = align_up(cur_off, PAGE_SIZE)
    dyn_file_off = cur_off
    out_phdrs.append(
        (
            PT_LOAD,
            0,
            dyn_file_off,
            dyn_base_va,
            dyn_base_va,
            len(dyn_buf),
            len(dyn_buf),
            PAGE_SIZE,
        )
    )
    file_chunks.append((dyn_file_off, bytes(dyn_buf)))
    cur_off += len(dyn_buf)

    # PT_NOTE and PT_DYNAMIC inside dynlibdata
    note_file_off = dyn_file_off + (note_va - dyn_base_va)
    dynamic_file_off = dyn_file_off + (dynamic_va - dyn_base_va)
    out_phdrs.append(
        (PT_NOTE, 0, note_file_off, note_va, note_va, len(note_bytes), len(note_bytes), 4)
    )
    out_phdrs.append(
        (
            PT_DYNAMIC,
            6,
            dynamic_file_off,
            dynamic_va,
            dynamic_va,
            len(dynamic_bytes),
            len(dynamic_bytes),
            8,
        )
    )

    # PT_SCE_COMMENT and PT_SCE_VERSION (non-loaded metadata at tail)
    cur_off = align_up(cur_off, 16)
    comment_off = cur_off
    out_phdrs.append((PT_SCE_COMMENT, 0, comment_off, 0, 0, len(comment_bytes), 0, 16))
    file_chunks.append((comment_off, comment_bytes))
    cur_off += len(comment_bytes)

    cur_off = align_up(cur_off, 16)
    version_off = cur_off
    out_phdrs.append(
        (PT_SCE_VERSION, 0, version_off, 0, 0, len(version_bytes), len(version_bytes), 1)
    )
    file_chunks.append((version_off, version_bytes))
    cur_off += len(version_bytes)

    oelf = bytearray(cur_off)
    ehdr = struct.pack(
        "<4s5B6xBHHIQQQIHHHHHH",
        ELF_MAGIC,
        2,  # ELFCLASS64
        1,  # ELFDATA2LSB
        1,  # EV_CURRENT
        ELFOSABI_FREEBSD,
        EI_ABIVERSION_PS5,
        0,
        ET_SCE_DYNAMIC,
        EM_X86_64,
        1,
        0,  # e_entry
        64,  # e_phoff
        0,  # e_shoff
        0,  # e_flags
        64,  # e_ehsize
        56,  # e_phentsize
        len(out_phdrs),
        0,  # e_shentsize
        0,  # e_shnum
        0,  # e_shstrndx
    )
    oelf[0:64] = ehdr
    for idx, ph in enumerate(out_phdrs):
        struct.pack_into("<IIQQQQQQ", oelf, 64 + idx * 56, *ph)
    for off, chunk in file_chunks:
        oelf[off : off + len(chunk)] = chunk
    return bytes(oelf)


def wrap_fself(
    oelf: bytes,
    magic: bytes = SELF_MAGIC_PS5,
    paid: int = DEFAULT_PAID,
    ptype: int = PTYPE_FAKE,
) -> bytes:
    e_ehsize, e_phentsize, e_phnum = struct.unpack_from("<HHH", oelf, 52)
    e_phoff = struct.unpack_from("<Q", oelf, 32)[0]
    elf_digest = hashlib.sha256(oelf).digest()

    phdrs = [
        struct.unpack_from("<IIQQQQQQ", oelf, e_phoff + i * e_phentsize)
        for i in range(e_phnum)
    ]

    version_data = b""
    entries = []
    entry_idx = 0
    for i, (p_type, p_flags, p_off, p_va, p_pa, p_fsz, p_msz, p_al) in enumerate(phdrs):
        if p_type == PT_SCE_VERSION:
            version_data = oelf[p_off : p_off + p_fsz]
        if p_type not in (PT_LOAD, PT_SCE_COMMENT):
            continue
        seg_data = oelf[p_off : p_off + p_fsz]
        num_blocks = max(1, align_up(p_fsz, BLOCK_SIZE) // BLOCK_SIZE)
        meta_data = b"\x00" * (32 * num_blocks)

        # Signed, has_digests, target = entry_idx + 1
        meta_props = (1 << 2) | (1 << 16) | ((entry_idx + 1) << 20)
        # Signed, has_blocks, block_size=16 KiB (ilog2(0x4000)-12 = 2), seg_index=i
        data_props = (1 << 2) | (1 << 11) | (2 << 12) | (i << 20)

        entries.append({"props": meta_props, "data": meta_data, "filesz": len(meta_data)})
        entries.append({"props": data_props, "data": seg_data, "filesz": p_fsz})
        entry_idx += 2

    num_entries = len(entries)
    elf_hdr_len = align_up(max(e_ehsize, e_phoff + e_phentsize * e_phnum), 16)

    # Common (8) + Ext (24) + Entries (32*N) + ELF headers + ExInfo (64) + NPDRM (48)
    header_size = align_up(32 + 32 * num_entries + elf_hdr_len, 16) + 64 + 48
    meta_size = num_entries * 80 + 80 + 256

    offset = header_size + meta_size
    for e in entries:
        e["offset"] = offset
        offset = align_up(offset + e["filesz"], 16)
    file_size = offset

    out = bytearray(file_size + len(version_data))
    # Common header + Extended header
    struct.pack_into(
        "<4s4BIHHQHH4x",
        out,
        0,
        magic,
        0x00,  # version
        0x01,  # mode
        0x01,  # endian
        0x12,  # attribs
        0x101,  # key_type
        header_size,
        meta_size,
        file_size,
        num_entries,
        0x22,  # flags (signed_block_count=2 | 0x2)
    )

    pos = 32
    for e in entries:
        struct.pack_into("<QQQQ", out, pos, e["props"], e["offset"], e["filesz"], e["filesz"])
        pos += 32

    # Embedded ELF header + program headers
    out[pos : pos + e_phoff + e_phentsize * e_phnum] = oelf[
        : e_phoff + e_phentsize * e_phnum
    ]
    pos = align_up(pos + elf_hdr_len, 16)

    # Extended info (64 bytes): paid, ptype, app_version, fw_version, sha256
    struct.pack_into("<QQQQ32s", out, pos, paid, ptype, 0, 0, elf_digest)
    pos += 64

    # NPDRM control block (48 bytes zeroed)
    pos += 48

    # Meta blocks (80 * num_entries bytes zeroed) + Meta footer (80 bytes, unk1=0x10000 at +48)
    pos += num_entries * 80
    struct.pack_into("<I", out, pos + 48, 0x10000)
    pos += 80

    # Empty signature (256 bytes zeroed)
    pos += 256
    assert pos == header_size + meta_size

    for e in entries:
        out[e["offset"] : e["offset"] + len(e["data"])] = e["data"]

    if version_data:
        out[file_size : file_size + len(version_data)] = version_data

    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert an x86-64 shared ELF into a PS5 FSELF PRX."
    )
    parser.add_argument("input", help="Input shared ELF (.so) linked with prx.ld")
    parser.add_argument("output", help="Output PS5 FSELF (.prx)")
    parser.add_argument("--oelf", help="Optional path to also write the unwrapped PS5 OELF")
    parser.add_argument("--module-name", default="OnionExample", help="SCE module name")
    parser.add_argument(
        "--sdk-ver",
        type=lambda x: int(x, 0),
        default=0x10590001,
        help="PT_SCE_MODULE_PARAM sdk_ver (default: 0x10590001)",
    )
    parser.add_argument(
        "--ppr-sdk",
        type=lambda x: int(x, 0),
        default=0x07000038,
        help="PT_SCE_MODULE_PARAM Prospero SDK version (default: 0x07000038)",
    )
    parser.add_argument(
        "--sdk-unk",
        type=lambda x: int(x, 0),
        default=0x00000411,
        help="PT_SCE_MODULE_PARAM trailing SDK field (default: 0x00000411)",
    )
    parser.add_argument(
        "--paid",
        type=lambda x: int(x, 0),
        default=DEFAULT_PAID,
        help="SELF Program Auth ID (default: 0x3100000000000002)",
    )
    parser.add_argument(
        "--magic",
        choices=("ps5", "ps4"),
        default="ps5",
        help="SELF magic header (default: ps5 -> 54 14 F5 EE)",
    )
    args = parser.parse_args()

    in_data = open(args.input, "rb").read()
    oelf = build_ps5_oelf(
        in_data,
        module_name=args.module_name,
        sdk_ver=args.sdk_ver,
        ppr_sdk=args.ppr_sdk,
        sdk_unk=args.sdk_unk,
    )
    if args.oelf:
        with open(args.oelf, "wb") as f:
            f.write(oelf)

    fself = wrap_fself(
        oelf,
        magic=SELF_MAGIC_PS5 if args.magic == "ps5" else SELF_MAGIC_PS4,
        paid=args.paid,
    )
    with open(args.output, "wb") as f:
        f.write(fself)

    print(
        f"[elf2ps5prx] wrote {args.output} ({len(fself)} bytes, "
        f"magic={args.magic}, sdk=0x{args.sdk_ver:08x}/0x{args.ppr_sdk:08x})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
