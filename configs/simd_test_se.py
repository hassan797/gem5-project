"""
Simple gem5 SE mode configuration for testing SIMD element-wise multiplication
"""

import m5
from m5.objects import *
import os

# Create the system
system = System()

# Set clock and voltage
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '2GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Memory configuration
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# Memory bus
system.membus = SystemXBar()

# CPU configuration - TimingSimpleCPU
system.cpu = TimingSimpleCPU()

# Connect CPU directly to memory bus (no caches for simplicity)
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# Interrupt controller
system.cpu.createInterruptController()

# ========== SIMD Accelerator ==========
system.simd = SimdAccel(
    pio_addr=0x40000000,       # MMIO base address
    pio_size=0x1000,           # 4KB MMIO region  
    pio_latency='1ns',         # PIO access latency
    num_lanes=4,               # Process 4 elements per cycle
    compute_latency="10ns"     # Latency per SIMD operation
)

# Connect SIMD accelerator
# PIO (MMIO registers) - connect as memory-side port (slave)
system.simd.pio = system.membus.mem_side_ports
# DMA (data transfers) - connect as CPU-side port (master)
system.simd.dma = system.membus.cpu_side_ports

print(f"SIMD Accelerator configured:")
print(f"  MMIO Address: 0x40000000")
print(f"  SIMD Lanes: {system.simd.num_lanes}")
print(f"  Compute Latency: {system.simd.compute_latency}")

# System port
system.system_port = system.membus.cpu_side_ports

# Memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Workload
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=str, default='tests/simd_elem_test.riscv',
                    help='Binary to run')
args, remaining_args = parser.parse_known_args()
binary = args.binary

if not os.path.exists(binary):
    print(f"ERROR: Binary not found: {binary}")
    import sys
    sys.exit(1)

system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# Instantiate system
root = Root(full_system=False, system=system)
m5.instantiate()

# Run simulation
print("\n" + "="*70)
print("Beginning SIMD Element-wise Multiplication Test...")
print("="*70 + "\n")
exit_event = m5.simulate()

print(f"\n" + "="*70)
print(f"Simulation Complete")
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
print("="*70)
