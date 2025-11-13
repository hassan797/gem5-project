"""
Baseline FC GEMM Test Configuration (No SIMD Acceleration)
Tests FC layer with sequential computation for comparison with SIMD version
Uses MinorCPU with configurable functional unit latencies for arithmetic operations
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
    
    # =========================================================================
    # CPU with Configurable Arithmetic Latencies
    # =========================================================================
    # Use MinorCPU which supports functional unit latencies
    system.cpu = RiscvMinorCPU()
    system.cpu.cpu_id = 0
    
    # Configure functional units with realistic latencies
    # Integer ALU (add, sub, logic operations)
    intALU = MinorDefaultIntFU()
    intALU.opLat = args.add_latency  # Latency for add operations (cycles)
    intALU.issueLat = 1              # Issue latency
    
    # Integer Multiplier
    intMul = MinorDefaultIntMulFU()
    intMul.opLat = args.mul_latency  # Latency for multiply operations (cycles)
    intMul.issueLat = 1              # Issue latency
    
    # Integer Divider (not used in GEMM, but included for completeness)
    intDiv = MinorDefaultIntDivFU()
    intDiv.opLat = 9
    intDiv.issueLat = 9
    
    # Memory unit
    memFU = MinorDefaultMemFU()
    
    # Floating point/SIMD unit (not used in our integer GEMM)
    floatSimdFU = MinorDefaultFloatSimdFU()
    
    # Miscellaneous unit
    miscFU = MinorDefaultMiscFU()
    
    # Create FU pool with our configured units
    system.cpu.executeFuncUnits = MinorFUPool()
    system.cpu.executeFuncUnits.funcUnits = [
        intALU,      # Integer ALU with configurable add latency
        intMul,      # Integer multiplier with configurable mul latency
        intDiv,      # Integer divider
        memFU,       # Memory operations
        floatSimdFU, # FP/SIMD operations
        miscFU       # Misc operations
    ]
    
    # =========================================================================
    # System Configuration
    # =========================================================================
    
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
    parser = argparse.ArgumentParser(
        description='Baseline FC GEMM Test with Configurable Arithmetic Latencies',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Default: 1 cycle multiply, 1 cycle add
  ./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py
  
  # Realistic latencies: 3 cycle multiply, 1 cycle add
  ./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py --mul-latency 3 --add-latency 1
  
  # High latency multiply: 5 cycle multiply
  ./build/RISCV/gem5.opt configs/mlperf/test_fc_baseline.py --mul-latency 5
        """)
    
    parser.add_argument('--binary', type=str,
                       default='configs/mlperf/test_fc_baseline.riscv',
                       help='Path to baseline test binary')
    parser.add_argument('--num-runs', type=int, default=10,
                       help='Number of inference runs (default: 10)')
    
    # Arithmetic operation latencies (in CPU cycles)
    parser.add_argument('--mul-latency', type=int, default=3,
                       help='Latency for integer multiply operations in cycles (default: 3)')
    parser.add_argument('--add-latency', type=int, default=1,
                       help='Latency for integer add/ALU operations in cycles (default: 1)')
    
    args = parser.parse_args()
    
    print("=" * 80)
    print("FC Layer GEMM Test - BASELINE with Configurable Arithmetic Latencies")
    print("=" * 80)
    print(f"Binary: {args.binary}")
    print(f"Inference runs: {args.num_runs}")
    print(f"CPU: RiscvMinorCPU @ 2GHz (in-order, pipelined)")
    print(f"Memory: DDR3-1600 (no caches)")
    print(f"Acceleration: NONE (sequential computation)")
    print()
    print("Arithmetic Latencies:")
    print(f"  Integer Multiply: {args.mul_latency} cycles")
    print(f"  Integer Add/ALU:  {args.add_latency} cycles")
    print("=" * 80)
    print()
    
    # Create system
    (system, process) = create_system(args)
    
    # Create root
    root = Root(full_system=False, system=system)
    
    # Instantiate
    m5.instantiate()
    
    print("=" * 80)
    print("Starting baseline simulation...")
    print("=" * 80)
    
    # Run simulation
    exit_event = m5.simulate()
    
    print()
    print("=" * 80)
    print(f"Simulation ended: {exit_event.getCause()}")
    print(f"Simulated time: {m5.curTick() / 1e12:.6f} seconds")
    print("=" * 80)

if __name__ == '__m5_main__':
    main()
