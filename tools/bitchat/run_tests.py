#!/usr/bin/env python3
"""Compile and run the bridge's host tests: codec and text, Bluetooth adapter, end-to-end flow.

No hardware is used. The tests link the same rweather Crypto 0.4.0 sources as the firmware, taken
from a PlatformIO library directory, e.g. .pio/libdeps/<env>/Crypto.
"""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "test/bitchat"
BRIDGE = ROOT / "src/helpers/bitchat"
CORE = ["BridgeCodec.cpp", "BridgeHistory.cpp", "BridgeState.cpp", "BridgeText.cpp"]
ADAPTER = ["BridgeBle.cpp", "BridgeCodec.cpp", "BridgeDiagnostics.cpp"]
FLOW = [
    "BitchatBridge.cpp",
    "BridgeBle.cpp",
    "BridgeCodec.cpp",
    "BridgeDiagnostics.cpp",
    "BridgeHistory.cpp",
    "BridgeState.cpp",
    "BridgeText.cpp",
]
CRYPTO = ["Crypto", "Hash", "SHA256", "SHA512", "BigNumberUtil", "Curve25519", "Ed25519", "HKDF"]


def vectors_header(build):
    cases = json.loads((TESTS / "official_vectors.json").read_text())["cases"]
    lines = [
        "#pragma once",
        "struct Vector { const char *name; const char *wire; const char *canonical; const char *key; };",
        "static const Vector VECTORS[] = {",
    ]
    for case in cases:
        lines.append("{" + ",".join(json.dumps(case[k]) for k in ("name", "wire", "canonical", "public_key")) + "},")
    lines.append("};")
    (build / "vectors.h").write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crypto-dir", type=Path, required=True)
    parser.add_argument("--no-sanitize", action="store_true")
    parser.add_argument("suites", nargs="*", default=["core", "adapter", "flow"])
    args = parser.parse_args()
    crypto = args.crypto_dir.resolve()
    if json.loads((crypto / "library.json").read_text()).get("version") != "0.4.0":
        raise SystemExit("Expected rweather Crypto 0.4.0")
    base = [
        os.environ.get("CXX", "clang++"),
        "-std=c++17",
        "-O1",
        "-g",
        "-ffunction-sections",
        "-fdata-sections",
        "-Wl,--gc-sections",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-DWITH_BITCHAT_BRIDGE=1",
    ]
    if not args.no_sanitize:
        base += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    crypto_sources = [str(crypto / (name + ".cpp")) for name in CRYPTO]
    with tempfile.TemporaryDirectory(prefix="bitchat-tests-") as directory:
        build = Path(directory)
        vectors_header(build)
        suites = {
            "core": (["-I" + str(ROOT / "src"), "-I" + str(crypto), "-I" + str(build)], "test_main.cpp", CORE, crypto_sources),
            "adapter": (["-I" + str(TESTS / "stubs"), "-I" + str(ROOT / "src")], "test_ble.cpp", ADAPTER, []),
            "flow": (
                ["-I" + str(TESTS / "host"), "-I" + str(TESTS / "stubs"), "-I" + str(ROOT / "src"), "-I" + str(crypto)],
                "test_flow.cpp",
                FLOW,
                crypto_sources,
            ),
        }
        for name in args.suites:
            includes, test, sources, extra = suites[name]
            binary = build / name
            print(f"[{name}] compiling", flush=True)
            command = [*base, *includes, str(TESTS / test), *(str(BRIDGE / source) for source in sources), *extra]
            if subprocess.run([*command, "-o", str(binary)]).returncode != 0:
                raise SystemExit(f"[{name}] did not compile")
            if subprocess.run([str(binary)], timeout=600).returncode != 0:
                raise SystemExit(f"[{name}] failed")


if __name__ == "__main__":
    main()
