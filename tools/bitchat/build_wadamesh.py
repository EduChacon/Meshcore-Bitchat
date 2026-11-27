#!/usr/bin/env python3
"""Build the WadaMesh touch firmware for the T-Deck / T-Deck Plus with this repository's Bitchat bridge.

WadaMesh (https://github.com/ALLFATHER-BV/wadamesh, GPL-3.0) is fetched at the commit pinned in
integrations/wadamesh/wadamesh.json; integrations/wadamesh/bridge.patch adds the hooks; the bridge's own
sources (src/helpers/bitchat) and integrations/wadamesh/src are copied in, so the bridge stays in one place.
The core's companion Bluetooth is dropped for the bridge's (same app, same PIN pairing), and the pinned
NimBLE gets the same startup fix as every bridge build.

    python3 tools/bitchat/build_wadamesh.py [--work DIR] [--region brasil] [--out DIR]

Needs git and PlatformIO (pio) in PATH and network access for the first fetch.
"""

import argparse
import json
import os
import runpy
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INTEGRATION = ROOT / "integrations/wadamesh"
LIB_MARK = "# MeshCore-Bitchat: bridge builds bring their own companion Bluetooth"
HEADER_MARK = "MeshCore-Bitchat: bridge builds use the bridge's SerialBLEInterface"


def run(command, cwd, env=None):
    print("+", " ".join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], cwd=cwd, env=env, check=True)


def fetch(work, pin):
    """WadaMesh at exactly the pinned commit, with nothing left over from a previous build but .pio."""
    if not (work / ".git").exists():
        work.mkdir(parents=True, exist_ok=True)
        run(["git", "init", "-q"], work)
        run(["git", "remote", "add", "origin", pin["repository"]], work)
    run(["git", "fetch", "-q", "--depth", "1", "origin", pin["commit"]], work)
    run(["git", "checkout", "-q", "--force", "--detach", pin["commit"]], work)
    run(["git", "clean", "-q", "-fdx", "-e", ".pio"], work)


def prepare(work):
    run(["git", "apply", "--whitespace=nowarn", INTEGRATION / "bridge.patch"], work)
    # the files of the integration, at their place in WadaMesh's tree
    for source in (INTEGRATION / "src").rglob("*"):
        if source.is_file():
            target = work / "src" / source.relative_to(INTEGRATION / "src")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    # the bridge itself: the core and its ESP32 Bluetooth; nothing of the nRF52 port
    bridge = ROOT / "src/helpers/bitchat"
    target = work / "src/helpers/bitchat"
    shutil.rmtree(target, ignore_errors=True)
    target.mkdir(parents=True)
    for source in bridge.glob("*.*"):
        shutil.copy2(source, target / source.name)
    shutil.copytree(bridge / "esp32", target / "esp32")


def patch_libraries(work, environment):
    """The core library keeps its own SerialBLEInterface.cpp out of bridge builds; NimBLE gets its fix."""
    libdeps = work / ".pio/libdeps" / environment
    core = libdeps / "MeshCore"
    script = core / "build_as_lib.py"
    if not script.exists():
        raise SystemExit(f"WadaMesh core library not found at {core}")
    text = script.read_text()
    if LIB_MARK not in text:
        addition = f"""
{LIB_MARK}
if "WITH_BITCHAT_BRIDGE" in [d if isinstance(d, str) else d[0] for d in menv.get("CPPDEFINES", [])]:
    menv.Replace(SRC_FILTER=menv["SRC_FILTER"] + ["-<helpers/esp32/SerialBLEInterface.cpp>"])
"""
        script.write_text(text + addition)
        print("WadaMesh core: SerialBLEInterface.cpp left to the bridge")
    # The core's header stays on the include path, ahead of WadaMesh's src/ for <...> includes. A file that
    # reached it would build a SerialBLEInterface of another layout than the bridge's, which links without
    # complaint and corrupts memory at the first Bluetooth start (the T-Deck boot loop of bitchat1.2.1).
    header = core / "src/helpers/esp32/SerialBLEInterface.h"
    text = header.read_text(encoding="utf-8")
    if HEADER_MARK not in text:
        guard = (
            f"// {HEADER_MARK}\n"
            "#ifdef WITH_BITCHAT_BRIDGE\n"
            '#error "the core\'s SerialBLEInterface.h in a bridge build: include WadaMesh\'s, which is the bridge\'s"\n'
            "#endif\n"
        )
        header.write_text(guard + text, encoding="utf-8")
        print("WadaMesh core: its SerialBLEInterface.h refused in bridge builds")
    nimble = libdeps / "NimBLE-Arduino"
    runpy.run_path(str(ROOT / "tools/bitchat/patch_nimble.py"))["apply"](nimble)
    print("NimBLE 1.4.3: startup guard applied")


def build_flags(region):
    built = subprocess.check_output(["git", "log", "-1", "--format=%ct"], cwd=ROOT, text=True).strip()
    flags = [f"-D BRIDGE_BUILD_EPOCH={built}"]
    if region:
        regions = json.loads((ROOT / "tools/bitchat/regions.json").read_text(encoding="utf-8"))
        if region not in regions:
            raise SystemExit(f"unknown region {region!r}; known: {', '.join(regions)}")
        radio = regions[region]
        # WadaMesh takes its factory radio from these; a radio already set up keeps its own
        flags += [
            f"-D LORA_FREQ={radio['freq']}",
            f"-D LORA_BW={radio['bw']}",
            f"-D LORA_SF={radio['sf']}",
            f"-D LORA_CR={radio['cr']}",
        ]
    return " ".join(flags)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", type=Path, default=ROOT / ".wadamesh")
    parser.add_argument("--region", default="", help="an entry of tools/bitchat/regions.json, e.g. brasil")
    parser.add_argument("--out", type=Path, default=ROOT / "out/wadamesh")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    pin = json.loads((INTEGRATION / "wadamesh.json").read_text())
    work = args.work.resolve()
    fetch(work, pin)
    prepare(work)
    if args.prepare_only:
        return
    environment = pin["environment"]
    run(["pio", "pkg", "install", "-e", environment], work)
    patch_libraries(work, environment)
    env = dict(os.environ, PLATFORMIO_BUILD_FLAGS=build_flags(args.region))
    run(["pio", "run", "-e", environment], work, env)
    # bootloader, partitions and app in one image, for a first flash at 0x0
    run(["pio", "run", "-e", environment, "-t", "mergebin"], work, env)
    build = work / ".pio/build" / environment
    args.out.mkdir(parents=True, exist_ok=True)
    name = pin["output"] + (f"-{args.region}" if args.region else "")
    copied = []
    for source, suffix in (("firmware.bin", ".bin"), ("firmware-merged.bin", "-merged.bin")):
        if (build / source).exists():
            shutil.copy2(build / source, args.out / (name + suffix))
            copied.append(name + suffix)
    if not copied:
        raise SystemExit(f"no firmware in {build}")
    print("WadaMesh + Bitchat:", ", ".join(copied), "in", args.out)


if __name__ == "__main__":
    main()
