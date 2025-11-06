from m5.objects import BasicPioDevice
from m5.params import *   # Param, Addr, Tick, Unsigned, OptionalParam, RequestPort, etc.

class SimdAccel(BasicPioDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'

    # BasicPioDevice params
    pio_addr = Param.Addr(0x40000000, "Base PIO address for the SIMD accelerator")
    pio_size = Param.Addr(0x1000, "PIO region size")

    # SIMD configuration
    num_lanes = Param.Unsigned(4, "Number of SIMD lanes (elements processed in parallel)")
    compute_latency = Param.Latency("10ns", "Latency for parallel compute operation")

    # DMA port (C++ has multiple inheritance: BasicPioDevice + DmaDevice)
    dma = RequestPort("DMA port")
    sid = OptionalParam.Unsigned("Stream ID for IOMMU")
    ssid = OptionalParam.Unsigned("Substream ID for IOMMU")
