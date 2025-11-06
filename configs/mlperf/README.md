# MLPerf Tiny Benchmark - gem5 Integration

This directory contains a simplified version of the MLPerf Tiny Image Classification benchmark adapted to run in gem5 simulator.

## Overview

**What this is:**
- A standalone C++ implementation of image classification inference (inspired by MLPerf Tiny)
- Simulates a small CNN (Convolutional Neural Network) running on CIFAR-10-like inputs
- Removes embedded system dependencies (mbed, serial I/O, hardware peripherals)
- Designed to run in gem5's syscall emulation (SE) mode

**What it simulates:**
- 32x32 RGB image input (3072 bytes)
- Convolution layers with 3x3 kernels
- Max pooling (2x2)
- Fully connected classification layer
- Softmax output for 10 classes
- Realistic memory access patterns and floating-point operations

## Files

- `simple_ic_benchmark.cpp` - The benchmark source code
- `simple_ic_benchmark.riscv` - Compiled RISC-V binary (statically linked)
- `mlperf_se.py` - gem5 simulation configuration script
- `README.md` - This file

## Quick Start

### 1. Compile the Benchmark (if not already done)

```bash
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
    configs/mlperf/simple_ic_benchmark.cpp \
    -o configs/mlperf/simple_ic_benchmark.riscv -lm
```

**Flags explained:**
- `-static`: Statically link all libraries (no dynamic linking in gem5 SE)
- `-O2`: Optimize for performance
- `-march=rv64gc`: RISC-V 64-bit with standard extensions (G = IMAFD + Zicsr + Zifencei, C = compressed)
- `-mabi=lp64d`: ABI with long and pointers as 64-bit, double-precision floating point
- `-lm`: Link math library (for exp, fmax, etc.)

### 2. Run in gem5 (Basic)

```bash
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py
```

### 3. Run with Custom Configuration

```bash
# Use O3 CPU with larger caches
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py \
    --cpu-type=O3 \
    --l1d-size=64kB \
    --l2-size=512kB \
    --num-runs=20

# Fast functional simulation
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py \
    --cpu-type=atomic \
    --num-runs=5
```

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `--cpu-type` | `TimingSimple` | CPU model: `atomic`, `timing`, `minor`, `o3` |
| `--cpu-clock` | `2GHz` | CPU clock frequency |
| `--l1i-size` | `32kB` | L1 instruction cache size |
| `--l1d-size` | `32kB` | L1 data cache size |
| `--l2-size` | `256kB` | L2 unified cache size |
| `--mem-size` | `512MB` | Physical memory size |
| `--num-runs` | `10` | Number of inference iterations |

## CPU Types Explained

### AtomicSimpleCPU (`--cpu-type=atomic`)
- **Fastest**: Functional simulation, no timing
- **Use for**: Quick testing, debugging
- **NOT for**: Performance analysis

### TimingSimpleCPU (`--cpu-type=timing`)
- **Moderate speed**: Models timing but simple in-order execution
- **Use for**: Basic performance estimates, cache studies
- **Good balance** between speed and accuracy

### MinorCPU (`--cpu-type=minor`)
- **Slower**: In-order, 4-stage pipeline
- **Use for**: More detailed microarchitecture analysis
- **Models**: Branch prediction, pipeline stalls

### O3CPU (`--cpu-type=o3`)
- **Slowest**: Out-of-order, superscalar
- **Use for**: Detailed performance modeling, IPC analysis
- **Models**: Instruction-level parallelism, ROB, LSQ, speculation

## Understanding the Output

### During Simulation

You'll see output from the benchmark like:

```
========================================
MLPerf Tiny Image Classification Benchmark
Simplified version for gem5 simulation
========================================

Configuration:
  - Input size: 3072 bytes (32x32x3 RGB)
  - Output classes: 10
  - Tensor arena: 100 KB
  - Number of runs: 10

Model initialized with 100 KB tensor arena

--- Run 1/10 ---
Generating synthetic input (pattern 0)...
Input loaded: 3072 bytes
Running inference...
Inference complete

=== Classification Results ===
m-results-[0.100,0.100,0.100,0.100,0.100,0.100,0.100,0.100,0.100,0.100]

Top prediction: airplane (10.0% confidence)
...
```

### After Simulation

gem5 will print:

```
======================================================================
Simulation Complete!
======================================================================
Exit reason: exiting with last active thread context
Simulated time: 0.025432 seconds
Simulated ticks: 50864000000
Instructions: 12453621
Cycles: 25432000
IPC: 0.489
```

**Key metrics:**
- **Simulated time**: Wall-clock time simulated (not real time!)
- **Instructions**: Total instructions executed
- **Cycles**: CPU cycles consumed
- **IPC**: Instructions Per Cycle (higher = better)

## Performance Analysis

### View Detailed Statistics

```bash
less m5out/stats.txt
```

**Important stats to look for:**

```
# Overall system stats
sim_seconds                            # Total simulated time
sim_ticks                              # Total ticks
sim_freq                               # Simulation frequency

# CPU stats
system.cpu.numCycles                   # Total cycles
system.cpu.committedInsts              # Instructions committed
system.cpu.ipc                         # Instructions per cycle

# Cache stats
system.cpu.icache.overall_hits::total  # L1I cache hits
system.cpu.icache.overall_misses::total # L1I cache misses
system.cpu.dcache.overall_hits::total  # L1D cache hits
system.cpu.dcache.overall_misses::total # L1D cache misses
system.l2cache.overall_hits::total     # L2 cache hits
system.l2cache.overall_misses::total   # L2 cache misses

# Memory stats
system.mem_ctrl.readReqs               # DRAM read requests
system.mem_ctrl.writeReqs              # DRAM write requests
system.mem_ctrl.bytesRead              # Total bytes read
system.mem_ctrl.avgRdBW                # Average read bandwidth
```

### Calculate Cache Miss Rate

```bash
python3 << 'EOF'
import re

with open('m5out/stats.txt', 'r') as f:
    stats = f.read()

    # Extract L1D cache stats
    l1d_hits = int(re.search(r'system\.cpu\.dcache\.overall_hits::total\s+(\d+)', stats).group(1))
    l1d_misses = int(re.search(r'system\.cpu\.dcache\.overall_misses::total\s+(\d+)', stats).group(1))
    l1d_accesses = l1d_hits + l1d_misses
    l1d_miss_rate = (l1d_misses / l1d_accesses * 100) if l1d_accesses > 0 else 0

    print(f"L1D Cache:")
    print(f"  Hits: {l1d_hits:,}")
    print(f"  Misses: {l1d_misses:,}")
    print(f"  Miss Rate: {l1d_miss_rate:.2f}%")
EOF
```

## Benchmark Characteristics

**Workload type:** Compute-intensive with moderate memory access

**Memory footprint:**
- Code: ~800 KB (static binary)
- Heap: ~100 KB (tensor arena)
- Stack: ~8 KB
- Input data: 3 KB per inference
- Intermediate tensors: varies (conv outputs, pooled features)

**Computational pattern:**
- Nested loops (convolutions)
- Many floating-point multiply-add operations
- Sequential memory access (good cache locality for weights)
- Some random access (pooling, indexing)

**Expected behavior:**
- High L1D cache hit rate for weights (reused across inference)
- Some L1D misses on intermediate activations (large tensors)
- Compute-bound rather than memory-bound (FP math dominates)

## Troubleshooting

### Binary not found
```
ERROR: Benchmark binary not found
```
**Solution:** Compile the benchmark first (see step 1)

### gem5 not built for RISC-V
```
fatal: Cannot find port ...
```
**Solution:** Build gem5 for RISC-V:
```bash
scons build/RISCV/gem5.opt -j$(nproc)
```

### Simulation too slow
**Solution:** Use faster CPU model:
```bash
./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py --cpu-type=atomic
```

## Next Steps

### 1. Compare CPU Models

Run with different CPU types and compare IPC:

```bash
for cpu in atomic timing minor o3; do
    echo "Testing $cpu..."
    ./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py \
        --cpu-type=$cpu --num-runs=5 | grep "IPC:"
done
```

### 2. Cache Sensitivity Study

Test different cache sizes:

```bash
for size in 16kB 32kB 64kB 128kB; do
    echo "L1D size: $size"
    ./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py \
        --l1d-size=$size --num-runs=5
done
```

### 3. Add Custom Accelerator

Modify `mlperf_se.py` to add a simple accelerator (like the SIMD accelerator example in `simd_accel_se.py`).

## References

- [MLPerf Tiny Benchmark](https://github.com/mlcommons/tiny)
- [gem5 Documentation](https://www.gem5.org/documentation/)
- [gem5 SE Mode](https://www.gem5.org/documentation/general_docs/fullsystem/guest_binaries)
- [RISC-V ISA](https://riscv.org/technical/specifications/)

## License

Based on MLPerf Tiny (Apache 2.0) and adapted for gem5 simulation.
