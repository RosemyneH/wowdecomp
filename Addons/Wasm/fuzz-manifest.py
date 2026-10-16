#!/usr/bin/env python3
"""ʕ •ᴥ•ʔ✿ Mutational fuzz: malformed manifests/modules must fail cleanly, never crash the host ✿ ʕ •ᴥ•ʔ"""
import os, random, subprocess, sys, tempfile

HOST = sys.argv[1] if len(sys.argv) > 1 else "build/wasm/bin/wasm_addon_host"
SEEDS = [
    b'[addon]\nid="a.b"\nname="A"\nversion="1"\napi_version="1"\n',
    b'[permissions]\nchat.print=true\n',
    b'[addon]\nid="../evil"\nname="A"\nversion="1"\napi_version="1"\n[limits]\nmemory_mb=999999\n',
    b'\x00\xff\xfe random \x80 bytes',
]
random.seed(335)
crashes = 0
with tempfile.TemporaryDirectory() as tmp:
    for i in range(200):
        data = bytearray(random.choice(SEEDS))
        for _ in range(random.randint(1, 24)):
            op = random.randrange(3)
            if op == 0 and data:
                data[random.randrange(len(data))] = random.randrange(256)
            elif op == 1:
                data[random.randrange(len(data) + 1):] = bytes([random.randrange(256)])
            elif op == 2 and len(data) > 2:
                del data[random.randrange(len(data))]
        addon = os.path.join(tmp, "fuzz")
        os.makedirs(addon, exist_ok=True)
        with open(os.path.join(addon, "addon.toml"), "wb") as f: f.write(data)
        with open(os.path.join(addon, "addon.wasm"), "wb") as f: f.write(data)
        r = subprocess.run([HOST, addon], capture_output=True, timeout=10)
        if r.returncode < 0:  # killed by a signal
            crashes += 1
            print(f"CRASH iter={i} rc={r.returncode}")
print(f"fuzz complete: 200 iterations, {crashes} crashes")
sys.exit(1 if crashes else 0)
