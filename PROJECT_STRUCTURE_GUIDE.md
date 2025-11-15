# Project Structure Guide – gem5 with SIMD Accelerator

Hey! This doc walks you through the repo so you can understand what's where, what each file does, and how everything connects. Written in plain English, no jargon overload.

---

## What This Project Is About

We're running tiny machine learning workloads (think: matrix multiplications for neural networks) in gem5, which is a CPU/system simulator. The twist? We built a **custom SIMD accelerator** that speeds up the math by doing 4 operations at once.

The goal: compare how fast the **baseline CPU** (doing math one step at a time) is vs. the **SIMD accelerator** (doing 4 things in parallel).

---

## The Big Picture (How It All Fits Together)

```
┌─────────────────────────────────────────────────────────┐
│  Your C++ Test Program (workload binary)               │
│  • Does matrix math (GEMM)                             │
│  • Talks to accelerator via MMIO registers             │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│  Python Config Script (sets up gem5 simulation)         │
│  • Creates CPU, memory, buses                          │
│  • Wires up the SIMD accelerator                       │
│  • Sets clock speeds and latencies                     │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│  gem5 Simulator (runs your workload)                    │
│  • CPU executes instructions                           │
│  • Accelerator does DMA + parallel math                │
│  • Memory system handles reads/writes                  │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│  Output Stats (results in m5out/)                       │
│  • How long it took (simSeconds)                       │
│  • How many cycles (numCycles)                         │
│  • Memory traffic, instruction counts, etc.            │
└─────────────────────────────────────────────────────────┘
```

---

## Directory Tour

### `src/dev/` – The Accelerator Implementation

**What's in here:**
- `simd_accel.hh` and `simd_accel.cc` – The C++ code for the SIMD accelerator device
- `SimdAccel.py` – Python wrapper that lets gem5 configs use the accelerator

**What it does:**
- Implements a hardware device with MMIO registers at address `0x40000000`
- Has DMA to read/write memory
- Does batched multiply-accumulate: reads 4 numbers, multiplies them with 4 weights, adds to results
- Has configurable `num_lanes` (parallelism) and `compute_latency` (how long each batch takes)

**Key registers (MMIO offsets):**
- `0x00` – source A address (like weight matrix)
- `0x08` – source B address (like input vector)
- `0x10` – destination address (where results go)
- `0x18` – length (how many elements)
- `0x20` – kick (start the operation)
- `0x28` – status (is it done yet?)
- `0x30` – operation type (0=element-wise multiply, 1=GEMM)
- `0x38` – dimension K (for GEMM)
- `0x40` – dimension N (for GEMM)

---

### `configs/mlperf/` – Test Configurations and Workloads

This is where you'll spend most of your time. All the ML benchmark stuff lives here.

#### Python Config Scripts (how to set up a simulation)

**`mlperf_simd_se.py`** – Main SIMD accelerator test
- Sets up a system with the SIMD accelerator attached
- Clock: 2 GHz
- Memory: DDR3-1600, usually no caches (for fair comparison)
- Maps MMIO at `0x40000000` so your C++ code can write to it
- Usage: `./build/RISCV/gem5.opt -d m5out_simd configs/mlperf/mlperf_simd_se.py --num-runs 10`

**`test_fc_baseline.py`** – Baseline CPU-only test (no accelerator)
- Same workload, but runs entirely on the CPU
- Uses `RiscvMinorCPU` which lets you set realistic arithmetic latencies:
  - `--mul-latency 4` → multiply takes 4 cycles
  - `--add-latency 1` → add takes 1 cycle
- This is your "control group" for comparisons
- Usage: `./build/RISCV/gem5.opt -d m5out_baseline configs/mlperf/test_fc_baseline.py --mul-latency 4 --add-latency 1 --num-runs 10`

**`test_fc_simple.py`** – Simpler SIMD test for quick checks
- Similar to `mlperf_simd_se.py` but more minimal
- Good for testing if your accelerator setup works

#### C++ Workload Programs (what actually runs)

**`test_fc_gemm_only.cpp` → compiles to `test_fc_gemm_only.riscv`**
- **What it does:** Runs just the fully-connected layer GEMM
  - Math: `C[10×1] = W[10×8192] × X[8192×1]`
  - That's 81,920 multiply-adds per inference
- **How it works:** Writes to MMIO registers to configure the accelerator, then kicks it off
- **Why we like it:** Clean, focused test. No extra fluff. Uses MMIO (not custom instructions) so it's reliable.
- **When to use:** Comparing SIMD vs baseline on pure matrix math

**`test_fc_baseline.cpp` → compiles to `test_fc_baseline.riscv`**
- Same math as above, but computed on CPU in a loop (no accelerator)
- Used by `test_fc_baseline.py`
- **When to use:** The "no acceleration" comparison point

**`simple_ic_benchmark_simd.cpp` → `simple_ic_benchmark_simd.riscv`**
- **What it does:** Small image classification pipeline
  - Conv layer → Pooling → Fully-connected layer
  - Uses the accelerator for multiply-heavy parts
- **Why we have it:** More realistic ML workload than just GEMM
- **Note:** Some versions used custom RISC-V instructions (`.insn`), which caused compatibility issues. The MMIO version is more stable.

**`simple_ic_benchmark.cpp` → `simple_ic_benchmark.riscv`**
- Same pipeline, but no accelerator (all CPU)
- Baseline for the image classification workload

#### Helper Scripts

**`run_mlperf_simd.sh`**
- Quick wrapper to run common SIMD tests
- Saves you from typing long gem5 commands

**`analyze_stats.py`**
- Parses `stats.txt` and prints useful summaries
- Calculates things like throughput, speedup, etc.

#### Documentation Files (your guides)

**`COMPARISON_4CYC_SIMD_vs_BASELINE.md`** (the one you're in now!)
- Compares SIMD vs baseline when both have matched latencies
- Shows compute vs memory time breakdown

**`ARITHMETIC_LATENCY_CONFIGURATION.md`**
- How to set mul/add latencies in the baseline CPU
- Explains the MinorCPU functional unit setup

**`LATENCY_MATCHING_GUIDE.md`**
- How to make SIMD batch latency match CPU multiply latency
- Example: 4 SIMD muls in 4 cycles = 1 CPU mul in 4 cycles

**`INTEGER_OPERATIONS_ANALYSIS.md`**
- Confirms we're using integer math (uint64_t), not floating point

**Other docs:**
- `SIMD_BATCHING_IMPLEMENTATION.md` – How the 4-lane batching works
- `SIMD_LATENCY_MODELING.md` – How compute latency is modeled in the device
- `MEMORY_QUICK_REF.md` – Memory overhead analysis
- Various profiling and analysis reports from earlier experiments

---

### `tests/` – General Test Programs

**What's in here:**
- `gemm_test.c` – Basic GEMM test (not MLPerf-specific)
- `coproc_test.c` – Coprocessor test example
- `rvv_*.c` – RISC-V vector extension tests
- These are more for gem5 development/testing than ML benchmarking

**When to use:** If you want to test basic simulator features or debug the accelerator in isolation

---

### `m5out*/` – Output Directories (your results)

Each time you run gem5 with `-d <dirname>`, it creates a directory with:

**`stats.txt`** – The goldmine
- `simSeconds` → how long the simulation took (in simulated time)
- `numCycles` → how many clock cycles
- `simInsts` → how many instructions executed
- `mem_ctrl.readBursts` → memory read traffic
- `mem_ctrl.writeBursts` → memory write traffic
- CPU stats, cache stats (if enabled), accelerator stats

**`config.ini`** – Full system configuration
- Every parameter of every component
- Useful for debugging "wait, what was that set to?"

**`config.json`** – Same as above, JSON format

**Console output** – What your program printed
- If you redirect terminal output to a file, you can review it later

**Examples from recent runs:**
- `m5out_baseline_mul4_add1/` – Baseline with 4-cycle multiply, 1-cycle add
- `m5out_simd_batch4cyc/` – SIMD with 4-cycle batch latency

---

## How Things Connect (The Data Flow)

### Baseline CPU Path

1. You compile `test_fc_baseline.cpp` to get `test_fc_baseline.riscv`
2. You run `gem5.opt configs/mlperf/test_fc_baseline.py --mul-latency 4 --add-latency 1`
3. gem5 creates a system:
   - `RiscvMinorCPU` at 2 GHz
   - Functional units with your latencies (mul=4 cycles, add=1 cycle)
   - Memory bus → DDR3 controller
4. The CPU executes the binary:
   - Loads weights and inputs from memory
   - Does `result += weight * input` in a loop (81,920 times per inference)
   - Each multiply takes 4 cycles, each add takes 1 cycle
5. Stats get written to `m5out/stats.txt`

### SIMD Accelerator Path

1. You compile `test_fc_gemm_only.cpp` to get `test_fc_gemm_only.riscv`
2. You run `gem5.opt configs/mlperf/mlperf_simd_se.py --num-runs 10`
3. gem5 creates a system:
   - CPU (usually `TimingSimpleCPU` or `MinorCPU`) at 2 GHz
   - SIMD accelerator at MMIO base `0x40000000`, with 4 lanes and `compute_latency="2ns"` (4 cycles)
   - Memory bus → DDR3 controller
4. The CPU executes the binary:
   - Writes to MMIO registers to tell the accelerator:
     - Where the weights are (`srca`)
     - Where the inputs are (`srcb`)
     - Where to write results (`dst`)
     - How many elements and what dimensions
   - Writes to the `kick` register to start the operation
   - Polls the `status` register until done
5. The accelerator does its thing:
   - DMA-reads 4 weights from memory
   - DMA-reads 4 inputs from memory
   - Does 4 multiply-accumulates in parallel (takes `compute_latency` = 4 cycles)
   - DMA-writes results back
   - Repeats for all batches (8192 inputs / 4 = 2048 batches per output row)
6. Stats get written to `m5out/stats.txt`

---

## Key Parameters You'll Change

### CPU Model (in config scripts)
- `RiscvTimingSimpleCPU` – Simplest, zero latency for ALU ops (not realistic)
- `RiscvMinorCPU` – In-order pipelined, supports configurable FU latencies ✅ (we use this for baseline)
- `RiscvO3CPU` – Out-of-order, complex (not usually needed for these tests)

### Clock Speed (in config scripts)
- Set via `system.clk_domain.clock = '2GHz'`
- Both baseline and SIMD use 2 GHz (0.5 ns per cycle)

### Arithmetic Latencies (baseline only)
- `--mul-latency N` → multiply takes N cycles
- `--add-latency M` → add takes M cycles
- Set via command-line args to `test_fc_baseline.py`

### Accelerator Timing (SIMD only)
- `num_lanes` → how many elements processed in parallel (default: 4)
- `compute_latency` → time per batch (e.g., `"2ns"` = 4 cycles at 2 GHz)
- Set in the config script (e.g., `SimdAccel(..., compute_latency="2ns")`)

### Memory System
- `DDR3_1600_8x8` – 12.8 GB/s peak bandwidth
- Caches: usually **disabled** for fair comparison (both paths hit DRAM)
- Can enable with `--with-caches` flag if you want

### Number of Runs
- `--num-runs 10` → runs 10 inferences
- More runs = better average, but takes longer to simulate

---

## Common Workflows

### Run a baseline test
```bash
./build/RISCV/gem5.opt -d m5out_baseline \
  configs/mlperf/test_fc_baseline.py \
  --mul-latency 4 \
  --add-latency 1 \
  --num-runs 10
```

### Run a SIMD test
```bash
./build/RISCV/gem5.opt -d m5out_simd \
  configs/mlperf/mlperf_simd_se.py \
  --num-runs 10 \
  --binary configs/mlperf/test_fc_gemm_only.riscv
```

### Compare results
```bash
# Extract key stats
grep "simSeconds" m5out_baseline/stats.txt
grep "simSeconds" m5out_simd/stats.txt

# Or use the analyzer
python3 configs/mlperf/analyze_stats.py m5out_baseline/stats.txt
python3 configs/mlperf/analyze_stats.py m5out_simd/stats.txt
```

### Compile a new workload
```bash
riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \
  configs/mlperf/test_fc_gemm_only.cpp \
  -o configs/mlperf/test_fc_gemm_only.riscv
```

---

## What to Look At in `stats.txt`

### Timing
- `simSeconds` – Total simulated time (lower = faster)
- `system.cpu.numCycles` – How many CPU cycles

### Instructions
- `simInsts` – Instructions executed
- `system.cpu.cpi` – Cycles per instruction (higher = more stalls)
- `system.cpu.committedInstType::IntMult` – How many multiplies
- `system.cpu.committedInstType::MemRead` – How many loads

### Memory
- `mem_ctrl.readBursts` – Memory reads
- `mem_ctrl.writeBursts` – Memory writes
- `mem_ctrl.avgRdQLen` – Average read queue depth
- More bursts = more memory traffic = potentially slower

### Throughput (you calculate)
- Throughput = `num_runs / simSeconds` (inferences per second)
- Speedup = `baseline_simSeconds / simd_simSeconds`

---

## Troubleshooting Tips

### "Unknown instruction" panic
- Your binary is using custom instructions that gem5 doesn't recognize
- **Fix:** Use the MMIO-based binaries (like `test_fc_gemm_only.riscv`)

### Stats file is empty
- Simulation didn't finish (maybe you Ctrl+C'd it)
- **Fix:** Let it run to completion, or check for errors in the console output

### "Address out of range" error
- MMIO region not mapped in SE mode
- **Fix:** Check the config has `proc_ptr.map(0x40000000, 0x40000000, 0x1000, False)`

### Simulation is really slow
- That's gem5 for you (it's detailed but not fast)
- **Fix:** Reduce `--num-runs`, or use a simpler CPU model, or compile gem5.fast instead of gem5.opt

### Results don't match expectations
- Check that both runs use the same clock, memory, and cache settings
- Verify latencies are set correctly (print them in the config script)
- Make sure you're comparing the same workload (same GEMM size, same number of runs)

---

## Quick Reference Table

| File | Type | Purpose | When to Use |
|------|------|---------|-------------|
| `src/dev/simd_accel.cc` | C++ | Accelerator implementation | Modifying accelerator behavior |
| `mlperf_simd_se.py` | Config | SIMD test setup | Running accelerated workloads |
| `test_fc_baseline.py` | Config | CPU-only test setup | Running baseline comparisons |
| `test_fc_gemm_only.cpp` | Workload | GEMM test with MMIO | Clean SIMD vs baseline tests |
| `test_fc_baseline.cpp` | Workload | GEMM test CPU-only | Baseline comparison |
| `simple_ic_benchmark_simd.cpp` | Workload | Image classification with SIMD | More realistic ML workload |
| `m5out/stats.txt` | Output | Simulation statistics | Analyzing performance |
| `COMPARISON_*.md` | Docs | Performance reports | Understanding results |

---

## Next Steps / Where to Go From Here

- **Want to change accelerator latency?** Edit the `compute_latency` parameter in the config script
- **Want to change CPU latencies?** Use `--mul-latency` and `--add-latency` flags
- **Want to try different memory?** Swap `DDR3_1600_8x8()` for another DRAM model
- **Want to add caches?** Look at how other gem5 configs set up L1/L2 caches
- **Want more lanes?** Change `num_lanes` in the accelerator config (also might need to adjust compute_latency)
- **Want to profile?** Enable debug flags: `--debug-flags=SimdAccel` or `--debug-flags=Exec`

---

That's the tour! You should now have a mental map of what's where and how to poke around. If something's still unclear, check the specific doc files or grep the code for examples. Happy simulating! 🚀
