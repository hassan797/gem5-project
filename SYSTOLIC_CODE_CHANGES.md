# Systolic Array - Code Changes Summary

## Changed File
`src/dev/systolic_accel.cc`

## Change Location
Function: `kick()` (starts around line 106)

## The Change

### BEFORE (with bug):
```cpp
void
SystolicAccel::kick()
{
    if (currentState != Idle) {
        std::cout << "[SystolicAccel] ERROR: Already busy" << std::endl;
        return;
    }
    
    // Validate dimensions
    if (regDimM == 0 || regDimK == 0 || regDimN == 0) {
        std::cout << "[SystolicAccel] ERROR: Invalid dimensions M=" << regDimM 
                  << " K=" << regDimK << " N=" << regDimN << std::endl;
        return;
    }
    
    std::cout << "[SystolicAccel] Starting GEMM: C[" << regDimM << "×" << regDimN 
              << "] = A[" << regDimM << "×" << regDimK << "] × B[" << regDimK 
              << "×" << regDimN << "]" << std::endl;
    
    regStatus |= 0x1;  // Set busy
    regCmd &= ~0x1ULL; // Clear start bit
    
    // Initialize tiling
    tileI = 0;
    tileJ = 0;
    tileK = 0;
    
    // Start by loading weights for first tile
    startWeightLoad();
}
```

### AFTER (fixed):
```cpp
void
SystolicAccel::kick()
{
    if (currentState != Idle) {
        std::cout << "[SystolicAccel] ERROR: Already busy" << std::endl;
        return;
    }
    
    // Validate dimensions
    if (regDimM == 0 || regDimK == 0 || regDimN == 0) {
        std::cout << "[SystolicAccel] ERROR: Invalid dimensions M=" << regDimM 
                  << " K=" << regDimK << " N=" << regDimN << std::endl;
        return;
    }
    
    std::cout << "[SystolicAccel] Starting GEMM: C[" << regDimM << "×" << regDimN 
              << "] = A[" << regDimM << "×" << regDimK << "] × B[" << regDimK 
              << "×" << regDimN << "]" << std::endl;
    
    regStatus |= 0x1;  // Set busy
    regCmd &= ~0x1ULL; // Clear start bit
    
    // ============ ADDED THIS SECTION ============
    // Reset ALL PEs at the start of a new GEMM operation
    std::cout << "[SystolicAccel] Resetting all PEs" << std::endl;
    for (unsigned i = 0; i < arrayRows; i++) {
        for (unsigned j = 0; j < arrayCols; j++) {
            peArray[i][j].reset();
        }
    }
    // ============ END OF ADDITION ============
    
    // Initialize tiling
    tileI = 0;
    tileJ = 0;
    tileK = 0;
    
    // Start by loading weights for first tile
    startWeightLoad();
}
```

## Lines Added
```cpp
// Reset ALL PEs at the start of a new GEMM operation
std::cout << "[SystolicAccel] Resetting all PEs" << std::endl;
for (unsigned i = 0; i < arrayRows; i++) {
    for (unsigned j = 0; j < arrayCols; j++) {
        peArray[i][j].reset();
    }
}
```

## What This Does
1. **Loops through all PEs** in the array (all rows and columns)
2. **Calls `reset()`** on each PE, which sets its accumulator to 0
3. **Prints a debug message** so you can verify it's happening

## Why This Location
- `kick()` is called when starting a NEW GEMM operation
- It's triggered by writing to the command register (regCmd) via MMIO
- Perfect place to ensure clean state before computation begins
- Happens BEFORE any weights or data are loaded

## Impact
- **Before:** PEs retained values from previous GEMMs → wrong results
- **After:** PEs start at 0 for each GEMM → correct results

## How to Verify the Fix

1. **Rebuild:**
   ```bash
   cd "/home/hassan/Desktop/stage/gem5 mlperf"
   scons build/RISCV/gem5.opt -j4
   ```

2. **Run test:**
   ```bash
   ./build/RISCV/gem5.opt configs/mlperf/test_systolic.py --binary=tests/systolic_test.riscv
   ```

3. **Look for in output:**
   - `[SystolicAccel] Resetting all PEs` (should appear 4 times - once per test)
   - `✓ PASS` for all tests
   - No `accum_before=4298` type errors (should be 0)

## Additional Debug Output Added (for troubleshooting)
Several debug outputs were added throughout to help trace the execution:
- B matrix loading progress
- A and B values used in computation  
- PE accumulator values before/after each MAC operation
- Result collection from PEs
- DMA write addresses and values

These can be removed later for production, but are helpful for understanding the flow.

---

*Date: December 10, 2025*
*Fix Type: Bug Fix - State Initialization*
*Lines Changed: ~7 lines added*
*Files Modified: 1 file (src/dev/systolic_accel.cc)*
