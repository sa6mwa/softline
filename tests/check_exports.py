"""Assert libsoftline's defined dynamic functions match its public allowlist."""

import argparse
from pathlib import Path
import subprocess


def allowlist(path):
    return {
        line.strip()
        for line in Path(path).read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def exported_functions(nm, library):
    result = subprocess.run(
        [nm, "-D", "--defined-only", "--format=posix", library],
        check=True,
        capture_output=True,
        text=True,
    )
    exports = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[1].upper() not in {"T", "W"}:
            continue
        exports.add(fields[0].split("@", 1)[0])
    return exports


parser = argparse.ArgumentParser()
parser.add_argument("--nm", required=True)
parser.add_argument("--library", required=True)
parser.add_argument("--allowlist", required=True)
args = parser.parse_args()

expected = allowlist(args.allowlist)
actual = exported_functions(args.nm, args.library)
if actual != expected:
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    raise SystemExit(
        "libsoftline dynamic export mismatch:\n"
        + ("missing: " + ", ".join(missing) + "\n" if missing else "")
        + ("unexpected: " + ", ".join(unexpected) if unexpected else "")
    )
print("libsoftline dynamic export allowlist passed")
