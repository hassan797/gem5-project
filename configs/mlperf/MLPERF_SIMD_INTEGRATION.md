# MLPerf Tiny + SIMD Accelerator Integration

**Date:** November 6, 2025  
**Status:** Ready to test  
**Integration:** SIMD accelerator with MLPerf Tiny benchmark

---

## Overview

We've integrated the MLPerf Tiny image classification benchmark with your custom SIMD accelerator that processes **4 elements per cycle**. This will allow us to measure the performance improvement when offloading compute-intensive operations to the accelerator.

---

## What We Created

### New Files

1. **`configs/mlperf/simple_ic_benchmark_simd.cpp`**
   - SIMD-accelerated version of the MLPerf benchmark
   - Uses custom RISC-V instructions to control the accelerator
   - Offloads multiply operations to SIMD hardware

2. **`configs/mlperf/mlperf_simd_se.py`**
   - gem5 configuration with SIMD accelerator instantiated
   - Connects accelerator's MMIO and DMA ports to system bus
   - Configurable CPU type and cache hierarchy

3. **`configs/mlperf/run_mlperf_simd.sh`**
   - Automated build and run script
   - Cross-compiles for RISC-V
   - Runs simulation
   - Provides comparison commands

---

## SIMD Accelerator Interface

### Custom Instructions (inline assembly)

```cpp
xcop_optype(op);   // Set operation: 0=elem-wise multiply, 1=GEMM
xcop_len(m);       // Set length (M dimension for GEMM)
xcop_dimk(k);      // Set K dimension for GEMM
xcop_dimn(n);      // Set N dimension for GEMM
xcop_srca(addr);   // Set source A address
xcop_srcb(addr);   // Set source B address
xcop_dst(addr);    // Set destination address
xcop_kick();       // Start operation
xcop_wait();       // Wait for completion
```

### MMIO Register Map

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | regSrcA | Source A address |
| 0x08 | regSrcB | Source B address |
| 0x10 | regDst | Destination address |
| 0x18 | regLen | Length/M dimension |
| 0x20 | regCmd | Command (bit0=start) |
| 0x28 | regStatus | Status (bit0=busy) |
| 0x30 | regOpType | Operation type |
| 0x38 | regDimK | K dimension |
| 0x40 | regDimN | N dimension |

### Accelerator Configuration

- **Base Address:** 0x40000000 (MMIO)
- **SIMD Lanes:** 4 (processes 4 elements in parallel)
- **Compute Latency:** 10ns per operation
- **Operations:**
  - opType=0: Element-wise multiply (A[i] * B[i])
  - opType=1: GEMM matrix multiply (C = A × B)

---

## Integration Strategy

### What We Accelerate

1. **Convolution Inner Loops** (opType=0)
   - Element-wise multiplications in conv layers
   - Input patches × weights
   - Benefit: 4x throughput for multiply operations

2. **Fully Connected Layer** (potential opType=1)
   - Can be structured as matrix multiply
   - Weight matrix × input vector
   - Benefit: Structured GEMM offload

### What Stays on CPU

- Control flow and loop management
- Memory address calculations
- Pooling operations (max pooling)
- Activation functions (ReLU, softmax)
- Non-contiguous memory accesses

---

## How to Use

### Option 1: Automated Script

```bash
# From gem5 root directory
./configs/mlperf/run_mlperf_simd.sh
```

This will:
1. Cross-compile the SIMD benchmark
2. Run simulation with SIMD accelerator
3. Show results and comparison commands

### Option 2: Manual Steps

```bash
# 1. Cross-compile
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
    configs/mlperf/simple_ic_benchmark_simd.cpp \
    -o configs/mlperf/simple_ic_benchmark_simd.riscv \
    -lm

# 2. Run with SIMD accelerator
./build/RISCV/gem5.opt configs/mlperf/mlperf_simd_se.py \
    --num-runs 10 \
    --cpu-type timing

# 3. Analyze results
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```

### Command-Line Options

```bash
# Different CPU models
--cpu-type timing    # Simple in-order (fastest simulation)
--cpu-type minor     # More realistic in-order
--cpu-type o3        # Out-of-order (slowest simulation)

# Enable caches
--with-caches        # Add L1/L2 cache hierarchy

# Vary runs
--num-runs 100       # Run 100 inferences instead of 10
```

---

## Performance Comparison

### Baseline (No SIMD)

```bash
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py --num-runs 10
```

Expected results:
- Sim time: ~0.05 seconds
- Cycles: ~98M
- IPC: ~0.32

### With SIMD Accelerator

```bash
./build/RISCV/gem5.opt configs/mlperf/mlperf_simd_se.py --num-runs 10
```

Expected improvements:
- Reduced cycles for multiply operations
- Faster inference time (depends on how much is offloaded)
- DMA traffic visible in stats

### Metrics to Compare

```python
# From m5out/stats.txt

# Overall performance
system.cpu.numCycles           # Total cycles
simSeconds                     # Simulated time
system.cpu.ipc                 # Instructions per cycle

# Accelerator usage
system.simd.*                  # SIMD-specific stats
system.membus.trans_dist       # Bus transactions

# Memory system
system.cpu.dcache.*            # D-cache stats
system.l2cache.*               # L2 stats (if enabled)
```

---

## Expected Bottlenecks

### 1. DMA Overhead
- Each accelerator invocation requires:
  - Setting up MMIO registers (xcop_* instructions)
  - DMA read of source data
  - Computation
  - DMA write of results
- For small operations, DMA overhead may exceed compute savings

### 2. Data Type Mismatch
- Accelerator works with `uint64_t` (8 bytes)
- Benchmark uses `float` (4 bytes) and `int8_t` (1 byte)
- Type conversion required (not yet implemented in this version)

### 3. Memory Layout
- Accelerator works best with contiguous arrays
- Convolution has non-contiguous access patterns
- May need data reformatting

### 4. Control Overhead
- Custom instructions have latency
- Frequent accelerator calls may not amortize overhead

---

## Future Optimizations

### 1. Batch Operations
Instead of accelerating individual multiply operations:
```cpp
// Bad: Call accelerator for each element
for (int i = 0; i < N; i++) {
    xcop_len(1);
    xcop_kick();
    xcop_wait();
}

// Good: Call accelerator for entire array
xcop_len(N);
xcop_kick();
xcop_wait();
```

### 2. Type Conversion Layer
Add float↔uint64_t conversion:
```cpp
void convert_float_to_uint64(float* src, uint64_t* dst, int len);
void convert_uint64_to_float(uint64_t* src, float* dst, int len);
```

### 3. Im2Col Transformation
Restructure convolution to use GEMM:
```cpp
// Transform input patches into columns
im2col(input, input_col);  // CPU

// Use GEMM for entire conv layer
xcop_optype(1);  // GEMM mode
xcop_len(M);     // Output channels
xcop_dimk(K);    // Kernel size
xcop_dimn(N);    // Number of patches
gemm(weights, input_col, output);  // Accelerator
```

### 4. Persistent Accelerator State
Reuse buffers across inferences:
```cpp
// Setup once
xcop_srca(weights_addr);  // Weights don't change

// Per inference
for (int inf = 0; inf < num_runs; inf++) {
    xcop_srcb(input_addr);     // Only update input
    xcop_kick();
    xcop_wait();
}
```

---

## Debugging Tips

### 1. Check Accelerator Connection

```bash
# In m5out/config.ini, verify:
[system.simd]
type=SimdAccel
pio_addr=1073741824    # 0x40000000
num_lanes=4
```

### 2. Enable Debug Output

```bash
# Add debug flags when running gem5
./build/RISCV/gem5.opt --debug-flags=SimdAccel \
    configs/mlperf/mlperf_simd_se.py
```

### 3. Verify Custom Instructions

```bash
# Disassemble the binary
riscv64-linux-gnu-objdump -d configs/mlperf/simple_ic_benchmark_simd.riscv \
    | grep -A5 "xcop"

# Should see .insn patterns like:
# .insn r 0x0B,0x0,0x01, x0,a0,x0
```

### 4. Test Accelerator Standalone

```bash
# Use the existing GEMM test
./build/RISCV/gem5.opt configs/example/simd_accel_se.py tests/gemm_test.riscv
```

---

## Troubleshooting

### Issue: Binary not found
```
ERROR: Binary not found: configs/mlperf/simple_ic_benchmark_simd.riscv
```
**Solution:** Run cross-compilation first (step in the script)

### Issue: Accelerator not instantiated
```
AttributeError: 'System' object has no attribute 'simd'
```
**Solution:** Check that SimdAccel is in your gem5 build:
```bash
grep -r "class SimdAccel" src/
```

### Issue: Custom instructions not recognized
```
Illegal instruction
```
**Solution:** Verify decoder.isa has the custom instruction definitions

### Issue: Simulation hangs
**Possible causes:**
- Accelerator never completes (check regStatus polling)
- DMA deadlock (check port connections)
- Infinite loop in benchmark code

**Debug:**
```bash
# Run with SimdAccel debug flags
./build/RISCV/gem5.opt --debug-flags=SimdAccel,Exec \
    configs/mlperf/mlperf_simd_se.py 2>&1 | less
```

---

## Files Modified/Created Summary

### New Files
- `configs/mlperf/simple_ic_benchmark_simd.cpp` - SIMD benchmark
- `configs/mlperf/mlperf_simd_se.py` - gem5 config with SIMD
- `configs/mlperf/run_mlperf_simd.sh` - Build/run script
- `configs/mlperf/MLPERF_SIMD_INTEGRATION.md` - This document

### Existing Files Referenced
- `src/dev/simd_accel.{cc,hh}` - Accelerator implementation
- `src/python/m5/objects/SimdAccel.py` - Python object
- `src/arch/riscv/isa/decoder.isa` - Custom instruction decoder
- `tests/gemm_test.c` - Reference test for accelerator

---

## Quick Start Checklist

- [ ] gem5 built for RISC-V: `build/RISCV/gem5.opt`
- [ ] RISC-V toolchain installed: `riscv64-linux-gnu-g++`
- [ ] SIMD accelerator files in `src/dev/`
- [ ] Custom instructions in `decoder.isa`
- [ ] Run build script: `./configs/mlperf/run_mlperf_simd.sh`
- [ ] Compare results with baseline
- [ ] Analyze stats: `python3 configs/mlperf/analyze_stats.py`

---

## Next Steps

1. **Run the benchmark** and collect baseline SIMD performance
2. **Profile the execution** to see accelerator utilization
3. **Optimize data layout** for better SIMD efficiency
4. **Implement batching** to reduce DMA overhead
5. **Add type conversion** for float operations
6. **Compare** SIMD vs baseline performance metrics

---

**Status:** Ready to run  
**Last Updated:** November 6, 2025
