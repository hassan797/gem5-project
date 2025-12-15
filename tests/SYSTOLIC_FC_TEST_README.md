# MLPerf FC Layer Test for Systolic Array

## Test File
`tests/systolic_fc_test.c` - MLPerf-style fully connected layer test

## Description
This test simulates FC layer operations similar to those in MLPerf workloads:
1. **Test 1**: Small FC (10 outputs, 64 inputs) - warmup test
2. **Test 2**: MLPerf Tiny FC (10 outputs, 8192 inputs) - actual MLPerf dimensions
3. **Test 3**: Medium FC (64 outputs, 1024 inputs)
4. **Test 4**: Large FC (256 outputs, 2048 inputs)

## FC Layer Mapping to GEMM
FC layer: `Y = W × X`
- W [M×K]: Weight matrix (M outputs, K inputs)
- X [K×1]: Input vector (K features)
- Y [M×1]: Output vector (M values)

Maps to GEMM: `C[M×1] = A[M×K] × B[K×1]`

## Compilation
Since the path has spaces, compile manually or from a path without spaces:

```bash
# Option 1: Use Docker (from a path without spaces)
docker run --rm -v /path/to/gem5:/workspace -w /workspace \
  riscv64/ubuntu:22.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq gcc-riscv64-linux-gnu && \
   riscv64-linux-gnu-gcc -O2 -static -march=rv64gc -mabi=lp64d \
   -o tests/systolic_fc_test.riscv tests/systolic_fc_test.c"

# Option 2: If you have riscv64-linux-gnu-gcc installed
riscv64-linux-gnu-gcc -O2 -static -march=rv64gc -mabi=lp64d \
  -o tests/systolic_fc_test.riscv tests/systolic_fc_test.c
```

## Running the Test
```bash
./build/RISCV/gem5.opt configs/mlperf/test_systolic.py \
  --binary=tests/systolic_fc_test.riscv
```

## Expected Output
Each test should:
1. Initialize weight matrix W and input vector X (all 1s)
2. Configure systolic accelerator via MMIO
3. Compute Y = W × X on accelerator
4. Compute reference on CPU
5. Verify all outputs match
6. Print "PASSED" if correct

## Test Dimensions
- Test 1: 10×64 weights, 64 inputs → 10 outputs (640 parameters, 5 KB)
- Test 2: 10×8192 weights, 8192 inputs → 10 outputs (81,920 params, 640 KB)
- Test 3: 64×1024 weights, 1024 inputs → 64 outputs (65,536 params, 512 KB)
- Test 4: 256×2048 weights, 2048 inputs → 256 outputs (524,288 params, 4 MB)

## Systolic Array Tiling
The systolic array (default 4×4 PEs) will automatically tile large FC layers:
- Test 2 (10×8192): 10 output tiles × 2048 K tiles = 20,480 tile operations
- Test 4 (256×2048): 64 output tiles × 512 K tiles = 32,768 tile operations

This tests the accelerator's ability to handle large FC layers similar to those in MLPerf!
