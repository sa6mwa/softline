#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "${ROOT_DIR}" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])

checks = [
    root / "include" / "softline" / "softline.h",
    root / "cmake" / "softline_version.h.in",
]

release_docs = [
    root / "README.md",
    root / "lua" / "README.md",
]

lua_readme = root / "lua" / "README.md"
lua_methods = [
    "readline",
    "history_add",
    "history_set_max_len",
    "history_save",
    "history_load",
    "set_bounds",
    "set_screen_width",
    "insert",
    "set_buffer",
    "buffer",
    "cursor",
    "set_cursor",
    "submit",
    "cancel",
    "bind_key",
    "print_above",
    "last_readline_status",
    "last_error",
    "close",
]
lua_constants = [
    "READLINE_NONE",
    "READLINE_SUBMITTED",
    "READLINE_EOF",
    "READLINE_CANCELLED",
    "READLINE_INTERRUPTED",
    "READLINE_ERROR",
    "OK",
    "KEY_CTRL_C",
    "KEY_ACTION_PASS",
    "KEY_ACTION_HANDLED",
    "KEY_ACTION_SUBMIT",
    "KEY_ACTION_CANCEL",
    "KEY_ACTION_INTERRUPT",
]

decl_patterns = [
    re.compile(r"^\s*typedef struct sl sl_t;"),
    re.compile(r"^\s*typedef (?:void|int) \(\*sl_[a-z0-9_]+_t\)\("),
    re.compile(r"^\s*typedef enum sl_[a-z0-9_]+ \{"),
    re.compile(r"^\s*(?:SL|SOFTLINE)_[A-Z0-9_]+(?:\s*=|\s|$)"),
    re.compile(r"^\s*typedef struct sl_config \{"),
    re.compile(r"^\s*(?:int|size_t)\s+[a-z][a-z0-9_]*;"),
    re.compile(r"^\s*struct sl \{"),
    re.compile(r"^\s*.*\(\*[a-z][a-z0-9_]*\)\("),
    re.compile(
        r"^\s*(?:char \*|const char \*|void|int|size_t|sl_t \*|sl_readline_status_t)\s+sl_[a-z0-9_]+\("
    ),
    re.compile(r"^\s*#define SOFTLINE_VERSION(?:_[A-Z]+)?\b"),
]


def previous_code_or_comment(lines, index):
    j = index - 1
    while j >= 0 and lines[j].strip() == "":
        j -= 1
    return lines[j].strip() if j >= 0 else ""


def has_doc_comment(lines, index):
    prev = previous_code_or_comment(lines, index)
    if prev.startswith("/**") or prev.startswith("///"):
        return True
    if prev.endswith("*/"):
        j = index - 1
        while j >= 0:
            text = lines[j].strip()
            if text.startswith("/**"):
                return True
            if text and not text.startswith("*") and not text.endswith("*/"):
                return False
            j -= 1
    return False


failures = []
for path in checks:
    lines = path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("*") or stripped.startswith("/*"):
            continue
        if stripped in {"#ifndef SOFTLINE_VERSION_H", "#define SOFTLINE_VERSION_H"}:
            continue
        if stripped in {"extern \"C\" {", "}", "};"}:
            continue
        if any(pattern.match(line) for pattern in decl_patterns):
            if not has_doc_comment(lines, i):
                failures.append(f"{path.relative_to(root)}:{i + 1}: missing Doxygen comment")

if failures:
    raise SystemExit("ERROR: public header documentation gaps:\n" + "\n".join(failures))

for path in release_docs:
    text = path.read_text(encoding="utf-8")
    if re.search(r"\bunreleased\b", text, re.IGNORECASE):
        failures.append(f"{path.relative_to(root)}: stale unreleased wording")

lua_text = lua_readme.read_text(encoding="utf-8")
for method in lua_methods:
    if f"sl:{method}" not in lua_text:
        failures.append(f"{lua_readme.relative_to(root)}: missing Lua method doc sl:{method}")
for constant in lua_constants:
    if f"softline.{constant}" not in lua_text:
        failures.append(f"{lua_readme.relative_to(root)}: missing Lua constant doc softline.{constant}")

if failures:
    raise SystemExit("ERROR: public documentation gaps:\n" + "\n".join(failures))

print("Public header documentation tests passed.")
PY
