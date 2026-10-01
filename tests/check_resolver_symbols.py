#!/usr/bin/env python3
"""Assert private namespaces and no libc resolver/module dependencies."""

import re
import subprocess
import sys

symbols = subprocess.check_output([sys.argv[1], '-g', sys.argv[2]], text=True)
for line in symbols.splitlines():
    match = re.search(r'\b([A-Za-z_][A-Za-z0-9_]*)$', line)
    if not match:
        continue
    name = match.group(1)
    assert not name.startswith('ares_'), ('unprefixed upstream symbol', name)
    if re.search(r'\bU\s+', line):
        assert name not in {'getaddrinfo', 'freeaddrinfo', 'getnameinfo',
                            'gethostbyname', 'gethostbyname2', 'gethostbyaddr',
                            'getservbyname', 'getservbyname_r', 'getservbyport',
                            'getservbyport_r', 'dlopen', 'dlsym'}, (
                                'libc resolver/module dependency', name)
assert 'sl_cares_ares_getaddrinfo' in symbols, 'resolver missing from archive'
print('Static archive embeds a private resolver without libc NSS or module loading.')
