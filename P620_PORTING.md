# P620 / Zen 3 / DDR4 RDIMM compatibility (experimental)

## Scope
Build with `cmake -S . -B build-p620 -DSUDOKU_PLATFORM=P620_ZEN3_DDR4`.
The P620 configuration is **uncalibrated**. The main reverse-engineering,
bit-identification, validation, and decomposition commands are intentionally
disabled for this target. The `watch_*` binaries can be used for exploratory
observation, but their interpretations and any thresholds require validation.
This branch does not modify ZenHammer and does not implement a physical-wordline
adjacency oracle.

## Target inventory (not a verified decomposition)
- CPU: AMD Ryzen Threadripper PRO 5975WX (Zen 3).
- Four Hynix HMA82GR7DJR8N-XN 16-GiB DDR4-3200 ECC RDIMMs, 2Rx8.
- Populated AMD UMC2--UMC5; physical-address/channel routing is not modeled
  by the upstream single-channel CLI.
- Existing DF3/UMC reconstruction must be evaluated independently.
- An RCD may transform command/address signals delivered to DRAM chips.

## Known upstream limitations, not silently repaired by this first port
- `MemoryConfig(1, 1, num_dimms, ...)` assumes a single channel;
  `-n 4` does **not** represent four populated UMCs.
- `PCI_OFFSET` subtraction is not the complete AMD DF/UMC translation.
- Refresh threshold and same-bank/different-row thresholds require calibration.
- Some component assignment logic in `DecomposeUsingConsecutiveAccesses`
  is commented out upstream.
- An injective software encoding alone does not establish physical rank/bank
  labels or physical wordline adjacency.
- Physical-address sampling must be restricted to accessible memory ranges.
- No experiment has been run on the user's machine from this repository.

## Safe local checks
`cmake --build build-p620 --parallel` builds all binaries.
`build-p620/bin/reverse_functions -h` intentionally exits nonzero for
this uncalibrated target. Observe the available hugepages and NUMA settings
before running any timing binaries. Do not run contemporaneously with other
memory-intensive experiments.

## Next review work
Implement an explicit, validated topology and SPA normalization API,
bounded candidate generation, independent timing calibration, full GF(2)
rank validation, and raw-data provenance. Do not enable automatic inference
until these are tested on P620.
