from m5.objects import DmaDevice
from m5.params import *   # Param, Addr, Tick, Unsigned, OptionalParam, RequestPort, etc.

class SimdAccel(DmaDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'

    # PIO params (DmaDevice already inherits from PioDevice which has pio port)
    pio_addr = Param.Addr(0x40000000, "Base PIO address for the SIMD accelerator")
    pio_latency = Param.Latency("100ns", "Programmed IO latency")
    pio_size = Param.Addr(0x100, "PIO region size")

    # DmaDevice already provides dma port, sid, ssid
