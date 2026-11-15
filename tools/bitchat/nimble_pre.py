"""Bridge builds only: date the build, set the factory radio of a country and patch the pinned BLE dependency."""

import json
import os
import runpy
import subprocess
import time
from pathlib import Path

Import("env")

# The bridge refuses a clock earlier than its own build. The commit date keeps repeated builds identical.
try:
    built = int(subprocess.check_output(["git", "log", "-1", "--format=%ct"], cwd=env.subst("$PROJECT_DIR"), text=True))
except (OSError, subprocess.CalledProcessError, ValueError):
    built = int(time.time())
env.Append(CPPDEFINES=[("BRIDGE_BUILD_EPOCH", built)])

# BITCHAT_REGION names an entry of regions.json: the radio a never-configured device starts with.
region = os.environ.get("BITCHAT_REGION", "")
if region:
    project = Path(env.subst("$PROJECT_DIR"))
    table = next((path for path in (project / "tools/bitchat/regions.json", project / "lib/MeshCore/tools/bitchat/regions.json") if path.exists()))
    regions = json.loads(table.read_text(encoding="utf-8"))
    if region not in regions:
        raise SystemExit(f"Bridge: unknown BITCHAT_REGION {region!r}; known: {', '.join(regions)}")
    radio = regions[region]
    env.Append(
        CPPDEFINES=[
            ("BITCHAT_FACTORY_FREQ", f"{radio['freq']}f"),
            ("BITCHAT_FACTORY_BW", f"{radio['bw']}f"),
            ("BITCHAT_FACTORY_SF", radio["sf"]),
            ("BITCHAT_FACTORY_CR", radio["cr"]),
            ("BITCHAT_FACTORY_PATH_HASH_MODE", radio["path_hash_bytes"] - 1),
        ]
    )
    if radio.get("repeat"):
        # MeshCore lets a companion radio repeat only on a few frequencies; 918.000 MHz is one of them.
        env.Append(CPPDEFINES=[("BITCHAT_FACTORY_REPEAT", 1)])
    print(f"Bridge: factory radio for {region}: {radio['freq']} MHz, BW {radio['bw']}, SF {radio['sf']}, CR {radio['cr']}")

patched = False
TOOLS = "tools/bitchat/patch_nimble.py"


def patch_nimble(build_env, node):
    global patched
    if not patched:
        library = Path(node.srcnode().get_abspath()).parent.parent
        project = Path(build_env.subst("$PROJECT_DIR"))
        # in this repository, or in a project that carries it as lib/MeshCore
        script = next(
            (path for path in (project / TOOLS, project / "lib/MeshCore" / TOOLS) if path.exists()), project / TOOLS
        )
        runpy.run_path(str(script))["apply"](library)
        patched = True
        print("Bridge: verified and applied NimBLE 1.4.3 startup guard")
    return node


env.AddBuildMiddleware(patch_nimble, "*/NimBLE-Arduino/src/NimBLEDevice.cpp")
