# Custom Instructions and Execution Flow in gem5 SIMD Accelerator

## Introduction

This document explains how custom RISC-V instructions were implemented to control our SIMD accelerator device within the gem5 simulator, and provides a detailed walkthrough of how program execution flows from user code through the simulator to the hardware device model. The goal is to make these concepts accessible to readers who may not be deeply familiar with computer architecture simulation or instruction set extensions.

At a high level, we needed a way for user programs to communicate with our accelerator device. Rather than using traditional system calls or device drivers, we chose to create special processor instructions that act as a control interface. This approach keeps the software simple while demonstrating how instruction set architectures can be extended to support new hardware features.

---

## Part 1: Custom Instruction Implementation

### Understanding the Problem

When you write a program that needs to use special hardware, there are typically several ways to communicate with that hardware. In a full operating system environment, you might use device driver interfaces or system calls. However, in our gem5 simulation running in System Call Emulation (SE) mode, we wanted a more direct path. We decided to create custom processor instructions that would act as commands to configure and control our accelerator.

It's crucial to understand what these custom instructions are NOT doing: they are not performing SIMD operations themselves. RISC-V already has a standard Vector Extension (RVV) that provides true SIMD instructions where the CPU itself can process multiple data elements in parallel (instructions like `vmul.vv` that multiply entire vectors). Our custom instructions serve a different purpose entirely—they are a **control interface** for an external hardware accelerator.

Think of these custom instructions as special words in the processor's vocabulary. Just as a processor knows how to add two numbers when it sees an ADD instruction, we taught it to recognize new instructions that mean "set the source address" or "start the computation." The actual SIMD computation happens inside the dedicated accelerator hardware, not in the CPU.

**Why Use Custom Instructions Instead of RVV?**

This is an important design question. We have two approaches to SIMD computing:

1. **CPU-based SIMD (RVV)**: Use RISC-V Vector Extension instructions where the CPU itself has vector registers and execution units. The CPU processes multiple elements per instruction cycle. This is what tests like `rvv_multiply_test.c` demonstrate.

2. **Accelerator-based SIMD (our approach)**: Offload array/matrix operations to a separate hardware device that specializes in these operations. The CPU uses custom instructions only to configure and control the accelerator, then the accelerator does the heavy lifting independently via DMA.

We chose the accelerator approach because:
- It demonstrates **hardware-software co-design**: how custom hardware and ISA extensions work together
- It models **real-world systems** where GPUs, TPUs, and other accelerators are separate devices controlled by the CPU
- It allows **independent operation**: the accelerator can work concurrently with the CPU via DMA
- It's more **extensible**: we can add complex operations (like GEMM) that would be cumbersome with standard vector instructions
- It teaches **device integration**: how to build, configure, and interface with custom gem5 devices

The custom instructions are the communication channel, not the computational engine.

### The RISC-V Instruction Format

RISC-V processors use a fixed 32-bit instruction format, meaning every instruction is exactly 32 bits long. Different types of instructions follow different layouts within those 32 bits. We chose the R-type format, which is typically used for register-to-register operations. An R-type instruction is structured as follows:

```
|31    25|24  20|19  15|14  12|11   7|6     0|
| funct7 |  rs2 |  rs1 |funct3|   rd |opcode |
```

Each field has a specific purpose:
- **opcode** (7 bits): Identifies the major category of the instruction
- **funct3** (3 bits): Provides additional specification within that category
- **funct7** (7 bits): Offers even more fine-grained control
- **rs1, rs2** (5 bits each): Source register numbers
- **rd** (5 bits): Destination register number

For our custom instructions, we selected opcode `0x0B` (which corresponds to the custom-0 space reserved in RISC-V for user extensions) and funct7 `0x01`. We then used different funct3 values to distinguish between our various operations: setting source A address (funct3=0x0), source B address (funct3=0x1), destination address (funct3=0x2), length (funct3=0x3), and kick/start (funct3=0x4).

### Writing Custom Instructions in C Code

To use these custom instructions from a C program, we need to embed assembly code directly. The RISC-V assembler provides a convenient `.insn` directive that lets us specify instruction encodings without needing a dedicated mnemonic. Here is how we defined the function to set the destination address:

```c
static inline void xcop_dst(uint64_t d){
    asm volatile(".insn r 0x0B,0x2,0x01, x0,%0,x0" :: "r"(d) : "memory");
}
```

Let's break down what this does:

- `asm volatile` tells the compiler to insert inline assembly and not to optimize it away
- `.insn r` specifies we are encoding an R-type instruction
- `0x0B` is our custom opcode
- `0x2` is funct3, identifying this as the "set destination" operation
- `0x01` is funct7
- The operands `x0,%0,x0` mean: destination register is x0 (the zero register), source register rs1 comes from the C variable `d` (represented by `%0`), and rs2 is x0
- The constraint `"r"(d)` tells the compiler to place the value `d` in any general-purpose register
- The `"memory"` clobber tells the compiler this instruction may access memory, preventing unwanted reordering

Why use x0 as destination? In RISC-V, x0 always reads as zero and writes to it are discarded. Since our instruction's purpose is to send a value to the device (not to compute a result for the CPU), we don't need a meaningful destination register.

Similarly, we defined functions for setting the source addresses, length, and a special "kick" function that starts the operation:

```c
static inline void xcop_kick(void){
    asm volatile("fence iorw, iorw\n\t"
                 ".insn r 0x0B,0x4,0x01, x0,x0,x0\n\t"
                 : : : "memory");
}
```

The kick function includes a `fence iorw, iorw` instruction before the custom instruction. A fence is a memory ordering instruction that ensures all previous input/output operations complete before any subsequent operations begin. This is crucial because we want to guarantee that all the configuration writes (source addresses, length, etc.) have definitely reached the device before we tell it to start working.

### Teaching gem5 to Recognize Custom Instructions

Creating these instructions in user code is only half the story. We also need to teach the gem5 simulator what to do when it encounters them. This happens in the instruction decoder, which is the component responsible for taking a 32-bit instruction word and determining what action the processor should take.

The decoder is defined in the file `src/arch/riscv/isa/decoder.isa`. This file uses a domain-specific language that describes how instruction bits map to operations. We added our custom instructions under the `0x0B` opcode (which corresponds to `0x02` in the major opcode field after bit manipulation):

```cpp
0x02: decode FUNCT3 {
    format Store {
        0x0: decode FUNCT7 { 0x01: xcop_srca({{
            EA  = rvZext(0x0000000040000000ULL + 0x00);
            Mem = Rs1;
        }}, inst_flags=MemWriteOp); }
        
        0x2: decode FUNCT7 { 0x01: xcop_dst({{
            EA  = rvZext(0x0000000040000000ULL + 0x10);
            Mem = Rs1;
        }}, inst_flags=MemWriteOp); }
        // ... more instructions
    }
}
```

What this means is: when the decoder sees opcode 0x02 (our custom space) and funct3=0x0, and funct7=0x01, it should execute the `xcop_srca` operation. The operation body describes what happens:

- `EA` (Effective Address) is set to `0x40000000 + 0x00`, which is the memory-mapped address of the device's source A register
- `Mem` (the value to write to memory) is set to `Rs1`, the value from the source register
- `inst_flags=MemWriteOp` tells gem5 this is a memory write operation

In essence, we have translated our custom instruction into a standard memory write. When the CPU executes `xcop_srca(address_of_A)`, it becomes a write to device register at offset 0x00 with the value `address_of_A`. This is a powerful technique: rather than creating entirely new execution semantics, we map our custom instructions onto operations that gem5 already understands (memory-mapped I/O writes).

Each of our custom instructions follows this pattern, writing to different offsets within the device's memory-mapped register space:
- `xcop_srca` writes to offset 0x00 (source A register)
- `xcop_srcb` writes to offset 0x08 (source B register)
- `xcop_dst` writes to offset 0x10 (destination register)
- `xcop_len` writes to offset 0x18 (length register)
- `xcop_kick` writes the value 1 to offset 0x20 (control/kick register)

The kick instruction is slightly different because it doesn't need to pass a value from a register; it just writes a literal 1:

```cpp
0x4: decode FUNCT7 { 0x01: xcop_kick({{
    EA  = rvZext(0x0000000040000000ULL + 0x20);
    Mem = 1ULL;
}}, inst_flags=MemWriteOp); }
```

### Why This Approach Works

This design elegantly bridges the gap between software and hardware. From the programmer's perspective, they are calling simple C functions that issue special instructions. From the hardware device's perspective, it receives configuration data through its memory-mapped registers, exactly as if the CPU had used standard load/store instructions. From gem5's perspective, it treats these as memory operations, routing them through its memory system to the device.

The beauty of using the custom instruction space is that we avoid conflicts with standard RISC-V instructions, and we gain fine-grained control over instruction encoding. The beauty of mapping them to MMIO writes is that we leverage gem5's existing infrastructure for device communication without needing to implement entirely new execution paths.

### The Control Path vs. Data Path Distinction

It's essential to understand the separation between control and data in our system:

**Control Path (Custom Instructions)**:
- `xcop_srca`, `xcop_srcb`, `xcop_dst`, `xcop_len`, `xcop_kick`
- Executed by the CPU
- Configure the accelerator's registers
- Tell the accelerator what to do and where the data is
- Minimal overhead—just a few instructions to set up a large operation

**Data Path (Accelerator Hardware + DMA)**:
- The accelerator device itself performs the actual SIMD computations
- DMA reads arrays from memory into the accelerator
- Hardware multiply units or multiply-accumulate units process the data
- DMA writes results back to memory
- All of this happens independently of the CPU

This separation is fundamental to accelerator design. Compare this to using RVV instructions:
- **RVV approach**: CPU executes `vle64.v` (load vector), `vmul.vv` (multiply vector), `vse64.v` (store vector) repeatedly. The CPU's vector unit does all the work, and the CPU is busy during the entire computation.
- **Accelerator approach**: CPU executes 5 configuration instructions (`xcop_srca` through `xcop_kick`), then the CPU is free. The accelerator handles everything via DMA while the CPU can do other work or simply poll for completion.

In real systems, this is why we have GPUs, TPUs, and DSPs—specialized accelerators that do heavy lifting while the CPU manages control flow and other tasks.

---

## Part 2: Execution Flow and Device Interaction

Now that we understand how custom instructions are created and decoded, let's trace what happens when a program actually runs and uses these instructions to operate the accelerator.

### Program Initialization

When the test program starts, it first initializes three arrays in memory:

```cTeaching gem5 to Recognize Custom Instructions

Creating these instructions in user code is only half the story. We also need to teach the gem5 simulator what to do when it encounters them. This happens in the instruction decoder, which is the component responsible for taking a 32-bit instruction word and determining what action the processor should take.

The decoder is defined in the file `src/arch/riscv/isa/decoder.isa`. This file uses a domain-specific language that describes how instruction bits map to operations. We added our custom instructions under the `0x0B` opcode (which corresponds to `0x02` in the major opcode field after bit manipulation):

```cpp
0x02: decode FUNCT3 {
    format Store {
        0x0: decode FUNCT7 { 0x01: xcop_srca({{
            EA  = rvZext(0x0000000040000000ULL + 0x00);
            Mem = Rs1;
        }}, inst_flags=MemWriteOp); }
        
        0x2: decode FUNCT7 { 0x01: xcop_dst({{
            EA  = rvZext(0x0000000040000000ULL + 0x10);
            Mem = Rs1;
        }}, inst_flags=MemWriteOp); }
        // ... more instructions
    }
}
```

What this means is: when the decoder sees opcode 0x02 (our custom space) and funct3=0x0, and funct7=0x01, it should execute the `xcop_srca` operation. The operation body describes what happens:

- `EA` (Effective Address) is set to `0x40000000 + 0x00`, which is the memory-mapped address of the device's source A register
- `Mem` (the value to write to memory) is set to `Rs1`, the value from the source register
- `inst_flags=MemWriteOp` tells gem5 this is a memory write operation

In essence, we have translated our custom instruction into a standard memory write. When the CPU executes `xcop_srca(address_of_A)`, it becomes a write to device register at offset 0x00 with the value `address_of_A`. This is a powerful technique: rather than creating entirely new execution semantics, we map our custom instructions onto operations that gem5 already understands (memory-mapped I/O writes).

Each of our custom instructions follows this pattern, writing to different offsets within the device's memory-mapped register space:
- `xcop_srca` writes to offset 0x00 (source A register)
- `xcop_srcb` writes to offset 0x08 (source B register)
- `xcop_dst` writes to offset 0x10 (destination register)
- `xcop_len` writes to offset 0x18 (length register)
- `xcop_kick` writes the value 1 to offset 0x20 (control/kick register)

The kick instruction is slightly different because it doesn't need to pass a value from a register; it just writes a literal 1:

```cpp
0x4: decode FUNCT7 { 0x01: xcop_kick({{
    EA  = rvZext(0x0000000040000000ULL + 0x20);
    Mem = 1ULL;
}}, inst_flags=MemWriteOp); }
```

### Why This Approach Works

This design elegantly bridges the gap between software and hardware. From the programmer's perspective, they are calling simple C functions that issue special instructions. From the hardware device's perspective, it receives configuration data through its memory-mapped registers, exactly as if the CPU had used standard load/store instructions. From gem5's perspective, it treats these as memory operations, routing them through its memory system to the device.

The beauty of using the custom instruction space is that we avoid conflicts with standard RISC-V instructions, and we gain fine-grained control over instruction encoding. The beauty of mapping them to MMIO writes is that we leverage gem5's existing infrastructure for device communication without needing to implement entirely new execution paths.

### The Control Path vs. Data Path Distinction

It's essential to understand the separation between control and data in our system:

**Control Path (Custom Instructions)**:
- `xcop_srca`, `xcop_srcb`, `xcop_dst`, `xcop_len`, `xcop_kick`
- Executed by the CPU
- Configure the accelerator's registers
- Tell the accelerator what to do and where the data is
- Minimal overhead—just a few instructions to set up a large operation

**Data Path (Accelerator Hardware + DMA)**:
- The accelerator device itself performs the actual SIMD computations
- DMA reads arrays from memory into the accelerator
- Hardware multiply units or multiply-accumulate units process the data
- DMA writes results back to memory
- All of this happens independently of the CPU

This separation is fundamental to accelerator design. Compare this to using RVV instructions:
- **RVV approach**: CPU executes `vle64.v` (load vector), `vmul.vv` (multiply vector), `vse64.v` (store vector) repeatedly. The CPU's vector unit does all the work, and the CPU is busy during the entire computation.
- **Accelerator approach**: CPU executes 5 configuration instructions (`xcop_srca` through `xcop_kick`), then the CPU is free. The accelerator handles everything via DMA while the CPU can do other work or simply poll for completion.

In real systems, this is why we have GPUs, TPUs, and DSPs—specialized accelerators that do heavy lifting while the CPU manages control flow and other tasks.

---

## Part 2: Execution Flow and Device Interaction

Now that we understand how custom instructions are created and decoded, let's trace what happens when a program actually runs and uses these instructions to operate the accelerator.

### Program Initialization

When the test program starts, it first initializes three arrays in memory:

```c
#define N 8
static uint64_t A[N], B[N], C[N];

int main(void) {
    for (int i = 0; i < N; i++) { 
        A[i] = i + 1;      // A = [1, 2, 3, 4, 5, 6, 7, 8]
        B[i] = 2 * i;      // B = [0, 2, 4, 6, 8, 10, 12, 14]
        C[i] = 0;          // C = [0, 0, 0, 0, 0, 0, 0, 0]
    }
```

At this point, we have three arrays sitting in the program's memory space. In gem5's SE mode, these have virtual addresses assigned by the simulator. The arrays are just regular memory; nothing special has happened yet.

### Configuring the Device

Next, the program calls the custom instruction wrappers to configure the device:

```c
xcop_srca((uint64_t)(uintptr_t)A);
xcop_srcb((uint64_t)(uintptr_t)B);
xcop_dst ((uint64_t)(uintptr_t)C);
xcop_len (N);
```

Let's follow the first call, `xcop_srca`, step by step:

1. **In User Code**: The compiler places the address of array A into a register (let's say register `a0`) and generates the custom instruction with encoding `0x0B` (opcode), `0x0` (funct3), `0x01` (funct7).

2. **In gem5's CPU Model**: The simulated CPU fetches this instruction, and the decoder recognizes it as `xcop_srca`. The decoder generates a memory write operation: write the value in `a0` to address `0x40000000 + 0x00`.

3. **In gem5's Memory System**: The memory write packet is routed through the memory hierarchy. The address `0x40000000` is configured to be the base address of our SIMD accelerator device, so the packet is delivered to the device's `write()` method.

4. **In the Device Model** (`src/dev/simd_accel.cc`): The device's write handler is invoked:

```cpp
Tick SimdAccel::write(PacketPtr pkt) {
    const Addr off = pkt->getAddr() - pioAddr;
    const uint64_t val = pkt->getUintX(ByteOrder::little);

    switch (off) {
      case 0x00: regSrcA = val; break;  // This case executes
      case 0x08: regSrcB = val; break;
      case 0x10: regDst  = val; break;
      case 0x18: regLen  = val; break;
      case 0x20: regCmd  = val; kick(); break;
      // ...
    }
    pkt->makeResponse();
    return pioDelay;
}
```

The device calculates the offset by subtracting its base address (`pioAddr`, which is `0x40000000`) from the packet's address. Since the packet is addressed to `0x40000000 + 0x00`, the offset is `0x00`. The switch statement executes `regSrcA = val`, storing the address of array A into the device's internal register `regSrcA`.

The subsequent calls to `xcop_srcb`, `xcop_dst`, and `xcop_len` follow exactly the same flow, each writing to a different offset and thus storing values in different device registers (`regSrcB`, `regDst`, `regLen`).

### Starting the Operation

After configuration, the program issues the kick:

```c
xcop_kick();
```

Recall that `xcop_kick` includes a fence instruction first. This ensures that all the previous writes (to regSrcA, regSrcB, regDst, regLen) have completed and are visible before the kick is issued. Without this fence, the CPU or memory system might reorder operations, and the device could receive the kick before receiving the configuration—leading to incorrect behavior or a malfunction.

After the fence, the kick instruction writes the value 1 to offset `0x20`. In the device's write handler, offset `0x20` corresponds to the command register:

```cpp
case 0x20: regCmd = val; kick(); break;
```

This stores the value into `regCmd` and immediately calls the internal `kick()` method. The `kick()` method is where the device transitions from idle to active:

```cpp
void SimdAccel::kick() {
    if (!(regCmd & 0x1))
        return;

    regStatus |= 0x1;      // Set busy flag
    regCmd &= ~0x1ULL;     // Clear start bit
    
    if (regOpType == 0) {
        // Element-wise multiply operation
        if (regLen == 0) {
            regStatus &= ~0x1ULL;
            return;
        }
        idx = 0;
        issueReadA();
    }
    // ... other operation types
}
```

The device first checks that the start bit is set, then it sets the busy flag in the status register (`regStatus |= 0x1`). This busy flag is important: it tells any polling software that the device is currently working. Next, it clears the start bit in the command register so that the device doesn't re-trigger on subsequent accesses.

For element-wise multiplication (the default operation type), the device initializes an index counter `idx = 0` and calls `issueReadA()` to begin DMA reads from array A.

### Understanding the SIMD Accelerator Architecture

Before diving into the execution pipeline, it's important to understand what makes this a SIMD (Single Instruction, Multiple Data) accelerator and what operations it performs.

SIMD is a computing paradigm where a single operation is applied to multiple data elements simultaneously or in a highly efficient parallel manner. Traditional CPUs process data one element at a time (or a few at a time with vector instructions), but dedicated SIMD accelerators can process entire arrays or matrices with specialized hardware optimized for repetitive operations.

Our accelerator supports two types of SIMD operations:

**1. Element-wise Array Operations (Vector Operations)**

The simplest operation is element-wise multiplication of two arrays. Given arrays A and B, the accelerator computes C[i] = A[i] * B[i] for all elements. This is a classic SIMD pattern: the same multiplication operation is applied to corresponding pairs of elements from two input arrays.

```
A = [1, 2, 3, 4, 5, 6, 7, 8]
B = [0, 2, 4, 6, 8, 10, 12, 14]
C = [0, 4, 12, 24, 40, 60, 84, 112]  // Each C[i] = A[i] * B[i]
```

In a real hardware implementation, multiple multiply units could operate in parallel, computing several C[i] values simultaneously. Our gem5 model simulates this by processing elements in a pipeline, representing what would happen in actual hardware where elements flow through computational units.

**2. Matrix Multiplication (GEMM - General Matrix Multiply)**

The more complex operation is matrix multiplication: C = A × B, where matrices have dimensions C[M×N] = A[M×K] × B[K×N]. This is computed as:

```
for each row i in A:
    for each column j in B:
        C[i][j] = sum of A[i][k] * B[k][j] for all k
```

Matrix multiplication is a fundamental SIMD operation because it involves repetitive multiply-accumulate operations across large arrays of data. High-performance implementations use specialized hardware that can perform many multiplications and additions in parallel. Modern GPUs and tensor processing units are essentially sophisticated SIMD accelerators optimized for matrix operations.

The accelerator uses a configurable register (`regOpType`) to select between these operations:
- `regOpType = 0`: Element-wise multiplication
- `regOpType = 1`: Matrix multiplication (GEMM)

Additional registers provide operation-specific parameters: for element-wise operations, `regLen` specifies the number of elements; for GEMM, `regLen` stores the M dimension while `regDimK` and `regDimN` store K and N dimensions.

### Why Use a Dedicated Accelerator?

You might wonder why we need a separate device for operations the CPU could perform. There are several reasons:

1. **Specialized Hardware**: An accelerator can have dedicated multiply-accumulate units, wide data paths, and optimized memory access patterns that are more efficient than general-purpose CPU instructions.

2. **Offloading**: By handling these operations on a separate device, the CPU is free to do other work, improving overall system throughput.

3. **Memory Bandwidth**: The accelerator can be connected directly to memory with high bandwidth, potentially accessing multiple array elements per cycle.

4. **Energy Efficiency**: Specialized hardware typically consumes less power per operation than general-purpose processors.

In our gem5 model, we simulate these benefits by having the device operate independently via DMA, modeling realistic memory access patterns and allowing concurrent operation with the CPU.

### The DMA Read-Compute-Write Pipeline

Now let's examine how the accelerator actually executes operations using DMA (Direct Memory Access). DMA allows the device to read and write memory independently of the CPU, which is crucial for both performance and realistic hardware modeling.

**The Element-wise Operation Pipeline**

For element-wise multiplication, the device follows this sequence for each element:

```cpp
void SimdAccel::issueReadA() {
    const Addr a = regSrcA + idx * sizeof(uint64_t);
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onReadADone(); });
    dmaReadVirt(a, sizeof(uint64_t), cb, bufA.data());
}

void SimdAccel::onReadADone() {
    std::memcpy(&tmpA, bufA.data(), sizeof(uint64_t));
    issueReadB();  // Trigger read of B[idx]
}

void SimdAccel::onReadBDone() {
    std::memcpy(&tmpB, bufB.data(), sizeof(uint64_t));
    tmpR = tmpA * tmpB;  // Perform the multiplication
    std::memcpy(bufR.data(), &tmpR, sizeof(uint64_t));
    issueWrite();  // Write result back
}
```

1. **Read A[idx]**: The device calculates the address of element A[idx] and issues a DMA read. gem5's `DmaDevice` infrastructure handles the memory request, and when data arrives, it invokes the callback `onReadADone()`.

2. **Read B[idx]**: Once A[idx] is available, the device reads the corresponding B[idx] element.

3. **Compute**: With both inputs available, the device performs the multiplication: `tmpR = tmpA * tmpB`. In real hardware, this would happen in a multiply unit; in our model, it's a C++ multiplication that represents one clock cycle of computation.

4. **Write C[idx]**: The result is written back to memory at the destination address via DMA.

5. **Iterate**: The index increments, and the process repeats for the next element until all N elements are processed.

This serialized approach in our model represents what would happen in hardware with pipelining: while element i is being written, element i+1 could be in the computation stage, and element i+2 in the read stage. The DMA callbacks and event scheduling in gem5 naturally model these overlapped operations.

**The GEMM Operation Pipeline**

Matrix multiplication is more complex because each output element C[i][j] requires multiple inputs:

```cpp
void SimdAccel::gemmOnReadBDone() {
    std::memcpy(&tmpB, bufB.data(), sizeof(uint64_t));
    
    // Multiply-accumulate: accumulate += A[i][k] * B[k][j]
    gemmAccum += tmpA * tmpB;
    
    gemmKIdx++;  // Move to next k
    
    if (gemmKIdx < gemmK) {
        // Continue inner loop: more elements in dot product
        gemmReadA();
    } else {
        // Inner loop complete: write C[i][j] = gemmAccum
        gemmWriteResult();
    }
}
```

For each output element C[i][j], the device:

1. Initializes an accumulator to zero: `gemmAccum = 0`
2. Loops through k from 0 to K-1:
   - Reads A[i][k] from memory
   - Reads B[k][j] from memory  
   - Computes `gemmAccum += A[i][k] * B[k][j]`
3. Writes the accumulated result to C[i][j]
4. Moves to the next output position and repeats

This triple-nested loop structure (over i, j, and k) is characteristic of matrix multiplication. In real SIMD hardware, many of these operations would happen in parallel—for example, computing multiple output elements simultaneously or performing multiple multiply-accumulate operations per cycle. Our model executes them sequentially but tracks the state machine progression to represent realistic hardware behavior.

**Completion**: Once all elements (or all matrix positions) are processed and written back, the device clears the busy flag:

```cpp
regStatus &= ~0x1ULL;
```

This signals to software that the operation is finished.

### Polling and Result Verification

While the device is busy doing DMA and computation, the CPU continues executing the user program. Immediately after issuing the kick, the program enters a busy-wait loop:

```c
volatile uint64_t *STATUS = (uint64_t*)(uintptr_t)(0x40000000ull + 0x28);
while (*STATUS & 1) { /* spin */ }
```

Here, `0x40000000 + 0x28` is the address of the status register (offset 0x28 in the device). The `volatile` keyword is critical: it tells the compiler that this memory location can change without the program explicitly writing to it (because the device updates it), so the compiler must not optimize away repeated reads.

Each iteration of the loop generates a memory read request that goes through gem5's memory system to the device's `read()` handler:

```cpp
Tick SimdAccel::read(PacketPtr pkt) {
    const Addr off = pkt->getAddr() - pioAddr;
    uint64_t val = 0;
    switch (off) {
      case 0x00: val = regSrcA; break;
      case 0x08: val = regSrcB; break;
      case 0x10: val = regDst;  break;
      case 0x18: val = regLen;  break;
      case 0x20: val = regCmd;  break;
      case 0x28: val = regStatus; break;  // This case executes
      default:   val = 0; break;
    }
    pkt->setUintX(val, ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}
```

For offset 0x28, the device returns the current value of `regStatus`. As long as the busy bit is set (`regStatus & 1` is true), the program continues spinning. Once the device clears the busy bit, the loop exits and the program proceeds.

This polling approach is simple but inefficient in real systems (it wastes CPU cycles). A more sophisticated design would use interrupts: the device would signal the CPU when it's done, allowing the CPU to do other work in the meantime. However, for our demonstration and testing purposes, polling is straightforward and effective.

### Reading Back and Verifying Results

After the loop exits, the program knows the device has finished. It then reads the result array C directly from memory:

```c
int ok = 1;
for (int i = 0; i < N; i++) {
    uint64_t exp = A[i] * B[i];
    if (C[i] != exp) {
        ok = 0;
        C[0] = 0xDEADBEEFDEADBEEFULL;
        break;
    }
}
if (ok) {
    C[N-1] = 0xC0FFEEC0FFEEULL;
}
```

Notice that the program reads C[i] using normal memory accesses, not custom instructions. This is because the device has written results into memory via DMA, so the data is now just sitting in the memory system like any other data. The program verifies that each element matches the expected value (A[i] * B[i]). If any mismatch is found, it writes a failure marker (`0xDEADBEEF`) to the first element. If all checks pass, it writes a success marker (`0xC0FFEE`) to the last element.

Finally, the program exits:

```c
return ok ? 0 : 1;
```

The exit is implemented via a system call emulation (ecall instruction with syscall number 93, which is the Linux exit syscall). gem5's SE mode intercepts this and terminates the simulation.

### Timing and Concurrency

An important aspect of gem5 simulation is timing. gem5 models the passage of simulated time in ticks (a fine-grained time unit). Every operation—instruction execution, memory access, DMA transfer—consumes simulated time. When the device issues a DMA read, it doesn't complete instantly; gem5 schedules a completion event some number of ticks in the future, based on memory latency and bandwidth models.

This means that while the device is waiting for DMA to complete, the simulated CPU continues running (in this case, spinning in the polling loop). The device and CPU operate concurrently in the simulation. The device's state machine advances through read, compute, and write phases as DMA events complete, while the CPU repeatedly polls the status register.

This concurrent behavior is a key feature that makes gem5 valuable: it realistically models the interleaving of CPU and device activities, memory system contention, and timing dependencies, allowing researchers and engineers to study performance and correctness issues that would arise in real hardware.

### Summary of the Execution Flow

To summarize the entire flow from user code to device and back:

1. **User program** initializes data arrays (A, B, C) in memory and calls custom instruction wrappers (`xcop_srca`, `xcop_srcb`, etc.) to configure the accelerator.

2. **Compiler** generates custom instruction encodings embedded in the binary according to RISC-V R-type format.

3. **gem5 CPU model** fetches and decodes each custom instruction, recognizing them as memory-mapped I/O writes to device registers at the configured MMIO base address.

4. **gem5 memory system** routes these writes to the SIMD accelerator device at address `0x40000000`.

5. **Device write handler** receives the writes, stores configuration values (source addresses, length, operation type) in internal registers, and when it receives the kick command, transitions to active state.

6. **Device begins SIMD operation**: Based on `regOpType`, it either performs element-wise multiplication or matrix multiplication (GEMM). The device operates as a state machine, issuing DMA reads to fetch input data, performing computations (multiply or multiply-accumulate), and issuing DMA writes to store results.

7. **Parallel execution**: While the device processes data via DMA, the CPU continues running, demonstrating true hardware concurrency. The device updates its internal status register to reflect busy/idle states.

8. **User program** polls the status register via memory reads until the busy bit clears, indicating completion.

9. **Device read handler** responds to status register reads, returning the current busy/idle state from `regStatus`.

10. **User program** reads the result array from memory (now populated by the device's DMA writes), verifies correctness (checking that C[i] equals the expected value), and writes success/failure markers before exiting.

11. **gem5** captures the exit system call and terminates the simulation, reporting statistics including memory access patterns, DMA traffic, and timing information.

Each step involves multiple layers of abstraction: high-level C code, low-level assembly instructions, instruction decoding, memory transactions, device state machines, and timing models. The power of gem5 is that it accurately simulates all these layers, giving us confidence that software and hardware will behave correctly when integrated in a real system.

---

## Conclusion

Implementing custom instructions and executing a program on a simulated SIMD accelerator in gem5 involves a careful orchestration of software, instruction set architecture, and hardware modeling. By defining custom instruction encodings, teaching the simulator's decoder to recognize them, and mapping them to memory-mapped I/O operations, we created a clean and efficient interface between software and our accelerator.

The accelerator itself embodies the SIMD computing paradigm by applying repetitive operations across arrays of data—either element-wise vector operations or complex matrix multiplications. While our gem5 model executes these operations sequentially through a state machine (to accurately represent hardware timing), it demonstrates the key principles: independent operation from the CPU via DMA, specialized computational patterns (multiply-accumulate for GEMM), and efficient data movement between memory and compute units.

The execution flow demonstrates how user code, CPU simulation, memory system, and device model all collaborate to perform computations, with realistic timing and concurrency. The device operates independently once kicked, accessing memory through DMA while the CPU continues with other work (in our case, polling). This concurrent behavior is fundamental to understanding how accelerators improve system performance: by offloading specific computational patterns to specialized hardware that can execute them more efficiently than general-purpose processors.

This approach not only validates the correctness of our accelerator design but also provides a platform for exploring performance optimizations (such as batching DMA transfers, pipelining computation stages, or implementing true parallel execution), testing different configurations (varying matrix sizes, operation types), and understanding the interplay between software and hardware in a controlled, reproducible environment.

The techniques described here—custom instruction encoding, decoder integration, MMIO-based device control, DMA-driven data movement, and state machine-based computation—are applicable to a wide range of custom hardware extensions including neural network accelerators, cryptographic engines, signal processing units, and other domain-specific architectures. This work serves as a foundation for more complex system design and evaluation in the gem5 simulator, demonstrating how modern computing systems leverage specialized hardware to achieve performance and efficiency beyond what general-purpose CPUs can provide alone.
