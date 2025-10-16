# Minimal RISC-V SE configuration with SimdAccel device
# Automatically generated helper config.

import argparse

import m5
from m5.objects import (
    AddrRange,
    AtomicSimpleCPU,
    DDR3_1600_8x8,
    MemCtrl,
    Process,
    RiscvSEWorkload,
    Root,
    SimdAccel,
    SrcClockDomain,
    System,
    SystemXBar,
    TimingSimpleCPU,
    VoltageDomain,
)
from m5.util import addToPath

# NOTE: Keep this simple and explicit; avoids deprecated example infra.

parser = argparse.ArgumentParser(description="RISC-V SE test with SimdAccel")
parser.add_argument(
    "--cmd", required=True, help="Path to RISC-V binary (coproc_test)"
)
parser.add_argument(
    "--max-ticks",
    type=int,
    default=10_000_000_000,
    help="Simulation limit (ticks)",
)
parser.add_argument(
    "--accel-base",
    type=lambda x: int(x, 0),
    default=0x40000000,
    help="Base PIO address of accelerator",
)
parser.add_argument(
    "--accel-size", type=lambda x: int(x, 0), default=0x100, help="PIO size"
)
args = parser.parse_args()

system = System()

# Clock / voltage
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Single simple CPU (Atomic mode for SE MMIO simplicity)
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

# For SE mode, the MMU needs to know about the IO address range
# We'll configure it to allow pass-through for the MMIO region
system.cpu.mmu.pma_checker.uncacheable = [
    AddrRange(args.accel_base, args.accel_base + args.accel_size)
]

# SimdAccel device
system.simd = SimdAccel(pio_addr=args.accel_base, pio_size=args.accel_size)
# PIO connection (map into address space) and DMA connection
system.simd.pio = system.membus.mem_side_ports  # device is a slave at pio_addr
system.simd.dma = system.membus.cpu_side_ports  # DMA master connects to bus

# Workload / process
process = Process()
process.executable = args.cmd
process.cmd = [args.cmd]

# Use RISC-V SE workload wrapper
system.workload = RiscvSEWorkload.init_compatible(process.executable)
# Load process
system.cpu.workload = process
system.cpu.createThreads()

# Root and instantiate
root = Root(full_system=False, system=system)

m5.instantiate()

# After instantiation, map the MMIO region in the SE process address space
# Access the process via the workload and map the IO region
proc_ptr = process.getCCObject()
# Map MMIO region (virt == phys identity mapping for IO)
proc_ptr.map(args.accel_base, args.accel_base, args.accel_size, False)

print(f"[simd-config] Running binary: {args.cmd}")
exit_event = m5.simulate(args.max_ticks)
print(f"[simd-config] Exit cause: {exit_event.getCause()}")

# Dump stats to get detailed info
m5.stats.dump()

# Check if exit was normal
if "exiting with last active thread" in exit_event.getCause():
    print("[simd-config] Simulation completed normally")
    # print("[simd-config]")
    # print("[simd-config] To verify test result:")
    # print("[simd-config]   - Check C[7] value in memory")
    # print("[simd-config]   - If C[7] = 0xC0FFEEC0FFEE → TEST PASSED")
    # print("[simd-config]   - If C[0] = 0xDEADBEEFDEADBEEF → TEST FAILED")
    # print("[simd-config]   - Expected results: C[i] = A[i] * B[i]")
    # print(
    #     "[simd-config]     C[0]=0, C[1]=4, C[2]=12, C[3]=24, C[4]=40, C[5]=60, C[6]=84, C[7]=112"
    # )
else:
    print(f"[simd-config] ✗ Abnormal termination: {exit_event.getCause()}")
