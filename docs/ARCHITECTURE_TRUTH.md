# Architecture Truth: What We Actually Built

## The Honest Architecture Diagram

### What We ACTUALLY Have (Sequential Processing)

```
┌─────────────────────────────────────────────────────────────┐
│              Our "SIMD" Accelerator (Actually Sequential)   │
│                                                             │
│  Time ───────────────────────────────────────────>         │
│                                                             │
│  Cycle 1-10:   [Read A[0] via DMA]                         │
│  Cycle 11-20:  [Read B[0] via DMA]                         │
│  Cycle 21:     [Compute: C[0] = A[0] * B[0]]  ← ONE       │
│  Cycle 22-31:  [Write C[0] via DMA]                        │
│                                                             │
│  Cycle 32-41:  [Read A[1] via DMA]                         │
│  Cycle 42-51:  [Read B[1] via DMA]                         │
│  Cycle 52:     [Compute: C[1] = A[1] * B[1]]  ← ONE       │
│  Cycle 53-62:  [Write C[1] via DMA]                        │
│                                                             │
│  ... (repeat for each element, ONE AT A TIME)              │
│                                                             │
│  Hardware:                                                  │
│  ┌──────────────┐                                          │
│  │ Single ALU   │  ← Only ONE multiplier                   │
│  │ (1 multiply) │                                          │
│  └──────────────┘                                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### What TRUE SIMD Looks Like (4-Lane SIMD)

```
┌─────────────────────────────────────────────────────────────┐
│                   True 4-Lane SIMD Accelerator              │
│                                                             │
│  Time ───────>                                              │
│                                                             │
│  Cycle 1-10:   [Read A[0,1,2,3] via DMA - 4 elements]      │
│  Cycle 11-20:  [Read B[0,1,2,3] via DMA - 4 elements]      │
│  Cycle 21:     ┌─ C[0] = A[0] * B[0] ─┐                    │
│                ├─ C[1] = A[1] * B[1] ─┤  ← All 4           │
│                ├─ C[2] = A[2] * B[2] ─┤  ← In parallel!    │
│                └─ C[3] = A[3] * B[3] ─┘                    │
│  Cycle 22-31:  [Write C[0,1,2,3] via DMA - 4 elements]     │
│                                                             │
│  Cycle 32-41:  [Read A[4,5,6,7] via DMA]                   │
│  Cycle 42-51:  [Read B[4,5,6,7] via DMA]                   │
│  Cycle 52:     [Compute 4 more results in parallel]        │
│  Cycle 53-62:  [Write C[4,5,6,7] via DMA]                  │
│                                                             │
│  Hardware:                                                  │
│  ┌──────────────┐ ┌──────────────┐                         │
│  │  ALU Lane 0  │ │  ALU Lane 1  │                         │
│  │ (multiply 0) │ │ (multiply 1) │                         │
│  └──────────────┘ └──────────────┘                         │
│  ┌──────────────┐ ┌──────────────┐                         │
│  │  ALU Lane 2  │ │  ALU Lane 3  │  ← 4 multipliers!       │
│  │ (multiply 2) │ │ (multiply 3) │                         │
│  └──────────────┘ └──────────────┘                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### What RVV Provides (CPU-based SIMD)

```
┌─────────────────────────────────────────────────────────────┐
│              RISC-V Vector Extension (RVV) on CPU           │
│                                                             │
│  Time ───>                                                  │
│                                                             │
│  Cycle 1-2:  vle64.v v0, (A)  ← Load vector (8 elements)   │
│  Cycle 3-4:  vle64.v v1, (B)  ← Load vector (8 elements)   │
│  Cycle 5:    vmul.vv v2, v0, v1                             │
│              ┌─ C[0] = A[0] * B[0] ─┐                       │
│              ├─ C[1] = A[1] * B[1] ─┤                       │
│              ├─ C[2] = A[2] * B[2] ─┤                       │
│              ├─ C[3] = A[3] * B[3] ─┤  ← All 8 in one      │
│              ├─ C[4] = A[4] * B[4] ─┤  ← instruction!       │
│              ├─ C[5] = A[5] * B[5] ─┤                       │
│              ├─ C[6] = A[6] * B[6] ─┤                       │
│              └─ C[7] = A[7] * B[7] ─┘                       │
│  Cycle 6-7:  vse64.v v2, (C)  ← Store vector (8 elements)   │
│                                                             │
│  Total: ~7 cycles for 8 elements!                           │
│                                                             │
│  CPU Hardware:                                              │
│  ┌────────────────────────────────────────────────────────┐│
│  │  Vector Register File (128-bit to 2048-bit wide)      ││
│  │  [v0] [v1] [v2] ... [v31]                             ││
│  └────────────────────────────────────────────────────────┘│
│  ┌────────────────────────────────────────────────────────┐│
│  │  Vector Execution Unit (8 or more ALUs in parallel)   ││
│  │  [ALU0] [ALU1] [ALU2] ... [ALU7]                      ││
│  └────────────────────────────────────────────────────────┘│
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Performance Comparison (Processing 8 Elements)

| Method | Cycles | Speedup | True SIMD? |
|--------|--------|---------|------------|
| Our Accelerator | ~248 | 1.0x (baseline) | ❌ NO |
| True 4-Lane SIMD | ~122 | 2.0x | ✅ YES |
| RVV (8-wide) | ~7 | 35x | ✅ YES |
| CPU Scalar | ~40 | 6.2x | ❌ NO |

**Note:** Our accelerator is actually SLOWER than CPU scalar because of DMA overhead!

---

## What Each Approach Does

### Our Accelerator:
```
for (int i = 0; i < 8; i++) {    // Loop 8 times
    A_i = DMA_read(A + i);        // Read one
    B_i = DMA_read(B + i);        // Read one
    C_i = A_i * B_i;              // Compute one
    DMA_write(C + i, C_i);        // Write one
}
// Result: Sequential, one element per iteration
```

### True 4-Lane SIMD:
```
for (int i = 0; i < 8; i += 4) {     // Loop 2 times (8/4)
    A[0:3] = DMA_read(A + i, 4);     // Read four
    B[0:3] = DMA_read(B + i, 4);     // Read four
    // Compute four IN PARALLEL
    C[0] = A[0] * B[0];  }
    C[1] = A[1] * B[1];  } Same cycle
    C[2] = A[2] * B[2];  } 
    C[3] = A[3] * B[3];  }
    DMA_write(C + i, C[0:3], 4);     // Write four
}
// Result: Parallel, four elements per iteration
```

### RVV:
```
vsetvli vl, 8, e64    // Set vector length = 8
vle64.v v0, (A)       // Load 8 elements into v0
vle64.v v1, (B)       // Load 8 elements into v1
vmul.vv v2, v0, v1    // Multiply ALL 8 pairs in ONE instruction
vse64.v v2, (C)       // Store 8 elements from v2
// Result: ALL elements in a few instructions!
```

---

## Code Evidence

### From Our Implementation (simd_accel.cc):

```cpp
// Line 134: Process ONE element
void SimdAccel::issueReadA() {
    const Addr a = regSrcA + idx * sizeof(uint64_t);  // Address of ONE element
    dmaReadVirt(a, sizeof(uint64_t), cb, bufA.data()); // Read ONE uint64_t
}

// Line 158: Compute ONE result
void SimdAccel::onReadBDone() {
    std::memcpy(&tmpB, bufA.data(), sizeof(uint64_t));
    tmpR = tmpA * tmpB;  // ONE multiplication
    issueWrite();
}

// Line 177: Move to next element
void SimdAccel::onWriteDone() {
    idx++;  // Increment by ONE
    if (idx < regLen) {
        issueReadA();  // Process NEXT single element
    }
}
```

**KEY OBSERVATION:** 
- Variables are `tmpA`, `tmpB`, `tmpR` (singular, not arrays)
- DMA reads `sizeof(uint64_t)` (8 bytes, ONE element)
- Index increments by 1 (not by 4)

### What True SIMD Would Look Like:

```cpp
// Hypothetical true 4-lane SIMD
static const int NUM_LANES = 4;
uint64_t tmpA[NUM_LANES];  // Array for 4 elements
uint64_t tmpB[NUM_LANES];  // Array for 4 elements
uint64_t tmpR[NUM_LANES];  // Array for 4 elements

void SimdAccel::issueReadA() {
    const Addr a = regSrcA + idx * sizeof(uint64_t);
    size_t batch = min(NUM_LANES, regLen - idx);
    dmaReadVirt(a, batch * sizeof(uint64_t), cb, bufA.data()); // Read 4 elements
}

void SimdAccel::onReadBDone() {
    // Unpack 4 elements
    std::memcpy(tmpA, bufA.data(), NUM_LANES * sizeof(uint64_t));
    std::memcpy(tmpB, bufB.data(), NUM_LANES * sizeof(uint64_t));
    
    // Compute 4 results (simulated parallel)
    for (int lane = 0; lane < NUM_LANES; lane++) {
        tmpR[lane] = tmpA[lane] * tmpB[lane];  // 4 multiplications
    }
    
    std::memcpy(bufR.data(), tmpR, NUM_LANES * sizeof(uint64_t));
    issueWrite();
}

void SimdAccel::onWriteDone() {
    idx += NUM_LANES;  // Jump by 4!
    if (idx < regLen) {
        issueReadA();
    }
}
```

---

## The Configuration Lie

### In simd_accel_se.py:
```python
system.simd = SimdAccel(pio_addr=0x40000000, pio_size=0x1000,
                        numLanes=4,  # ← This parameter...
                        computeLatencyPerElem=1)
```

### In SimdAccel.py (Python SimObject):
```python
class SimdAccel(DmaDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'
    
    pio_addr = Param.Addr(0x40000000, "Base PIO address")
    pio_latency = Param.Latency("100ns", "Programmed IO latency")
    pio_size = Param.Addr(0x100, "PIO region size")
    
    # NOTE: numLanes is NOT defined here!
    # NOTE: computeLatencyPerElem is NOT defined here!
```

**Result:** gem5 silently ignores these parameters. They do NOTHING.

---

## Conclusion

### What We Have:
- ✅ Working accelerator
- ✅ DMA implementation
- ✅ Custom instructions
- ✅ Correct gem5 integration
- ❌ NOT SIMD (no parallelism)

### What We Should Call It:
**"Sequential Array Processing Accelerator with DMA"**

### What It Actually Demonstrates:
- Accelerator design principles
- Hardware offloading
- ISA extensions
- Device modeling

### The Key Takeaway:
**It's a successful project, just not SIMD.**  
**Be honest about what it does.**
