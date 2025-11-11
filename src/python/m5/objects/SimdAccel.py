from m5.objects import DmaDevice
from m5.params import *   # Param, Addr, Tick, Unsigned, OptionalParam, RequestPort, etc.

class SimdAccel(DmaDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'

    # PIO params
    pio_addr = Param.Addr(0x40000000, "Base PIO address for the SIMD accelerator")
    pio_size = Param.Addr(0x1000, "PIO region size")
    pio_latency = Param.Latency("100ns", "Programmed IO latency")

    # SIMD configuration
    num_lanes = Param.Unsigned(4, "Number of SIMD lanes (elements processed in parallel)")
    compute_latency = Param.Latency("10ns", "Latency for parallel compute operation")
