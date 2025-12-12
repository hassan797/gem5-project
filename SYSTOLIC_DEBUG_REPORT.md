# Systolic Array Accelerator - Debug Report

## Executive Summary
We built a systolic array accelerator for matrix multiplication (GEMM) in gem5. The accelerator compiles and runs, but produces **incorrect results**. Through systematic debugging, we've identified that the computation itself is correct, but we need to verify the memory write-back mechanism.

---

## What is a Systolic Array?

Think of a systolic array as a grid of tiny calculators (called Processing Elements or PEs). Each calculator:
1. Holds one number from matrix B (the "weight")
2. Receives numbers from matrix A flowing through
3. Multiplies them and accumulates the result

For a 4×4 systolic array computing C = A × B:
```
PE[0][0] computes C[0][0]
PE[0][1] computes C[0][1]
PE[0][2] computes C[0][2]
PE[0][3] computes C[0][3]
... and so on
```

---

## The Problem: What We're Seeing

### Test Case: 4×4 Matrix Multiplication
```
Matrix A:              Matrix B:              Expected C:
[0, 1, 2, 3]          [1, 1, 1,  1]          [6,  20,  34,  48]
[1, 2, 3, 4]          [1, 2, 3,  4]          [10, 30,  50,  70]
[2, 3, 4, 5]    ×     [1, 3, 5,  7]    =     [14, 40,  66,  92]
[3, 4, 5, 6]          [1, 4, 7, 10]          [18, 50,  82, 114]
```

### What the Accelerator Returns:
```
Actual C (WRONG):
[6, 6, 6, 6]      ← Only first column is correct!
[?, ?, ?, ?]
[?, ?, ?, ?]
[?, ?, ?, ?]
```

**The test shows: C[0][1] = 6 (expected 20), C[0][2] = 6 (expected 34), C[0][3] = 6 (expected 34)**

---

## The Investigation: What We Did

### Step 1: Verify Memory Access is Correct ✓

**Question:** Are we reading from memory correctly? (Virtual vs Physical addresses)

**Answer:** YES! We confirmed:
- Using `dmaReadVirt()` and `dmaWriteVirt()` (same as working SimdAccel)
- These handle virtual addresses from the test program correctly
- The `translate()` method converts virtual → physical addresses automatically

**Files Checked:**
- `src/dev/dma_virt_device.hh` - Confirmed API documentation
- `src/dev/simd_accel.cc` - Confirmed same usage pattern
- `src/dev/systolic_accel.cc` - Uses identical DMA methods

**Conclusion:** Memory access is NOT the problem.

---

### Step 2: Debug the Data Loading ✓

**Question:** Is the B matrix being loaded correctly into the accelerator?

**What We Added:** Debug output in `onBRowLoaded()` to trace B matrix loading.

**Debug Output:**
```
[SystolicAccel] Loaded B row 0 of 4
[SystolicAccel] Loaded B row 1 of 4
[SystolicAccel] Loaded B row 2 of 4
[SystolicAccel] Loaded B row 3 of 4
[SystolicAccel] B tile fully loaded (4×4 elements)
```

**Conclusion:** All B matrix rows are being loaded successfully.

---

### Step 3: Debug the Computation Values ✓

**Question:** Are the correct values being used in the computation?

**What We Added:** Debug output in `performCompute()` to show A and B values for each k iteration.

**Debug Output for Row 0:**
```
k=0: A[0][0]=0, B[0]=[1, 1, 1, 1]     ✓ Correct!
k=1: A[0][1]=1, B[1]=[1, 2, 3, 4]     ✓ Correct!
k=2: A[0][2]=2, B[2]=[1, 3, 5, 7]     ✓ Correct!
k=3: A[0][3]=3, B[3]=[1, 4, 7, 10]    ✓ Correct!
```

**Manual Verification:**
- C[0][0] = 0×1 + 1×1 + 2×1 + 3×1 = 6 ✓
- C[0][1] = 0×1 + 1×2 + 2×3 + 3×4 = 20 ✓
- C[0][2] = 0×1 + 1×3 + 2×5 + 3×7 = 34 ✓
- C[0][3] = 0×1 + 1×4 + 2×7 + 3×10 = 48 ✓

**Conclusion:** The correct values are being loaded from memory.

---

### Step 4: Debug the PE Accumulation ✓

**Question:** Are the PEs computing the correct results?

**What We Added:** Debug output showing PE accumulator values before and after each multiply-accumulate.

**Debug Output for Row 0:**
```
k=0: PE[0][0]: 0→0,  PE[0][1]: 0→0,  PE[0][2]: 0→0,   PE[0][3]: 0→0
k=1: PE[0][0]: 0→1,  PE[0][1]: 0→2,  PE[0][2]: 0→3,   PE[0][3]: 0→4
k=2: PE[0][0]: 1→3,  PE[0][1]: 2→8,  PE[0][2]: 3→13,  PE[0][3]: 4→18
k=3: PE[0][0]: 3→6,  PE[0][1]: 8→20, PE[0][2]: 13→34, PE[0][3]: 18→48
     ^^^^^^^^^^^^^^  ^^^^^^^^^^^     ^^^^^^^^^^^^      ^^^^^^^^^^^^
     CORRECT!        CORRECT!        CORRECT!          CORRECT!
```

**Detailed Trace for PE[0][1] (Computing C[0][1]):**
1. k=0: accum = 0 + (0 × 1) = 0
2. k=1: accum = 0 + (1 × 2) = 2
3. k=2: accum = 2 + (2 × 3) = 8
4. k=3: accum = 8 + (3 × 4) = **20** ✓

**Conclusion:** The PEs are computing PERFECTLY! Each PE has the correct final value.

---

### Step 5: Debug the Result Collection ✓

**Question:** Are we collecting the correct values from the PEs into the output buffer?

**What We Added:** Debug output in `startCTileWrite()` showing what's being collected from each PE.

**Debug Output:**
```
Collecting results: mTile=4, nTile=4
  PE[0][0]=6  → bufCTile[0]=6   ✓
  PE[0][1]=20 → bufCTile[1]=20  ✓
  PE[0][2]=34 → bufCTile[2]=34  ✓
  PE[0][3]=48 → bufCTile[3]=48  ✓
  PE[1][0]=10 → bufCTile[4]=10  ✓
  ... (and so on for all 16 values)
```

**Conclusion:** The result collection into `bufCTile` is CORRECT!

---

## What We THOUGHT Was The Problem (Initial Investigation)

### Suspected Issue 1: DMA Write-Back Mechanism
We initially thought the problem was in how we write results back to memory.

**Theory:** The C matrix write might be incorrect for tiled operations.

**Current Implementation (in `startCTileWrite()`):**
```cpp
// Collect results from PEs into bufCTile
for (unsigned i = 0; i < mTile; i++) {
    for (unsigned j = 0; j < nTile; j++) {
        uint64_t result = peArray[i][j].getResult();
        uint64_t idx = i * nTile + j;
        std::memcpy(bufCTile.data() + idx * sizeof(uint64_t), &result, sizeof(uint64_t));
    }
}

// Write entire tile as one contiguous block
Addr addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t);
uint64_t writeSize = mTile * nTile * sizeof(uint64_t);
dmaWriteVirt(addr, writeSize, cb, bufCTile.data());
```

**Why We Thought This Was Wrong:**

For an 8×8 matrix, if we're writing a 4×4 tile at position (0,0):
- We want: C[0][0..3] in one place, then C[1][0..3] in another place (64 bytes away)
- We're doing: Writing all 16 values contiguously, which overwrites C[0][4..7] instead of C[1][0..3]!

**However:** The 4×4 test should work with this implementation since the entire matrix is one contiguous block!

### Other Theories We Considered:

**Theory 2: DMA Write Not Completing**
- Maybe the DMA write is issued but hasn't completed when the test reads?
- Need to check: Is `onCTileWriteDone()` being called?

**Theory 3: Wrong Address Calculation**
- Maybe the write address is calculated incorrectly?
- Current: `addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t)`
- For 4×4 test: `addr = regMatC + (0 * 4 + 0) * 8 = regMatC` ✓ Looks correct

**Theory 4: Endianness or Memcpy Issue**
- Maybe the way we're copying data has a problem with byte ordering?
- Unlikely since we use standard `std::memcpy`

---

## The ACTUAL Problem (ROOT CAUSE FOUND!)

### What We Discovered:

Looking at extended debug output, we saw this:
```
[DEBUG] k=3, A[0][3]=3, B[3][0]=1, PE[0][0].accum_before=3, accum_after=6
[DEBUG] k=3, A[0][3]=3, B[3][1]=4, PE[0][1].accum_before=8, accum_after=20
...
Writing results tile (2,1), size=4×4
[WriteTile] PE[0][0]=6 -> bufCTile[0]
[WriteTile] PE[0][1]=20 -> bufCTile[1]
...

# THEN FOR THE SECOND GEMM TEST:
[DEBUG] k=0, A[0][0]=0, B[0][0]=1, PE[0][0].accum_before=4298, accum_after=4298
[DEBUG] k=0, A[0][0]=0, B[0][1]=1, PE[0][1].accum_before=5332, accum_after=5332
                                    ^^^^^^^^^^^^^^^^^^^^^
                                    SHOULD BE ZERO!!!
```

**THE BUG:** PEs are NOT being reset between different GEMM operations!

### Current Status: Where We Are Now

### What's Working ✓
1. **Memory Access:** Virtual address DMA working perfectly
2. **B Matrix Loading:** All rows loaded correctly into bufWeights
3. **A Matrix Loading:** Correct values loaded for each row
4. **Computation:** PEs compute correct results (verified mathematically) **when PEs are properly reset**
5. **Result Collection:** Correct values collected into bufCTile

### What's NOT Working ✗
**PE Reset Logic** - PEs retain old accumulator values when a NEW GEMM operation starts!

### The Bug in Detail:

```cpp
// In onWeightLoadDone(), around line 209:
if (tileK == 0) {
    // Reset PEs only when starting first K-tile of a TILE
    for (unsigned i = 0; i < arrayRows; i++) {
        for (unsigned j = 0; j < arrayCols; j++) {
            peArray[i][j].reset();
        }
    }
}
```

**Why This Is Wrong:**
- This resets PEs when `tileK == 0`, which means "first K-tile for this output tile"
- Within ONE GEMM operation, this works fine
- But when we start a SECOND GEMM (new matrices A, B, C), the PEs still have accumulator values from the FIRST GEMM!

**Example Timeline:**
1. Test 1 (4×4 GEMM): PEs compute correctly, end with values [6, 20, 34, 48, ...]
2. Test 2 (4×4 GEMM): **PEs still have [6, 20, 34, 48, ...] in their accumulators!**
3. When Test 2 tries to compute starting with A[0][0]=0, the PEs add to the old values
4. Result: Complete garbage!

---

## The Fix

### What We Need To Do:

**Reset PEs at the START of each NEW GEMM operation**, not just when tileK==0.

### Where To Add The Fix:

In `src/dev/systolic_accel.cc`, in the `kick()` function (called when starting a new GEMM):

```cpp
void
SystolicAccel::kick()
{
    if (regStatus & 0x1) {
        // Already busy
        return;
    }
    
    // NEW: Reset ALL PEs before starting a new GEMM operation
    for (unsigned i = 0; i < arrayRows; i++) {
        for (unsigned j = 0; j < arrayCols; j++) {
            peArray[i][j].reset();  // Clear accumulators
        }
    }
    
    // Mark as busy
    regStatus = 0x1;
    
    // Initialize tile iteration
    tileI = 0;
    tileJ = 0;
    tileK = 0;
    
    // Start the first tile
    startWeightLoad();
}
```

### Why This Will Work:

1. **When Test 1 starts:** `kick()` is called → All PEs reset to 0 → Computation proceeds correctly
2. **When Test 1 finishes:** PEs have final values, accelerator goes idle
3. **When Test 2 starts:** `kick()` is called → All PEs reset to 0 → Fresh start!

### Additional Consideration:

We should **KEEP** the existing reset logic at `tileK == 0` for a different reason:
- When computing multiple output tiles that share K dimension (accumulation across K tiles)
- We DO want to accumulate across K tiles for the SAME output position
- We DON'T want to accumulate between DIFFERENT output positions

**So the complete reset strategy is:**
- Reset in `kick()`: Clears between different GEMM operations
- Reset at `tileK == 0`: Clears between different output tiles within the same GEMM

---

## Testing Results After The Fix

### ✓ Fix Applied Successfully!

We added PE reset to the `kick()` function, and now we see:
```
[SystolicAccel] Resetting all PEs
  [DEBUG] k=0, A[0][0]=0, B[0][0]=1, PE[0][0].accum_before=0, accum_after=0
```

**All PEs now start with accum_before=0 for each new GEMM!** ✓

### ⚠️ New Problem Discovered: Memory Write-Back Issue

**Test Results:**
- Test 1 (4×4): Status unknown (need to check)
- Test 2 (4×8): Status unknown (need to check)  
- Test 3 (8×8): Status unknown (need to check)
- Test 4 (12×16): **FAILS** with mismatches

**Error from 12×16 test:**
```
MISMATCH at C[1][0]: got 5576, expected 136
MISMATCH at C[1][1]: got 6936, expected 1496
MISMATCH at C[1][2]: got 8296, expected 2856
MISMATCH at C[1][3]: got 9656, expected 4216
```

### The New Issue: Tile Write-Back

**What's working:**
- PE computation is correct (verified by debug output)
- PE accumulation across K tiles is correct
- bufCTile collection is correct (verified earlier)

**What's NOT working:**
- The DMA write to memory is putting values in the wrong locations!

**The Problem:**
When we write a tile back to memory, we do:
```cpp
Addr addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t);
dmaWriteVirt(addr, writeSize, cb, bufCTile.data());
```

This writes the **entire bufCTile as one contiguous block**. This only works if the tile fits perfectly in memory without gaps!

**For a 12×16 matrix with 4×4 tiles:**
- Tile (0,0) should write to C[0-3][0-3]
- Tile (0,1) should write to C[0-3][4-7]
- Tile (1,0) should write to C[4-7][0-3]

But in memory (row-major, N=16):
- C[0][0-3] is at offsets 0, 8, 16, 24
- C[0][4-7] is at offsets 32, 40, 48, 56
- C[1][0-3] is at offsets 128, 136, 144, 152 (64 bytes away from C[0][0]!)

If we write 16 values (4×4 tile) contiguously starting at C[1][0]:
- We'll write to C[1][0], C[1][1], C[1][2], C[1][3] ✓
- Then C[1][4], C[1][5], C[1][6], C[1][7] ✓
- Then C[1][8], C[1][9], C[1][10], C[1][11] ✓
- Then C[1][12], C[1][13], C[1][14], C[1][15] ✓

Wait, that should work! Let me reconsider...

Actually, **we need to write each ROW of the tile separately** because they're not contiguous in memory!

---

## Next Steps

### Immediate Actions:
1. **Fix the tile write-back:** Write each row of the tile separately with correct stride
2. **Implement row-by-row DMA write:**
   ```cpp
   for (unsigned i = 0; i < mTile; i++) {
       Addr rowAddr = regMatC + ((rowStart + i) * regDimN + colStart) * sizeof(uint64_t);
       dmaWriteVirt(rowAddr, nTile * sizeof(uint64_t), cb, bufCTile[i*nTile]);
   }
   ```
3. **Rebuild and test again**
4. **Verify all 4 tests pass**

---

## Code Changes Made So Far

### File: `src/dev/systolic_accel.cc`

**Change 1:** Added B row loading debug (line ~180)
```cpp
void SystolicAccel::onBRowLoaded(const uint64_t &)
{
    std::cout << "[SystolicAccel] Loaded B row " << colIdx << " of " << nTile << std::endl;
    // ... rest of function
}
```

**Change 2:** Added full computation trace (lines ~290-303)
```cpp
for (unsigned k = 0; k < kTile; k++) {
    // Load A value
    uint64_t aValue;
    std::memcpy(&aValue, bufARow.data() + k * sizeof(uint64_t), sizeof(uint64_t));
    
    for (unsigned j = 0; j < nTile && j < arrayCols; j++) {
        // Load B value
        uint64_t bValue;
        std::memcpy(&bValue, bufWeights.data() + (k * nTile + j) * sizeof(uint64_t), sizeof(uint64_t));
        
        // DEBUG: Print values and accumulator before/after
        if (rowIdx == 0 && j < 4) {
            std::cout << "  [DEBUG] k=" << k << ", A[0][" << k << "]=" << aValue 
                      << ", B[" << k << "][" << j << "]=" << bValue;
            std::cout << ", PE[0][" << j << "].accum_before=" << peArray[0][j].getResult();
        }
        
        // Perform computation
        peArray[rowIdx][j].loadWeight(bValue);
        peArray[rowIdx][j].compute(aValue);
        
        // DEBUG: Print accumulator after
        if (rowIdx == 0 && j < 4) {
            std::cout << ", accum_after=" << peArray[0][j].getResult() << std::endl;
        }
    }
}
```

**Change 3:** Added result collection debug (line ~333)
```cpp
// Collect results from PEs
for (unsigned i = 0; i < mTile; i++) {
    for (unsigned j = 0; j < nTile; j++) {
        uint64_t result = peArray[i][j].getResult();
        uint64_t idx = i * nTile + j;
        std::memcpy(bufCTile.data() + idx * sizeof(uint64_t), &result, sizeof(uint64_t));
        
        if (i < 2 && j < 4) {  // Debug first 2 rows
            std::cout << "  PE[" << i << "][" << j << "]=" << result 
                      << " → bufCTile[" << idx << "]=" << result << std::endl;
        }
    }
}
```

---

## Summary in Simple Terms

**What we built:** A hardware accelerator that multiplies matrices using a grid of calculators.

**The problem:** The accelerator computes correct intermediate results, but the test program reads wrong final results.

**What we've verified:**
- ✓ Data is read from memory correctly
- ✓ Calculations are performed correctly (when PEs are reset)
- ✓ Results are collected correctly into a buffer

**ROOT CAUSE FOUND:**
- ✗ **PEs are NOT being reset between DIFFERENT GEMMs!**
- When the first GEMM completes and the second one starts, the PEs still have accumulator values from the previous computation
- Debug output shows: `PE[0][0].accum_before=4298` (should be 0!)

**The Bug:**
```cpp
// In onWeightLoadDone(), line 209:
if (tileK == 0) {
    // Reset PEs
}
```

This only resets PEs when starting a new K-tile within the SAME GEMM operation. But when a NEW GEMM starts (new call to the accelerator), the PEs are never reset!

**The Fix:**
We need to reset PEs when starting a BRAND NEW GEMM operation, not just when tileK==0. This should happen in the write() function when regCmd is written with the start command.

**Next step:** Add PE reset to the start of each new GEMM operation.

---

## Files Reference

- **Implementation:** `src/dev/systolic_accel.{cc,hh}`
- **Test Program:** `tests/systolic_test.{c,riscv}`
- **Configuration:** `configs/mlperf/test_systolic.py`
- **Build System:** `src/dev/SConscript`

---

*Last Updated: December 10, 2025*
*Debug Session: Iteration 4 - PE Accumulation Verified Correct*
