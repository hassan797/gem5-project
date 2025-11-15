# Major Challenges and Solutions - gem5 SIMD Accelerator Project

A student-friendly record of the hardest problems we faced and how we solved them.

---

## 1. The Inheritance Nightmare 🔥

**The Problem:**
We spent days fighting compiler errors because we tried to use **multiple inheritance** with `BasicPioDevice` and `DmaDevice`.

```cpp
// THIS DIDN'T WORK:
class SimdAccel : public BasicPioDevice, public DmaDevice
```

**Why it failed:**
- The generated params class `SimdAccelParams` didn't include DmaDevice's parameters
- We got errors like: `no matching function for call to DmaDevice::DmaDevice(const SimdAccelParams&)`
- Turns out gem5's Python→C++ params generation doesn't play nice with multiple inheritance
- We tried all kinds of workarounds: `cxx_extra_bases`, custom param copying, everything

**The Solution:**
Use `DmaVirtDevice` which **already** inherits from `DmaDevice`:

```cpp
// THIS WORKS:
class SimdAccel : public DmaVirtDevice
```

**Why it works:**
- `DmaVirtDevice` is a single base class that gives you both PIO and DMA
- It's designed for devices that need memory-mapped I/O + DMA
- Params generation works perfectly with single inheritance

**Lesson learned:** Don't fight the framework. If gem5 has a class that does what you need, use it instead of trying to combine primitives.

---

## 2. Page Fault Hell in SE Mode 💀

**The Problem:**
Every time we tried to run a test, gem5 crashed with:

```
panic: Page table fault when accessing virtual address 0x40000000
```

And sometimes:
```
panic: Page table fault when accessing virtual address 0xfffffffffffffa00
```

**What was happening:**

### Issue 2a: Physical vs Virtual Addresses
Our DMA reads/writes used **physical addresses**, but in SE (System Emulation) mode, user programs work with **virtual addresses**.

```cpp
// WRONG - tries to read physical address 0x111c8:
dmaRead(0x111c8, size, callback, buffer);
```

But `0x111c8` is a **virtual** address in the process! The DMA needs to translate it first.

### Issue 2b: MMIO Not Mapped
The MMIO region at `0x40000000` wasn't in the process's page table, so any access to it caused a page fault.

**The Solution:**

**Part 1:** Use virtual DMA functions
```cpp
// Use DmaVirtDevice and call dmaReadVirt/dmaWriteVirt:
dmaReadVirt(vaddr, size, callback, buffer);
```

**Part 2:** Implement address translation
```cpp
TranslationGenPtr SimdAccel::translate(Addr vaddr, Addr size)
{
    auto process = sys->threads[0]->getProcessPtr();
    return process->pTable->translateRange(vaddr, size);
}
```

This hooks into the process's page table to convert virtual → physical addresses.

**Part 3:** Map MMIO in SE mode
```python
# In the config script:
proc_ptr = process.getCCObject()
proc_ptr.map(0x40000000, 0x40000000, 0x1000, False)
```

This tells SE mode: "Hey, virtual address 0x40000000 maps to physical 0x40000000, and it's MMIO."

**Lesson learned:** SE mode doesn't magically know about your device's MMIO. You have to explicitly map it.

---

## 3. Out of Memory During Build 🧠

**The Problem:**
We ran `scons -j$(nproc)` to build faster using all CPU cores, but got:

```
collect2: fatal error: ld terminated with signal 9 [Killed]
compilation terminated.
```

**What happened:**
- gem5 is a huge C++ project with tons of object files
- Linking all those objects into the final binary is memory-intensive
- With `-j8` on a machine with 8GB RAM, we were running 8 parallel linker jobs
- Each linker job consumed 2-4 GB of RAM
- Total: 16-32 GB needed, but we only had 8 GB
- The OS killed the linker with SIGKILL (signal 9)

**The Solution:**
```bash
# Instead of:
scons -j$(nproc) build/RISCV/gem5.opt

# Use:
scons -j4 build/RISCV/gem5.opt
# Or even safer:
scons -j2 build/RISCV/gem5.opt
```

Limit parallel jobs to what your RAM can handle. Rule of thumb: if you have 8GB RAM, use `-j2` or `-j4`.

**Lesson learned:** More parallel = faster, but only if you have the RAM. Check `free -h` before going crazy with `-j`.

---

## 4. Custom Instructions vs MMIO 🔧

**The Problem:**
We started using custom RISC-V instructions (`.insn` inline assembly) to control the accelerator:

```c
xcop_srca(addr);  // Custom instruction to set source A register
```

This worked... until it didn't:
```
panic: Unknown instruction 0x500001000150000b at pc (0x10bb0=>0x10bb4)
```

**Why it failed:**
- Custom instructions need to be defined in the ISA files
- We had them in `src/arch/riscv/isa/custom_coproc.isa`
- But maintaining custom ISA code is fragile and toolchain-dependent
- When we switched to a different test or gem5 version, the instructions broke

**The Solution:**
Use **MMIO** (Memory-Mapped I/O) instead:

```c
// Instead of custom instructions:
static inline void xcop_srca(uint64_t a) {
    volatile uint64_t* reg = (uint64_t*)(0x40000000 + 0x00);
    *reg = a;
}
```

Just write to memory addresses! The accelerator sees these writes and configures itself.

**Why this is better:**
- No custom ISA needed
- Works with standard RISC-V toolchains
- More portable (any CPU can write to memory addresses)
- Easier to debug (you can see MMIO writes in debug logs)

**Lesson learned:** Custom instructions are cool, but MMIO is simpler and more robust for most cases.

---

## 5. The Mystery of the Missing Results 🕵️

**The Problem:**
The accelerator ran, finished, but the output array was all zeros. The computation just... vanished.

**What was happening:**
We were mixing up **addresses** and **values**:

```cpp
// WRONG - reads the address, not the data:
uint64_t value = regSrcA;  // This is 0x111c8, not the data at 0x111c8!
```

And later:
```cpp
// WRONG - writes to the wrong place:
dmaWrite(regDst, sizeof(uint64_t), cb, &tmpR);
// tmpR is a local variable on the stack!
// After the function returns, that memory is invalid!
```

**The Solution:**

1. **Read data from memory, not just the address:**
```cpp
void issueReadA() {
    Addr addr = regSrcA + currentIdx * sizeof(uint64_t);
    dmaReadVirt(addr, sizeof(uint64_t), cb, bufA.data());
}
```

2. **Use persistent buffers, not stack variables:**
```cpp
class SimdAccel : public DmaVirtDevice {
    std::array<uint8_t, 8> bufA;  // Persistent storage
    std::array<uint8_t, 8> bufB;
    std::array<uint8_t, 8> bufR;
```

3. **Write results to the right place:**
```cpp
*(uint64_t*)bufR.data() = result;
dmaWriteVirt(regDst + idx * sizeof(uint64_t), sizeof(uint64_t), cb, bufR.data());
```

**Lesson learned:** Pointers and addresses are tricky. Always double-check: "Am I reading/writing the address or the data?"

---

## 6. SIMD Batching: Theory vs Practice 📊

**The Problem:**
We wanted the accelerator to process **4 elements in parallel** (4 lanes), but how do you model that in software?

**The Challenge:**
- Hardware can do 4 multiplies at once: `[a0*b0, a1*b1, a2*b2, a3*b3]` in one cycle
- But our C++ code is sequential: one multiply at a time
- How do we make the **timing** match real hardware while keeping the code simple?

**The Solution:**

1. **Process elements in batches of 4:**
```cpp
for (int batch = 0; batch < totalElements; batch += 4) {
    // Read 4 elements from A
    // Read 4 elements from B
    // Compute 4 multiplies (in software, sequentially)
    // Write 4 results
    // Wait for compute_latency (e.g., 10ns)
}
```

2. **Model timing with a latency parameter:**
```cpp
schedule(computeEvent, curTick() + computeLatency);
```

The `computeLatency` represents the time to do **one batch of 4 operations**. If a single multiply takes 10ns, then 4 parallel multiplies also take 10ns.

3. **In the logs, it looks like this:**
```
[SimdAccel] BATCHED READ A: idx=0, batchSize=4, addr=0x111c8
[SimdAccel] BATCHED COMPUTE: Processing 4 elements in parallel
[SimdAccel] BATCHED WRITE: idx=0, batchSize=4, addr=0x11248
```

**The math:**
- Non-SIMD: 8 elements × 10ns/element = 80ns
- SIMD (4 lanes): 2 batches × 10ns/batch = 20ns
- Speedup: 4× faster! ✅

**Lesson learned:** You can model parallel hardware in sequential software by batching operations and using timing events correctly.

---

## 7. Functional Units and Arithmetic Latencies ⚙️

**The Problem:**
For the baseline CPU comparison, we needed to add realistic latencies for multiply and add operations. But how?

**The Challenge:**
- `TimingSimpleCPU` has zero latency for all ALU ops (unrealistic)
- We needed multiply to take 4 cycles, add to take 1 cycle
- But changing CPU models is complicated

**The Solution:**

Use `MinorCPU` which supports **functional unit latencies**:

```python
# Create custom FU with specific latencies:
intMul = MinorDefaultIntMulFU()
intMul.opLat = 4  # Multiply takes 4 cycles

intALU = MinorDefaultIntFU()
intALU.opLat = 1  # Add takes 1 cycle

# Assign to CPU:
system.cpu = RiscvMinorCPU()
system.cpu.executeFuncUnits = MinorFUPool()
system.cpu.executeFuncUnits.funcUnits = [intMul, intALU, ...]
```

Now we can control latencies from the config script with command-line args:
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
  --mul-latency 4 --add-latency 1
```

**Lesson learned:** Different CPU models have different capabilities. Pick the right one for your experiment.

---

## 8. Stats Parsing and Metrics 📈

**The Problem:**
We had tons of simulation output, but how do we turn `stats.txt` into meaningful comparisons?

**What we needed:**
- Simulated time
- Number of cycles
- Instructions executed
- Memory traffic
- Speedup calculations

**The Solution:**

Created helper scripts and markdown templates:

```bash
# Extract key metrics:
grep "simSeconds" m5out_baseline/stats.txt
grep "numCycles" m5out_baseline/stats.txt
grep "readBursts" m5out_baseline/stats.txt

# Calculate speedup:
baseline_time=$(grep 'simSeconds' m5out_baseline/stats.txt | awk '{print $2}')
simd_time=$(grep 'simSeconds' m5out_simd/stats.txt | awk '{print $2}')
speedup=$(python3 -c "print(f'{$baseline_time/$simd_time:.2f}x')")
```

And created comparison reports that break down:
- Per-inference time
- Compute time vs memory time
- Percentage breakdowns
- Speedup factors

**Lesson learned:** Automation is key. Don't manually copy numbers from 10 different stats files!

---

## 9. The Compute vs Memory Puzzle 🧩

**The Problem:**
Even with matched arithmetic latencies (baseline mul=4 cycles, SIMD batch=4 cycles), SIMD was still faster. Why?

**The Analysis:**

For baseline:
- Compute: 81,920 MACs × 5 cycles = 409,600 cycles (1.94% of total time)
- Memory + overhead: 98.06% of total time

For SIMD:
- Compute: 20,480 batches × 4 cycles = 81,920 cycles (0.46% of total time)
- Memory + overhead: 99.54% of total time

**The insight:**
- **Compute is tiny** compared to memory access time
- Even when compute latencies match, **DMA is more efficient** than CPU loads/stores
- DMA can burst larger chunks, uses dedicated paths, doesn't stall the pipeline

**Lesson learned:** In memory-bound workloads, improving compute speed barely helps. Focus on memory bandwidth and access patterns instead.

---

## 10. Documentation Debt 📝

**The Problem:**
We built all this stuff, but future-us (or other students) would forget how it works in a month.

**The Solution:**
Write everything down! Created:
- `PROJECT_STRUCTURE_GUIDE.md` – What everything is and how it connects
- `ESSENTIAL_TEST_COMMANDS.md` – Copy-paste commands for running tests
- `COMPARISON_4CYC_SIMD_vs_BASELINE.md` – Performance analysis reports
- `ARITHMETIC_LATENCY_CONFIGURATION.md` – How to set CPU latencies
- `COPROC_TEST_STATUS.md` – Test validation results

**Lesson learned:** Good docs save hours of "wait, how do I run this again?" Make README files your friend.

---

## Summary of Key Solutions

| Problem | Root Cause | Solution |
|---------|-----------|----------|
| Inheritance errors | Multiple inheritance with gem5 params | Use `DmaVirtDevice` single base class |
| Page faults | Physical DMA in SE mode | Use `dmaReadVirt/WriteVirt` + translate() |
| MMIO not accessible | SE mode doesn't map devices | Call `proc_ptr.map()` in config |
| Build OOM | Too many parallel jobs | Use `scons -j2` or `-j4` instead of `-j$(nproc)` |
| Unknown instructions | Custom ISA fragility | Use MMIO instead of custom instructions |
| Missing results | Writing to stack/wrong addresses | Use persistent buffers and verify addresses |
| SIMD timing | How to model parallelism? | Batch 4 elements + use compute_latency param |
| CPU latencies | TimingSimple has zero ALU latency | Use MinorCPU with configurable FUs |
| Stats analysis | Manual number crunching | Create scripts and markdown templates |
| Memory dominance | Compute too fast | Optimize DMA patterns, not compute |

---

## What We'd Do Differently Next Time

1. **Start with DmaVirtDevice** – Don't try to be clever with multiple inheritance
2. **Test with MMIO first** – Custom instructions are cool but brittle
3. **Limit build parallelism** – Check `free -h`, then use `scons -j<safe_number>`
4. **Write docs as you go** – Not after you've forgotten half the details
5. **Use MinorCPU from the start** – For any latency-sensitive experiments
6. **Profile early** – Don't guess where time is spent, measure it
7. **Keep test cases small** – Debug with 8 elements, not 8192

---

## The Most Important Lesson 🎓

**gem5 has conventions for a reason.** 

When you fight the framework, you lose time. When you follow the patterns (like using `DmaVirtDevice`, mapping MMIO properly, using existing CPU models), things just work.

Trust the docs, read the examples, copy working code patterns, and you'll save yourself days of debugging.

---

*This document was created to help future students avoid the same pitfalls. If you're reading this because you hit one of these errors: you're not alone, and the solution is probably simpler than you think!*
