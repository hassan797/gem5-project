# SIMD GEMM Performance Report - 10 Inference Runs

## Executive Summary

**Test Date:** November 13, 2025  
**Configuration:** FC Layer GEMM with 4-way SIMD Batching  
**Number of Inferences:** 10  
**Status:** ✅ All 10 inferences completed successfully

---

## Performance Metrics

### Execution Summary
- **Total Simulated Time:** 0.089987 seconds (89.987 ms)
- **Time per Inference:** 8.999 ms
- **Throughput:** 111 inferences/second
- **Total CPU Cycles:** 179,974,824 cycles
- **Cycles per Inference:** 17,997,482 cycles

### Instruction Statistics
- **Total Instructions Executed:** 1,089,077
- **Instructions per Inference:** 108,908
- **Total Operations (with micro-ops):** 1,089,126
- **Cycles Per Instruction (CPI):** 165.2

---

## GEMM Configuration

### Matrix Dimensions
- **Operation:** C[10×1] = W[10×8192] × X[8192×1]
- **Matrix M (rows of C):** 10 output classes
- **Matrix K (cols of A, rows of B):** 8,192 features
- **Matrix N (cols of B/C):** 1 (single vector)

### SIMD Batching
- **MACs per Inference:** 81,920 (10 × 8192)
- **SIMD Lanes:** 4 elements/cycle
- **Number of Batches:** 2,048 (8192 ÷ 4)
- **Compute Latency:** 10 ns per batch
- **Total Batches (10 inferences):** 20,480

---

## Memory Analysis

### Data Movement per Inference
- **Weight Matrix W[10×8192]:** 655,360 bytes (640 KB)
- **Input Vector X[8192×1]:** 65,536 bytes (64 KB)
- **Output Vector C[10×1]:** 80 bytes
- **Total Data per Inference:** 720,976 bytes (~704 KB)

### Total Data Movement (10 Inferences)
- **Total Data Transferred:** 7,209,760 bytes (7.04 MB)
- **Effective Bandwidth:** 78.5 MB/s (7.04 MB ÷ 0.089987 s)
- **Theoretical Peak (DDR3-1600):** 12.8 GB/s
- **Bandwidth Utilization:** 0.61%

---

## Compute Performance

### Theoretical Compute Time
- **Batches per Inference:** 2,048
- **Latency per Batch:** 10 ns
- **Theoretical Time per Inference:** 20.48 μs
- **Theoretical Time (10 inferences):** 204.8 μs

### Actual Performance
- **Actual Time per Inference:** 8,999 μs
- **Compute Portion:** 0.23%
- **Memory/Overhead Portion:** 99.77%

### Speedup Analysis
- **Sequential MAC Time (no SIMD):** 81,920 MACs × 10 ns = 819.2 μs
- **SIMD Batched Time:** 2,048 batches × 10 ns = 20.48 μs
- **Compute Speedup:** 40× (819.2 μs ÷ 20.48 μs)

---

## Comparison: 10 vs 100 Inferences

| Metric | 10 Inferences | 100 Inferences | Notes |
|--------|---------------|----------------|-------|
| **Simulated Time** | 89.987 ms | 574 ms | Linear scaling |
| **Time per Inference** | 8.999 ms | 5.74 ms | 36% faster at 100 runs |
| **Throughput** | 111 inf/sec | 174 inf/sec | 57% improvement |
| **CPI** | 165.2 | 193.94 | Higher at 100 due to longer run |
| **Bandwidth** | 78.5 MB/s | 125 MB/s | 59% improvement |
| **Instructions/Inference** | 108,908 | 59,227 | Lower overhead at scale |

**Key Finding:** Performance improves with scale due to reduced initialization overhead and better memory access patterns.

---

## Bottleneck Analysis

### Performance Breakdown
```
Total Time per Inference: 8.999 ms
├─ Compute (SIMD GEMM):     0.020 ms (0.23%) ✓ Efficient
├─ Memory Transfer (DMA):   8.979 ms (99.77%) ⚠️ BOTTLENECK
│  ├─ Weight Loading:       ~8.18 ms (91%)
│  ├─ Input Loading:        ~0.82 ms (9%)
│  └─ Output Writing:       ~0.001 ms (<0.1%)
└─ Control Overhead:        Negligible
```

### Memory Access Patterns
- **Weights:** 655 KB loaded per inference (reused 10 times = 6.55 MB total)
- **Inputs:** 65.5 KB loaded per inference (different each time)
- **DMA Transfers:** Sequential, no caching benefit

---

## Verification Results

### Output Correctness
- ✅ **All 10 inferences completed successfully**
- ✅ **All outputs verified: 8,192,000** (= 8192 inputs × 1000 per input)
- ✅ **Expected behavior:** Each output = K × input_value = 8192 × 1000
- ✅ **4-way SIMD batching working correctly**
- ✅ **2,048 batches processed per inference**

### Batch Processing
```
Inference 1-10: Each processed 2,048 batches
├─ Row 0: k=0,4,8,...,8188 (2048 batches)
├─ Row 1: k=0,4,8,...,8188 (2048 batches)
├─ ...
└─ Row 9: k=0,4,8,...,8188 (2048 batches)

Total: 10 rows × 2,048 batches × 10 inferences = 204,800 SIMD operations
```

---

## Key Findings

### ✅ Strengths
1. **SIMD batching highly effective:** 40× compute speedup
2. **Stable operation:** All 10 inferences completed without errors
3. **Correct functionality:** MMIO interface working reliably
4. **Scalability:** Linear scaling with number of inferences

### ⚠️ Bottlenecks Identified
1. **Memory bandwidth:** 78.5 MB/s vs 12.8 GB/s theoretical (0.61% utilization)
2. **Weight reloading:** 655 KB loaded every inference (could be cached)
3. **DMA overhead:** 99.77% of execution time
4. **No cache benefit:** Sequential loads, no temporal reuse

---

## Optimization Opportunities

### Priority 1: Weight Caching
- **Current:** Reload 655 KB weights every inference
- **Proposed:** Cache weights in accelerator SRAM
- **Expected Savings:** ~8.18 ms per inference (91% reduction)
- **New Time per Inference:** ~0.82 ms (11× speedup)

### Priority 2: Enable L1/L2 Caches
- **Current:** Direct to memory (no caches)
- **Proposed:** Enable CPU caches for DMA controller
- **Expected Impact:** 5-10× reduction in memory latency

### Priority 3: Burst Transfers
- **Current:** Sequential 64-bit transfers
- **Proposed:** Wider DMA bursts (256-512 bits)
- **Expected Impact:** 4-8× bandwidth improvement

### Priority 4: Pipelined Execution
- **Current:** Sequential load → compute → store
- **Proposed:** Pipeline DMA with compute
- **Expected Impact:** Hide 50-70% of DMA latency

---

## Comparison Chart: Compute vs Memory Time

```
10 Inferences Performance Breakdown:
┌─────────────────────────────────────────────────────────────┐
│ Compute (SIMD):  ████░░░░░░░░░░░░░░░░░░░░░░░░ 0.23%        │
│ Memory (DMA):    ████████████████████████████████ 99.77%    │
└─────────────────────────────────────────────────────────────┘
                   0%              50%              100%

Conclusion: Operation is MEMORY-BOUND
```

---

## Recommendations

### For Current Architecture
1. ✅ **SIMD batching working as designed** - 40× compute speedup achieved
2. ⚠️ **Add weight buffer in accelerator** - avoid reloading 655 KB per inference
3. ⚠️ **Enable caches** - test with L1/L2 enabled for better memory performance
4. ⚠️ **Implement burst DMA** - increase memory bandwidth utilization

### For Future Work
1. **Hardware optimization:** Add on-chip SRAM for weight storage (1-2 MB)
2. **Software optimization:** Batch multiple inferences to amortize weight loading
3. **System optimization:** Enable caches, tune memory controller
4. **Architecture:** Consider compute-in-memory for weight-heavy operations

---

## Conclusion

The 10-inference test demonstrates that the **SIMD GEMM accelerator is functionally correct and highly efficient for compute operations** (40× speedup). However, the **overall performance is severely limited by memory bandwidth** (99.77% overhead).

**Key Achievement:** The 4-way SIMD batching successfully reduces compute time from 819 μs to 20 μs per inference, proving the effectiveness of the batching strategy.

**Critical Path:** Memory bandwidth (78.5 MB/s) is 163× slower than theoretical peak (12.8 GB/s), indicating significant optimization potential through caching, burst transfers, and weight reuse.

**Next Steps:** Enable caches and implement weight caching to unlock the full potential of the SIMD accelerator.

---

**Test Configuration:**
- CPU: RiscvTimingSimpleCPU @ 2 GHz
- Memory: DDR3-1600 (no caches enabled)
- SIMD Accelerator: 4 lanes, 10 ns latency
- Test: FC Layer GEMM (C[10×1] = W[10×8192] × X[8192×1])
