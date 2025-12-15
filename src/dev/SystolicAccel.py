from m5.params import *
from m5.proxy import *
from m5.objects.Device import DmaVirtDevice

class SystolicAccel(DmaVirtDevice):
    type = 'SystolicAccel'
    cxx_header = 'dev/systolic_accel.hh'
    cxx_class = 'gem5::SystolicAccel'

    pio_addr = Param.Addr("MMIO base address for control registers")
    pio_size = Param.Addr(0x1000, "MMIO size (4KB default)")
    pio_latency = Param.Latency('1ns', "PIO access latency")
    
    array_rows = Param.Unsigned(4, "Number of rows in PE array")
    array_cols = Param.Unsigned(4, "Number of columns in PE array")
    mac_latency = Param.Latency('10ns', "Latency for parallel MAC operation across all PEs")
    k_tile_batch = Param.Unsigned(16, "Number of K-tiles to load per DMA (reduces overhead)")
