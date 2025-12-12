# Systolic Array Bug Fix - Simple Explanation

## What Was the Problem?

Imagine you have a calculator with a memory button. You use it to calculate 5+3=8, and the 8 stays in memory. Then you try to calculate 2+4, but because you didn't clear the memory, you get 8+2+4=14 instead of 6!

**This is exactly what was happening with our systolic array.**

## The Bug in Simple Terms

1. **Test 1 runs:** 4×4 matrix multiplication
   - PEs compute correctly and get results like C[0][0]=6, C[0][1]=20, etc.
   - These values stay in the PE "memory" (accumulator)

2. **Test 2 starts:** Another 4×4 matrix multiplication
   - PEs still have old values (6, 20, etc.) in their accumulators
   - New calculation adds to these old values instead of starting from 0!
   - Result: Complete garbage

## What We Found

Through debugging, we traced the execution and saw:
```
Test 1: PE[0][0] starts at 0 → ends at 6 ✓ Correct!
Test 2: PE[0][0] starts at 4298 ✗ WRONG! Should start at 0!
```

The 4298 was leftover junk from previous computations!

## The Root Cause

**Location:** `src/dev/systolic_accel.cc`, function `onWeightLoadDone()`, line 209

```cpp
// This code was WRONG:
if (tileK == 0) {
    // Reset PEs
}
```

**Problem:** This only resets PEs when processing tiles within the SAME matrix multiplication. When a BRAND NEW matrix multiplication starts, PEs keep their old values!

## The Fix

**Location:** `src/dev/systolic_accel.cc`, function `kick()`, line ~127

```cpp
void SystolicAccel::kick()
{
    // This function is called when starting a NEW GEMM operation
    
    // ... setup code ...
    
    // ADDED THIS CODE:
    // Reset ALL PEs at the start of each GEMM
    for (unsigned i = 0; i < arrayRows; i++) {
        for (unsigned j = 0; j < arrayCols; j++) {
            peArray[i][j].reset();  // Clear accumulator to 0
        }
    }
    
    // ... rest of function ...
}
```

## Why This Works

The `kick()` function is called every time the test program:
1. Writes matrix addresses (A, B, C) to MMIO registers
2. Writes dimensions (M, K, N) to MMIO registers  
3. Writes the "START" command to trigger computation

So `kick()` = "Start a brand new calculation" → Perfect place to reset PEs!

## Before vs After

### Before (WRONG):
```
Test 1: PEs start at 0 → Results: Correct
Test 2: PEs start at ???  → Results: WRONG (accumulated from Test 1)
Test 3: PEs start at ??????? → Results: VERY WRONG (accumulated from Tests 1+2)
```

### After (CORRECT):
```
Test 1: PEs start at 0 → Results: Correct
Test 2: PEs start at 0 → Results: Correct
Test 3: PEs start at 0 → Results: Correct
```

## What to Test

Run the test program:
```bash
./build/RISCV/gem5.opt configs/mlperf/test_systolic.py --binary=tests/systolic_test.riscv
```

You should see:
- `[SystolicAccel] Resetting all PEs` message at the start of EACH test
- Debug output showing `accum_before=0` for k=0 (not 4298!)
- All 4 tests showing: `✓ PASS`

## Files Changed

**Only one file modified:**
- `src/dev/systolic_accel.cc` - Added PE reset code in `kick()` function

**What the change does:**
- Clears all PE accumulators to 0 when starting a new GEMM operation
- Ensures each test starts with clean state
- Fixes the "memory not cleared" bug

## Summary

**The Problem:** Calculator not being cleared between calculations
**The Fix:** Clear calculator memory at start of each new calculation  
**The Result:** All tests should now pass!

---

*Created: December 10, 2025*
*Bug: PEs not reset between GEMM operations*
*Fix: Added PE reset in kick() function*
