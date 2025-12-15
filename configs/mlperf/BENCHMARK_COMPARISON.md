# Difference Between simple_ic_benchmark_simd.cpp and test_fc_gemm*.cpp

## **simple_ic_benchmark_simd.cpp** - Full MLPerf Pipeline
This is the **COMPLETE MLPerf Tiny Image Classification benchmark** with all layers:

### What it does:
1. **Conv1 Layer**: 32×32×3 → 32×32×32 (3D convolution with SIMD acceleration)
   - Uses SIMD element-wise multiply (opType=0) for inner loops
   - Processes 4 elements per cycle
   - Output: 32,768 float values (32×32×32)

2. **Pool1 Layer**: 32×32×32 → 16×16×32 (2×2 max pooling)
   - CPU-based (no acceleration)
   - Downsamples spatial dimensions
   - Output: 8,192 float values (16×16×32)

3. **FC Layer**: 8,192 → 10 (fully connected classification)
   - **GEMM**: C[10×1] = W[10×8192] × X[8192×1]
   - Uses SIMD GEMM acceleration (opType=1)
   - 4-way batching: K=8192 processed in batches of 4
   - Output: 10 class scores

4. **Softmax**: 10 → 10 (probability distribution)
   - CPU-based
   - Final classification probabilities

### Characteristics:
- **Input**: 3,072 bytes (32×32×3 RGB image)
- **Output**: 10 class probabilities
- **Acceleration**: SIMD for Conv1 multiply + FC GEMM
- **Runtime**: SLOW (~minutes) because Conv1 is computationally heavy
  - Conv1 has nested loops: 32×32×32 outputs × 3×3×3 kernel = ~9 million operations
  - Pool1 is simple max operations
  - FC is 10×8192 = 81,920 multiply-adds
- **Purpose**: End-to-end inference pipeline (realistic MLPerf workload)

---

## **test_fc_gemm.cpp** - GEMM Correctness Test Only
This tests **ONLY the FC/GEMM layer** in isolation (single run):

### What it does:
1. **GEMM ONLY**: C[10×1] = W[10×8192] × X[8192×1]
   - Allocates matrices
   - Initializes weights = 1, inputs = 1
   - Configures SIMD accelerator
   - Kicks GEMM operation
   - Verifies output (expects all outputs = 8192)

### Characteristics:
- **Input**: Just weight matrix and input vector (allocated in code)
- **Output**: 10 values
- **Acceleration**: SIMD GEMM only
- **Runtime**: FAST (~seconds) - just one GEMM operation
- **Purpose**: Verify SIMD GEMM correctness in isolation
- **Runs**: 1 inference

---

## **test_fc_gemm_only.cpp** - GEMM Performance Test
This is the **performance benchmark** version (multiple runs):

### What it does:
1. **Same as test_fc_gemm.cpp** but repeated 100 times (default)
   - Each run: Configure → Kick → Wait → Verify
   - Prints progress every 10 runs
   - Measures sustained throughput

### Characteristics:
- **Same operation** as test_fc_gemm.cpp
- **Runs**: 100 inferences (configurable via command line)
- **Runtime**: ~100× longer than single GEMM
- **Purpose**: Measure average time per inference
- **Interface**: MMIO registers (not custom instructions)

---

## Key Differences Summary

| Feature | simple_ic_benchmark_simd.cpp | test_fc_gemm.cpp | test_fc_gemm_only.cpp |
|---------|------------------------------|------------------|------------------------|
| **Scope** | Full MLPerf pipeline | GEMM only | GEMM only |
| **Layers** | Conv1 + Pool1 + FC + Softmax | FC only | FC only |
| **Acceleration** | Conv1 (SIMD) + FC (GEMM) | FC (GEMM) | FC (GEMM) |
| **Input** | 32×32×3 RGB image (3072 bytes) | Allocated matrices | Allocated matrices |
| **Operations** | ~9M (Conv) + 81K (FC) | 81,920 (FC only) | 81,920 × 100 runs |
| **Runtime** | **SLOW** (minutes) | **FAST** (seconds) | Medium (100× GEMM) |
| **Purpose** | End-to-end inference | Correctness test | Performance test |
| **Runs** | 1 (default) | 1 | 100 (default) |
| **Interface** | Custom instructions | Custom instructions | MMIO registers |
| **Output** | 10 class probabilities | 10 GEMM outputs | 10 GEMM outputs |

---

## Why simple_ic_benchmark_simd is Slow

When you ran `simple_ic_benchmark_simd.riscv` and saw:
```
Running inference with SIMD acceleration...
  Conv1: 32x32x32 outputs (with SIMD)...
```

It **hung on Conv1** because:

### Conv1 Computation Complexity:
- **Output size**: 32×32×32 = 32,768 elements
- **Kernel size**: 3×3×3 = 27 multiplications per output
- **Total operations**: 32,768 × 27 = **884,736 multiply-accumulate operations**
- Each multiply is offloaded to SIMD, but there's overhead for:
  - Setting up SIMD for each batch (xcop_* instructions)
  - DMA transfers (reading A, B, writing C)
  - Polling status registers

Even with SIMD acceleration (4 elements/cycle), this takes:
- 884,736 / 4 = 221,184 batches
- Plus memory access time for each operation
- **Estimated time**: Several minutes in gem5 simulation

### FC Layer (for comparison):
- **Total operations**: 10 × 8,192 = 81,920 multiply-adds
- With SIMD: 81,920 / 4 = 20,480 batches
- **~10× less work** than Conv1

---

## What You Should Run

For **quick SIMD testing** focused on FC/GEMM:

### Option 1: Test FC GEMM Only (Need to compile)
```bash
# Single run correctness test (~seconds)
./build/RISCV/gem5.opt configs/mlperf/test_fc_simple.py \
  --binary=configs/mlperf/test_fc_gemm.riscv \
  --num-runs 1

# Performance test (100 runs, ~minutes)
./build/RISCV/gem5.opt configs/mlperf/test_fc_simple.py \
  --binary=configs/mlperf/test_fc_gemm_only.riscv \
  --num-runs 100
```

### Option 2: Full Pipeline (if you want end-to-end results)
```bash
# WARNING: This will take a LONG time (10-30 minutes per inference)
./build/RISCV/gem5.opt configs/mlperf/mlperf_simd_se.py \
  --num-runs 1 \
  --cpu-type timing
```

---

## Recommendation for Your Systolic Array

Since you want to test **FC layer only** (like MLPerf but fast), you should:

1. **Follow the test_fc_gemm pattern** (not simple_ic_benchmark)
2. **Your systolic_fc_test.c is perfect** - it's the systolic equivalent of test_fc_gemm.cpp
3. **Runtime comparison**:
   - test_fc_gemm.riscv: ~seconds (SIMD GEMM only)
   - systolic_fc_test.riscv: ~seconds (Systolic GEMM only)
   - simple_ic_benchmark_simd.riscv: ~minutes (Full pipeline with Conv1)

The test you created (`systolic_fc_test.c`) is the right approach - testing just the FC/GEMM layer in isolation is much faster and focuses on your accelerator!
