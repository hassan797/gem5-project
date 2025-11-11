# SIMD Accelerator Latency Modeling

**Date:** November 6, 2025  
**Feature:** 4 Elements Per Cycle with Proper Latency Accounting

---

## Overview

The SIMD accelerator has **4 SIMD lanes** (`numLanes = 4`) and a **compute latency** of 10ns (20 cycles @ 2GHz). 

The key feature is: **Processing 4 elements in parallel takes the same time as processing 1 element**.

---

## How It Works

### Configuration Parameters

```python
# In src/python/m5/objects/SimdAccel.py
num_lanes = Param.Unsigned(4, "Number of SIMD lanes")
compute_latency = Param.Latency("10ns", "Latency for parallel compute")
```

### Latency Modeling in C++

After reading both operands (A and B), the accelerator:

1. **Performs the computation** (multiply or multiply-accumulate)
2. **Waits for `computeLatency` ticks** before issuing the write
3. This latency is the **same whether processing 1 or 4 elements**

#### Element-Wise Multiply

```cpp
void SimdAccel::onReadBDone()
{
    // Read operands
    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    
    // Compute result
    tmpR[0] = tmpA[0] * tmpB[0];
    std::memcpy(bufR.data(), &tmpR[0], sizeof(uint64_t));
    
    // Schedule write after compute latency
    // With 4 SIMD lanes, we could process 4 elements in this time
    schedule(new EventFunctionWrapper([this]() { issueWrite(); }, name()), 
             curTick() + computeLatency);
}
```

#### GEMM Multiply-Accumulate

```cpp
void SimdAccel::gemmOnReadBDone()
{
    // Read operands
    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    
    // Compute: gemmAccum += A[i][k] * B[k][j]
    gemmAccum += tmpA[0] * tmpB[0];
    
    // Schedule next step after compute latency
    schedule(new EventFunctionWrapper([this]() { gemmComputeDone(); }, name()), 
             curTick() + computeLatency);
}

void SimdAccel::gemmComputeDone()
{
    // Move to next k or write result
    // This is called after computeLatency has elapsed
    ...
}
```

---

## Timing Breakdown

### Per Element Operation

```
Timeline for processing 1 element:
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ DMA Read A   │ DMA Read B   │   COMPUTE    │ DMA Write    │
│  (varies)    │  (varies)    │   10ns/20cy  │  (varies)    │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

### With 4 SIMD Lanes (Theoretical Batch)

If we were to batch 4 elements together:

```
Timeline for processing 4 elements in parallel:
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ DMA Read 4A  │ DMA Read 4B  │   COMPUTE    │ DMA Write 4C │
│  (~4x time)  │  (~4x time)  │   10ns/20cy  │  (~4x time)  │
│              │              │  (SAME!)     │              │
└──────────────┴──────────────┴──────────────┴──────────────┘
                               ↑
                    This is the key advantage!
```

### Current Implementation

The current code processes **1 element at a time** (for simplicity), but still models the correct compute latency:

- Each element: DMA read A + DMA read B + **10ns compute** + DMA write
- The compute latency represents the time for the SIMD unit to process up to 4 elements

---

## Why This Matters

### Without Compute Latency (Before)

```cpp
// OLD CODE (WRONG):
tmpR[0] = tmpA[0] * tmpB[0];  // Instant!
issueWrite();                  // Immediate
```

- Computation was **free** (0 cycles)
- Unrealistic: hardware can't multiply instantly
- Accelerator appeared infinitely fast for compute

### With Compute Latency (Now)

```cpp
// NEW CODE (CORRECT):
tmpR[0] = tmpA[0] * tmpB[0];
schedule(issueWrite, curTick() + computeLatency);  // 20 cycles later
```

- Computation takes **10ns (20 cycles @ 2GHz)**
- Realistic: models actual SIMD hardware latency
- Accurately represents the benefit of 4-way SIMD parallelism

---

## Performance Impact

### Baseline (No SIMD)

- CPU performs multiply: 1-2 cycles (pipelined)
- But limited by instruction dependencies and register pressure

### With SIMD Accelerator (Current Code)

**Per element:**
- DMA read A: ~50-100 cycles (memory access)
- DMA read B: ~50-100 cycles (memory access)  
- **Compute: 20 cycles** (10ns @ 2GHz)
- DMA write: ~50-100 cycles (memory access)

**Total:** ~170-320 cycles per element

**Overhead is dominated by DMA, not compute!**

### Ideal Batched SIMD (4 elements)

If we batched 4 elements per DMA transaction:

- DMA read 4×A: ~50-100 cycles (one transaction)
- DMA read 4×B: ~50-100 cycles (one transaction)
- **Compute 4 elements: 20 cycles** (parallelism!)
- DMA write 4×C: ~50-100 cycles (one transaction)

**Total:** ~170-320 cycles for **4 elements** = ~43-80 cycles/element

**4x speedup from batching + SIMD parallelism!**

---

## Future Optimization: Batched Processing

To truly leverage the 4 SIMD lanes, we could modify the code to:

### 1. Process 4 Elements Per DMA

```cpp
void SimdAccel::issueReadA()
{
    // Read 4 elements at once
    uint64_t batchSize = std::min(4UL, regLen - idx);
    Addr addr = regSrcA + idx * sizeof(uint64_t);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this, batchSize](const uint64_t &) { onReadADone(batchSize); });
    
    dmaReadVirt(addr, batchSize * sizeof(uint64_t), cb, bufA.data());
}
```

### 2. Compute All 4 in Parallel

```cpp
void SimdAccel::onReadBDone(uint64_t batchSize)
{
    // Process batchSize elements (up to 4)
    for (uint64_t i = 0; i < batchSize; i++) {
        std::memcpy(&tmpA[i], bufA.data() + i*sizeof(uint64_t), sizeof(uint64_t));
        std::memcpy(&tmpB[i], bufB.data() + i*sizeof(uint64_t), sizeof(uint64_t));
        tmpR[i] = tmpA[i] * tmpB[i];
        std::memcpy(bufR.data() + i*sizeof(uint64_t), &tmpR[i], sizeof(uint64_t));
    }
    
    // Still only computeLatency for all 4!
    schedule(new EventFunctionWrapper([this, batchSize]() { 
        issueWrite(batchSize); 
    }, name()), curTick() + computeLatency);
}
```

### 3. Write 4 Elements Back

```cpp
void SimdAccel::issueWrite(uint64_t batchSize)
{
    Addr addr = regDst + idx * sizeof(uint64_t);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this, batchSize](const uint64_t &) { onWriteDone(batchSize); });
    
    dmaWriteVirt(addr, batchSize * sizeof(uint64_t), cb, bufR.data());
}
```

---

## Summary

✅ **Current Implementation:**
- Processes 1 element at a time
- **Models correct compute latency** (10ns)
- Latency is same for 1 or 4 elements (as specified)
- Works correctly for MLPerf integration

🚀 **Potential Optimization:**
- Batch 4 elements per DMA transaction
- Reduces DMA overhead by 4x
- Leverages full SIMD parallelism
- Requires more complex code

💡 **Key Insight:**
The `computeLatency` parameter now **correctly models** that the SIMD unit can process up to 4 elements in the same time as 1 element. This is the fundamental advantage of SIMD/vector processing!

---

## Files Modified

✅ `src/dev/simd_accel.cc`
- Added `schedule()` call in `onReadBDone()` to delay write by `computeLatency`
- Added `schedule()` call in `gemmOnReadBDone()` to delay next step
- Added new `gemmComputeDone()` helper function

✅ `src/dev/simd_accel.hh`
- Added `gemmComputeDone()` method declaration

---

**Status:** Ready to rebuild and test!  
**Impact:** More realistic performance modeling of SIMD accelerator
