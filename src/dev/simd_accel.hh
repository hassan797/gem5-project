#ifndef __DEV_SIMD_ACCEL_HH__
#define __DEV_SIMD_ACCEL_HH__

#include <array>
#include <vector>

#include "base/types.hh"
#include "dev/dma_virt_device.hh"
#include "params/SimdAccel.hh"
#include "sim/eventq.hh"

namespace gem5 {

class SimdAccel : public DmaVirtDevice
{
  public:
    SimdAccel(const SimdAccelParams &p);

    void init() override;
    Tick read(PacketPtr pkt) override;   // MMIO
    Tick write(PacketPtr pkt) override;  // MMIO
    AddrRangeList getAddrRanges() const override;
    TranslationGenPtr translate(Addr vaddr, Addr size) override;

  private:

    // PIO registers
    Addr pioAddr;
    Addr pioSize;
    Tick pioDelay;

    // SIMD configuration
    unsigned numLanes;
    Tick computeLatency;

    // MMIO regs (64-bit)
    Addr     regSrcA   = 0;
    Addr     regSrcB   = 0;
    Addr     regDst    = 0;
    uint64_t regLen    = 0;   // elements (64-bit each) OR M dimension for GEMM
    uint64_t regCmd    = 0;   // bit0=start
    uint64_t regStatus = 0;   // bit0=busy
    uint64_t regOpType = 0;   // 0=elemwise_mult, 1=GEMM
    uint64_t regDimK   = 0;   // K dimension for GEMM (shared dimension)
    uint64_t regDimN   = 0;   // N dimension for GEMM (columns of B/C)

    // Op state - SIMD processing
    uint64_t idx = 0;
    std::vector<uint64_t> tmpA;  // Array for SIMD lanes
    std::vector<uint64_t> tmpB;  // Array for SIMD lanes
    std::vector<uint64_t> tmpR;  // Array for SIMD lanes
    
    // GEMM state (when opType=1)
    uint64_t gemmM = 0, gemmK = 0, gemmN = 0;  // Matrix dimensions
    uint64_t gemmI = 0, gemmJ = 0;              // Current output position (i,j)
    uint64_t gemmKIdx = 0;                      // Current K accumulation index
    uint64_t gemmAccum = 0;                     // Accumulator for dot product

    // DMA buffers (for multiple elements)
    std::vector<uint8_t> bufA;  // Sized to numLanes * sizeof(uint64_t)
    std::vector<uint8_t> bufB;  // Sized to numLanes * sizeof(uint64_t)
    std::vector<uint8_t> bufR;  // Sized to numLanes * sizeof(uint64_t)

    // Helpers - Element-wise operations (SIMD with batching)
    void kick();                        // start op when CMD.start is written
    void issueReadA();                  // Read batch of A elements
    void onReadADone(uint64_t batchSize);
    void issueReadB(uint64_t batchSize);
    void onReadBDone(uint64_t batchSize);
    void issueWrite(uint64_t batchSize);
    void onWriteDone(uint64_t batchSize);
    void nextOrDone();
    
    // Helpers - GEMM operation (C = A × B)
    void kickGemm();        // start GEMM operation
    void gemmReadA();       // Read A[i][k]
    void gemmOnReadADone();
    void gemmReadB();       // Read B[k][j]
    void gemmOnReadBDone();
    void gemmComputeDone(); // After compute latency
    void gemmWriteC();      // Write C[i][j]
    void gemmOnWriteDone();
};

} // namespace gem5

#endif // __DEV_SIMD_ACCEL_HH__
