#!/bin/bash
# Build and run MLPerf Tiny with SIMD Accelerator
set -e

echo "=========================================="
echo "MLPerf Tiny + SIMD Accelerator Build Script"
echo "=========================================="
echo

# Check for RISC-V toolchain
if ! command -v riscv64-linux-gnu-g++ &> /dev/null; then
    echo "ERROR: riscv64-linux-gnu-g++ not found!"
    echo "Install with: sudo apt-get install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu"
    exit 1
fi

# Cross-compile the SIMD-enabled benchmark
echo "Step 1: Cross-compiling SIMD benchmark for RISC-V..."
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
    configs/mlperf/simple_ic_benchmark_simd.cpp \
    -o configs/mlperf/simple_ic_benchmark_simd.riscv \
    -lm

if [ $? -eq 0 ]; then
    echo "✓ Cross-compilation successful!"
    ls -lh configs/mlperf/simple_ic_benchmark_simd.riscv
else
    echo "✗ Cross-compilation failed!"
    exit 1
fi

echo

# Check if gem5 is built
if [ ! -f build/RISCV/gem5.opt ]; then
    echo "ERROR: gem5 not built for RISC-V!"
    echo "Build with: scons build/RISCV/gem5.opt -j\$(nproc)"
    exit 1
fi

echo "Step 2: Running in gem5 with SIMD accelerator..."
echo

# Run with default options (10 inferences, no caches for speed)
./build/RISCV/gem5.opt \
    configs/mlperf/mlperf_simd_se.py \
    --num-runs 10 \
    --cpu-type timing

echo
echo "=========================================="
echo "Simulation Complete!"
echo "=========================================="
echo
echo "Results:"
echo "  - Simulation output: m5out/simout"
echo "  - Performance stats: m5out/stats.txt"
echo
echo "To analyze results:"
echo "  python3 configs/mlperf/analyze_stats.py m5out/stats.txt"
echo
echo "To compare with baseline (non-SIMD):"
echo "  ./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py --num-runs 10"
echo
