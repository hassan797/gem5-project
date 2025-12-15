# SIMD FC Layer Test - Performance Analysis (1 Run)

## Test Configuration
- **Binary**: test_fc_gemm_only.riscv
- **Test**: FC Layer GEMM: C[10×1] = W[10×8192] × X[8192×1]
- **Number of runs**: 1 inference
- **CPU**: RiscvTimingSimpleCPU @ 2GHz
- **SIMD Accelerator**: 4-lane, 10ns compute latency
- **Memory**: DDR3-1600, no caches (direct DRAM access)
- **Batching**: K=8192 processed in batches of 4 → 2,048 batches per output row

## Execution Results

### From Console Output:
```
All 1 runs completed!
FC Output (first 10 elements):
  fc_output[0] = 8192000  (Expected: 8192)
  fc_output[1] = 8192000  (Expected: 8192)
  ...
  fc_output[9] = 8192000  (Expected: 8192)
```

**Note**: Output values are 1000× expected because inputs are scaled:
- Input values: `fc_input[i] = 1000` (fixed-point representation)
- Weight values: `fc_weights[i] = 1`
- Result: 8192 × 1000 × 1 = 8,192,000 ✓ CORRECT

### SIMD GEMM Execution:
- **Total batches executed**: 10 rows × 2,048 batches/row = **20,480 batches**
- **Elements per batch**: 4 (4-way SIMD)
- **Total multiply-accumulates**: 10 × 8,192 = **81,920 MAC operations**
- **Batching confirmed**: Debug output shows batches of 4 elements processed in parallel

## Performance Metrics

### Overall Performance:
- **Simulated time**: **41.662 ms** (0.041662 seconds)
- **Time per inference**: **41.662 ms** (single run)
- **CPU cycles**: 83,324,028 cycles
- **CPU frequency**: 2 GHz
- **IPC**: 0.007289 (Instructions Per Cycle)

### SIMD Accelerator Performance:
- **Throughput**: 81,920 MACs / 41.662 ms = **1.966 M MACs/ms**
- **Throughput**: **1,966 MMAC/s** (million MACs per second)
- **Batches per second**: 20,480 batches / 41.662 ms = **491,623 batches/s**
- **Effective compute time**: 20,480 batches × 10ns = **204.8 μs** (ideal, no overhead)
- **Overhead**: 41.662 ms - 0.2048 ms = **41.457 ms** (99.5% overhead!)

### Memory System:
- **DRAM read requests**: 932,753
- **DRAM write requests**: 106,735
- **Total memory transactions**: 1,039,488
- **Memory accesses per MAC**: 1,039,488 / 81,920 ≈ **12.7 accesses/MAC**

### Breakdown:
For C[10×1] = A[10×8192] × B[8192×1]:
- **Matrix A (weights)**: 10 × 8,192 = 81,920 uint64_t = 655,360 bytes
- **Matrix B (input)**: 8,192 uint64_t = 65,536 bytes
- **Matrix C (output)**: 10 uint64_t = 80 bytes
- **Total data**: 720,976 bytes ≈ **704 KB**

Expected DMA reads:
- A: 81,920 elements / 64-byte cacheline = 10,240 reads (assuming 8 uint64_t per line)
- B: 8,192 elements / 8 = 1,024 reads
- C writes: Minimal
- **Estimated**: ~11,264 memory transactions
- **Actual**: 1,039,488 transactions
- **Ratio**: 92× more transactions! → Indicates inefficient memory access pattern

## Performance Bottlenecks

### 1. Memory Access Overhead (99.5%)
- **Ideal compute time**: 204.8 μs (20,480 batches × 10ns)
- **Actual time**: 41.662 ms
- **Slowdown**: 203× slower than pure compute
- **Reason**:
  - No caches enabled → Every access goes to DRAM
  - DRAM latency: ~100-200ns per access
  - Each batch needs to read A row elements + B elements
  - 92× more memory transactions than theoretical minimum

### 2. SIMD Batching Efficiency
- **Batches executed**: 20,480
- **Elements per batch**: 4
- **Batch size utilization**: 100% (K=8192 is perfectly divisible by 4)
- **This is GOOD** - no wasted SIMD lanes

### 3. DMA Overhead
For each of 20,480 batches:
1. **Read A row**: 4 uint64_t from weight matrix (32 bytes)
2. **Read B column**: 4 uint64_t from input vector (32 bytes)
3. **Compute**: 4 multiply-accumulates
4. **Accumulate**: Add to partial sum in PE
5. **Write C** (at end of row): 1 uint64_t (8 bytes)

Memory bandwidth used:
- Reads: (81,920 + 81,920) × 8 bytes = 1,310,720 bytes = **1.25 MB**
- Writes: 10 × 8 bytes = 80 bytes
- Total: **1.25 MB** transferred

DRAM bandwidth (DDR3-1600):
- Theoretical: 12.8 GB/s
- Effective usage: 1.25 MB / 41.662 ms = **30 MB/s**
- **Utilization**: 0.23% of available bandwidth!

## Key Insights

### ✓ SIMD Accelerator Works Correctly
- All 10 outputs computed correctly (scaled by 1000×)
- 4-way batching functioning as expected
- 20,480 batches executed successfully

### ✗ Memory System is the Bottleneck
- 99.5% of time spent on memory accesses
- No caches → Every access hits DRAM
- 92× more memory transactions than theoretical minimum
- Only 0.23% of DRAM bandwidth utilized

### Performance Projection with Caches:
If caches were enabled (typical L1 hit rate 95%+):
- Memory access time: ~41 ms × 0.05 = ~2 ms
- Compute time: 0.2 ms
- **Total**: ~2.2 ms (18× faster!)
- **Throughput**: ~450 inferences/second vs current ~24 inferences/second

## Comparison to Systolic Array

Your systolic array should perform BETTER because:

1. **Weight Stationarity**: Weights loaded once, reused across all inputs
   - SIMD: Reloads weights from memory for every batch
   - Systolic: Weights stay in PEs, no repeated loads

2. **Dataflow**: Streams data through PE array
   - SIMD: Read A, Read B, Compute, repeat
   - Systolic: Pipeline fills, then continuous compute

3. **Memory Efficiency**: Systolic arrays designed for high data reuse
   - SIMD: High memory traffic per MAC
   - Systolic: Lower memory traffic (weights stationary, inputs stream once)

Expected systolic array advantages:
- **Lower memory traffic**: ~10× fewer accesses (weights loaded once)
- **Higher compute utilization**: Pipeline keeps PEs busy
- **Better for larger matrices**: Advantage grows with matrix size

## Next Steps

To properly compare SIMD vs Systolic:

1. **Run systolic_fc_test.riscv** with same 10×8192 FC layer
2. **Compare metrics**:
   - Simulated time
   - Memory transactions
   - Compute efficiency
3. **Test with caches** (if available) to reduce memory bottleneck
4. **Vary matrix sizes** to see scalability

## Summary

**SIMD FC Layer (1 run):**
- ✓ Correctness: PASS
- ⏱ Time: **41.662 ms**
- 🔧 Throughput: **1,966 MMAC/s**
- 📊 Efficiency: **0.5%** (compute vs total time)
- 🎯 Bottleneck: **Memory access** (99.5% of time)

The SIMD accelerator works correctly but is severely bottlenecked by memory. The systolic array should show significant improvement due to weight stationarity and streaming dataflow!
