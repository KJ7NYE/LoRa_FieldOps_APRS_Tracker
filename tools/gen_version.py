Import("env")
import subprocess
import os

GENERATED = "include/generated/firmware_version.h"

def _git(args):
    try:
        return subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""

# git describe: clean tag → "v1.0.9", post-tag → "v1.0.9-5-gabcdef", dirty → appends "-dirty"
version = _git(["describe", "--tags", "--always", "--dirty"]) or "unknown"

content = f'#pragma once\n#define FIRMWARE_VERSION_DATE "{version}"\n'

os.makedirs(os.path.dirname(GENERATED), exist_ok=True)
existing = open(GENERATED).read() if os.path.exists(GENERATED) else ""
if existing != content:
    with open(GENERATED, "w") as f:
        f.write(content)
    print(f"[gen_version] {version}")
else:
    print(f"[gen_version] {version} (unchanged)")
