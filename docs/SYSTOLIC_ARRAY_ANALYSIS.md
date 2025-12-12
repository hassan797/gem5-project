# Systolic Array Accelerator Analysis (gem5-aladdin)

## High-Level Architecture Overview

The gem5-aladdin systolic array is a 2D grid of Processing Elements (PEs) designed for **convolution operations** in neural networks. It uses an **output-stationary dataflow** where partial results accumulate in PEs as data flows through.

### Core Concept
Think of it like a grid of tiny calculators (PEs) where:
- **Inputs** flow horizontally (left to right) across PE rows
- **Weights** flow vertically (top to bottom) down PE columns  
- **Outputs** accumulate inside PEs and are collected at the end

Each PE does one multiply-accumulate (MAC): `output = input × weight + previous_output`

---

## Key Components (Conceptual Breakdown)

### 1. **Processing Element (PE)** (`pe.h`, `pe.cpp`)

**What it is:**  
The basic compute unit. A PE has:
- **Input Register** (holds activation/input pixel data)
- **Weight Register** (holds filter/kernel weight)
- **Output Register** (accumulates partial sum)
- **MAC Unit** (multiply-accumulate logic)

**How it works:**
```
MAC operation: output_reg = input_reg × weight_reg + output_reg
```

After computing, the PE:
- Passes its input to the next PE on the right
- Passes its weight to the next PE below
- Keeps its accumulated output until the full convolution window is done

**Key insight:** PEs are **stateless** except for the output accumulator. Data just flows through registers.

---

### 2. **PE Array (2D Grid)** (`dataflow.h`, `dataflow.cpp`)

**Structure:**
- Organized as `peArrayRows × peArrayCols` (e.g., 8×8 = 64 PEs)
- Each **column** produces one output feature map
- Each **row** processes different spatial positions in the output

**Dataflow states:**
- **Idle:** No work assigned
- **Prefill:** Fetch units are loading data into FIFOs; PEs wait
- **Compute:** Data flows through PEs, MACs happen every cycle

**Folding concept:**  
Because real conv layers are huge (e.g., 512 output channels, 1024 spatial positions), they **fold** the workload:
- **Weight fold:** Split kernel channels across multiple passes
- **Output fold:** Split output spatial positions across multiple passes

---

### 3. **Fetch Units** (`fetch.h`, `fetch.cpp`)

**Purpose:** Stream data from scratchpads (local SRAMs) into the PE array.

**Two types:**
- **InputFetch:** Reads activation windows, feeds PE rows (left edge)
- **WeightFetch:** Reads kernel weights, feeds PE columns (top edge)

**How streaming works:**
1. Fetch unit reads a line from its scratchpad (via DMA-like port)
2. Buffers it in a FIFO queue (`fetchQueue`)
3. Pushes one element per cycle into the connected PE's input/weight register
4. Tracks tensor indices to know what spatial position each element represents

**Prefill phase:** Before compute starts, fetch units fill their queues so the first cycle of compute has data ready.

**Barrier synchronization:** When one weight fold finishes, all fetch units pause at a barrier before starting the next fold.

---

### 4. **Commit Units** (`commit.h`, `commit.cpp`)

**Purpose:** Collect finished outputs from PE rows and write them back to the output scratchpad.

**How it works:**
1. Each commit unit monitors a PE row's output registers
2. When an output pixel is fully accumulated (all weight folds done), it collects it into a buffer
3. Once enough data (a cache line's worth) is collected, it writes back to the scratchpad
4. Uses a queue (`commitQueue`) to buffer pending writebacks

**Accumulation mode:** If `accum_results=true`, commit units read previous output values from the scratchpad and add them to new results (for multi-layer fusion).

---

### 5. **Scratchpads (Local SRAMs)** (`scratchpad.h`, `scratchpad.cpp`, `SystolicArray.py`)

**Three scratchpads:**
- **inputSpad:** Stores input activations
- **weightSpad:** Stores convolution kernels
- **outputSpad:** Stores output feature maps

**Key features:**
- Banked memory (e.g., 16 banks) to allow parallel access
- Cyclic partitioning (round-robin address mapping to banks)
- Configurable line size (e.g., 8 bytes)
- Connected to fetch/commit units via crossbar buses (`SpadXBar`)

**Bank conflicts:** If multiple requests hit the same bank in one cycle, they serialize (modeled with latency tracking).

**Why scratchpads?**  
Much faster than main memory (DRAM). Data is DMA'd from DRAM → scratchpad once, then reused many times during tiled convolution.

---

### 6. **DMA and Memory Hierarchy** (`systolic_array.cpp`, state machine)

**Memory flow:**
1. **Host CPU** writes parameters (input/weight/output base addresses, dimensions, stride, etc.) to accelerator via MMIO
2. **Accelerator** issues DMA reads to pull inputs/weights from DRAM into scratchpads
3. **Computation** happens using scratchpad data
4. **Accelerator** issues DMA writes to push outputs from scratchpad back to DRAM
5. **Finish signal** sent to CPU (interrupt or flag write)

**State machine:**
```
Idle → ReadyForDmaInputRead → WaitingForDmaInputRead →
ReadyForDmaWeightRead → WaitingForDmaWeightRead →
ReadyToCompute → WaitingForCompute →
ReadyForDmaWrite → WaitingForDmaWrite →
ReadyToSendFinish → WaitForFinishSignalAck → Idle
```

**Aladdin dependency:**  
They inherit from `Gem5Datapath` (Aladdin's accelerator base class) which provides:
- DMA request splitting/coalescing
- TLB for virtual address translation
- Finish signal mechanism
- Command queue for multi-operation sequences

---

## Data Layout & Tensor Handling

### Tensor Format: **NHWC** (Not NCHW)
- **N:** Batch (usually 1 in embedded systems)
- **H:** Height (rows)
- **W:** Width (cols)
- **C:** Channels

**Why NHWC?**  
Spatial neighbors (adjacent pixels in same channel) are close in memory → better for the systolic flow where input windows slide spatially.

### TensorShape & TensorIndexIterator (`tensor.h`, `tensor.cpp`)
- Tracks dimensions with optional padding for alignment
- Iterator provides linearized indices for multi-dimensional access
- Handles halo padding (zero-padded borders for convolution)

---

## Output-Stationary Dataflow Explained

**Core idea:** Partial sums stay in PE output registers; inputs and weights flow past them.

**Example (4×4 PE array, 3×3 conv):**
```
    Kernel0   Kernel1   Kernel2   Kernel3
       ↓         ↓         ↓         ↓
InputWin0 → [PE00] → [PE01] → [PE02] → [PE03]
InputWin1 → [PE04] → [PE05] → [PE06] → [PE07]
InputWin2 → [PE08] → [PE09] → [PE10] → [PE11]
InputWin3 → [PE12] → [PE13] → [PE14] → [PE15]
```

- Each PE column accumulates one output channel across many input windows
- Each PE row processes different spatial output positions
- Input windows (e.g., 3×3×8 regions) slide through horizontally
- Kernels (different filters) stream down vertically

**Timing:**  
- Cycle 1: InputWin0 enters PE00, Kernel0 enters PE00
- Cycle 2: InputWin0 moves to PE01, new InputWin1 enters PE00
- ...PEs compute MACs every cycle as data flows

---

## Activation Functions (`activations.h`, `activations.cpp`)

After convolution completes, outputs can pass through activation:
- ReLU, Leaky ReLU, ELU, SELU
- Tanh, Hard Tanh, Sigmoid, Softmax

Implemented as a post-processing step on output data before writeback or during commit.

---

## What They Did vs. What You Need

### Their Dependencies (What to Remove/Replace)

1. **Aladdin (`Gem5Datapath`):**
   - Base class providing DMA infrastructure, TLB, command queue
   - **Replace with:** Your own `DmaVirtDevice` inheritance + simple state machine

2. **Aladdin TLB:**
   - Virtual-to-physical address translation for DMA
   - **Replace with:** `translate()` method using process page table (like your SIMD accelerator)

3. **Complex scratchpad modeling:**
   - Banked SRAM with conflict tracking
   - **Simplify to:** Simple buffers or gem5 `SRAM`/`SimpleMemory` with fixed latency

4. **Ticked objects:**
   - gem5's `Ticked` base for cycle-accurate evaluation
   - **Replace with:** Event-driven callbacks (like your SIMD accelerator uses)

5. **Multi-dimensional tensor iterators:**
   - **Simplify to:** Direct index math for your specific use case (e.g., just GEMM)

### Core Concepts to Keep

1. **PE array structure:** Grid of MACs
2. **Dataflow pattern:** How data flows through (you can adapt output-stationary or use weight-stationary)
3. **Fetch-Compute-Commit pipeline:** Separate units for input streaming, computation, output collection
4. **Scratchpad hierarchy:** Local fast memory close to PEs
5. **Folding/tiling:** Break large matrices into chunks that fit your PE array
6. **MMIO parameter passing:** CPU writes config, accelerator reads and executes

---

## Minimal Systolic Array for Your Project

**Goal:** Matrix multiplication C = A × B using systolic array instead of your current SIMD batching.

### Simplified Architecture

**Components:**
1. **PE:** Single MAC unit with 3 registers (A input, B input, C accumulator)
2. **PE Grid:** N×N array (e.g., 4×4 or 8×8)
3. **Control FSM:** State machine to orchestrate data movement
4. **Input Buffers:** Small FIFOs to feed A rows and B columns into PE array
5. **Output Collection:** Gather results from PEs when done

**No need for:**
- Complex scratchpad banking
- Aladdin integration
- Tensor shape iterators
- Multiple activation functions (start with just raw output)
- DMA chunking (can be added later)

**Data flow (weight-stationary for GEMM):**
- Preload weights (B matrix elements) into PEs
- Stream A matrix rows from left
- Accumulate C in PEs
- Drain C values when done

---

## Next Steps (What I'll Help You Build)

1. **PE class:** Simple multiply-accumulate with registers
2. **Systolic grid:** 2D array of PEs with connectivity
3. **Control logic:** FSM to load/compute/drain phases
4. **MMIO interface:** Registers for A/B/C pointers, dimensions, start/status
5. **DMA integration:** Use your existing `dmaReadVirt`/`dmaWriteVirt` pattern
6. **gem5 SimObject:** Python wrapper similar to your `SimdAccel.py`

**Advantages over SIMD batching:**
- True 2D parallelism (e.g., 16 MACs per cycle in 4×4 array vs. 4 MACs)
- More scalable (add more PEs without complex DMA logic)
- Industry-standard architecture (Google TPU, Apple ANE use systolic arrays)

---

## Summary

The gem5-aladdin systolic array is a **cycle-accurate model** of a **convolution accelerator** with:
- 2D PE grid doing MACs
- Output-stationary dataflow
- Local scratchpads for fast data access
- Fetch/commit units for streaming I/O
- Folding to handle large layers

For your project, we'll **strip out Aladdin dependencies** and **simplify to GEMM-focused systolic array** using concepts you already understand (DMA, MMIO, event-driven simulation) from your SIMD accelerator.

**Ready to build?** Let me know and I'll start creating the minimal systolic array device for your gem5-mlperf project.
