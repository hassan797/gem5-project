# Systolic Accelerator Performance Analysis Report

**Date:** December 15, 2025  
**Workload:** Fully Connected Layer (FC) - 10x8192 Matrix Multiplication  
**Configuration:** 4x4 Systolic Array, Uncached AtomicSimpleCPU, DDR3 Memory  

---

## 1. Executive Summary

The simulation of the Fully Connected layer (10 output neurons, 8192 input features) completed in **0.082 seconds** of simulated time. The system is heavily **memory-bound**, with the CPU spending >99% of its time waiting for memory requests. The systolic accelerator successfully offloaded the computation but generated a massive amount of small DMA transactions, saturating the memory controller's read queue.

| Metric | Value |
| :--- | :--- |
| **Total Simulated Time** | 82.36 ms |
| **Total Cycles** | 164.7 Million |
| **CPU IPC** | 0.006 (Extremely Low) |
| **Total Memory Bandwidth** | 194.2 MB/s |
| **Accelerator DMA Reads** | 852 KB |
| **Accelerator DMA Writes** | 80 Bytes |

---

## 2. Detailed Performance Breakdown

### 2.1 CPU Performance (The Bottleneck)
The CPU (`AtomicSimpleCPU`) is effectively stalled for the entire duration of the simulation.
- **IPC (Instructions Per Cycle):** `0.0063`. This means the CPU executes 1 instruction every ~158 cycles.
- **Reason:** Without caches, every instruction fetch goes to main memory.
    - **Instruction Fetch Bandwidth:** 143 MB/s (73% of total system bandwidth).
    - **Data Bandwidth:** 40 MB/s.
- **Idle vs. Busy:** The stats show `numIdleCycles` is near zero, but this is misleading. In `AtomicSimpleCPU`, memory latency is accounted for as "busy" time during the instruction execution. The CPU is "busy waiting" for DRAM.

### 2.2 Memory Subsystem Analysis
The memory controller (`system.mem_ctrl`) is the busiest component.

- **Total Bandwidth Usage:** 194.2 MB/s.
    - **Read:** 178.7 MB/s (92%)
    - **Write:** 15.5 MB/s (8%)
- **Requestor Breakdown:**
    1.  **CPU Instruction Fetch:** 143.3 MB/s (Dominant consumer)
    2.  **CPU Data:** 40.5 MB/s
    3.  **Systolic Accelerator:** 10.3 MB/s
- **Latency:**
    - **CPU Data Read Latency:** ~7 ns
    - **Accelerator Read Latency:** ~35 ns (5x higher than CPU!)
    - **Why?** The accelerator's requests are getting stuck in the queue behind the CPU's massive stream of instruction fetches.
- **Queue Depth:**
    - **Average Write Queue Length:** 25.27 (High). The write queue is backing up, likely due to the CPU writing data to the accelerator's MMIO registers or stack operations.

### 2.3 Systolic Accelerator Efficiency
The accelerator moved **852 KB** of data to perform a matrix multiplication that theoretically requires only ~90 KB of unique data (81KB weights + 8KB inputs).

- **Data Amplification Factor:** ~9.5x
    - **Theoretical Load:** ~90 KB
    - **Actual DMA Load:** 852 KB
- **Root Cause:** **Lack of Input Broadcast.**
    - The accelerator fetches the Input Vector elements separately for *each row* of the systolic array.
    - With a 4x4 array, the same input value is fetched 4 times (once for each PE row) instead of being fetched once and broadcast.
    - **DMA Access Count:** 55,296 read requests.
    - **Average Request Size:** ~15.4 bytes.
    - **Inefficiency:** The memory system hates small, non-sequential accesses. The accelerator is peppering the DRAM with tiny 16-byte requests.

---

## 3. Critical Bottlenecks Identified

### 🔴 1. CPU Instruction Fetch (Uncached)
Running without caches (`--with-caches=False`) is the primary system bottleneck. The CPU consumes 73% of the memory bandwidth just fetching code. This artificially throttles the accelerator because the memory bus is contended.

### 🔴 2. Redundant DMA Fetches (Input Vector)
The accelerator is re-fetching the input vector `B` for every row of the weight matrix `A` processed in parallel.
- **Impact:** Wastes ~75% of the accelerator's allocated bandwidth.
- **Fix:** Implement a "Broadcast" mode in the DMA engine or a local Input Buffer (SRAM) to store the vector `B` once and reuse it across all PE rows.

### 🟠 3. Small DMA Transaction Size
The average DMA request is ~16 bytes (one 4x4 weight tile or one input value).
- **Impact:** Low DRAM row buffer hit rate for accelerator requests.
- **Fix:** Increase `k_tile_batch` further or implement a "Line Buffer" to fetch larger chunks (e.g., 64 bytes) at once.

---

## 4. Recommendations

1.  **Enable Caches:** Rerun the simulation with `--with-caches`. This will eliminate the 143 MB/s instruction fetch traffic and isolate the accelerator's performance.
2.  **Optimize Input Loading:** Modify the hardware description (C++) to fetch the Input Vector `B` only once per column-step and share it across all rows. This will reduce DMA traffic by ~4x.
3.  **Increase Array Size:** A 4x4 array is too small for 8192-dimension vectors. The overhead of setting up loops dominates. Moving to 16x16 or 32x32 would amortize the control overhead.
