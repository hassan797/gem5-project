# Simple RISC-V SE configuration with SimdAccel device for testing
# Based on working coproc_test configuration

import argparse
import m5
from m5.objects import *

parser = argparse.ArgumentParser(description="RISC-V SE test with SimdAccel")
parser.add_argument("--binary", default="tests/simple_simd_test.riscv", 
                    help="Path to RISC-V binary")
parser.add_argument("--max-ticks", type=int, default=10_000_000_000,
                    help="Simulation limit (ticks)")
args = parser.parse_args()

# Create system
system = System()

# Clock / voltage
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "2GHz"  # Match your MLPerf config
system.clk_domain.voltage_domain = VoltageDomain()

# Single CPU - use AtomicSimpleCPU for SE MMIO simplicity
system.cpu = AtomicSimpleCPU()
system.cpu.createInterruptController()  # Required for RISC-V

# Memory bus
system.membus = SystemXBar(width=64)

# Memory controller and DRAM
system.mem_ranges = [AddrRange("512MB")]
system.mem_mode = "atomic"  # Atomic mode - simpler for SE with MMIO
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Attach CPU ports
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# Critical: For SE mode, the MMU needs to know about the IO address range
ACCEL_BASE = 0x40000000
ACCEL_SIZE = 0x100
system.cpu.mmu.pma_checker.uncacheable = [
    AddrRange(ACCEL_BASE, ACCEL_BASE + ACCEL_SIZE)
]

# SimdAccel device with batching parameters
system.simd = SimdAccel(
    pio_addr=ACCEL_BASE,
    pio_size=ACCEL_SIZE,
    num_lanes=4,           # 4-way SIMD lanes
    compute_latency="10ns" # Compute latency for batched operations
)

# PIO connection (map into address space) and DMA connection
system.simd.pio = system.membus.mem_side_ports  # device is a slave at pio_addr
system.simd.dma = system.membus.cpu_side_ports  # DMA master connects to bus

# Workload / process
process = Process()
process.executable = args.binary
process.cmd = [args.binary]

# Use RISC-V SE workload wrapper
system.workload = RiscvSEWorkload.init_compatible(process.executable)
system.cpu.workload = process
system.cpu.createThreads()

# Root and instantiate
root = Root(full_system=False, system=system)

print("=" * 70)
print("SIMD Accelerator Simple Test Configuration")
print("=" * 70)
print(f"Binary: {args.binary}")
print(f"CPU: AtomicSimpleCPU @ 2GHz")
print(f"SIMD Accelerator:")
print(f"  - Base Address: 0x{ACCEL_BASE:08x}")
print(f"  - Lanes: 4")
print(f"  - Compute Latency: 10ns")
print(f"  - Batched operations enabled")
print("=" * 70)

m5.instantiate()

# Critical: After instantiation, map the MMIO region in the SE process address space
proc_ptr = process.getCCObject()
proc_ptr.map(ACCEL_BASE, ACCEL_BASE, ACCEL_SIZE, False)

print("\nStarting simulation...\n")
exit_event = m5.simulate(args.max_ticks)
print(f"\n[DONE] Exit cause: {exit_event.getCause()}")

# Dump stats
m5.stats.dump()

if "exiting with last active thread" in exit_event.getCause():
    print("\n✓ Simulation completed successfully")
    print("Check m5out/stats.txt for detailed statistics")
else:
    print(f"\n✗ Abnormal termination: {exit_event.getCause()}")
