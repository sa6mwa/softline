"""Changing the export allowlist must regenerate the map on a plain build."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


root = Path(sys.argv[1]).resolve()
cmake = sys.argv[2]
nm = sys.argv[3]
symbol = "sl_output_stream_write_quoted_prompt"


def run(*command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(
            f"{' '.join(map(str, command))} failed:\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def exports(library):
    lines = run(nm, "-D", "--defined-only", "--format=posix", str(library))
    return {line.split()[0].split("@", 1)[0] for line in lines.splitlines() if line}


with tempfile.TemporaryDirectory(prefix="export-reconfigure-", dir=root / "build") as path:
    fixture = Path(path)
    source = fixture / "source"
    build = fixture / "build"
    source.mkdir()
    shutil.copy2(root / "CMakeLists.txt", source / "CMakeLists.txt")
    for name in ("src", "include", "cmake", "scripts"):
        shutil.copytree(root / name, source / name)

    run(
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DSL_TARGET_ID=review",
        "-DSL_BUILD_STATIC=OFF",
        "-DSL_BUILD_TESTS=OFF",
        "-DSL_BUILD_EXAMPLES=OFF",
        "-DSL_INSTALL=OFF",
    )
    run(cmake, "--build", str(build), "--parallel", "1")
    allowlist = source / "cmake" / "softline.exports"
    export_map = build / "softline.exports.map"
    library = build / "libsoftline.so.0.0.0"
    if symbol not in exports(library) or symbol not in export_map.read_text():
        raise AssertionError("initial shared-library export is missing")

    lines = allowlist.read_text().splitlines()
    if lines.count(symbol) != 1:
        raise AssertionError("expected exactly one quoted-prompt export entry")
    allowlist.write_text("\n".join(line for line in lines if line != symbol) + "\n")
    run(cmake, "--build", str(build), "--parallel", "1")
    if symbol in export_map.read_text() or symbol in exports(library):
        raise AssertionError("allowlist edit did not regenerate the shared-library exports")

print("shared-library export allowlist triggers incremental CMake regeneration")
