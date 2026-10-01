#!/usr/bin/env python3
"""Verify retained source hashes, license and documented patch inventory."""

import hashlib
import json
import pathlib
import re
import sys

vendor = pathlib.Path(sys.argv[1]) / 'vendor/c-ares'
provenance = json.loads((vendor / 'provenance.json').read_text())
assert provenance['license'] == 'MIT'
assert provenance['licenses'] == ['MIT', 'BSD-3-Clause']
assert provenance['version'] == '1.34.8'
assert provenance['sha256'] == 'c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78'
assert 'MIT License' in (vendor / 'LICENSE.md').read_text()
local = {'provenance.json', 'softline.patch', 'README.softline.md', 'LICENSE.BSD-3-Clause'}
assert {p.relative_to(vendor).as_posix() for p in vendor.rglob('*') if p.is_file()} == (
    set(provenance['files']) | local)
for name, digest in provenance['files'].items():
    assert hashlib.sha256((vendor / name).read_bytes()).hexdigest() == digest, name
    assert 'APPLE_LICENSE_HEADER_START' not in (vendor / name).read_text(), name
assert 'SPDX-License-Identifier: BSD-3-Clause' in (vendor / 'LICENSE.BSD-3-Clause').read_text()
patch = (vendor / 'softline.patch').read_text()
assert re.findall(r'^\+\+\+ b/(.*)$', patch, re.M) == [
    'src/lib/ares_getaddrinfo.c', 'src/lib/ares_getnameinfo.c']
print('Vendored resolver source manifest, checksums, MIT license and patch inventory passed.')
