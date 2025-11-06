#!/usr/bin/env python3
import sys
import m5
from m5.objects import *

# Minimal SE system
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

system.membus = SystemXBar()

system.cpu = TimingSimpleCPU()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()

# SimdAccel device
system.simd = SimdAccel(pio_addr=0x40000000, pio_size=0x1000,
                        num_lanes=4, compute_latency="10ns")
system.simd.pio = system.membus.mem_side_ports
system.simd.dma = system.membus.cpu_side_ports

# System port
system.system_port = system.membus.cpu_side_ports

# DRAM
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Workload
system.workload = SEWorkload.init_compatible(sys.argv[1])

process = Process()
process.cmd = [sys.argv[1]]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)

m5.instantiate()

# Map MMIO region in the process page table (identity mapping for 0x40000000)
# This allows SE mode to access the SIMD accelerator's MMIO registers
process.map(0x40000000, 0x40000000, 0x1000, False)

print('Beginning simulation…')
exit_event = m5.simulate()
print('Exiting @ tick', m5.curTick(), 'because', exit_event.getCause())
