from m5.objects import BasicPioDevice, DmaDevice, Param, Addr, Parent, System


class SimdAccel(BasicPioDevice, DmaDevice):
    type = 'SimdAccel'
    cxx_header = 'dev/simd_accel.hh'
    cxx_class = 'gem5::SimdAccel'

    # BasicPioDevice params:
    pio_addr = Param.Addr(0x40000000, "Base PIO address for the SIMD accelerator")
    pio_size = Param.Addr(0x100, "PIO region size")

    # DmaDevice needs the system to get a DMA port
    system = Param.System(Parent.any, "system")

    # Optional modeling params
    numLanes = Param.Unsigned(1, "Number of SIMD lanes (for modeling)")
    computeLatencyPerElem = Param.Tick(1, "Compute latency per element in ticks")
