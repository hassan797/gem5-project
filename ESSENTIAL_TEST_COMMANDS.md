# Essential Test Commands

Quick reference for running all the important tests in this project.

---

## 1. Baseline FC GEMM (CPU-only, no accelerator)

**What it does:** Pure CPU matrix multiplication with configurable arithmetic latencies

```bash
# Default latencies (mul=3 cycles, add=1 cycle)
./build/RISCV/gem5.opt -d m5out_baseline \
  configs/mlperf/test_fc_baseline.py \
  --num-runs 10

# Matched latencies (mul=4 cycles, add=1 cycle)
./build/RISCV/gem5.opt -d m5out_baseline_mul4_add1 \
  configs/mlperf/test_fc_baseline.py \
  --mul-latency 4 \
  --add-latency 1 \
  --num-runs 10

# High latency multiply (mul=20 cycles)
./build/RISCV/gem5.opt -d m5out_baseline_mul20 \
  configs/mlperf/test_fc_baseline.py \
  --mul-latency 20 \
  --add-latency 1 \
  --num-runs 10
```

---

## 2. SIMD FC GEMM (with accelerator)

**What it does:** Matrix multiplication using the SIMD accelerator with 4-lane batching

```bash
# Using MMIO-based GEMM test (recommended, most stable)
./build/RISCV/gem5.opt -d m5out_simd_gemm \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 10 \
  --binary configs/mlperf/test_fc_gemm_only.riscv

# With 4-cycle batch latency
./build/RISCV/gem5.opt -d m5out_simd_batch4cyc \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 10 \
  --binary configs/mlperf/test_fc_gemm_only.riscv
# Note: compute_latency="2ns" is set in the config (4 cycles @ 2GHz)

# Full image classification with SIMD
./build/RISCV/gem5.opt -d m5out_simd_ic \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 10 \
  --binary configs/mlperf/simple_ic_benchmark_simd.riscv
```

---

## 3. Coprocessor Element-Wise Multiply Test

**What it does:** Simple 8-element array multiplication to verify accelerator basics

```bash
# Basic run
./build/RISCV/gem5.opt configs/example/coproc_test_simple.py \
  tests/coproc_test.riscv

# With debug output to see DMA operations
./build/RISCV/gem5.opt --debug-flags=SimdAccel \
  configs/example/coproc_test_simple.py \
  tests/coproc_test.riscv
```

---

## 4. Simple SIMD SE Test

**What it does:** Minimal SIMD accelerator test for quick validation

```bash
./build/RISCV/gem5.opt configs/example/simd_accel_se.py \
  tests/simple_simd_test.riscv
```

---

## 5. Analyzing Results

**Check simulation stats:**
```bash
# View key metrics
grep -E "simSeconds|numCycles|simInsts" m5out_*/stats.txt

# Detailed stats for a specific run
less m5out_baseline/stats.txt

# Memory traffic
grep -E "readBursts|writeBursts" m5out_*/stats.txt
```

**Use the analyzer script:**
```bash
python3 configs/mlperf/analyze_stats.py m5out_baseline/stats.txt
python3 configs/mlperf/analyze_stats.py m5out_simd_gemm/stats.txt
```

---

## 6. Comparison Workflow (Baseline vs SIMD)

**Step 1: Run baseline**
```bash
./build/RISCV/gem5.opt -d m5out_baseline_mul4_add1 \
  configs/mlperf/test_fc_baseline.py \
  --mul-latency 4 \
  --add-latency 1 \
  --num-runs 10
```

**Step 2: Run SIMD (with matched 4-cycle batch latency)**
```bash
./build/RISCV/gem5.opt -d m5out_simd_batch4cyc \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 10 \
  --binary configs/mlperf/test_fc_gemm_only.riscv
```

**Step 3: Compare**
```bash
# Quick comparison
echo "Baseline:"
grep "simSeconds" m5out_baseline_mul4_add1/stats.txt
echo "SIMD:"
grep "simSeconds" m5out_simd_batch4cyc/stats.txt

# Calculate speedup
python3 -c "
baseline = $(grep 'simSeconds' m5out_baseline_mul4_add1/stats.txt | awk '{print $2}')
simd = $(grep 'simSeconds' m5out_simd_batch4cyc/stats.txt | awk '{print $2}')
print(f'Speedup: {baseline/simd:.2f}x')
"
```

---

## 7. Debug/Verbose Runs

**Enable SIMD accelerator debug output:**
```bash
./build/RISCV/gem5.opt --debug-flags=SimdAccel \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 1 \
  --binary configs/mlperf/test_fc_gemm_only.riscv
```

**Enable execution trace (very verbose!):**
```bash
./build/RISCV/gem5.opt --debug-flags=Exec \
  configs/mlperf/test_fc_baseline.py \
  --num-runs 1 \
  2>&1 | less
```

**Enable multiple debug flags:**
```bash
./build/RISCV/gem5.opt --debug-flags=SimdAccel,Dma \
  configs/example/coproc_test_simple.py \
  tests/coproc_test.riscv
```

---

## 8. Quick Smoke Tests

**Test if everything compiles and runs (1 inference each):**
```bash
# Baseline
./build/RISCV/gem5.opt -d m5out_test_baseline \
  configs/mlperf/test_fc_baseline.py --num-runs 1

# SIMD
./build/RISCV/gem5.opt -d m5out_test_simd \
  configs/mlperf/mlperf_simd_se.py --num-runs 1 \
  --binary configs/mlperf/test_fc_gemm_only.riscv

# Coproc
./build/RISCV/gem5.opt configs/example/coproc_test_simple.py \
  tests/coproc_test.riscv
```

---

## 9. Recompiling Test Binaries

**If you modify the C++ test files:**

```bash
# FC GEMM baseline
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
  configs/mlperf/test_fc_baseline.cpp \
  -o configs/mlperf/test_fc_baseline.riscv

# FC GEMM with SIMD (MMIO version)
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
  configs/mlperf/test_fc_gemm_only.cpp \
  -o configs/mlperf/test_fc_gemm_only.riscv

# Image classification with SIMD
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
  configs/mlperf/simple_ic_benchmark_simd.cpp \
  -o configs/mlperf/simple_ic_benchmark_simd.riscv -lm

# Coproc test (freestanding)
riscv64-linux-gnu-gcc -nostdlib -static -march=rv64gc -mabi=lp64d \
  tests/coproc_test.c \
  -o tests/coproc_test.riscv
```

---

## 10. Common Output Directories

After running tests, check these directories for results:

```bash
# View directory structure
ls -lh m5out_*/

# Check stats from all runs
for dir in m5out_*; do
  echo "=== $dir ==="
  grep "simSeconds" $dir/stats.txt
done

# Find the fastest run
grep -H "simSeconds" m5out_*/stats.txt | sort -t: -k2 -n | head -1
```

---

## Quick Reference Table

| Test | Command | Purpose |
|------|---------|---------|
| Baseline GEMM | `./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py` | CPU-only matrix multiply |
| SIMD GEMM | `./build/RISCV/gem5.opt configs/mlperf/mlperf_simd_se.py --binary configs/mlperf/test_fc_gemm_only.riscv` | Accelerated matrix multiply |
| Coproc Test | `./build/RISCV/gem5.opt configs/example/coproc_test_simple.py tests/coproc_test.riscv` | Basic accelerator validation |
| View Stats | `grep "simSeconds" m5out/stats.txt` | Check simulation time |
| Debug Run | `./build/RISCV/gem5.opt --debug-flags=SimdAccel <config> <binary>` | See detailed accelerator logs |

---

## Tips

- **Simulation is slow**: gem5 is detailed but not fast. Start with `--num-runs 1` to test, then scale up.
- **Output too verbose**: Redirect to file: `command > output.log 2>&1`
- **Compare fairly**: Use same clock, memory, and cache settings for baseline vs SIMD
- **Check for errors**: Look for `panic:` or `warn:` in output
- **Stats location**: Always in `<outdir>/stats.txt`

For more details, see `PROJECT_STRUCTURE_GUIDE.md`
