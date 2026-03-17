#!/usr/bin/env python3
"""
UCD table generator for the ds unicode module.

Downloads Unicode Character Database files and generates C headers with
compact two-stage lookup tables for:
  - NFD canonical decomposition mappings
  - Canonical Combining Class (CCC)
  - Grapheme_Break property values (UAX #29)
  - Word_Break property values (UAX #29)
  - Line_Break property values (UAX #14)

Usage:
    python3 generate.py [--version 16.0.0] [--cache-dir ./cache]
"""

import argparse
import os
import sys
import urllib.request
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

BLOCK_SHIFT = 5  # block size = 32 codepoints
BLOCK_SIZE = 1 << BLOCK_SHIFT
MAX_CP = 0x110000  # exclusive upper bound (U+10FFFF + 1)

UCD_FILES = [
    "UnicodeData.txt",
    "auxiliary/GraphemeBreakProperty.txt",
    "auxiliary/WordBreakProperty.txt",
    "LineBreak.txt",
    "DerivedNormalizationProps.txt",
    "CompositionExclusions.txt",
    "emoji/emoji-data.txt",
]

# Grapheme_Break property values (UAX #29)
GBP_VALUES = [
    "Other",        # 0
    "CR",           # 1
    "LF",           # 2
    "Control",      # 3
    "Extend",       # 4
    "ZWJ",          # 5
    "Regional_Indicator",  # 6
    "Prepend",      # 7
    "SpacingMark",  # 8
    "L",            # 9  (Hangul)
    "V",            # 10
    "T",            # 11
    "LV",           # 12
    "LVT",          # 13
    "Extended_Pictographic",  # 14
]

# Word_Break property values (UAX #29)
WBP_VALUES = [
    "Other",        # 0
    "CR",           # 1
    "LF",           # 2
    "Newline",      # 3
    "Extend",       # 4
    "ZWJ",          # 5
    "Regional_Indicator",  # 6
    "Format",       # 7
    "Katakana",     # 8
    "Hebrew_Letter",# 9
    "ALetter",      # 10
    "Single_Quote", # 11
    "Double_Quote", # 12
    "MidNumLet",    # 13
    "MidLetter",    # 14
    "MidNum",       # 15
    "Numeric",      # 16
    "ExtendNumLet", # 17
    "WSegSpace",    # 18
]

# Line_Break property values (UAX #14)
LBP_VALUES = [
    "XX",  # 0  Unknown / default
    "BK",  # 1  Mandatory break
    "CR",  # 2
    "LF",  # 3
    "CM",  # 4  Combining mark
    "NL",  # 5  Next line
    "WJ",  # 6  Word joiner
    "ZW",  # 7  Zero width space
    "GL",  # 8  Non-breaking (glue)
    "SP",  # 9  Space
    "ZWJ", # 10
    "B2",  # 11 Break Opportunity Before and After
    "BA",  # 12 Break After
    "BB",  # 13 Break Before
    "HY",  # 14 Hyphen
    "CB",  # 15 Contingent Break
    "CL",  # 16 Close Punctuation
    "CP",  # 17 Close Parenthesis
    "EX",  # 18 Exclamation/Interrogation
    "IN",  # 19 Inseparable
    "NS",  # 20 Nonstarter
    "OP",  # 21 Open Punctuation
    "QU",  # 22 Quotation
    "IS",  # 23 Infix Numeric Separator
    "NU",  # 24 Numeric
    "PO",  # 25 Postfix Numeric
    "PR",  # 26 Prefix Numeric
    "SY",  # 27 Symbols Allowing Break After
    "AI",  # 28 Ambiguous (Alphabetic or Ideographic)
    "AL",  # 29 Alphabetic
    "CJ",  # 30 Conditional Japanese Starter
    "EB",  # 31 Emoji Base
    "EM",  # 32 Emoji Modifier
    "H2",  # 33 Hangul LV Syllable
    "H3",  # 34 Hangul LVT Syllable
    "HL",  # 35 Hebrew Letter
    "ID",  # 36 Ideographic
    "JL",  # 37 Hangul L Jamo
    "JV",  # 38 Hangul V Jamo
    "JT",  # 39 Hangul T Jamo
    "RI",  # 40 Regional Indicator
    "SA",  # 41 Complex Context Dependent (South East Asian)
    "AK",  # 42 Aksara
    "AP",  # 43 Aksara Pre-base
    "AS",  # 44 Aksara Start
    "VF",  # 45 Virama Final
    "VI",  # 46 Virama
]

# Quick_Check_NFD values
QC_NFD_VALUES = ["Y", "N"]  # 0 = Yes, 1 = No


# ---------------------------------------------------------------------------
# Download helpers
# ---------------------------------------------------------------------------

def download_ucd(version, filename, cache_dir):
    """Download a UCD file, caching locally."""
    cache_path = os.path.join(cache_dir, filename.replace("/", "_"))
    if os.path.exists(cache_path):
        with open(cache_path, "r", encoding="utf-8") as f:
            return f.read()

    url = f"https://www.unicode.org/Public/{version}/ucd/{filename}"
    print(f"  Downloading {url} ...", file=sys.stderr)
    try:
        resp = urllib.request.urlopen(url)
        data = resp.read().decode("utf-8")
    except Exception as e:
        print(f"  ERROR downloading {url}: {e}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(cache_path) or cache_dir, exist_ok=True)
    with open(cache_path, "w", encoding="utf-8") as f:
        f.write(data)
    return data


# ---------------------------------------------------------------------------
# UCD parsers
# ---------------------------------------------------------------------------

def parse_range(range_str):
    """Parse 'XXXX' or 'XXXX..YYYY' into (start, end) inclusive."""
    if ".." in range_str:
        a, b = range_str.split("..")
        return int(a, 16), int(b, 16)
    v = int(range_str, 16)
    return v, v


def parse_property_file(text, value_map):
    """Parse a UCD property file (e.g., GraphemeBreakProperty.txt).
    Returns a dict: codepoint -> value_index."""
    result = {}
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split(";")
        if len(parts) < 2:
            continue
        cp_range = parts[0].strip()
        value = parts[1].strip()
        if value not in value_map:
            continue
        idx = value_map[value]
        start, end = parse_range(cp_range)
        for cp in range(start, end + 1):
            result[cp] = idx
    return result


def parse_unicode_data(text):
    """Parse UnicodeData.txt.
    Returns:
        ccc: dict cp -> combining class (int)
        decomp: dict cp -> list of codepoints (canonical decomposition only)
        general_cat: dict cp -> category string
    """
    ccc = {}
    decomp = {}
    general_cat = {}

    prev_range_start = None

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(";")
        if len(fields) < 6:
            continue

        cp = int(fields[0], 16)
        name = fields[1]
        cat = fields[2]
        ccc_val = int(fields[3])

        # Handle range entries like "<Hangul Syllable, First>"
        if name.endswith(", First>"):
            prev_range_start = cp
            general_cat[cp] = cat
            if ccc_val != 0:
                ccc[cp] = ccc_val
            continue
        if name.endswith(", Last>"):
            if prev_range_start is not None:
                for c in range(prev_range_start, cp + 1):
                    general_cat[c] = cat
                    if ccc_val != 0:
                        ccc[c] = ccc_val
                prev_range_start = None
            continue

        general_cat[cp] = cat
        if ccc_val != 0:
            ccc[cp] = ccc_val

        # Decomposition mapping (field 5)
        decomp_field = fields[5].strip()
        if decomp_field:
            # Skip compatibility decompositions (have <tag> prefix)
            if decomp_field.startswith("<"):
                continue
            cps = [int(x, 16) for x in decomp_field.split()]
            decomp[cp] = cps

    return ccc, decomp, general_cat


def parse_derived_normalization(text):
    """Parse DerivedNormalizationProps.txt for NFD_QC=No."""
    qc_nfd_no = set()
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split(";")
        if len(parts) < 2:
            continue
        prop = parts[1].strip()
        if prop == "NFD_QC" and len(parts) >= 3 and parts[2].strip() == "N":
            start, end = parse_range(parts[0].strip())
            for cp in range(start, end + 1):
                qc_nfd_no.add(cp)
        elif prop == "NFD_QC":
            # Some files use "NFD_QC; N" without third field
            pass
    # Also handle the two-field format: "cp ; NFD_QC  # No"
    # Actually the format is: cp ; Full_Composition_Exclusion or NFD_QC; N
    # Let me re-parse more carefully
    qc_nfd_no2 = set()
    for line in text.splitlines():
        comment = ""
        if "#" in line:
            idx = line.index("#")
            comment = line[idx+1:].strip()
            line = line[:idx].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(";")]
        if len(parts) >= 2 and parts[1] == "NFD_QC":
            if len(parts) >= 3 and parts[2] == "N":
                start, end = parse_range(parts[0])
                for cp in range(start, end + 1):
                    qc_nfd_no2.add(cp)
        # Two-field: "cp ; NFD_QC" where value is implied No
        # This doesn't happen in practice, but handle it
    return qc_nfd_no2


def parse_composition_exclusions(text):
    """Parse CompositionExclusions.txt. Returns set of excluded codepoints."""
    result = set()
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        cp = int(line.split()[0], 16)
        result.add(cp)
    return result


def fully_decompose(cp, decomp_map, cache=None):
    """Recursively decompose a codepoint to its full NFD form."""
    if cache is None:
        cache = {}
    if cp in cache:
        return cache[cp]
    if cp not in decomp_map:
        cache[cp] = [cp]
        return [cp]
    result = []
    for sub_cp in decomp_map[cp]:
        result.extend(fully_decompose(sub_cp, decomp_map, cache))
    cache[cp] = result
    return result


# ---------------------------------------------------------------------------
# Two-stage table builder
# ---------------------------------------------------------------------------

def build_two_stage_table(cp_to_value, default_value, max_cp=MAX_CP):
    """Build a two-stage lookup table.
    Returns (stage1, stage2, block_count).
    stage1: list of block indices, length = max_cp >> BLOCK_SHIFT
    stage2: flat list of values, length = block_count * BLOCK_SIZE
    """
    num_blocks = (max_cp + BLOCK_SIZE - 1) >> BLOCK_SHIFT

    # Build all blocks
    blocks = []
    for b in range(num_blocks):
        block = []
        base = b << BLOCK_SHIFT
        for i in range(BLOCK_SIZE):
            cp = base + i
            block.append(cp_to_value.get(cp, default_value))
        blocks.append(tuple(block))

    # Deduplicate blocks
    unique_blocks = []
    block_index_map = {}
    stage1 = []

    for block in blocks:
        if block not in block_index_map:
            block_index_map[block] = len(unique_blocks)
            unique_blocks.append(block)
        stage1.append(block_index_map[block])

    # Flatten stage2
    stage2 = []
    for block in unique_blocks:
        stage2.extend(block)

    return stage1, stage2, len(unique_blocks)


# ---------------------------------------------------------------------------
# C code emitter
# ---------------------------------------------------------------------------

def emit_array(f, name, values, ctype, cols=16):
    """Write a C array definition."""
    f.write(f"static const {ctype} {name}[] = {{\n")
    for i in range(0, len(values), cols):
        chunk = values[i:i+cols]
        line = ", ".join(str(v) for v in chunk)
        f.write(f"    {line},\n")
    f.write(f"}};\n\n")


def emit_enum(f, name, values, prefix):
    """Write a C enum definition."""
    f.write(f"typedef enum {{\n")
    for i, v in enumerate(values):
        f.write(f"    {prefix}_{v.upper()} = {i},\n")
    f.write(f"}} {name};\n\n")


# ---------------------------------------------------------------------------
# NFD decomposition table builder
# ---------------------------------------------------------------------------

def build_decomp_table(decomp_map, ccc_map):
    """Build the NFD decomposition data structures.

    Returns:
        decomp_index: dict cp -> index into decomp_data (0 = no decomposition)
        decomp_data: flat list of uint32_t values.
                     Format: [len, cp1, cp2, ...] sequences.
                     Index 0 is a sentinel (len=0).
    """
    # Pre-compute fully recursive decompositions
    cache = {}
    full_decomp = {}
    for cp in sorted(decomp_map.keys()):
        result = fully_decompose(cp, decomp_map, cache)
        if result != [cp]:
            full_decomp[cp] = result

    # Build flat data array
    # Index 0: sentinel (no decomposition) - store length 0
    decomp_data = [0]  # sentinel: length=0
    decomp_index = {}  # cp -> offset into decomp_data

    # Deduplicate decomposition sequences
    seq_to_offset = {}

    for cp in sorted(full_decomp.keys()):
        seq = tuple(full_decomp[cp])
        if seq in seq_to_offset:
            decomp_index[cp] = seq_to_offset[seq]
        else:
            offset = len(decomp_data)
            decomp_data.append(len(seq))
            decomp_data.extend(seq)
            seq_to_offset[seq] = offset
            decomp_index[cp] = offset

    return decomp_index, decomp_data


# ---------------------------------------------------------------------------
# Main generation
# ---------------------------------------------------------------------------

def generate(version, cache_dir, output_dir):
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    print(f"Generating Unicode tables for version {version}", file=sys.stderr)

    # Download all UCD files
    ucd = {}
    for filename in UCD_FILES:
        ucd[filename] = download_ucd(version, filename, cache_dir)

    # Parse UnicodeData.txt
    ccc_map, decomp_map, general_cat = parse_unicode_data(ucd["UnicodeData.txt"])

    # Parse property files
    gbp_value_map = {v: i for i, v in enumerate(GBP_VALUES)}
    gbp_data = parse_property_file(ucd["auxiliary/GraphemeBreakProperty.txt"], gbp_value_map)

    # Parse emoji-data.txt for Extended_Pictographic
    emoji_ep = {}
    for line in ucd["emoji/emoji-data.txt"].splitlines():
        line_clean = line.split("#")[0].strip()
        if not line_clean:
            continue
        parts = line_clean.split(";")
        if len(parts) >= 2 and parts[1].strip() == "Extended_Pictographic":
            start, end = parse_range(parts[0].strip())
            for cp in range(start, end + 1):
                emoji_ep[cp] = gbp_value_map["Extended_Pictographic"]

    # Merge Extended_Pictographic into GBP (only if not already set to a
    # more specific value)
    for cp, val in emoji_ep.items():
        if cp not in gbp_data:
            gbp_data[cp] = val

    wbp_value_map = {v: i for i, v in enumerate(WBP_VALUES)}
    wbp_data = parse_property_file(ucd["auxiliary/WordBreakProperty.txt"], wbp_value_map)

    lbp_value_map = {v: i for i, v in enumerate(LBP_VALUES)}
    lbp_data = parse_property_file(ucd["LineBreak.txt"], lbp_value_map)

    # Parse NFD quick check
    qc_nfd_no = parse_derived_normalization(ucd["DerivedNormalizationProps.txt"])
    qc_nfd_map = {cp: 1 for cp in qc_nfd_no}  # 1 = No

    # Parse composition exclusions
    comp_excl = parse_composition_exclusions(ucd["CompositionExclusions.txt"])

    # Build NFD decomposition table
    decomp_index, decomp_data = build_decomp_table(decomp_map, ccc_map)

    # Build two-stage tables
    print("  Building two-stage tables...", file=sys.stderr)

    # CCC table (uint8_t values, 0-255)
    ccc_s1, ccc_s2, ccc_blocks = build_two_stage_table(ccc_map, 0)
    print(f"    CCC: {ccc_blocks} unique blocks, "
          f"stage1={len(ccc_s1)}, stage2={len(ccc_s2)}", file=sys.stderr)

    # NFD decomposition index table (uint16_t, index into decomp_data)
    decomp_s1, decomp_s2, decomp_blocks = build_two_stage_table(decomp_index, 0)
    print(f"    NFD decomp: {decomp_blocks} unique blocks, "
          f"stage1={len(decomp_s1)}, stage2={len(decomp_s2)}, "
          f"data={len(decomp_data)} entries", file=sys.stderr)

    # NFD quick check table (uint8_t, 0=Y, 1=N)
    qc_s1, qc_s2, qc_blocks = build_two_stage_table(qc_nfd_map, 0)
    print(f"    NFD QC: {qc_blocks} unique blocks", file=sys.stderr)

    # Grapheme_Break property
    gbp_s1, gbp_s2, gbp_blocks = build_two_stage_table(gbp_data, 0)
    print(f"    GBP: {gbp_blocks} unique blocks", file=sys.stderr)

    # Word_Break property
    wbp_s1, wbp_s2, wbp_blocks = build_two_stage_table(wbp_data, 0)
    print(f"    WBP: {wbp_blocks} unique blocks", file=sys.stderr)

    # Line_Break property
    lbp_s1, lbp_s2, lbp_blocks = build_two_stage_table(lbp_data, 0)
    print(f"    LBP: {lbp_blocks} unique blocks", file=sys.stderr)

    # -----------------------------------------------------------------------
    # Emit unicode_tables.h
    # -----------------------------------------------------------------------
    tables_path = os.path.join(output_dir, "unicode_tables.h")
    print(f"  Writing {tables_path} ...", file=sys.stderr)

    with open(tables_path, "w") as f:
        f.write(f"/*\n")
        f.write(f" * unicode_tables.h — Auto-generated Unicode property tables\n")
        f.write(f" *\n")
        f.write(f" * Unicode version: {version}\n")
        f.write(f" * Generated: {timestamp}\n")
        f.write(f" * Generator: unicode/gen/generate.py\n")
        f.write(f" *\n")
        f.write(f" * DO NOT EDIT — regenerate with:\n")
        f.write(f" *   python3 unicode/gen/generate.py --version {version}\n")
        f.write(f" */\n\n")
        f.write(f"#ifndef UNICODE_TABLES_H\n")
        f.write(f"#define UNICODE_TABLES_H\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"#define UNICODE_TABLE_VERSION \"{version}\"\n")
        f.write(f"#define UNICODE_BLOCK_SHIFT {BLOCK_SHIFT}\n")
        f.write(f"#define UNICODE_BLOCK_SIZE {BLOCK_SIZE}\n")
        f.write(f"#define UNICODE_BLOCK_MASK ({BLOCK_SIZE - 1})\n\n")

        # Two-stage lookup macro
        f.write(f"/* Two-stage table lookup: stage1[cp >> SHIFT] * BLOCK_SIZE + (cp & MASK) */\n")
        f.write(f"#define UNICODE_LOOKUP(stage1, stage2, cp) \\\n")
        f.write(f"    ((stage2)[(stage1)[(cp) >> UNICODE_BLOCK_SHIFT] * UNICODE_BLOCK_SIZE + ((cp) & UNICODE_BLOCK_MASK)])\n\n")

        # Enums
        emit_enum(f, "UnicodeGraphemeBreak", GBP_VALUES, "GBP")
        emit_enum(f, "UnicodeWordBreak", WBP_VALUES, "WBP")
        emit_enum(f, "UnicodeLineBreak", LBP_VALUES, "LBP")

        # CCC tables
        f.write("/* --- Canonical Combining Class (CCC) --- */\n\n")
        # stage1 needs uint16_t if block count > 255
        s1_type = "uint16_t" if ccc_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_ccc_stage1", ccc_s1, s1_type)
        emit_array(f, "unicode_ccc_stage2", ccc_s2, "uint8_t")

        # NFD decomposition tables
        f.write("/* --- NFD Canonical Decomposition --- */\n\n")
        s1_type = "uint16_t" if decomp_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_nfd_index_stage1", decomp_s1, s1_type)
        emit_array(f, "unicode_nfd_index_stage2", decomp_s2, "uint16_t")
        emit_array(f, "unicode_nfd_data", decomp_data, "uint32_t")

        # NFD Quick Check table
        f.write("/* --- NFD Quick Check (0=Yes, 1=No) --- */\n\n")
        s1_type = "uint16_t" if qc_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_nfd_qc_stage1", qc_s1, s1_type)
        emit_array(f, "unicode_nfd_qc_stage2", qc_s2, "uint8_t")

        # Grapheme_Break tables
        f.write("/* --- Grapheme_Break Property (UAX #29) --- */\n\n")
        s1_type = "uint16_t" if gbp_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_gbp_stage1", gbp_s1, s1_type)
        emit_array(f, "unicode_gbp_stage2", gbp_s2, "uint8_t")

        # Word_Break tables
        f.write("/* --- Word_Break Property (UAX #29) --- */\n\n")
        s1_type = "uint16_t" if wbp_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_wbp_stage1", wbp_s1, s1_type)
        emit_array(f, "unicode_wbp_stage2", wbp_s2, "uint8_t")

        # Line_Break tables
        f.write("/* --- Line_Break Property (UAX #14) --- */\n\n")
        s1_type = "uint16_t" if lbp_blocks > 255 else "uint8_t"
        emit_array(f, "unicode_lbp_stage1", lbp_s1, s1_type)
        emit_array(f, "unicode_lbp_stage2", lbp_s2, "uint8_t")

        # Inline lookup functions
        f.write("/* --- Inline lookup functions --- */\n\n")
        f.write("static inline uint8_t unicode_ccc(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return 0;\n")
        f.write("    return UNICODE_LOOKUP(unicode_ccc_stage1, unicode_ccc_stage2, cp);\n")
        f.write("}\n\n")

        f.write("static inline UnicodeGraphemeBreak unicode_grapheme_break(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return GBP_OTHER;\n")
        f.write("    return (UnicodeGraphemeBreak)UNICODE_LOOKUP(unicode_gbp_stage1, unicode_gbp_stage2, cp);\n")
        f.write("}\n\n")

        f.write("static inline UnicodeWordBreak unicode_word_break(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return WBP_OTHER;\n")
        f.write("    return (UnicodeWordBreak)UNICODE_LOOKUP(unicode_wbp_stage1, unicode_wbp_stage2, cp);\n")
        f.write("}\n\n")

        f.write("static inline UnicodeLineBreak unicode_line_break(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return LBP_XX;\n")
        f.write("    return (UnicodeLineBreak)UNICODE_LOOKUP(unicode_lbp_stage1, unicode_lbp_stage2, cp);\n")
        f.write("}\n\n")

        f.write("static inline uint8_t unicode_nfd_qc(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return 0; /* Yes */\n")
        f.write("    return UNICODE_LOOKUP(unicode_nfd_qc_stage1, unicode_nfd_qc_stage2, cp);\n")
        f.write("}\n\n")

        f.write("/* Returns pointer to decomposition data for cp, or NULL if no decomposition.\n")
        f.write(" * Format: data[0] = length, data[1..length] = decomposed codepoints. */\n")
        f.write("static inline const uint32_t* unicode_nfd_decomp(uint32_t cp) {\n")
        f.write("    if (cp >= 0x110000) return 0;\n")
        f.write("    uint16_t idx = UNICODE_LOOKUP(unicode_nfd_index_stage1, unicode_nfd_index_stage2, cp);\n")
        f.write("    if (idx == 0) return 0;\n")
        f.write("    return &unicode_nfd_data[idx];\n")
        f.write("}\n\n")

        f.write("#endif /* UNICODE_TABLES_H */\n")

    # -----------------------------------------------------------------------
    # Emit unicode_nfc_tables.h (stub)
    # -----------------------------------------------------------------------
    nfc_path = os.path.join(output_dir, "unicode_nfc_tables.h")
    print(f"  Writing {nfc_path} ...", file=sys.stderr)

    with open(nfc_path, "w") as f:
        f.write(f"/*\n")
        f.write(f" * unicode_nfc_tables.h — NFC composition tables (stub)\n")
        f.write(f" *\n")
        f.write(f" * Unicode version: {version}\n")
        f.write(f" * Generated: {timestamp}\n")
        f.write(f" * Generator: unicode/gen/generate.py\n")
        f.write(f" *\n")
        f.write(f" * This file contains the structure and types needed for NFC\n")
        f.write(f" * composition. The composition pair table is not yet populated.\n")
        f.write(f" *\n")
        f.write(f" * DO NOT EDIT — regenerate with:\n")
        f.write(f" *   python3 unicode/gen/generate.py --version {version}\n")
        f.write(f" */\n\n")
        f.write(f"#ifndef UNICODE_NFC_TABLES_H\n")
        f.write(f"#define UNICODE_NFC_TABLES_H\n\n")
        f.write(f"#include <stdint.h>\n")
        f.write(f"#include <stddef.h>\n\n")

        f.write(f"/* CCC lookup is shared with unicode_tables.h */\n\n")

        # Composition exclusion set
        f.write(f"/* Composition exclusions — codepoints that may not be composed.\n")
        f.write(f" * Includes: Singleton decompositions, non-starter decompositions,\n")
        f.write(f" * and explicit exclusions from CompositionExclusions.txt. */\n")

        # Build full exclusion set: explicit exclusions + singletons + non-starter decomps
        full_excl = set(comp_excl)
        for cp, seq in decomp_map.items():
            # Singleton decomposition (decomposes to single codepoint)
            if len(seq) == 1:
                full_excl.add(cp)
            # Non-starter decomposition (first char of decomp has CCC != 0)
            if seq and seq[0] in ccc_map and ccc_map[seq[0]] != 0:
                full_excl.add(cp)

        excl_sorted = sorted(full_excl)
        f.write(f"#define UNICODE_NFC_EXCLUSION_COUNT {len(excl_sorted)}\n")
        emit_array(f, "unicode_nfc_exclusions", excl_sorted, "uint32_t")

        f.write(f"/* Composition pair table — maps (starter, combining) -> composite.\n")
        f.write(f" * TODO: populate when NFC support is added.\n")
        f.write(f" * Structure: sorted array of {{starter, combining, composite}} triples,\n")
        f.write(f" * binary-searched at runtime. */\n")
        f.write(f"typedef struct {{\n")
        f.write(f"    uint32_t starter;\n")
        f.write(f"    uint32_t combining;\n")
        f.write(f"    uint32_t composite;\n")
        f.write(f"}} UnicodeCompositionPair;\n\n")

        f.write(f"#define UNICODE_NFC_PAIR_COUNT 0\n")
        f.write(f"static const UnicodeCompositionPair unicode_nfc_pairs[] = {{\n")
        f.write(f"    {{0, 0, 0}} /* placeholder — table empty until NFC is implemented */\n")
        f.write(f"}};\n\n")

        f.write(f"#endif /* UNICODE_NFC_TABLES_H */\n")

    print(f"  Done.", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Generate Unicode property tables")
    parser.add_argument("--version", default="16.0.0",
                        help="Unicode version (default: 16.0.0)")
    parser.add_argument("--cache-dir", default=None,
                        help="Directory to cache downloaded UCD files")
    parser.add_argument("--output-dir", default=None,
                        help="Output directory for generated headers")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    unicode_dir = os.path.dirname(script_dir)

    if args.cache_dir is None:
        args.cache_dir = os.path.join(script_dir, "cache")
    if args.output_dir is None:
        args.output_dir = unicode_dir

    os.makedirs(args.cache_dir, exist_ok=True)
    os.makedirs(args.output_dir, exist_ok=True)

    generate(args.version, args.cache_dir, args.output_dir)


if __name__ == "__main__":
    main()
