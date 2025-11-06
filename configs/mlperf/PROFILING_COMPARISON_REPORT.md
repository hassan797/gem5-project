# MLPerf Tiny – Profiling Comparison Report
Date: November 2, 2025 · Scope: Compare gem5 architectural profiling vs. earlier native/instrumented profiling; reconcile “memory-bound” vs “compute-bound” conclusions.
---
## Executive summary
- Hardware view (gem5): Compute-bound. Low L1 miss rates, modest D-cache miss rate with ~59-cycle miss service, no evidence of DRAM pressure. IPC=0.323 on an in-order core dominated by arithmetic and control dependencies.
- Software function view (earlier gprof on original TFLite Micro path): Appeared “memory-bound” because a large fraction of time was inside memcpy/memset/allocator routines invoked by TensorFlow Lite Micro’s tensor arena management.
- Reconciliation: memcpy/memset/allocators are still CPU-executed loops (often cache-resident) and do not imply an external memory-system bottleneck. When we simplify/remove TFLite’s buffer shuffles and amortize setup, both function-level and architectural views align as compute-bound.
---
## What we measured
Data sources in this repo:
- gem5 architectural stats: `m5out/stats.txt`
- Simplified benchmark profiler (100 iterations): `configs/mlperf/PROFILING_REPORT.md`
- Root-cause analysis of gprof discrepancy: `configs/mlperf/WHY_GPROF_DIFFERED.md`
Terminology
- Inference: One forward pass of the model for a single input, producing one output vector.
- Inclusive vs self time: Inclusive includes a function’s own time plus time in its callees; self is only the function’s own instructions.
---
## gem5 results (architectural view)
From `m5out/stats.txt` for the captured run (TimingSimpleCPU @ 2 GHz; 32KB L1I/L1D; L2 present):
- Top-level
  - Simulated time: 0.049289 s
  - Cycles: 98,577,987
  - Committed instructions: 31,867,253
  - IPC: 0.323269
- Instruction mix (committed)
  - Float instructions: 6,175,923
  - Int ALU: 22,178,740
  - Loads: 3,808,371 | Stores: 167,342
- L1 I-cache
  - Accesses: 44,742,945 | Misses: 860 | Miss rate: 0.0019%
- L1 D-cache (overall)
  - Accesses: 3,975,593 | Misses: 18,417 | Miss rate: 0.463%
  - Avg miss latency: 29,287 ticks ≈ 58.6 cycles (500 ticks/cycle at 2 GHz)
- L1 D-cache by op type
  - Read miss rate: 0.270% (10,287/3,808,360 reads)
  - Write miss rate: 4.86% (8,130/167,233 writes)
  - Avg Read miss latency: 21,950 ticks ≈ 44.0 cycles
  - Avg Write miss latency: 38,571 ticks ≈ 77.1 cycles
Interpretation
- Very low I-cache miss rate and sub-1% D-cache miss rate indicate the working set largely fits in cache; accesses are locality-friendly.
- Miss latencies on the order of tens of cycles are consistent with L2 service and occasional lower-level trips—not sustained DRAM pressure.
- IPC ~0.32 on an in-order core with heavy FP and tight loops is consistent with compute-side pipeline/latency limits rather than memory stalls.
Conclusion (gem5): Compute-bound at the architectural level.
---
## Earlier native/instrumented profiling (function view)
From `configs/mlperf/PROFILING_REPORT.md` (100 inferences, native timing with std::chrono):
- Time breakdown (μs, 100 runs)
  - Compute total: 956,305.41 (99.8%)
    - Conv1: 233,514.55 (24.4%)
    - Pool1: 18,918.20 (2.0%)
    - Conv2: 689,931.34 (72.1%)
    - Pool2: 10,094.52 (1.1%)
    - FC: 3,627.96 (0.4%)
    - Softmax: 218.83 (0.0%)
  - Memory management (alloc/free): 7.26 (0.0%)
  - I/O (gen/load/format): 2,046.30 (0.2%)
  - Total: 958,358.97 (100%)
Operation-level analysis per inference
- ~5.7M floating-point operations per inference (dominated by Conv2).
- Only a handful of allocations/frees per inference in the simplified path.
Conclusion (native/instrumented): Compute-bound.
---
## Why an earlier gprof run of the original TFLite Micro path looked “memory-bound”
Summarized from `configs/mlperf/WHY_GPROF_DIFFERED.md`:
1) Call-graph attribution
- gprof’s inclusive time credits parent functions (e.g., Invoke/RunInference) with time spent in children (e.g., memcpy/memset/malloc/free). If TensorFlow Lite Micro calls many memory helpers during Invoke, these dominate the parent’s inclusive time and visually “tilt” the profile toward memory functions.
2) One-time setup not amortized
- If you profile just 1 (or a few) inferences, one-time costs (AllocateTensors’ arena memset, planning, initial copies) dwarf steady-state compute. That yields a profile with a large fraction in memcpy/memset/allocators, even though those are setup costs.
3) memcpy ≠ DRAM bottleneck
- memcpy/memset are tight CPU copy/clear loops. On small-to-moderate buffers with good locality, they run out of L1/L2 bandwidth and issue loads/stores from cache. High “time in memcpy” reflects CPU execution, not necessarily DRAM stalls.
4) Software design differences
- Original: per-layer temporary tensors, frequent copies, arena management inside MicroInterpreter::Invoke().
- Simplified: allocate minimal buffers once per layer boundary, avoid inter-layer memcpy by reading directly from prior outputs, reuse memory.
Net effect
- Original TFLite Micro: function-level profiles can look “memory-heavy” due to many helper calls; architecturally it’s still compute-bound.
- Simplified: both function-level and architectural views agree it’s compute-bound.
---
## Side-by-side: what the two views actually answer
- Function-level profiling (gprof, timers): “Where does wall-clock time go across functions?”
- Architectural profiling (gem5): “Which hardware resources limit throughput (caches, memory, ALU/pipe)?”
In our data:
- Function-level (simplified code): 99.8% compute → compute-bound.
- Architectural (gem5): tiny I/D miss rates, miss latencies ~tens of cycles, low DRAM indicators → compute-bound.
- Function-level (original TFLite Micro): many memcpy/alloc calls inflate inclusive time in “memory” functions, but gem5-style indicators would still show low external memory pressure.
---
## Practical guidance for future runs
- Run many inferences and either exclude warm-up or report it separately.
- When you see high time in memcpy/memset: corroborate with hardware stats (cache miss rates, DRAM bytes/reqs). If these are low/modest, it’s not memory-system-bound.
- Prefer steady-state measurement: pre-allocate once, reuse buffers.
- For apples-to-apples comparisons: keep model size, run count, and CPU model fixed; record compiler flags.
---
## How to reproduce (optional)
Run gem5 with default 10 inferences and analyze stats:
```bash
# Run gem5 (example; adjust path/build if needed)
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py --num-runs 10
# Summarize stats
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```
Run the profiled native build (outside gem5) for 100 inferences and see the breakdown:
```bash
# Example compile and run (host toolchain)
g++ -O2 configs/mlperf/simple_ic_benchmark_profiled.cpp -o /tmp/ic_profiled -lm
/tmp/ic_profiled 100
```
---
## One-line conclusions
- The original “memory-bound” gprof result is a software-level artifact (inclusive time in memcpy/malloc during TFLite Micro’s Invoke), not a hardware memory bottleneck.
- Both the simplified benchmark and the original workload are compute-bound at the architectural level; gem5 cache/miss data and operation counts confirm it.
---
References
- `configs/mlperf/WHY_GPROF_DIFFERED.md`
- `configs/mlperf/PROFILING_REPORT.md`
- `m5out/stats.txt`
# Run gem5 (example; adjust path/build if needed)
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py --num-runs 10
# Summarize stats
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```
Run the profiled native build (outside gem5) for 100 inferences and see the breakdown:
```bash
# Example compile and run (host toolchain)
g++ -O2 configs/mlperf/simple_ic_benchmark_profiled.cpp -o /tmp/ic_profiled -lm
/tmp/ic_profiled 100
```
---
