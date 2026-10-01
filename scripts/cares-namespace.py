#!/usr/bin/env python3
"""Generate private c-ares names without editing upstream declarations."""

import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
identifiers = set()
macros = set()
for path in sorted(source.rglob("*")):
    if path.suffix not in (".c", ".h"):
        continue
    text = path.read_text()
    identifiers.update(re.findall(r"\bares_[A-Za-z0-9_]+\b", text))
    macros.update(re.findall(r"^\s*#\s*define\s+(ares_\w+)", text, re.M))
output.write_text("/* Generated private resolver namespace. */\n"
                  "#ifndef SL_CARES_NAMESPACE_H\n#define SL_CARES_NAMESPACE_H\n" +
                  "".join(f"#define {name} sl_cares_{name}\n"
                          for name in sorted(identifiers - macros)) + "#endif\n")
