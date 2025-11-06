#!/usr/bin/env python3
"""
MLPerf Tiny Image Classification Benchmark - gem5 Simulation Configuration
===========================================================================

This script configures a gem5 system to run the MLPerf Tiny benchmark in
syscall emulation (SE) mode with a standard CPU and memory hierarchy.

Usage:
    ./build/RISCV/gem5.opt configs/mlperf/mlperf_se.py [options]

Options:
    --cpu-type: CPU model (TimingSimpleCPU, MinorCPU, O3CPU, AtomicSimpleCPU)
    --num-runs: Number of inference runs (default: 10)
    --l1i-size: L1 instruction cache size (default: 32kB)
    --l1d-size: L1 data cache size (default: 32kB)
    --l2-size: L2 cache size (default: 256kB)
    --mem-size: Physical memory size (default: 512MB)

Author: gem5 MLPerf Integration
Date: October 2025
"""

import argparse
import sys
import os

import m5
from m5.objects import *
import sys
import os

def create_cache(size, assoc, tag_lat, data_lat, response_lat, is_instruction=False):
    """Helper function to create a cache with specified parameters."""
    cache = Cache()
    cache.size = size
    cache.assoc = assoc
    cache.tag_latency = tag_lat
    cache.data_latency = data_lat
    cache.response_latency = response_lat
    cache.mshrs = 4
    cache.tgts_per_mshr = 20
    
    if is_instruction:
        cache.is_read_only = True
        cache.writeback_clean = True
    
    return cache

def create_system(args):
    """Create and configure the system for MLPerf benchmark simulation."""
    
    print("=" * 70)
    print("MLPerf Tiny Image Classification - gem5 Simulation")
    print("=" * 70)
    print(f"Configuration:")
    print(f"  CPU Type: {args.cpu_type}")
    print(f"  Clock: {args.cpu_clock}")
    print(f"  L1I Cache: {args.l1i_size}")
    print(f"  L1D Cache: {args.l1d_size}")
    print(f"  L2 Cache: {args.l2_size}")
    print(f"  Memory: {args.mem_size}")
    print(f"  Benchmark runs: {args.num_runs}")
    print("=" * 70 + "\n")
    
    # Create the system
    system = System()
    
    # Set up clock domain
    system.clk_domain = SrcClockDomain()
    system.clk_domain.clock = args.cpu_clock
    system.clk_domain.voltage_domain = VoltageDomain()
    
    # Memory mode and ranges
    system.mem_mode = 'timing'  # Timing mode for realistic simulation
    system.mem_ranges = [AddrRange(args.mem_size)]
    
    # Create the CPU based on user selection
    cpu_class = {
        'atomic': AtomicSimpleCPU,
        'timing': TimingSimpleCPU,
        'minor': MinorCPU,
        'o3': O3CPU,
    }.get(args.cpu_type.lower(), TimingSimpleCPU)
    
    system.cpu = cpu_class()
    
    print(f"Creating CPU: {cpu_class.__name__}")
    
    # Create memory bus
    system.membus = SystemXBar()
    
    # Create L1 instruction and data caches
    system.cpu.icache = create_cache(
        size=args.l1i_size, assoc=2, tag_lat=2, data_lat=2, 
        response_lat=2, is_instruction=True
    )
    system.cpu.dcache = create_cache(
        size=args.l1d_size, assoc=2, tag_lat=2, data_lat=2, 
        response_lat=2, is_instruction=False
    )
    
    print(f"Created L1I cache: {args.l1i_size}, 2-way associative")
    print(f"Created L1D cache: {args.l1d_size}, 2-way associative")
    
    # Connect CPU ports to L1 caches
    system.cpu.icache_port = system.cpu.icache.cpu_side
    system.cpu.dcache_port = system.cpu.dcache.cpu_side
    
    # Create L2 bus
    system.l2bus = L2XBar()
    
    # Connect L1 caches to L2 bus
    system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
    system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports
    
    # Create L2 cache
    system.l2cache = create_cache(
        size=args.l2_size, assoc=8, tag_lat=20, data_lat=20, 
        response_lat=20, is_instruction=False
    )
    system.l2cache.cpu_side = system.l2bus.mem_side_ports
    system.l2cache.mem_side = system.membus.cpu_side_ports
    
    print(f"Created L2 cache: {args.l2_size}, 8-way associative")
    
    # Interrupt controller (required for SE mode)
    system.cpu.createInterruptController()
    
    # System port (required for SE mode)
    system.system_port = system.membus.cpu_side_ports
    
    # Create memory controller
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports
    
    print(f"Memory controller: DDR3-1600 (12.8 GB/s bandwidth)")
    
    # Set up the workload (our MLPerf benchmark)
    binary_path = os.path.join(os.path.dirname(__file__), 
                               'simple_ic_benchmark.riscv')
    
    if not os.path.exists(binary_path):
        print(f"ERROR: Benchmark binary not found at {binary_path}")
        print("Please compile it first with:")
        print("  riscv64-linux-gnu-g++ -static -O2 -march=rv64gc -mabi=lp64d \\")
        print("    configs/mlperf/simple_ic_benchmark.cpp \\")
        print("    -o configs/mlperf/simple_ic_benchmark.riscv -lm")
        sys.exit(1)
    
    print(f"\nBenchmark binary: {binary_path}")
    
    # Set up the workload and process
    system.workload = SEWorkload.init_compatible(binary_path)
    
    process = Process()
    process.cmd = [binary_path, str(args.num_runs)]
    system.cpu.workload = process
    system.cpu.createThreads()
    
    print(f"Workload: MLPerf Image Classification ({args.num_runs} inference runs)\n")
    
    return system

def main():
    """Main simulation entry point."""
    
    parser = argparse.ArgumentParser(
        description='Run MLPerf Tiny benchmark in gem5',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # CPU configuration
    parser.add_argument('--cpu-type', type=str, default='TimingSimple',
                       choices=['atomic', 'timing', 'Timing', 'TimingSimple', 
                               'minor', 'Minor', 'o3', 'O3'],
                       help='CPU model to use')
    
    parser.add_argument('--cpu-clock', type=str, default='2GHz',
                       help='CPU clock frequency')
    
    # Cache configuration
    parser.add_argument('--l1i-size', type=str, default='32kB',
                       help='L1 instruction cache size')
    
    parser.add_argument('--l1d-size', type=str, default='32kB',
                       help='L1 data cache size')
    
    parser.add_argument('--l2-size', type=str, default='256kB',
                       help='L2 cache size')
    
    # Memory configuration
    parser.add_argument('--mem-size', type=str, default='512MB',
                       help='Physical memory size')
    
    # Benchmark configuration
    parser.add_argument('--num-runs', type=int, default=10,
                       help='Number of inference runs')
    
    args = parser.parse_args()
    
    # Create the system
    system = create_system(args)
    
    # Create the root object
    root = Root(full_system=False, system=system)
    
    # Instantiate the system
    print("Instantiating system...")
    m5.instantiate()
    
    print("\n" + "=" * 70)
    print("Starting simulation...")
    print("=" * 70 + "\n")
    
    # Run the simulation
    exit_event = m5.simulate()
    
    # Print simulation results
    print("\n" + "=" * 70)
    print("Simulation Complete!")
    print("=" * 70)
    print(f"Exit reason: {exit_event.getCause()}")
    print(f"Simulated time: {m5.curTick() / 1e12:.6f} seconds")
    print(f"Simulated ticks: {m5.curTick()}")
    
    # Calculate IPC if available
    if hasattr(system.cpu, 'numCycles'):
        cycles = system.cpu.numCycles
        if hasattr(system.cpu, 'committedInsts'):
            insts = system.cpu.committedInsts
            ipc = float(insts) / float(cycles) if cycles > 0 else 0
            print(f"Instructions: {insts}")
            print(f"Cycles: {cycles}")
            print(f"IPC: {ipc:.3f}")
    
    print("\nPerformance statistics saved to: m5out/stats.txt")
    print("=" * 70 + "\n")
    
    sys.exit(0)

if __name__ == '__m5_main__':
    main()
