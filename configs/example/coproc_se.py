#!/usr/bin/env python3
import sys
from m5.objects import *
from m5.util import addToPath

# Bring in the simple cache helper classes (older-style config)
addToPath('configs/common')
from Caches import L1ICache, L1DCache, L2Cache  # noqa: E402

# Basic SE RISC-V system with classic caches and our coprocessor
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

system.membus = SystemXBar()

system.cpu = TimingSimpleCPU()

# L1 caches
system.cpu.icache = L1ICache(size='32kB', assoc=2)
system.cpu.dcache = L1DCache(size='32kB', assoc=2)
system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# L2
system.l2bus = L2XBar()
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

system.l2cache = L2Cache(size='256kB', assoc=8)
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.l2cache.mem_side = system.membus.cpu_side_ports  # L2 acts as a master toward the xbar

# Coprocessor MMIO/DMA
system.coproc = SimpleCoprocessor(pio_addr=0x40000000, pio_size=0x1000)

# MMIO (PIO) goes on the *memory side* of the bus (devices are slaves for PIO)
system.coproc.pio = system.membus.mem_side_ports

# DMA master goes on the *CPU side* of the bus (initiates requests)
system.coproc.dma = system.membus.cpu_side_ports

# System port (for functional accesses) connects to bus as a slave
system.system_port = system.membus.slave

# DRAM on the memory side of the bus
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# SE workload
process = Process()
process.executable = sys.argv[1]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)

def main():
    import m5
    m5.instantiate()
    print('Beginning simulation…')
    exit_event = m5.simulate()
    print('Exiting @ tick', m5.curTick(), 'because', exit_event.getCause())

if __name__ == '__m5_main__':
    main()

