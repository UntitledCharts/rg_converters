#!/usr/bin/env python
"""Build the _rg_converters C++ extension with MinGW-w64 g++ (for when MSVC is absent)."""
import glob
import os
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from concurrent.futures import ThreadPoolExecutor

try:
    import pybind11
except ModuleNotFoundError:
    raise SystemExit("[build.py] pybind11 is required; run: pip install pybind11")

ROOT = os.path.dirname(os.path.abspath(__file__))


def read_version() -> str:
    with open(os.path.join(ROOT, "rg_converters", "version.py"), encoding="utf-8") as f:
        match = re.search(r'^__version__ = "([^"]+)"', f.read(), re.MULTILINE)
    if not match:
        raise SystemExit("[build.py] could not read __version__ from rg_converters/version.py")
    return match.group(1)


def main() -> int:
    pyinc = sysconfig.get_path("include")
    pyver = f"{sys.version_info.major}{sys.version_info.minor}"
    pylib = os.path.join(sys.base_prefix, f"python{pyver}.dll")
    pb11 = pybind11.get_include()
    extsuf = sysconfig.get_config_var("EXT_SUFFIX") or ".pyd"
    srcs = glob.glob(os.path.join(ROOT, "cpp", "src", "**", "*.cpp"), recursive=True)
    out = os.path.join(ROOT, "rg_converters", f"_rg_converters{extsuf}")
    cxx = os.environ.get("CXX") or shutil.which("g++") or "g++"

    cflags = [
        cxx, "-O2", "-std=c++23", "-DMS_WIN64", f'-DRG_VERSION="{read_version()}"',
        f"-I{os.path.join(ROOT, 'cpp', 'include')}",
        f"-I{os.path.join(ROOT, 'cpp', 'src', 'bindings')}",
        f"-I{pyinc}", f"-I{pb11}",
    ]
    print(f"[build.py] compiling {len(srcs)} sources in parallel -> {out}")
    with tempfile.TemporaryDirectory() as objdir:
        def compile_one(src: str) -> str:
            obj = os.path.join(objdir, os.path.relpath(src, ROOT).replace(os.sep, "_") + ".o")
            subprocess.run([*cflags, "-c", src, "-o", obj], check=True)
            return obj

        with ThreadPoolExecutor(os.cpu_count()) as ex:
            objs = list(ex.map(compile_one, srcs))
        subprocess.run(
            [cxx, "-shared", *objs, "-o", out, pylib,
             "-static-libgcc", "-static-libstdc++", "-static", "-lpthread", "-lz"],
            check=True,
        )

    # Regenerate .pyi stubs so editors see the extension API (best-effort).
    try:
        subprocess.run(
            [sys.executable, "-m", "pybind11_stubgen", "rg_converters._rg_converters", "-o", ROOT],
            check=True, cwd=ROOT,
        )
        print("[build.py] stubs regenerated")
    except Exception as exc:  # pybind11-stubgen not installed, etc.
        print(f"[build.py] stub generation skipped ({exc})")

    print("[build.py] done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
