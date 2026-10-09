#!/usr/bin/env python3
"""Generate source facts for native API/doc parity tests; no test assertions."""
import json
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])
header = (root / 'include/softline/softline.h').read_text()
code = re.sub(r'/\*.*?\*/', '', header, flags=re.S)
binding = (root / 'lua/softline_lua.c').read_text()

def section(text, pattern):
    match = re.search(pattern, text, re.S)
    return match.group(1) if match else ''

receiver = section(code, r'struct sl \{(.*?)\n\};')
registry = section(binding, r'static const luaL_Reg softline_lua_methods\[\] = \{(.*?)\{NULL, NULL\}')
config = section(code, r'typedef struct sl_config \{(.*?)\} sl_config_t;')
parser = section(binding, r'static void softline_lua_config\(.*?\) \{(.*?)\n\}')
accepted = section(binding, r'softline_lua_config_fields\[\] = \{(.*?)\};')
constants = re.findall(r'lua_pushinteger\(L,\s*(SL_[A-Z0-9_]+)\);\s*lua_setfield\(L, -2, "([A-Z0-9_]+)"\);', binding)
abi = re.search(r'set\(SOFTLINE_ABI_VERSION "([0-9]+)"', (root / 'CMakeLists.txt').read_text())
facts = {
    'functions': sorted({n for n in re.findall(r'\b(sl_[a-z0-9_]+)\s*\([^;{}]*\);', code) if not n.endswith('_t')}),
    'receiver': re.findall(r'\(\*([a-z][a-z0-9_]*)\)\(', receiver),
    'lua_methods': re.findall(r'\{"([a-z][a-z0-9_]*)",\s*softline_lua_[a-z0-9_]+\}', registry),
    'config': re.findall(r'\b(?:int|size_t|char|sl_prompt_theme_t|const char\s*\*)\s*([a-z][a-z0-9_]*);', config),
    'lua_config': sorted(set(re.findall(r'lua_getfield\(L, index, "([a-z][a-z0-9_]*)"\)', parser))),
    'accepted_config': re.findall(r'"([a-z][a-z0-9_]*)"', accepted),
    'constants': dict(constants),
    'abi': abi[1] if abi else '',
}
output.parent.mkdir(parents=True, exist_ok=True)
content = json.dumps(facts, sort_keys=True, indent=2) + '\n'
if not output.exists() or output.read_text() != content:
    output.write_text(content)
