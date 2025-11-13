# SIMD (4-cycle batch) vs Baseline (mul=4, add=1) — FC GEMM Comparison

This report compares a SIMD-accelerated FC GEMM against a pure-CPU baseline under matched compute latencies.

## Setup

- Workload: Fully-connected GEMM only
  - C[10×1] = W[10×8192] × X[8192×1]
  - Data type: 64-bit integers (uint64_t)
  - Runs: 10 inferences
- Clock: 2 GHz (500 ticks per cycle)
- Memory: DDR3_1600_8x8 (12.8 GB/s), no caches
- SE mode with 512 MiB memory; MMIO at 0x4000_0000

Compute latency settings:
- Baseline CPU: RiscvMinorCPU
  - Integer multiply latency: 4 cycles
  - Integer add latency: 1 cycle
  - Config script: `configs/mlperf/test_fc_baseline.py`
  - Binary: `configs/mlperf/test_fc_baseline.riscv`
  - Outdir: `m5out_baseline_mul4_add1`
- SIMD accelerator: 4 lanes (processes 4 elements/batch)
  - Compute latency per batch: 2 ns = 4 cycles @ 2 GHz
  - Config script: `configs/mlperf/mlperf_simd_se.py`
  - Binary: `configs/mlperf/test_fc_gemm_only.riscv` (MMIO-based)
  - Outdir: `m5out_simd_batch4cyc`

Notes:
- We used the GEMM-only binary for the SIMD path to avoid custom instruction dependencies and to match the baseline workload.
- Both runs are in SE mode with the same memory system and clock. The baseline models ALU latencies via MinorCPU; the SIMD compute latency is modeled inside the accelerator device.

## Results (from stats.txt)

- Baseline (mul=4, add=1):
  - simSeconds: 0.105593 s
  - numCycles: 211,186,492 cycles
  - simInsts: 6,293,037
- SIMD (batch=4 cycles):
  - simSeconds: 0.088318 s
  - numCycles: 176,635,741 cycles
  - simInsts: 1,067,169

Derived per-inference metrics (10 runs):
- Baseline
  - Time/run: 0.0105593 s
  - Cycles/run: 21,118,649
  - Throughput: 94.7 inferences/s
- SIMD
  - Time/run: 0.0088318 s
  - Cycles/run: 17,663,574
  - Throughput: 113.2 inferences/s

Speedup (SIMD vs Baseline):
- Time-based: 0.105593 / 0.088318 ≈ 1.20× faster
- Cycle-based: 211,186,492 / 176,635,741 ≈ 1.20× fewer cycles

## Memory traffic (selected counters)

- Baseline
  - mem_ctrl.readBursts: 3,604,186
  - mem_ctrl.writeBursts: 107,179
  - cpu.committed MemRead/MemWrite insts: 1,686,671 / 107,327
- SIMD
  - mem_ctrl.readBursts: 2,636,536
  - mem_ctrl.writeBursts: 106,933
  - cpu.committed MemRead/MemWrite insts: 175,923 / 107,063

Interpretation:
- The SIMD path uses DMA for bulk reads/writes, resulting in significantly fewer read bursts vs the baseline CPU performing many scalar loads. This reduces memory overhead and contributes to the observed speedup, even with matched compute latencies.
- Write traffic is similar due to the same output size and initialization patterns.

## Configuration snippets

- SIMD compute latency set in `configs/mlperf/mlperf_simd_se.py`:
  - `SimdAccel(... compute_latency="2ns")` (4 cycles @ 2 GHz per 4-element batch)
- Baseline FU latencies set via CLI in `configs/mlperf/test_fc_baseline.py`:
  - `--mul-latency 4 --add-latency 1`

## Validation

- Both runs completed successfully with 10 inferences; stats recorded in their respective outdirs.
- The SIMD binary `test_fc_gemm_only.riscv` uses MMIO (no custom ISA), ensuring compatibility in SE mode.

## Next steps (optional)

- Align host CPU model between runs (e.g., use MinorCPU for SIMD host too) to remove any residual CPU-model bias.
- Increase SIMD lanes to 8 to reach full 64B burst utilization and reduce read burst count further.
- Add L1 caches to the baseline and model SIMD-side scratchpad with double-buffering to explore overlap and bandwidth effects.

## Compute vs memory time breakdown

Method: We estimate pure compute time from configured arithmetic latencies, and attribute the remainder of the total simulated time to memory operations and software overhead (loop/control, MMIO, DMA progression). This yields an upper bound on compute share; the actual compute fraction may be smaller due to pipeline/issue overheads.

Constants and assumptions:
- Clock = 2 GHz → 1 cycle = 0.5 ns.
- FC workload per inference = 10 outputs × 8192 inputs = 81,920 MACs.
- Baseline compute per MAC: 1 multiply (4 cycles) + 1 add (1 cycle) ≈ 5 cycles/MAC, serialized due to dependency on sum.
- SIMD compute per batch (4 MACs in parallel): 4 cycles/batch. Batches per inference: (8192 / 4) × 10 = 20,480 batches.

Per-inference compute cost:
- Baseline compute cycles: 81,920 MACs × 5 cycles ≈ 409,600 cycles ⇒ 0.205 ms
- SIMD compute cycles: 20,480 batches × 4 cycles ≈ 81,920 cycles ⇒ 0.041 ms

Per-inference totals from stats:
- Baseline total time/run: 10.5593 ms (21,118,649 cycles)
- SIMD total time/run: 8.8318 ms (17,663,574 cycles)

Breakdown (time and percentage):
- Baseline
  - Compute time ≈ 0.205 ms → 1.94%
  - Memory + overhead ≈ 10.5593 − 0.205 = 10.3543 ms → 98.06%
- SIMD
  - Compute time ≈ 0.041 ms → 0.46%
  - Memory + overhead ≈ 8.8318 − 0.041 = 8.7908 ms → 99.54%

Notes:
- These compute-time estimates come directly from the configured latencies (MinorCPU FU opLat and accelerator compute_latency) and the known operation counts. They intentionally exclude instruction fetch/issue overhead, control flow, and memory access time.
- The large memory/overhead share reflects extensive data movement (CPU scalar loads in the baseline; DMA bursts and many small transactions in the SIMD path) plus software/MMIO control.
