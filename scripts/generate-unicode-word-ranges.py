"""Generate Unicode 16.0.0 word-category ranges for the C editor."""

import pathlib
import sys
import unicodedata

if unicodedata.unidata_version != "16.0.0":
    raise SystemExit("Unicode 16.0.0 is required to regenerate word categories")

ranges = []
start = None
for cp in range(0x110001):
    word = cp < 0x110000 and unicodedata.category(chr(cp))[0] in "LMN"
    if word and start is None:
        start = cp
    elif not word and start is not None:
        ranges.append((start, cp - 1))
        start = None

lines = [
    "#ifndef SOFTLINE_UNICODE_WORD_H",
    "#define SOFTLINE_UNICODE_WORD_H",
    '#include "softline_unicode.h"',
    "/* Unicode 16.0.0 letters, marks and numbers. Regenerate with the",
    " * unicode-word-data CMake target, then format with clang-format. */",
    "static int sl_codepoint_is_word(unsigned long cp) {",
    "  static const unsigned long ranges[][2] = {",
]
lines += [f"    {{0x{a:x}UL, 0x{b:x}UL}}," for a, b in ranges]
lines += [
    "  };",
    "  return sl_codepoint_in_ranges(cp, ranges, sizeof(ranges) / sizeof(ranges[0]));",
    "}",
    "#endif",
    "",
]
pathlib.Path(sys.argv[1]).write_text("\n".join(lines))
