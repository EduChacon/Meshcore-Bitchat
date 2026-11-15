"""Bridge builds only: date the build and patch the pinned BLE dependency before its compilation."""

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
