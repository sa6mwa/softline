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
lua_binding = root / "lua" / "softline_lua.c"
header = root / "include" / "softline" / "softline.h"

decl_patterns = [
    re.compile(r"^\s*typedef struct sl sl_t;"),
    re.compile(r"^\s*typedef unsigned long sl_watch_id_t;"),
    re.compile(r"^\s*typedef (?:void|int) \(\*sl_[a-z0-9_]+_t\)\("),
    re.compile(r"^\s*typedef enum sl_[a-z0-9_]+ \{"),
    re.compile(r"^\s*(?:SL|SOFTLINE)_[A-Z0-9_]+(?:\s*=|\s|$)"),
    re.compile(r"^\s*typedef struct sl_[a-z0-9_]+ \{"),
    re.compile(
        r"^\s*(?:int|size_t|unsigned int|unsigned char|char|const char\s*\*|sl_[a-z0-9_]+_t)"
        r"\s*[a-z][a-z0-9_]*;"
    ),
    re.compile(r"^\s*struct sl \{"),
    re.compile(r"^\s*void \*impl;"),
    re.compile(r"^\s*.*\(\*[a-z][a-z0-9_]*\)\("),
    re.compile(
        r"^\s*(?:char \*|const char \*|void|int|size_t|sl_t \*|sl_readline_status_t)\s+sl_[a-z0-9_]+\("
    ),
    re.compile(r"^\s*#define (?:SOFTLINE_VERSION(?:_[A-Z]+)?|SL_STATUS_MAX_ELEMENTS)\b"),
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

# ABI policy prose must agree with the configured shared-library default.
abi = re.search(
    r'set\(SOFTLINE_ABI_VERSION "([0-9]+)"',
    (root / "CMakeLists.txt").read_text(),
)
abi_docs = {
    "README.md": r"SOFTLINE_ABI_VERSION=([0-9]+)",
    "docs/softline-spec.md": r"`SOFTLINE_ABI_VERSION`, currently\s+`([0-9]+)`",
    "docs/softline-mdf-stream-design.md": r"ABI ([0-9]+) is retained",
}
for name, pattern in abi_docs.items():
    documented = re.search(pattern, (root / name).read_text())
    if not abi or not documented or documented.group(1) != abi.group(1):
        failures.append(f"{name}: ABI policy differs from CMake default")

lua_text = lua_readme.read_text(encoding="utf-8")
header_text = header.read_text(encoding="utf-8")
binding_text = lua_binding.read_text(encoding="utf-8")
# The installed dynamic API must exactly match the declared free functions.
header_code = re.sub(r"/\*.*?\*/", "", header_text, flags=re.DOTALL)
public_functions = {name for name in re.findall(
    r"\b(sl_[a-z0-9_]+)\s*\([^;{}]*\);", header_code
) if not name.endswith("_t")}
exports_path = root / "cmake" / "softline.exports"
exports = {
    line.strip() for line in exports_path.read_text().splitlines()
    if line.strip() and not line.lstrip().startswith("#")
}
if public_functions != exports:
    failures.append(
        "C declarations/export mismatch: "
        f"missing exports {sorted(public_functions - exports)}, "
        f"undeclared exports {sorted(exports - public_functions)}"
    )
if "sl_set_bounds" in public_functions or "set_bounds" in header_code:
    failures.append("removed boxed bounds API reintroduced")
receiver = re.search(r"struct sl \{(.*?)\n\};", header_text, re.DOTALL)
registry = re.search(
    r"static const luaL_Reg softline_lua_methods\[\] = \{(.*?)\{NULL, NULL\}\};",
    binding_text,
    re.DOTALL,
)
if not receiver or not registry:
    failures.append("cannot locate C receiver or Lua method registry")
else:
    receiver_methods = set(
        re.findall(r"\(\*([a-z][a-z0-9_]*)\)\(", receiver.group(1))
    )
    for method in sorted(receiver_methods):
        if not re.search(rf"\bsl_{method}\s*\(", header_text):
            failures.append(
                f"{header.relative_to(root)}: missing free-function wrapper sl_{method}()"
            )
    bound_list = re.findall(
        r'\{"([a-z][a-z0-9_]*)",\s*softline_lua_[a-z0-9_]+\}',
        registry.group(1),
    )
    bound_methods = set(bound_list)
    if len(bound_list) != len(bound_methods):
        failures.append("Lua method registry contains duplicate names")
    aliases = {
        "destroy": "close",
        "prompt_queue_count": "queue_count",
        "prompt_queue_capacity": "queue_capacity",
        "prompt_queue_peek": "queue_peek",
        "prompt_queue_insert": "queue_insert",
        "prompt_queue_append": "queue_append",
        "prompt_queue_replace": "queue_replace",
        "prompt_queue_take": "queue_take",
        "prompt_queue_get_mode": "queue_mode",
        "prompt_queue_set_mode": "queue_set_mode",
        "prompt_queue_clear": "queue_clear",
        "prompt_queue_enqueue_draft": "queue_draft",
        "set_prompt_queue_delivery": "set_queue_delivery",
        "get_prompt_queue_delivery": "queue_delivery",
        "set_prompt_queue_profile": "set_queue_profile",
        "get_prompt_queue_profile": "queue_profile",
        "set_prompt_queue_keys": "set_queue_keys",
        "get_prompt_queue_keys": "queue_keys",
    }
    # Lua strings are GC-managed, so C's free_string() needs no Lua method.
    expected = {
        aliases.get(name, name) for name in receiver_methods - {"free_string"}
    }
    for method in sorted(expected - bound_methods):
        failures.append(
            f"{lua_binding.relative_to(root)}: missing C receiver counterpart {method}"
        )
    for method in sorted(bound_methods - expected):
        failures.append(
            f"{lua_binding.relative_to(root)}: undocumented Lua-only method {method}"
        )
    for method in sorted(bound_methods):
        if not re.search(rf"`sl:{re.escape(method)}\(", lua_text):
            failures.append(
                f"{lua_readme.relative_to(root)}: missing Lua method doc sl:{method}()"
            )

config = re.search(
    r"typedef struct sl_config \{(.*?)\} sl_config_t;", header_text, re.DOTALL
)
config_parser = re.search(
    r"static void softline_lua_config\(.*?\) \{(.*?)\n\}",
    binding_text,
    re.DOTALL,
)
if not config or not config_parser:
    failures.append("cannot locate C config or Lua config parser")
else:
    config_fields = set(
        re.findall(
            r"\b(?:int|size_t|char|sl_prompt_theme_t|const char\s*\*)\s*([a-z][a-z0-9_]*);",
            config.group(1),
        )
    )
    lua_fields = set(
        re.findall(
            r'lua_getfield\(L, index, "([a-z][a-z0-9_]*)"\)',
            config_parser.group(1),
        )
    )
    removed_fields = {"bounded", "screen_x", "screen_y", "screen_height"}
    if config_fields & removed_fields or lua_fields & removed_fields:
        failures.append("removed boxed config fields reintroduced")
    accepted = re.search(
        r"softline_lua_config_fields\[\] = \{(.*?)\};", binding_text, re.DOTALL
    )
    accepted_names = re.findall(r'"([a-z][a-z0-9_]*)"', accepted.group(1)) if accepted else []
    if set(accepted_names) != config_fields or len(accepted_names) != len(config_fields):
        failures.append("Lua accepted config names do not match sl_config_t")
    for field in sorted(config_fields - lua_fields):
        failures.append(
            f"{lua_binding.relative_to(root)}: missing config field {field}"
        )
    for field in sorted(lua_fields - config_fields):
        failures.append(
            f"{lua_binding.relative_to(root)}: unknown config field {field}"
        )
    for field in sorted(config_fields):
        if f"`{field}`" not in lua_text:
            failures.append(
                f"{lua_readme.relative_to(root)}: missing config doc {field}"
            )
if not re.search(r'\{"new",\s*softline_lua_new\}', binding_text):
    failures.append(f"{lua_binding.relative_to(root)}: missing softline.new()")

exported = dict(
    re.findall(
        r'lua_pushinteger\(L,\s*(SL_[A-Z0-9_]+)\);\s*'
        r'lua_setfield\(L, -2, "([A-Z0-9_]+)"\);',
        binding_text,
    )
)
for enum_name in (
    "watch_events", "key", "key_action", "status", "readline_status",
    "prompt_source", "prompt_queue_mode", "prompt_theme", "theme_color",
):
    enum = re.search(
        rf"typedef enum sl_{enum_name} \{{(.*?)\}} sl_{enum_name}_t;",
        header_text,
        re.DOTALL,
    )
    if not enum:
        failures.append(f"{header.relative_to(root)}: cannot locate sl_{enum_name} enum")
        continue
    for symbol in re.findall(r"\b(SL_[A-Z0-9_]+)\s*(?:=|,)", enum.group(1)):
        public_name = symbol[3:].replace("PROMPT_QUEUE_MODE_", "QUEUE_MODE_", 1)
        if exported.get(symbol) != public_name:
            failures.append(
                f"{lua_binding.relative_to(root)}: missing Lua constant {public_name}"
            )
for symbol in ("SL_STATUS_MAX_ELEMENTS",):
    if exported.get(symbol) != symbol[3:]:
        failures.append(f"{lua_binding.relative_to(root)}: missing Lua constant {symbol}")
for constant in sorted(exported.values()):
    if f"softline.{constant}" not in lua_text:
        failures.append(
            f"{lua_readme.relative_to(root)}: missing Lua constant doc softline.{constant}"
        )

if failures:
    raise SystemExit("ERROR: public documentation gaps:\n" + "\n".join(failures))

print(
    "Public header docs and Lua API parity passed "
    f"({len(receiver_methods)} receiver methods, "
    f"{len(config_fields)} config fields, {len(exported)} constants)."
)
PY
