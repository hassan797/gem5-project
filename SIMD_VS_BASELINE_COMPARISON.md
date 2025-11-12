# SIMD vs Baseline Comparison - Test Plan

## Overview
Comparing SIMD accelerated GEMM performance against baseline sequential computation.

## Test Configuration

### Common Settings (Both Tests)
- **CPU:** RiscvTimingSimpleCPU @ 2 GHz
- **Memory:** DDR3-1600 (12.8 GB/s theoretical bandwidth)
- **Caches:** Disabled (direct to memory)
- **Problem Size:** FC layer GEMM C[10×1] = W[10×8192] × X[8192×1]
- **Number of Inferences:** 10
- **Data:**
  - Weights: 81,920 elements (640 KB)
  - Input: 8,192 elements (64 KB)
  - Output: 10 elements

### SIMD Accelerated Version
- **Binary:** `test_fc_gemm_only.riscv`
- **Config:** `test_fc_simple.py`
- **Acceleration:** 4-way SIMD batching
- **Batches:** 2,048 (8192 ÷ 4)
- **Compute Latency:** 10 ns per batch (4 MACs in parallel)
- **Interface:** MMIO registers (0x40000000)

### Baseline Version (No Acceleration)
- **Binary:** `test_fc_baseline.riscv`
- **Config:** `test_fc_baseline.py`
- **Computation:** Sequential MACs (no parallelism)
- **Expected Time:** ~40× slower compute (no SIMD batching)

## Expected Results

### SIMD Version (Already Completed)
```
Total Time:          89.987 ms
Time/Inference:      8.999 ms
Throughput:          111 inf/sec
Bandwidth:           78.5 MB/s
Compute Time:        0.020 ms (0.23%)
Memory Time:         8.979 ms (99.77%)
```

### Baseline Version (Running...)
```
Expected Compute Time:   ~819 μs per inference (40× slower)
Expected Total Time:     Similar to SIMD (memory-bound!)
Expected Bottleneck:     Memory bandwidth (same as SIMD)
```

## Key Question

**Will baseline be 40× slower overall?**

**Answer:** NO! Because both are memory-bound:
- SIMD compute: 20 μs (0.23% of time)
- Baseline compute: 819 μs (~9% of time)
- Memory time: ~8.979 ms (dominates both)

**Expected slowdown:** ~10-15% slower (not 40×), because memory dominates.

## Comparison Metrics

We will compare:

1. **Total Simulated Time**
   - SIMD: 89.987 ms
   - Baseline: ? (running...)

2. **Time per Inference**
   - SIMD: 8.999 ms
   - Baseline: ? (running...)

3. **Compute vs Memory Breakdown**
   - SIMD: 0.23% compute, 99.77% memory
   - Baseline: ~9% compute, ~91% memory (expected)

4. **Instructions Executed**
   - SIMD: Fewer (offloaded to accelerator)
   - Baseline: More (all compute in CPU)

5. **CPU Cycles**
   - SIMD: Lower (less CPU work)
   - Baseline: Higher (CPU does all MACs)

6. **Memory Bandwidth**
   - SIMD: 78.5 MB/s
   - Baseline: Similar (same data movement)

## Why This Comparison Matters

This comparison will show:

1. **SIMD Effectiveness:** How much the 4-way batching helps
2. **Amdahl's Law in Action:** Memory bandwidth limits overall speedup
3. **Optimization Priority:** Confirms memory is the bottleneck
4. **Accelerator Value:** Quantifies benefit of hardware acceleration

## Current Status

- ✅ SIMD test completed (10 inferences in 89.987 ms)
- 🔄 Baseline test running in background (PID: 10065)
- ⏳ Waiting for baseline completion...

Monitor with: `tail -f test_fc_baseline_10_full.log`
Check status: `ps aux | grep 10065`

## Files

### SIMD Version
- Binary: `configs/mlperf/test_fc_gemm_only.riscv`
- Config: `configs/mlperf/test_fc_simple.py`
- Log: `test_fc_10_inferences.log`
- Report: `PERFORMANCE_REPORT_10_INFERENCES.md`

### Baseline Version  
- Binary: `configs/mlperf/test_fc_baseline.riscv`
- Config: `configs/mlperf/test_fc_baseline.py`
- Log: `test_fc_baseline_10_full.log` (running...)
- Report: (will be created after completion)

---

**Note:** The baseline test is running. Once complete, we will create a comprehensive comparison report showing SIMD vs Baseline performance.
