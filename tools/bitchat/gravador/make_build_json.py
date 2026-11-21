#!/usr/bin/env python3
"""Writes build.json for the flasher: the release version (variants/bitchat/platformio.ini) and the boards a
release publishes (tools/bitchat/main_targets.txt), each marked esp32 or nrf52 by the bridge section its
environment uses."""

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def environments(ini_text):
    """Each [env:...] section's name and text."""
    parts = re.split(r"^\[env:([^\]]+)\]\s*$", ini_text, flags=re.M)
    return {parts[i]: parts[i + 1] for i in range(1, len(parts) - 1, 2)}


def build(root=ROOT):
    ini = (root / "variants/bitchat/platformio.ini").read_text(encoding="utf-8")
    version = re.search(r"FIRMWARE_VERSION='\"([^\"]+)\"", ini).group(1)
    sections = environments(ini)
    targets = [
        line.strip()
        for line in (root / "tools/bitchat/main_targets.txt").read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    ]
    devices = []
    for env in targets:
        text = sections.get(env)
        if text is None:
            raise SystemExit(f"{env} is not in variants/bitchat/platformio.ini")
        kind = "nrf52" if "bitchat_bridge_nrf52" in text else "esp32" if "bitchat_bridge_esp32" in text else None
        if not kind:
            raise SystemExit(f"{env}: neither an ESP32 nor an nRF52 bridge environment")
        devices.append({"env": env, "kind": kind})
    return {"version": version, "devices": devices}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = build()
    args.output.write_text(json.dumps(data, indent=1), encoding="utf-8")
    print(f"{data['version']}: {len(data['devices'])} devices")


if __name__ == "__main__":
    main()
