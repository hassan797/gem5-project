"""
Baseline FC GEMM Test Configuration (No SIMD Acceleration)
Tests FC layer with sequential computation for comparison with SIMD version
"""

import m5
from m5.objects import *
from m5.util import addToPath
import argparse
import sys

# Add configs/common to path
addToPath('../')
from common import Options, MemConfig

def create_system(args):
    """Create the gem5 system WITHOUT SIMD accelerator (baseline)"""
    system = System()
    
    # Clock and voltage domain
    system.clk_domain = SrcClockDomain()
    system.clk_domain.clock = '2GHz'
    system.clk_domain.voltage_domain = VoltageDomain()
    
    # Memory setup
    system.mem_mode = 'timing'
    system.mem_ranges = [AddrRange('512MB')]
    
    # CPU
    system.cpu = RiscvTimingSimpleCPU()
    system.cpu.cpu_id = 0
    
    # Create interrupt controller
    system.cpu.createInterruptController()
    
    # Memory bus
    system.membus = SystemXBar()
    
    # Connect CPU to memory bus (no caches - same as SIMD test)
    system.cpu.icache_port = system.membus.cpu_side_ports
    system.cpu.dcache_port = system.membus.cpu_side_ports
    
    # Create memory controller
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports
    
    # Create process
    process = Process()
    process.cmd = [args.binary, str(args.num_runs)]
    system.cpu.workload = process
    system.cpu.createThreads()
    
    # System-wide settings
    system.workload = SEWorkload.init_compatible(args.binary)
    
    return (system, process)

def main():
    parser = argparse.ArgumentParser(description='Baseline FC GEMM Test (No SIMD)')
    parser.add_argument('--binary', type=str,
                       default='configs/mlperf/test_fc_baseline.riscv',
                       help='Path to baseline test binary')
    parser.add_argument('--num-runs', type=int, default=10,
                       help='Number of inference runs (default: 10)')
    
    args = parser.parse_args()
    
    print("=" * 70)
    print("FC Layer GEMM Test - BASELINE (No SIMD Acceleration)")
    print("=" * 70)
    print(f"Binary: {args.binary}")
    print(f"Inference runs: {args.num_runs}")
    print("CPU: RiscvTimingSimpleCPU @ 2GHz")
    print("Memory: DDR3-1600 (no caches)")
    print("Acceleration: NONE (sequential computation)")
    print("=" * 70)
    print()
    
    # Create system
    (system, process) = create_system(args)
    
    # Create root
    root = Root(full_system=False, system=system)
    
    # Instantiate
    m5.instantiate()
    
    print("=" * 70)
    print("Starting baseline simulation...")
    print("=" * 70)
    
    # Run simulation
    exit_event = m5.simulate()
    
    print()
    print("=" * 70)
    print(f"Simulation ended: {exit_event.getCause()}")
    print(f"Simulated time: {m5.curTick() / 1e12:.6f} seconds")
    print("=" * 70)

if __name__ == '__m5_main__':
    main()
