from m5.objects import BasicPioDevice, Param, Addr, Parent, System
from m5.params import RequestPort, OptionalParam


class SimpleCoprocessor(BasicPioDevice):
    type = "SimpleCoprocessor"
    cxx_header = "dev/coproc/SimpleCoprocessor.hh"
    cxx_class = "gem5::SimpleCoprocessor"

    # BasicPioDevice params:
    pio_addr = Param.Addr(0x10000000, "PIO base address")
    pio_size = Param.Addr(0x1000, "PIO size")

    # Keep the C++ inheritance behavior: add DmaDevice as an extra C++ base
    # Avoid declaring extra C++ bases in Python binding; rely on C++ MI only.

    # DMA port and optional identifiers (mirror DmaDevice)
    dma = RequestPort("DMA port")
    sid = OptionalParam.Unsigned(
        "Stream identifier used by an IOMMU to distinguish amongst several devices attached to it",
    )
    ssid = OptionalParam.Unsigned(
        "Substream identifier used by an IOMMU to distinguish amongst several devices attached to it",
    )

