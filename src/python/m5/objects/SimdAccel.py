from m5.objects import BasicPioDevice
from m5.params import *   # Param, Addr, Tick, Unsigned, OptionalParam, RequestPort, etc.

class SimdAccel(BasicPioDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'

    # BasicPioDevice params
    pio_addr = Param.Addr(0x40000000, "Base PIO address for the SIMD accelerator")
    pio_size = Param.Addr(0x100, "PIO region size")

    # Make the generated C++ class inherit DmaDevice too (avoid Python multiple-inheritance)
    cxx_extra_bases = ["gem5::DmaDevice"]

    # DMA port and optional identifiers (mirror DmaDevice)
    dma = RequestPort("DMA port")
    sid = OptionalParam.Unsigned(
        "Stream identifier used by an IOMMU to distinguish amongst several devices attached to it",
    )
    ssid = OptionalParam.Unsigned(
        "Substream identifier used by an IOMMU to distinguish amongst several devices attached to it",
    )

    # Optional modeling params
    numLanes = Param.Unsigned(1, "Number of SIMD lanes (for modeling)")
    computeLatencyPerElem = Param.Tick(1, "Compute latency per element in ticks")
