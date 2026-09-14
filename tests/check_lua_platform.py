"""Build the actual local interpreter with Darwin settings (native or osxcross)."""
from pathlib import Path
import platform
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
options = []
if platform.system() != "Darwin":
    result = subprocess.run([str(root / "scripts/cpkt-toolchains.sh"), "discover",
                             "arm64-apple-darwin"], capture_output=True, text=True)
    if result.returncode or "status=ready" not in result.stdout:
        print("SKIP: local Lua Darwin build requires macOS or osxcross")
        raise SystemExit(0)
    options.append("-DCMAKE_TOOLCHAIN_FILE=" + str(root / "cmake/toolchains/osxcross-darwin.cmake"))
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="lua-platform.", dir=root / "build") as tmp:
    work = Path(tmp)
    subprocess.run(["cmake", "-S", str(root / "cmake/local-lua"), "-B", tmp,
                    "-G", "Ninja", *options], check=True)
    subprocess.run(["cmake", "--build", tmp], check=True)
    graph = (work / "build.ninja").read_text()
    assert "LUA_USE_MACOSX" in graph
    assert "LUA_USE_LINUX" not in graph
    assert "--export-dynamic" not in graph
    assert "--dynamic-linker" not in graph
    assert " -ldl" not in graph
    assert (work / "lua").is_file()
    if platform.system() == "Darwin":
        subprocess.run([str(work / "lua"), "-e", 'assert(io.popen("echo ok"):read("l") == "ok")'],
                       check=True)
print("Local Lua Darwin configure/build/platform flags passed")
