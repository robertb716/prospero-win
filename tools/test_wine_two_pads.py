#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Test two controllers using the actual compiled Wine XInput and HID Unix bridges.

Requires a Wine checkout with the patch series applied and its configured
64-bit Unix build. Build dlls/xinput1_3/xinput1_3.so and
dlls/winebus.sys/winebus.so first. No PS5, Wine prefix or Windows process is
needed: the test executable exports the real title sink and loads both bridges.
This does not replace testing Windows API enumeration or console acceptance.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source, build = args.source.resolve(), args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="wine-two-pads-") as scratch:
        exe = Path(scratch) / "two-pads"
        subprocess.run([
            "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-DWINE_UNIX_LIB", "-D_DEFAULT_SOURCE",
            "-I" + str(build / "include"), "-I" + str(source / "include"),
            "-I" + str(source / "dlls/winebus.sys"), "-I" + str(source / "dlls"),
            str(root / "tests/lab/pw_two_pads_unix.c"),
            str(root / "wine/ps5/pw_wine_sink.c"),
            "-rdynamic", "-pthread", "-ldl", "-o", str(exe),
        ], check=True, timeout=60)
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = str(build / "dlls/ntdll") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        subprocess.run([
            str(exe), str(build / "dlls/xinput1_3/xinput1_3.so"),
            str(build / "dlls/winebus.sys/winebus.so"),
        ], env=env, check=True, timeout=30)


if __name__ == "__main__":
    main()
