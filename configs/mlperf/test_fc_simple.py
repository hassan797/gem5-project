"""
Simple FC GEMM Test Configuration
Tests only the FC layer GEMM operation without Conv1/Pool1
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
    """Create the gem5 system with SIMD accelerator"""
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
    
    # Configure MMIO region as uncacheable
    system.cpu.mmu.pma_checker.uncacheable = [
        AddrRange(0x40000000, 0x40001000)
    ]
    
    # Create SIMD accelerator
    system.simd_accel = SimdAccel()
    system.simd_accel.pio_addr = 0x40000000
    system.simd_accel.num_lanes = 4
    system.simd_accel.compute_latency = '10ns'
    
    # Memory bus
    system.membus = SystemXBar()
    
    # Connect CPU to memory bus (no caches)
    system.cpu.icache_port = system.membus.cpu_side_ports
    system.cpu.dcache_port = system.membus.cpu_side_ports
    
    # Connect SIMD accelerator to memory bus
    system.simd_accel.pio = system.membus.mem_side_ports
    system.simd_accel.dma = system.membus.cpu_side_ports
    
    # RISC-V interrupt setup
    system.cpu.createInterruptController()
    
    # Create memory controller
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports
    
    # Create process
    process = Process()
    process.cmd = [args.binary, str(args.num_runs)]  # Pass num_runs as argument
    system.cpu.workload = process
    system.cpu.createThreads()
    
    # System-wide settings
    system.workload = SEWorkload.init_compatible(args.binary)
    
    return (system, process)

def main():
    parser = argparse.ArgumentParser(description='Simple FC GEMM Test')
    parser.add_argument('--binary', type=str,
                       default='configs/mlperf/test_fc_gemm_only.riscv',
                       help='Path to test binary')
    parser.add_argument('--num-runs', type=int, default=100,
                       help='Number of inference runs (default: 100)')
    
    args = parser.parse_args()
    
    print("=" * 70)
    print("FC Layer GEMM Performance Test - gem5 Simulation")
    print("=" * 70)
    print(f"Binary: {args.binary}")
    print(f"Inference runs: {args.num_runs}")
    print(f"SIMD lanes: 4-way batching")
    print(f"GEMM dimensions: C[10×1] = W[10×8192] × X[8192×1]")
    print(f"Expected batches per inference: 2048 (K=8192/4)")
    print("=" * 70)
    print()
    
    # Create system
    (system, process) = create_system(args)
    
    # Create root
    root = Root(full_system=False, system=system)
    
    # Instantiate
    m5.instantiate()
    
    # Map MMIO region in SE mode
    print("Mapping MMIO region...")
    proc_ptr = process.getCCObject()
    proc_ptr.map(0x40000000, 0x40000000, 0x1000, False)
    print("MMIO mapped: 0x40000000-0x40001000")
    print()
    
    print("=" * 70)
    print("Starting simulation...")
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
