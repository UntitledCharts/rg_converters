#!/usr/bin/env python
"""Build the _rg_converters C++ extension with MinGW-w64 g++ (for when MSVC is absent)."""
import glob
import os
import shutil
import subprocess
import sys
import sysconfig

try:
    import pybind11
except ModuleNotFoundError:
    raise SystemExit("[build.py] pybind11 is required; run: pip install pybind11")

ROOT = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    pyinc = sysconfig.get_path("include")
    pyver = f"{sys.version_info.major}{sys.version_info.minor}"
    pylib = os.path.join(sys.base_prefix, f"python{pyver}.dll")
    pb11 = pybind11.get_include()
    extsuf = sysconfig.get_config_var("EXT_SUFFIX") or ".pyd"
    srcs = glob.glob(os.path.join(ROOT, "cpp", "src", "**", "*.cpp"), recursive=True)
    out = os.path.join(ROOT, "rg_converters", f"_rg_converters{extsuf}")
    cxx = os.environ.get("CXX") or shutil.which("g++") or "g++"

    cmd = [
        cxx, "-O2", "-shared", "-std=c++23", "-DMS_WIN64",
        f"-I{os.path.join(ROOT, 'cpp', 'include')}",
        f"-I{os.path.join(ROOT, 'cpp', 'src', 'bindings')}",
        f"-I{pyinc}", f"-I{pb11}",
        *srcs, "-o", out, pylib,
        "-static-libgcc", "-static-libstdc++", "-static", "-lpthread", "-lz",
    ]
    print(f"[build.py] compiling {len(srcs)} sources -> {out}")
    subprocess.run(cmd, check=True)

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
