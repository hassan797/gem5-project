#!/usr/bin/env python3
"""
Systolic Array Accelerator - gem5 Configuration
===============================================

This script configures gem5 to run matrix multiplication workloads
with the custom systolic array accelerator.

Usage:
    ./build/RISCV/gem5.opt configs/mlperf/test_systolic.py [options]

Options:
    --binary: Path to the test binary
    --cpu-type: CPU model (timing, minor, o3)
    --array-size: Size of the PE array (default: 4x4)
    --mac-latency: Latency for parallel MAC operation (default: 10ns)
    --with-caches: Enable L1/L2 caches (default: False for simple testing)
"""

import argparse
import sys
import os

import m5
from m5.objects import *

def create_system(args):
    """Create system with systolic array accelerator."""
    
    print("=" * 70)
    print("Systolic Array Accelerator - gem5 Simulation")
    print("=" * 70)
    print(f"Configuration:")
    print(f"  CPU: {args.cpu_type}")
    print(f"  Clock: 2GHz")
    print(f"  PE Array: {args.array_size}×{args.array_size}")
    print(f"  MAC Latency: {args.mac_latency}")
    print(f"  Caches: {'Enabled' if args.with_caches else 'Disabled'}")
    print(f"  Binary: {args.binary}")
    print("=" * 70 + "\n")
    
    # Create system
    system = System()
    system.clk_domain = SrcClockDomain(clock='2GHz', 
                                       voltage_domain=VoltageDomain())
    system.mem_mode = 'timing'
    system.mem_ranges = [AddrRange('512MB')]
    
    # Memory bus
    system.membus = SystemXBar()
    
    # CPU selection
    cpu_class = {
        'timing': TimingSimpleCPU,
        'minor': MinorCPU,
        'o3': O3CPU,
    }.get(args.cpu_type.lower(), TimingSimpleCPU)
    
    system.cpu = cpu_class()
    print(f"Created CPU: {cpu_class.__name__}")
    
    # Cache configuration
    if args.with_caches:
        from m5.objects import Cache
        
        # L1 I-cache
        system.cpu.icache = Cache(size='32kB', assoc=2, tag_latency=2,
                                   data_latency=2, response_latency=2)
        system.cpu.icache.is_read_only = True
        
        # L1 D-cache
        system.cpu.dcache = Cache(size='32kB', assoc=2, tag_latency=2,
                                   data_latency=2, response_latency=2)
        
        # Connect CPU to caches
        system.cpu.icache_port = system.cpu.icache.cpu_side
        system.cpu.dcache_port = system.cpu.dcache.cpu_side
        
        # L2 bus
        system.l2bus = L2XBar()
        system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
        system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports
        
        # L2 cache
        system.l2cache = Cache(size='256kB', assoc=8, tag_latency=20,
                              data_latency=20, response_latency=20)
        system.l2cache.cpu_side = system.l2bus.mem_side_ports
        system.l2cache.mem_side = system.membus.cpu_side_ports
        
        print("Caches: L1I=32KB, L1D=32KB, L2=256KB")
    else:
        # No caches - direct connection
        system.cpu.icache_port = system.membus.cpu_side_ports
        system.cpu.dcache_port = system.membus.cpu_side_ports
        print("Caches: Disabled (direct to memory)")
    
    # Interrupt controller
    system.cpu.createInterruptController()
    
    # X86 requires connecting interrupt ports
    if hasattr(system.cpu, 'interrupts'):
        for i in range(len(system.cpu.interrupts)):
            system.cpu.interrupts[i].pio = system.membus.mem_side_ports
            system.cpu.interrupts[i].int_requestor = system.membus.cpu_side_ports
            system.cpu.interrupts[i].int_responder = system.membus.mem_side_ports
    
    # Configure MMIO as uncacheable (for systolic accelerator)
    # Only RISC-V has PMA checker
    if hasattr(system.cpu.mmu, 'pma_checker'):
        system.cpu.mmu.pma_checker.uncacheable = [
            AddrRange(0x50000000, 0x50001000)  # Different address than SIMD
        ]
    
    # ========== Systolic Array Accelerator ==========
    # Create the systolic array accelerator device
    system.systolic = SystolicAccel(
        pio_addr=0x50000000,              # MMIO base address
        pio_size=0x1000,                  # 4KB MMIO region
        pio_latency="1ns",                # MMIO access latency
        array_rows=args.array_size,       # Number of PE rows
        array_cols=args.array_size,       # Number of PE columns
        mac_latency=args.mac_latency      # Latency for parallel MAC ops
    )
    
    # Connect systolic accelerator
    # PIO (MMIO registers) - device is slave on memory side
    system.systolic.pio = system.membus.mem_side_ports
    # DMA (data transfers) - device is master on CPU side
    system.systolic.dma = system.membus.cpu_side_ports
    
    print(f"Systolic Array Accelerator:")
    print(f"  - MMIO Address: {hex(system.systolic.pio_addr)}")
    print(f"  - PE Array: {system.systolic.array_rows}×{system.systolic.array_cols}")
    print(f"  - MAC Latency: {system.systolic.mac_latency}")
    print(f"  - Total PEs: {system.systolic.array_rows * system.systolic.array_cols}")
    
    # System port
    system.system_port = system.membus.cpu_side_ports
    
    # Memory controller
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports
    
    print(f"Memory: DDR3-1600 (12.8 GB/s)")
    
    # Workload setup
    if not os.path.exists(args.binary):
        print(f"\nERROR: Binary not found: {args.binary}")
        print("Please compile with:")
        print("  riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \\")
        print("    <test_program.c> -o <test_program.riscv>")
        sys.exit(1)
    
    system.workload = SEWorkload.init_compatible(args.binary)
    
    process = Process()
    process.cmd = [args.binary]
    system.cpu.workload = process
    system.cpu.createThreads()
    
    print(f"\nBinary: {args.binary}")
    print()
    
    return system, process

def main():
    parser = argparse.ArgumentParser(
        description='Systolic Array Accelerator Test',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    parser.add_argument('--binary', type=str,
                       default='tests/gemm_test.riscv',
                       help='Path to test binary')
    
    parser.add_argument('--cpu-type', type=str, default='timing',
                       choices=['timing', 'minor', 'o3'],
                       help='CPU model')
    
    parser.add_argument('--array-size', type=int, default=4,
                       help='Size of PE array (NxN)')
    
    parser.add_argument('--mac-latency', type=str, default='10ns',
                       help='Latency for parallel MAC operation')
    
    parser.add_argument('--with-caches', action='store_true',
                       help='Enable L1/L2 caches')
    
    args = parser.parse_args()
    
    # Create system
    system, process = create_system(args)
    
    # Create root
    root = Root(full_system=False, system=system)
    
    # Instantiate
    print("Instantiating system...")
    m5.instantiate()
    
    # Map MMIO region in process address space (required for SE mode)
    proc_ptr = process.getCCObject()
    proc_ptr.map(0x50000000, 0x50000000, 0x1000, False)
    print("MMIO region mapped: 0x50000000-0x50001000")
    
    print("\n" + "=" * 70)
    print("Starting simulation...")
    print("=" * 70 + "\n")
    
    # Run
    exit_event = m5.simulate()
    
    # Results
    print("\n" + "=" * 70)
    print("Simulation Complete!")
    print("=" * 70)
    print(f"Exit reason: {exit_event.getCause()}")
    print(f"Simulated time: {m5.curTick() / 1e12:.6f} seconds")
    print(f"Simulated ticks: {m5.curTick()}")
    
    if hasattr(system.cpu, 'numCycles'):
        cycles = system.cpu.numCycles
        if hasattr(system.cpu, 'committedInsts'):
            insts = system.cpu.committedInsts
            ipc = float(insts) / float(cycles) if cycles > 0 else 0
            print(f"Instructions: {insts}")
            print(f"Cycles: {cycles}")
            print(f"IPC: {ipc:.3f}")
    
    print("\nStats saved to: m5out/stats.txt")
    print("=" * 70 + "\n")
    
    sys.exit(0)

if __name__ == '__m5_main__':
    main()
