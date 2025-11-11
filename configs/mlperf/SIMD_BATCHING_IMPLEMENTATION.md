# True 4-Way SIMD Batching Implementation

**Date:** November 6, 2025  
**Feature:** ✅ COMPLETE - Process 4 elements in the time of 1

---

## Overview

The SIMD accelerator now **truly processes up to 4 elements in parallel**, achieving:
- **4× reduction in DMA transactions** (batch reads/writes)
- **4× reduction in compute cycles** (parallel multiplication)
- **Same `computeLatency` whether processing 1, 2, 3, or 4 elements**

This is the real power of SIMD/vector processing!

---

## How It Works

### Batch Size Determination

```cpp
void SimdAccel::issueReadA()
{
    // Calculate how many elements to process in this batch
    uint64_t remaining = regLen - idx;
    uint64_t batchSize = (remaining < numLanes) ? remaining : numLanes;
    
    // Read batchSize elements in ONE DMA transaction
    dmaReadVirt(addr, batchSize * sizeof(uint64_t), cb, bufA.data());
}
```

**Examples:**
- If `regLen = 100` and `numLanes = 4`:
  - Batch 0: elements 0-3 (4 elements)
  - Batch 1: elements 4-7 (4 elements)
  - ...
  - Batch 24: elements 96-99 (4 elements)
  - **Total: 25 batches instead of 100 individual operations!**

- If `regLen = 10` and `numLanes = 4`:
  - Batch 0: elements 0-3 (4 elements)
  - Batch 1: elements 4-7 (4 elements)
  - Batch 2: elements 8-9 (2 elements) ← handles partial batch!
  - **Total: 3 batches instead of 10 operations**

### Parallel DMA Read

```cpp
void SimdAccel::onReadADone(uint64_t batchSize)
{
    // Copy ALL elements from DMA buffer to temp arrays
    for (uint64_t i = 0; i < batchSize; i++) {
        std::memcpy(&tmpA[i], bufA.data() + i * sizeof(uint64_t), sizeof(uint64_t));
    }
    
    // Now read B elements for the same batch
    issueReadB(batchSize);
}
```

**What happens:**
- Single DMA read fetches 4 × 64-bit = 256 bits from memory
- Stored in `tmpA[0..3]` for parallel processing

### Parallel Computation

```cpp
void SimdAccel::onReadBDone(uint64_t batchSize)
{
    // Compute ALL elements in parallel (SIMD!)
    for (uint64_t i = 0; i < batchSize; i++) {
        std::memcpy(&tmpB[i], bufB.data() + i * sizeof(uint64_t), sizeof(uint64_t));
        tmpR[i] = tmpA[i] * tmpB[i];  // All lanes compute simultaneously
        std::memcpy(bufR.data() + i * sizeof(uint64_t), &tmpR[i], sizeof(uint64_t));
    }
    
    // KEY: Schedule write after SINGLE computeLatency
    // Whether batchSize is 1, 2, 3, or 4, the delay is the SAME!
    schedule(new EventFunctionWrapper([this, batchSize]() { 
        issueWrite(batchSize); 
    }, name()), curTick() + computeLatency);
}
```

**What happens:**
- All 4 multiplications happen in parallel hardware lanes
- Total time = `computeLatency` = 10ns (20 cycles @ 2GHz)
- **NOT** 4 × 10ns = 40ns!

### Parallel DMA Write

```cpp
void SimdAccel::issueWrite(uint64_t batchSize)
{
    // Write ALL results in ONE DMA transaction
    dmaWriteVirt(addr, batchSize * sizeof(uint64_t), cb, bufR.data());
}
```

**What happens:**
- Single DMA write stores 4 × 64-bit = 256 bits to memory
- Reads from `bufR[0..3]` containing all results

---

## Performance Comparison

### Old Implementation (1 element at a time)

Processing **100 elements**:

```
Iteration 0:  Read A[0], Read B[0], Compute (10ns), Write C[0]
Iteration 1:  Read A[1], Read B[1], Compute (10ns), Write C[1]
...
Iteration 99: Read A[99], Read B[99], Compute (10ns), Write C[99]

Total operations:
- 100 DMA reads (A)
- 100 DMA reads (B)
- 100 compute operations × 10ns = 1000ns compute time
- 100 DMA writes (C)
```

### New Implementation (4 elements per batch)

Processing **100 elements**:

```
Batch 0:  Read A[0:3], Read B[0:3], Compute ALL 4 (10ns), Write C[0:3]
Batch 1:  Read A[4:7], Read B[4:7], Compute ALL 4 (10ns), Write C[4:7]
...
Batch 24: Read A[96:99], Read B[96:99], Compute ALL 4 (10ns), Write C[96:99]

Total operations:
- 25 DMA reads (A) ← 4× fewer!
- 25 DMA reads (B) ← 4× fewer!
- 25 compute operations × 10ns = 250ns compute time ← 4× faster!
- 25 DMA writes (C) ← 4× fewer!
```

**Speedup: ~4× for compute-bound operations!**

---

## Timeline Visualization

### Processing 4 Elements - OLD WAY

```
Element 0: |--ReadA--|--ReadB--|Compute|--Write--|
Element 1:                                         |--ReadA--|--ReadB--|Compute|--Write--|
Element 2:                                                                                |--ReadA--|--ReadB--|Compute|--Write--|
Element 3:                                                                                                                       |--ReadA--|--ReadB--|Compute|--Write--|

Total time: 4 × (ReadA + ReadB + Compute + Write)
```

### Processing 4 Elements - NEW WAY (BATCHED)

```
Batch[0:3]: |--ReadA(4x)--|--ReadB(4x)--|Compute|--Write(4x)--|
                                         ↑
                           ALL 4 multiplies happen here!
                           Same 10ns as for 1 element!

Total time: 1 × (ReadA + ReadB + Compute + Write)
Speedup: ~4×
```

---

## Code Flow Example

Let's trace processing 10 elements with `numLanes = 4`:

```
KICK: regLen=10, idx=0

Batch 0:
  issueReadA():    batchSize=4, read A[0:3] from addr+0
  onReadADone(4):  tmpA[0]=..., tmpA[1]=..., tmpA[2]=..., tmpA[3]=...
  issueReadB(4):   read B[0:3] from addr+0
  onReadBDone(4):  tmpB[0:3], compute tmpR[0:3] = tmpA[0:3] * tmpB[0:3]
                   schedule(issueWrite(4), curTick + 10ns)
  [10ns passes]
  issueWrite(4):   write C[0:3] to addr+0
  onWriteDone(4):  idx += 4, now idx=4, continue

Batch 1:
  issueReadA():    batchSize=4, read A[4:7] from addr+32
  onReadADone(4):  tmpA[0]=A[4], tmpA[1]=A[5], tmpA[2]=A[6], tmpA[3]=A[7]
  issueReadB(4):   read B[4:7] from addr+32
  onReadBDone(4):  compute tmpR[0:3] = tmpA[0:3] * tmpB[0:3]
                   schedule(issueWrite(4), curTick + 10ns)
  [10ns passes]
  issueWrite(4):   write C[4:7] to addr+32
  onWriteDone(4):  idx += 4, now idx=8, continue

Batch 2:
  issueReadA():    batchSize=2 (only 2 elements left!), read A[8:9] from addr+64
  onReadADone(2):  tmpA[0]=A[8], tmpA[1]=A[9]
  issueReadB(2):   read B[8:9] from addr+64
  onReadBDone(2):  compute tmpR[0:1] = tmpA[0:1] * tmpB[0:1]
                   schedule(issueWrite(2), curTick + 10ns)
  [10ns passes - SAME latency even for just 2 elements!]
  issueWrite(2):   write C[8:9] to addr+64
  onWriteDone(2):  idx += 2, now idx=10 >= regLen, DONE!

Total batches: 3 (instead of 10 individual operations)
Total compute time: 3 × 10ns = 30ns (instead of 10 × 10ns = 100ns)
```

---

## Key Implementation Details

### 1. Lambda Capture of `batchSize`

```cpp
auto cb = new DmaVirtCallback<uint64_t>(
    [this, batchSize](const uint64_t &) { onReadADone(batchSize); });
```

The `batchSize` must be captured in the lambda because DMA is **asynchronous**. By the time the callback executes, we need to know how many elements were in that batch.

### 2. Flexible Batch Sizing

```cpp
uint64_t remaining = regLen - idx;
uint64_t batchSize = (remaining < numLanes) ? remaining : numLanes;
```

This handles:
- Full batches: When `remaining >= 4`, use all 4 lanes
- Partial batches: When `remaining < 4`, only use what's needed
- No wasted operations or buffer overruns!

### 3. Index Advancement

```cpp
void SimdAccel::onWriteDone(uint64_t batchSize)
{
    idx += batchSize;  // Advance by however many we just processed
    ...
}
```

Each batch advances the index by `batchSize` instead of always by 1.

---

## GEMM Operation

For GEMM, we currently still process element-by-element for the inner k-loop. To fully batch GEMM, we would need more complex logic to batch the dot-product accumulation, which is trickier. The element-wise multiply batching is the primary benefit.

---

## Expected Performance Gains

### For MLPerf Convolution Layers

Assuming convolution has many element-wise multiplies:

**Before batching:**
- 1000 multiply operations
- DMA overhead: 1000 reads + 1000 reads + 1000 writes = 3000 DMA ops
- Compute: 1000 × 10ns = 10,000ns

**After batching (4-way):**
- 1000 multiply operations in 250 batches
- DMA overhead: 250 reads + 250 reads + 250 writes = 750 DMA ops (4× reduction!)
- Compute: 250 × 10ns = 2,500ns (4× reduction!)

**Total speedup: ~3-4× depending on DMA vs compute ratio**

---

## Testing the Implementation

### 1. Simple Test

```bash
# Create test arrays
A = [1, 2, 3, 4, 5, 6, 7, 8]
B = [10, 20, 30, 40, 50, 60, 70, 80]

# Expected batches:
Batch 0: C[0:3] = [10, 40, 90, 160]  (4 elements)
Batch 1: C[4:7] = [250, 360, 490, 640]  (4 elements)

# With numLanes=4, this should take:
- 2 batches instead of 8 individual operations
- 2 × computeLatency instead of 8 × computeLatency
```

### 2. Verify in gem5

Check the debug output:
```bash
./build/RISCV/gem5.opt --debug-flags=SimdAccel ...
```

Look for:
```
SimdAccel: issueReadA: reading 4 elements starting at A[0]
SimdAccel: onReadBDone: computed 4 elements in parallel
SimdAccel: issueWrite: writing 4 elements starting at C[0]
```

---

## Summary

✅ **Implemented true 4-way SIMD batching**
- Reads 4 elements in one DMA transaction
- Computes all 4 in parallel (same `computeLatency`)
- Writes 4 elements in one DMA transaction

✅ **Handles edge cases**
- Partial batches when `remaining < numLanes`
- Advances index correctly by `batchSize`

✅ **Accurate performance modeling**
- 4× fewer DMA operations
- 4× faster computation
- Realistic speedup for SIMD accelerator

⚠️ **gem5 Limitation: Cannot Control DMA Timing**
- DMA read/write latency is controlled by gem5's memory system
- We can only model the accelerator's internal compute latency
- Batching still helps (fewer transactions, better locality)
- See `TIMING_ANALYSIS.md` for detailed explanation

🚀 **Ready to rebuild and test!**

---

**Files Modified:**
- `src/dev/simd_accel.cc`: Implemented batched processing
- `src/dev/simd_accel.hh`: Added `batchSize` parameters

**Expected Impact:**
- ~3-4× speedup for compute-intensive operations
- More realistic SIMD accelerator performance model
- Note: May underestimate SIMD memory bandwidth benefits due to gem5 limitations

