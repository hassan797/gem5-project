# Arithmetic Latency Configuration for Baseline Test

## Overview

The baseline test now uses **MinorCPU** with configurable functional unit latencies for arithmetic operations. This allows precise modeling of computation costs.

---

## Configuration Details

### CPU Model: RiscvMinorCPU
- **Type**: In-order, pipelined CPU
- **Clock**: 2 GHz (500 ps period)
- **Functional Units**: Separate units for different operation classes

### Configurable Latencies

| Operation | Parameter | Default | Range | Description |
|-----------|-----------|---------|-------|-------------|
| **Integer Multiply** | `--mul-latency` | 3 cycles | 1-20 | Latency for `MUL` instructions |
| **Integer Add/ALU** | `--add-latency` | 1 cycle | 1-10 | Latency for `ADD`, `SUB`, logic ops |

---

## Usage Examples

### 1. Default Configuration (Realistic)
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py --num-runs 10
```
- Multiply: **3 cycles** (1.5 ns @ 2GHz)
- Add: **1 cycle** (0.5 ns @ 2GHz)

### 2. Fast Multiply (1 cycle)
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --mul-latency 1 --add-latency 1 --num-runs 10
```

### 3. Slow Multiply (5 cycles)
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --mul-latency 5 --add-latency 1 --num-runs 10
```

### 4. Zero-Latency (Like TimingSimpleCPU)
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --mul-latency 1 --add-latency 1 --num-runs 10
```

---

## GEMM Computation Breakdown

For FC layer: **C[10×1] = W[10×8192] × X[8192×1]**

### Operations per Inference
- **Multiplies**: 81,920 (M × K = 10 × 8192)
- **Adds**: 81,910 (accumulation, 8191 per row × 10 rows)

### Computation Time (per inference)

| Mul Latency | Add Latency | Compute Cycles | Compute Time @ 2GHz |
|-------------|-------------|----------------|---------------------|
| 1 cycle | 1 cycle | ~163,830 | 81.9 µs |
| 3 cycles | 1 cycle | ~327,670 | 163.8 µs |
| 5 cycles | 1 cycle | ~491,510 | 245.8 µs |
| 10 cycles | 1 cycle | ~900,110 | 450.1 µs |

**Note**: Actual time will be higher due to:
- Memory access latency (dominant factor)
- Pipeline stalls
- Instruction fetch overhead

---

## Comparison with SIMD Accelerator

### SIMD Accelerator
- **Computation**: Effectively **zero-latency** (4-way batching, instantaneous)
- **Bottleneck**: DMA transfers and memory bandwidth

### Baseline CPU (MinorCPU with latencies)
- **Computation**: Latency × operation count
- **Bottleneck**: Both computation AND memory

### Expected Speedup Formula

```
Speedup = (T_baseline_compute + T_baseline_memory) / (T_simd_compute + T_simd_memory)
```

With realistic latencies (3 cyc mul, 1 cyc add):
- SIMD compute: ~0.02 ms (negligible)
- Baseline compute: ~0.164 ms + memory overhead
- Expected: **Higher speedup** than zero-latency case

---

## Implementation Details

### File Modified
`configs/mlperf/test_fc_baseline.py`

### Key Changes
1. Switched from `RiscvTimingSimpleCPU` to `RiscvMinorCPU`
2. Added configurable functional units:
   - `MinorDefaultIntFU()` - Integer ALU (add latency)
   - `MinorDefaultIntMulFU()` - Integer multiplier (mul latency)
3. Added command-line arguments: `--mul-latency`, `--add-latency`

### Functional Unit Configuration
```python
# Integer ALU (add, sub, logic operations)
intALU = MinorDefaultIntFU()
intALU.opLat = args.add_latency  # Configurable

# Integer Multiplier
intMul = MinorDefaultIntMulFU()
intMul.opLat = args.mul_latency  # Configurable
```

---

## Testing Recommendations

### 1. Latency Sweep
Test different multiply latencies to see impact:
```bash
for mul_lat in 1 3 5 10; do
    echo "Testing mul_latency=$mul_lat"
    ./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
        --mul-latency $mul_lat --num-runs 10 \
        > baseline_mul${mul_lat}.log 2>&1
done
```

### 2. Extract Performance
```bash
grep "simSeconds" m5out/stats.txt
grep "simInsts" m5out/stats.txt
grep "numCycles" m5out/stats.txt
```

### 3. Compare with SIMD
Run both tests with same configuration and compare:
- Execution time
- Instruction count
- CPU utilization
- Memory bandwidth

---

## Expected Results

### With Default Latencies (mul=3, add=1)
- **Higher execution time** than zero-latency
- **More realistic** comparison with SIMD
- **Greater speedup** from SIMD acceleration
- **Clear demonstration** of compute vs memory bottleneck

### Analysis
The SIMD accelerator's advantage becomes more pronounced when baseline has realistic arithmetic latencies, as it offloads the expensive multiply-accumulate operations to dedicated hardware.

---

## Source Files

### Functional Unit Definitions
- `src/cpu/minor/BaseMinorCPU.py` - FU class definitions
- `src/cpu/minor/func_unit.cc` - FU implementation
- `src/cpu/minor/execute.cc` - Execution pipeline

### CPU Implementation
- `src/arch/riscv/RiscvCPU.py` - RISC-V CPU variants
- `src/cpu/minor/cpu.cc` - MinorCPU core

---

## Verification

To verify latencies are being used:
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --mul-latency 10 --num-runs 1 --debug-flags=MinorExecute
```

Check debug output for functional unit usage and latencies.
