#!/usr/bin/env python3
import glob
import json
from collections import defaultdict

paths = sorted(set(
    glob.glob("results/p620/rep*.manifest.json")
    + glob.glob("results/p620/coverage/rep*.manifest.json")
))

frames = defaultdict(set)

for path in paths:
    with open(path) as f:
        manifest = json.load(f)

    for entry in manifest["frames"]:
        base = int(entry["base_phys"], 0)
        frames[base].add(manifest["run_id"])

print("Unique physical frames:", len(frames))

for frame, runs in sorted(frames.items()):
    print(hex(frame), "run_count=", len(runs))

variable_bits = [
    bit for bit in range(30, 52)
    if len({(frame >> bit) & 1 for frame in frames}) == 2
]

print("Variable frame-base bits:", variable_bits)
