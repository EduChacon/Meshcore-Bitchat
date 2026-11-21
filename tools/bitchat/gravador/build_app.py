#!/usr/bin/env python3
"""Builds the flasher for the computer it runs on, checks it, and zips it.

    python tools/bitchat/gravador/build_app.py <zip name> [--arch x86_64|arm64]

Windows: one Gravador-Bitchat.exe. Mac: Gravador-Bitchat.app. The app is started once with --self-test
(esptool, its stubs, the release list, the certificates, the window) before it is zipped."""

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = "Gravador-Bitchat"


def run(command, **kwargs):
    print("+", " ".join(str(c) for c in command), flush=True)
    subprocess.run(command, check=True, **kwargs)


def zip_folder(source, target):
    """A zip that keeps the app's executable bits and links (as Finder's own does)."""
    if sys.platform == "darwin":
        run(["ditto", "-c", "-k", "--keepParent", str(source), str(target)])
        return
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        if source.is_file():
            archive.write(source, source.name)
        else:
            for path in sorted(source.rglob("*")):
                archive.write(path, path.relative_to(source.parent))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="the zip to write")
    parser.add_argument("--arch", help="Mac only: the architecture to build for")
    args = parser.parse_args()
    work = HERE / "build-app"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    for name in ("gravador.py", "gravador_core.py"):
        shutil.copy(HERE / name, work / name)
    run([sys.executable, str(HERE / "make_build_json.py"), str(work / "build.json")])
    separator = ";" if os.name == "nt" else ":"
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--windowed", "--name", NAME,
               "--add-data", f"build.json{separator}.", "--collect-data", "esptool", "--collect-data", "certifi"]
    if sys.platform == "darwin":
        command += ["--osx-bundle-identifier", "io.github.educhacon.gravador-bitchat"]
        if args.arch:
            command += ["--target-arch", args.arch]
    else:
        command += ["--onefile"]
    run(command + ["gravador.py"], cwd=work)
    dist = work / "dist"
    if sys.platform == "darwin":
        app = dist / f"{NAME}.app"
        executable = app / "Contents" / "MacOS" / NAME
    else:
        app = executable = dist / (f"{NAME}.exe" if os.name == "nt" else NAME)
    result = subprocess.run([str(executable), "--self-test"], cwd=work, timeout=300)
    log = work / "gravador-self-test.log"
    print(log.read_text(encoding="utf-8") if log.exists() else "(no self-test log)", flush=True)
    if result.returncode != 0:
        raise SystemExit(f"self-test failed ({result.returncode})")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    zip_folder(app, args.output.resolve())
    print(f"{args.output}: {args.output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
