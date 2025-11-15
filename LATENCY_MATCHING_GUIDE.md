# SIMD vs Baseline Latency Matching Configuration

## Objective

Configure the baseline CPU's multiply latency so that **4 SIMD multiplications (done in parallel) take the same total time as 1 baseline CPU multiplication**.

This creates a **fair comparison** where:
- SIMD advantage comes from **parallelism** (4-way batching)
- Both systems have **equal per-element compute cost**
- Speedup reflects **architectural efficiency**, not arbitrary latency differences

---

## SIMD Accelerator Configuration

### From `src/python/m5/objects/SimdAccel.py`

```python
num_lanes = 4                           # 4-way SIMD (processes 4 elements in parallel)
compute_latency = Param.Latency("10ns") # Total time to compute 4 multiplications
```

### Key Points
- **Batch size**: 4 elements processed **simultaneously**
- **Compute latency**: 10 ns for **all 4 multiplications** (parallel)
- **Per-element cost**: 10 ns / 4 = 2.5 ns (if amortized)
- **Total throughput**: 4 muls / 10 ns = 400 million muls/sec @ 10ns latency

---

## Baseline CPU Configuration

### Clock Frequency
- **CPU frequency**: 2 GHz
- **Cycle period**: 0.5 ns per cycle
- **Cycles per 10ns**: 10 ns / 0.5 ns = **20 cycles**

### Matched Latency Calculation

To match 4 SIMD multiplications with 1 baseline multiplication:

```
4 SIMD muls (in parallel) = 10 ns total
1 baseline mul            = 10 ns total

Baseline multiply latency = 10 ns / 0.5 ns per cycle = 20 cycles
```

### Configuration Command for baseline CPU
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --mul-latency 20 \
    --add-latency 1 \
    --num-runs 10
```

Or use the helper script:
```bash
./run_baseline_matched_latency.sh 10
```

---

## Performance Analysis

### For FC Layer: C[10×1] = W[10×8192] × X[8192×1]

#### Operations per Inference
- **Multiplications**: 81,920 (M × K)
- **Additions**: 81,910 (accumulations)

#### Computation Time Breakdown

| Configuration | Mul Time | Add Time | Total Compute | Note |
|---------------|----------|----------|---------------|------|
| **SIMD (4-way)** | (81,920/4) × 10ns = 204.8 µs | N/A | **~205 µs** | 4 muls in parallel |
| **Baseline (20-cyc)** | 81,920 × 10ns = 819.2 µs | 81,910 × 0.5ns = 41 µs | **~860 µs** | Sequential |

**Expected compute speedup**: ~4× (due to 4-way parallelism)

#### Reality Check
Actual simulation time will be **dominated by memory access**:
- SIMD: Bulk DMA transfers (more efficient)
- Baseline: Many individual loads/stores (less efficient)

Expected overall speedup: **Greater than 4×** due to better memory access patterns.

---

## Why This Configuration?

### Fair Comparison
✅ **Equal per-element cost**: Each element takes the same time to compute  
✅ **Architectural advantage only**: Speedup comes from parallelism, not magic  
✅ **Realistic**: Reflects real hardware where SIMD units match scalar unit latency  

### Alternative (Unfair) Configurations

#### 1. Zero Latency (Original TimingSimpleCPU)
```bash
--mul-latency 1 --add-latency 1  # ~0.5ns per mul
```
❌ **Problem**: Makes CPU unrealistically fast  
❌ **Result**: Underestimates SIMD advantage


#### 2. Matched Configuration (Recommended)
```bash
--mul-latency 20 --add-latency 1  # 10ns per mul
```
✅ **Correct**: 4 SIMD muls (10ns) = 1 CPU mul (10ns)  
✅ **Fair**: Speedup reflects parallelism + memory efficiency

---

## Expected Results

### With Matched Latencies (mul=20 cycles)

#### Baseline Performance
- **Compute time**: ~860 µs (81,920 muls × 10ns)
- **Memory time**: ~? ms (many small accesses)
- **Total time**: **Compute + Memory** (both significant)

#### SIMD Performance
- **Compute time**: ~205 µs (81,920/4 batches × 10ns)
- **Memory time**: ~? ms (bulk DMA transfers)
- **Total time**: **Memory dominated** (compute is small)

#### Speedup Breakdown
```
Speedup = T_baseline / T_simd
        = (860µs + M_baseline) / (205µs + M_simd)
```

Where:
- `M_baseline` > `M_simd` (worse memory access pattern)
- Compute contributes **more significantly** to baseline time
- Expected overall speedup: **5-10×** (depending on memory system)

---

## Verification

### 1. Run Baseline with Matched Latency
```bash
./run_baseline_matched_latency.sh 1  # 1 inference for quick test
```

### 2. Check Statistics
```bash
grep "simSeconds" m5out/stats.txt
grep "numCycles" m5out/stats.txt
```

### 3. Calculate Compute Cycles
```
Compute cycles ≈ (81,920 muls × 20 cycles) + (81,910 adds × 1 cycle)
                ≈ 1,638,400 + 81,910
                ≈ 1,720,310 cycles
                ≈ 860 µs @ 2 GHz
```

### 4. Compare with SIMD
Run SIMD test and compare total execution time.

---

## Implementation Files

### Modified Files
- `configs/mlperf/test_fc_baseline.py`
  - Changed from `RiscvTimingSimpleCPU` to `RiscvMinorCPU`
  - Added `--mul-latency` and `--add-latency` parameters
  - Configured functional units with latencies

### New Files
- `run_baseline_matched_latency.sh`
  - Helper script to run with matched latencies
  - Calculates required cycles automatically
  - Shows configuration before running

### Configuration Files
- `src/python/m5/objects/SimdAccel.py`
  - Defines SIMD `compute_latency = "10ns"`
  - Defines `num_lanes = 4`

---

## Summary Table

| Parameter | SIMD | Baseline (Matched) | Ratio |
|-----------|------|-------------------|-------|
| **Batch Size** | 4 elements | 1 element | 4:1 |
| **Compute Latency** | 10 ns (for 4 muls) | 10 ns (for 1 mul) | 1:1 |
| **Throughput** | 4 muls / 10ns | 1 mul / 10ns | 4:1 |
| **Operations (FC)** | 81,920 muls | 81,920 muls | 1:1 |
| **Compute Batches** | 20,480 (÷4) | 81,920 (÷1) | 1:4 |
| **Compute Time** | ~205 µs | ~860 µs | 1:4.2 |
| **Expected Speedup** | — | **4-10×** | — |

---

## Usage Examples

### Quick Test (1 inference)
```bash
./run_baseline_matched_latency.sh 1
```

### Standard Test (10 inferences)
```bash
./run_baseline_matched_latency.sh 10
```

### Full Comparison Test (100 inferences)
```bash
./run_baseline_matched_latency.sh 100
```

### Manual Configuration
```bash
./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py \
    --num-runs 10 \
    --mul-latency 20 \
    --add-latency 1
```

---

## Conclusion

With **matched latencies** (20 cycles for baseline multiply = 10ns for 4 SIMD multiplies):

✅ **Fair comparison**: Equal per-element compute cost  
✅ **Clear advantage**: SIMD wins through parallelism + memory efficiency  
✅ **Realistic**: Reflects actual hardware design tradeoffs  
✅ **Quantifiable**: Speedup is measurable and meaningful

The SIMD accelerator's advantage comes from:
1. **4-way parallelism** (4× fewer compute batches)
2. **Better memory access** (bulk DMA vs many small accesses)
3. **Lower overhead** (fewer instructions, less control flow)
