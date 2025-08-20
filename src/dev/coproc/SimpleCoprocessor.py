from m5.objects import BasicPioDevice, DmaDevice, Param, Addr, Parent, System

class SimpleCoprocessor(BasicPioDevice, DmaDevice):
    type = "SimpleCoprocessor"
    cxx_header = "dev/coproc/SimpleCoprocessor.hh"
    cxx_class = "gem5::SimpleCoprocessor"

    # BasicPioDevice params:
    pio_addr = Param.Addr(0x10000000, "PIO base address")
    pio_size = Param.Addr(0x1000, "PIO size")

    # DmaDevice needs the system to get a DMA port:
    system = Param.System(Parent.any, "system")

