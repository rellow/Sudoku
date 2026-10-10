#!/usr/bin/env python3
import csv
import json
import statistics
from pathlib import Path

root = Path("results/p620")
out = root / "disagreements"
out.mkdir(parents=True, exist_ok=True)

masks = [
    0x088890000, 0x111120000, 0x444408000,
    0x222278000, 0x00007fec0, 0x000804100
]

def same_selection(a, b):
    return all((((a ^ b) & m).bit_count() & 1) == 0 for m in masks)

exceptions = []

for rep in ("rep01", "rep02", "rep03"):
    manifest = json.loads(
        (root / f"{rep}.manifest.json").read_text()
    )
    trials_expected = manifest["trials_per_pair"]

    selected = {}

    with (root / f"{rep}.summary.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            a = int(r["paddr_a"], 0)
            b = int(r["paddr_b"], 0)

            predicted = same_selection(a, b)
            observed = float(r["median"]) >= 504

            if predicted != observed:
                key = (r["run_id"], r["pair_id"])
                selected[key] = {
                    "rep": rep,
                    **r,
                    "predicted_same": predicted,
                    "observed_high": observed,
                    "raw_cycles": [],
                    "expected_trials": trials_expected,
                }

    with (root / f"{rep}.raw.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            key = (r["run_id"], r["pair_id"])
            if key in selected:
                selected[key]["raw_cycles"].append(
                    int(r["elapsed_cycles"])
                )

    exceptions.extend(selected.values())

report = []

with (out / "exception_trials.csv").open("w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["rep", "run_id", "pair_id", "trial", "cycles"])

    for e in exceptions:
        for i, cycles in enumerate(e["raw_cycles"]):
            writer.writerow([
                e["rep"], e["run_id"], e["pair_id"], i, cycles
            ])

for e in exceptions:
    values = sorted(e["raw_cycles"])

    item = {
        "rep": e["rep"],
        "run_id": e["run_id"],
        "pair_id": e["pair_id"],
        "frame": e["frame_base_phys"],
        "paddr_a": e["paddr_a"],
        "paddr_b": e["paddr_b"],
        "predicted_same": e["predicted_same"],
        "observed_high": e["observed_high"],
        "trial_count": len(values),
        "expected_trials": e["expected_trials"],
        "raw_median": statistics.median(values) if values else None,
        "raw_min": min(values) if values else None,
        "raw_max": max(values) if values else None,
        "raw_p10": values[int(.10*(len(values)-1))] if values else None,
        "raw_p90": values[int(.90*(len(values)-1))] if values else None,
        "raw_fraction_ge_504": (
            sum(v >= 504 for v in values)/len(values)
            if values else None
        )
    }
    report.append(item)

(out / "exceptions.json").write_text(
    json.dumps(report, indent=2) + "\n"
)

print("Exceptions:", len(report))
print("Trials:", sum(r["trial_count"] for r in report))
print("Complete:", all(
    r["trial_count"] == r["expected_trials"]
    for r in report
))
