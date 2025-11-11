# SIMD Accelerator + MLPerf Integration Status

**Date:** November 6, 2025  
**Status:** ⚠️ BLOCKED - SIMD accelerator has compilation errors

---

## Summary

We successfully created all the files needed to integrate MLPerf Tiny with your SIMD accelerator, but discovered compilation errors in the existing SIMD accelerator code that prevent gem5 from building.

---

## What We Created ✅

### 1. SIMD-Enabled MLPerf Benchmark
**File:** `configs/mlperf/simple_ic_benchmark_simd.cpp`
- Modified MLPerf to use custom RISC-V instructions
- Implemented xcop_* inline assembly helpers
- Added `simd_elem_multiply()` function for accelerator offload
- Ready for cross-compilation once gem5 builds

### 2. gem5 Configuration
**File:** `configs/mlperf/mlperf_simd_se.py`
- Instantiates SimdAccel device with 4 SIMD lanes
- Connects MMIO and DMA ports correctly
- Supports multiple CPU types (timing/minor/o3)
- Optional cache hierarchy support

### 3. Build/Run Script
**File:** `configs/mlperf/run_mlperf_simd.sh`
- Automated cross-compilation for RISC-V
- Runs gem5 simulation
- Provides analysis commands
- Made executable (chmod +x)

### 4. Integration Documentation
**File:** `configs/mlperf/MLPERF_SIMD_INTEGRATION.md`
- Complete integration guide
- SIMD accelerator interface documentation
- Performance comparison methodology
- Troubleshooting tips
- Future optimization strategies

---

## Current Blocker ❌

### Compilation Errors in `src/dev/simd_accel.cc`

The SIMD accelerator C++ code has multiple errors:

```
src/dev/simd_accel.cc:35:39: error: no matching function for call to 
'gem5::DmaDevice::DmaDevice(const gem5::SimdAccelParams&)'

src/dev/simd_accel.cc:52:5: error: 'DmaVirtDevice' has not been declared
   52 |     DmaVirtDevice::init();

src/dev/simd_accel.cc:153:63: error: no matching function for call to 
'gem5::SimdAccel::onReadADone()'

src/dev/simd_accel.cc:158:1: error: no declaration matches 
'void gem5::SimdAccel::onReadADone()'
```

### Root Cause

**Header declares:** `void onReadADone(uint64_t batchSize);`  
**Implementation has:** `void SimdAccel::onReadADone()` (no parameter)

Same issue for:
- `issueReadB()`
- `onReadBDone()`
- `issueWrite()`
- `onWriteDone()`

All these functions have parameter mismatches between header and implementation.

### Additional Issue

Python param file inheritance was changed:
- **Before:** `class SimdAccel(DmaDevice)`
- **After (Nov 6):** `class SimdAccel(BasicPioDevice)` (today's change)
- **C++ code:** `class SimdAccel : public BasicPioDevice, public DmaDevice`

Reverted to `DmaDevice` base but still has compilation errors in C++.

---

## Files That Need Fixing

### 1. `src/dev/simd_accel.hh`
Line 65-69:
```cpp
void onReadADone(uint64_t batchSize);    // Has parameter
void issueReadB(uint64_t batchSize);      // Has parameter
void onReadBDone(uint64_t batchSize);     // Has parameter
void issueWrite(uint64_t batchSize);      // Has parameter
void onWriteDone(uint64_t batchSize);     // Has parameter
```

### 2. `src/dev/simd_accel.cc`
Lines 158, 166, 174, 184, 193:
```cpp
SimdAccel::onReadADone()      // NO parameter - WRONG
SimdAccel::issueReadB()       // NO parameter - WRONG
SimdAccel::onReadBDone()      // NO parameter - WRONG
SimdAccel::issueWrite()       // NO parameter - WRONG
SimdAccel::onWriteDone()      // NO parameter - WRONG
```

### 3. `src/dev/simd_accel.cc` Line 52
```cpp
DmaVirtDevice::init();  // DmaVirtDevice doesn't exist
```

Should probably be:
```cpp
DmaDevice::init();
BasicPioDevice::init();
```

---

## How to Fix

### Option 1: Fix Function Signatures (Recommended)

**Add `batchSize` parameter to all implementations:**

```cpp
void
SimdAccel::onReadADone(uint64_t batchSize)  // Add parameter
{
    // ... existing code using batchSize ...
}

void
SimdAccel::issueReadB(uint64_t batchSize)  // Add parameter
{
    // ... existing code using batchSize ...
}

// Same for onReadBDone(), issueWrite(), onWriteDone()
```

### Option 2: Remove Parameters from Header

**If batch processing isn't used, remove from header:**

```cpp
void onReadADone();      // Remove uint64_t parameter
void issueReadB();       // Remove uint64_t parameter
// etc.
```

### Option 3: Use Git History

The code was working at commit `3477a4a859`:

```bash
git show 3477a4a859:src/dev/simd_accel.cc > /tmp/simd_accel.cc
git show 3477a4a859:src/dev/simd_accel.hh > /tmp/simd_accel.hh
# Compare and merge
```

---

## Testing Plan (Once Fixed)

### 1. Rebuild gem5
```bash
scons build/RISCV/gem5.opt -j$(nproc)
```

### 2. Test SIMD Accelerator Standalone
```bash
./build/RISCV/gem5.opt \
    configs/example/simd_accel_se.py \
    tests/gemm_test.riscv
```

### 3. Cross-Compile SIMD MLPerf
```bash
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
    configs/mlperf/simple_ic_benchmark_simd.cpp \
    -o configs/mlperf/simple_ic_benchmark_simd.riscv \
    -lm
```

### 4. Run SIMD MLPerf
```bash
./build/RISCV/gem5.opt \
    configs/mlperf/mlperf_simd_se.py \
    --num-runs 10 \
    --cpu-type timing
```

### 5. Compare Performance
```bash
# Baseline (no SIMD)
python3 configs/mlperf/analyze_stats.py m5out.baseline/stats.txt

# With SIMD
python3 configs/mlperf/analyze_stats.py m5out/stats.txt

# Calculate speedup
```

---

## Expected Performance Improvements

### Theoretical Speedup

With 4 SIMD lanes and element-wise multiply offload:

**Baseline:**
- ~5.7M FP operations
- ~98.6M cycles
- IPC: 0.323

**With SIMD (ideal):**
- Multiply operations: 4x faster (4 lanes)
- Conv layers: ~50% multiply operations
- Expected speedup: ~1.5x to 2x

### Realistic Expectations

**DMA overhead** will reduce gains:
- Setup MMIO registers: ~10 cycles per call
- DMA read: Memory latency
- Compute: 10ns (20 cycles @ 2GHz) for 4 elements
- DMA write: Memory latency

**Small arrays** may not benefit:
- Overhead > compute savings for small operations
- Need batching or larger arrays

---

## Alternative: Use Baseline MLPerf

While SIMD is being fixed, you can still:

### Run Baseline MLPerf
```bash
./build/RISCV/gem5.opt \
    configs/mlperf/mlperf_se.py \
    --num-runs 100 \
    --cpu-type timing
```

### Profile and Optimize
```bash
python3 configs/mlperf/analyze_stats.py m5out/stats.txt
```

### Document Baseline Performance
```bash
# In configs/mlperf/
echo "Baseline Performance (No SIMD)" > BASELINE_RESULTS.md
grep "sim_seconds\|numCycles\|ipc" m5out/stats.txt >> BASELINE_RESULTS.md
```

---

## Next Steps

1. **Fix SIMD accelerator C++ code** (function signatures)
2. **Test gem5 build:** `scons build/RISCV/gem5.opt`
3. **Run standalone test:** `configs/example/simd_accel_se.py tests/gemm_test.riscv`
4. **Cross-compile SIMD MLPerf:** `run_mlperf_simd.sh`
5. **Run comparison:** Baseline vs SIMD performance
6. **Analyze results:** Calculate speedup and accelerator utilization

---

## Files Ready for Use

✅ `configs/mlperf/simple_ic_benchmark_simd.cpp` - SIMD benchmark (needs compilation)  
✅ `configs/mlperf/mlperf_simd_se.py` - gem5 config with SIMD device  
✅ `configs/mlperf/run_mlperf_simd.sh` - Build/run script (executable)  
✅ `configs/mlperf/MLPERF_SIMD_INTEGRATION.md` - Complete documentation  
⚠️ SIMD accelerator C++ code - **NEEDS FIXING**

---

**Contact:** Refer to INTEGRATION_GUIDE.md and MLPERF_SIMD_INTEGRATION.md for detailed instructions once SIMD accelerator builds successfully.
