# SIMD FC GEMM Test Structure Summary

## Test Files Overview

### 1. **test_fc_gemm.cpp** (Single Run Test)
- **Purpose**: Single inference test to verify SIMD GEMM correctness
- **Number of runs**: **1 inference** (single execution)
- **GEMM Operation**: `C[10×1] = W[10×8192] × X[8192×1]`
- **What happens in the run**:
  1. Allocates matrices (weights, input, output)
  2. Initializes all weights = 1, all inputs = 1
  3. Configures SIMD accelerator via custom instructions
  4. Kicks off GEMM operation
  5. Waits for completion
  6. Verifies output (expects each output = 8192)
  7. Prints PASS/FAIL
- **Expected batching**: K=8192 processed in batches of 4 → 2,048 batches per output
- **Interface**: Custom RISC-V instructions (xcop_*)

### 2. **test_fc_gemm_only.cpp** (Performance Test)
- **Purpose**: Multiple inference runs for performance measurement
- **Number of runs**: **100 inferences** (default, configurable via command line)
  - Can be changed: `./test_fc_gemm_only.riscv <num_runs>`
  - Example: `./test_fc_gemm_only.riscv 10` for 10 runs
  - Example: `./test_fc_gemm_only.riscv 1000` for 1000 runs
- **GEMM Operation**: Same as above - `C[10×1] = W[10×8192] × X[8192×1]`
- **What happens in each run**:
  1. **Run 0-99** (100 total):
     - Reset output buffer to zero
     - Configure SIMD accelerator via MMIO registers
     - Write all register values (srca, srcb, dst, len, dimk, dimn, optype)
     - Kick operation (write to KICK register)
     - Wait for completion (poll STATUS register)
     - Progress printed every 10 runs: "Completed 10/100 runs..."
  2. After all runs: Verify final output correctness
- **Interface**: MMIO registers at 0x40000000
  - SIMD_SRCA_OFFSET (0x00): Weight matrix address
  - SIMD_SRCB_OFFSET (0x08): Input vector address
  - SIMD_DST_OFFSET (0x10): Output vector address
  - SIMD_LEN_OFFSET (0x18): M dimension (10)
  - SIMD_DIMK_OFFSET (0x38): K dimension (8192)
  - SIMD_DIMN_OFFSET (0x40): N dimension (1)
  - SIMD_OPTYPE_OFFSET (0x30): Operation type (1 = GEMM)
  - SIMD_KICK_OFFSET (0x20): Start operation
  - SIMD_STATUS_OFFSET (0x28): Check completion

### 3. **test_fc_baseline.cpp** (CPU Baseline)
- **Purpose**: Sequential CPU computation for comparison
- **Number of runs**: **10 inferences** (default, configurable)
- **Operation**: Same GEMM computed on CPU (no acceleration)
- **What happens in each run**:
  1. Triple nested loop: for M, for N, for K
  2. Each element computed as: `C[i] += W[i*K+k] * X[k]`
  3. No accelerator involved - pure CPU arithmetic

---

## Python Configuration Files

### 1. **test_fc_simple.py**
- Configures gem5 system with SIMD accelerator
- System specs:
  - CPU: RiscvTimingSimpleCPU @ 2GHz
  - Memory: 512MB, no caches (direct DRAM access)
  - SIMD Accelerator: 4 lanes, 10ns compute latency
  - MMIO region: 0x40000000 (uncacheable)
- Default binary: `test_fc_gemm_only.riscv`
- Default runs: 100 (passed as command line arg to binary)
- DMA enabled for data transfers

### 2. **test_fc_baseline.py**
- Configures gem5 system **WITHOUT SIMD** (baseline comparison)
- System specs:
  - CPU: RiscvMinorCPU @ 2GHz (in-order, pipelined)
  - Memory: 512MB DDR3-1600, no caches
  - **Configurable arithmetic latencies**:
    - Integer multiply: 3 cycles (default)
    - Integer add: 1 cycle (default)
- Default binary: `test_fc_baseline.riscv`
- Default runs: 10
- Purpose: Measure CPU-only performance with realistic functional unit delays

### Command Examples:
```bash
# SIMD accelerated (100 runs)
./build/RISCV/gem5.opt configs/mlperf/test_fc_simple.py \
  --binary=configs/mlperf/test_fc_gemm_only.riscv \
  --num-runs 100

# CPU baseline (10 runs, 3 cycle multiply, 1 cycle add)
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
  --binary=configs/mlperf/test_fc_baseline.riscv \
  --num-runs 10 \
  --mul-latency 3 \
  --add-latency 1
```

---

## Simulation Output Location

### Default Output Directory: **`m5out/`**
Generated automatically by gem5 in the current working directory.

### Key Output Files:

1. **`m5out/stats.txt`** - Main performance statistics file
   - Simulation time (seconds)
   - CPU cycles
   - Instructions executed
   - IPC (instructions per cycle)
   - Cache statistics (L1I, L1D, L2 if enabled)
   - Memory controller stats (DRAM reads/writes)
   - Accelerator-specific stats:
     - `system.simd_accel.numOperations` - Total GEMM operations
     - `system.simd_accel.totalLatency` - Total compute cycles
     - `system.simd_accel.dmaReads/dmaWrites` - Data transfers

2. **`m5out/config.ini`** - Complete system configuration
   - All component parameters
   - Memory hierarchy
   - Clock domains
   - Device addresses

3. **`m5out/config.json`** - JSON version of config

4. **Console output** - Printed to terminal
   - Test progress ("Completed X/Y runs...")
   - Verification results (PASS/FAIL)
   - Final performance summary

### Analysis Tool: **`analyze_stats.py`**
```bash
# Parse stats and print human-readable summary
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```
Outputs:
- Overall performance (sim time, cycles, instructions, IPC, MIPS)
- Cache hit rates (L1I, L1D, L2)
- Memory system stats (DRAM reads/writes in KB)

### Custom Output Directory:
```bash
# Use --outdir to specify different output location
./build/RISCV/gem5.opt --outdir=results/simd_100runs \
  configs/mlperf/test_fc_simple.py --num-runs 100
```

---

## Run Configuration Summary

| Test Type | Binary | Default Runs | Purpose | Accelerator |
|-----------|--------|--------------|---------|-------------|
| **Correctness** | test_fc_gemm.riscv | 1 | Verify SIMD GEMM works | Yes (SIMD) |
| **Performance** | test_fc_gemm_only.riscv | 100 | Measure throughput | Yes (SIMD) |
| **Baseline** | test_fc_baseline.riscv | 10 | CPU comparison | No (pure CPU) |

### Typical Workflow:
1. **Correctness Check** (1 run):
   ```bash
   ./build/RISCV/gem5.opt configs/mlperf/test_fc_simple.py \
     --binary=configs/mlperf/test_fc_gemm.riscv
   ```

2. **Performance Measurement** (100 runs):
   ```bash
   ./build/RISCV/gem5.opt configs/mlperf/test_fc_simple.py \
     --binary=configs/mlperf/test_fc_gemm_only.riscv \
     --num-runs 100
   ```
   Check: `m5out/stats.txt` → Look for total sim time / 100 = time per inference

3. **Baseline Comparison** (10 runs):
   ```bash
   ./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
     --num-runs 10
   ```
   Check: `m5out/stats.txt` → Compare CPU-only time vs SIMD

4. **Analysis**:
   ```bash
   python3 configs/mlperf/analyze_stats.py m5out/stats.txt
   ```

---

## Why 100 Runs for SIMD?
- **Statistical Significance**: Multiple runs average out transient effects
- **Throughput Measurement**: Total time / 100 = average time per inference
- **Accelerator Stress Test**: Tests sustained performance, not just cold start
- **Real Workload Simulation**: MLPerf benchmarks run multiple inferences

## Why 10 Runs for Baseline?
- **Faster Simulation**: CPU baseline is much slower (no acceleration)
- **Still Representative**: 10 runs sufficient for average performance
- **Fair Comparison**: Can scale both tests proportionally if needed

---

## For Systolic Array Testing

You should follow the same pattern:

### 1. Create `systolic_fc_test.c` (correctness, 1 run)
- Single inference with FC layer
- Verifies systolic array works correctly
- Binary: `systolic_fc_test.riscv`

### 2. Create `systolic_fc_benchmark.c` (performance, 100 runs)
- 100 FC layer inferences
- Measures sustained throughput
- Binary: `systolic_fc_benchmark.riscv`
- Command line arg for num_runs

### 3. Run Configuration
```bash
# Correctness (1 run)
./build/RISCV/gem5.opt configs/mlperf/test_systolic.py \
  --binary=tests/systolic_fc_test.riscv

# Performance (100 runs) - create systolic_fc_benchmark
./build/RISCV/gem5.opt configs/mlperf/test_systolic.py \
  --binary=tests/systolic_fc_benchmark.riscv \
  --num-runs 100

# Analysis
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```

### 4. Expected Output Location
- **`m5out/stats.txt`** - Performance metrics
- **`m5out/config.ini`** - System configuration
- **Console** - Test progress and verification

### 5. Key Stats to Look For
- `system.systolic_accel.numGEMMs` - Total GEMM operations
- `system.systolic_accel.totalCycles` - Compute cycles
- `system.systolic_accel.dma_*` - Data transfer stats
- `simSeconds` - Total simulation time
- Throughput = num_runs / simSeconds = inferences per second
