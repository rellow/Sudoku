# P620 offline observation validation

Run the existing P620 observation collector on multiple independently allocated 1-GiB physical frames. Preserve the generated raw.csv, summary.csv, and manifest.json together. Use different output prefixes for independent replications.

Analyze one run:

```bash
python3 tools/analyze_p620_observations.py --summary results/p620/rep01.summary.csv --manifest results/p620/rep01.manifest.json --output results/p620/rep01_quality.json
```

Compare runs (corresponding paths in the same order):

```bash
python3 tools/analyze_p620_observations.py --summary results/p620/rep01.summary.csv results/p620/rep02.summary.csv --manifest results/p620/rep01.manifest.json results/p620/rep02.manifest.json --output results/p620/replications_quality.json
```

The script checks row and run IDs, frame alignment/membership, observed NUMA provenance, basic latency consistency, sample counts, and pair duplicates. Entire physical frames are assigned to exploratory/held-out partitions; summaries include bootstrap mean confidence intervals. Cross-run reports identify frame reuse and median drift. These are descriptive statistics, **not** a trained DRAM-bank classifier. There is no DRAM address-function inference or refresh manipulation. Holdout and cross-run observations do not independently prove physical wordline adjacency.
