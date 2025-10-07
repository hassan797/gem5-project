#ifndef __DEV_COPROC_SIMPLE_COPROCESSOR_HH__
#define __DEV_COPROC_SIMPLE_COPROCESSOR_HH__

#include <array>

#include "base/types.hh"
#include "dev/dma_device.hh"
#include "dev/io_device.hh"
#include "params/SimpleCoprocessor.hh"
#include "sim/eventq.hh"

namespace gem5 {

class SimpleCoprocessor : public BasicPioDevice, public DmaDevice
{
  public:
    SimpleCoprocessor(const SimpleCoprocessorParams &p);

    AddrRangeList getAddrRanges() const override;

    void init() override;
    Tick read(PacketPtr pkt) override;   // MMIO
    Tick write(PacketPtr pkt) override;  // MMIO

    // Do NOT override getAddrRanges() in v25; BasicPioDevice handles it.

  private:
    // MMIO regs (64-bit)
    Addr     regSrcA   = 0;
    Addr     regSrcB   = 0;
    Addr     regDst    = 0;
    uint64_t regLen    = 0;   // elements (64-bit each)
    uint64_t regCmd    = 0;   // bit0=start
    uint64_t regStatus = 0;   // bit0=busy

    // Op state
    uint64_t idx = 0;
    uint64_t tmpA = 0, tmpB = 0, tmpR = 0;

    // DMA buffers
    std::array<uint8_t, 8> bufA{};
    std::array<uint8_t, 8> bufB{};
    std::array<uint8_t, 8> bufR{};

    // Events (DMA completions)
    EventFunctionWrapper evReadADone;
    EventFunctionWrapper evReadBDone;
    EventFunctionWrapper evWriteDone;

    // Helpers
    void kick();            // start op when CMD.start is written
    void issueReadA();
    void onReadADone();
    void issueReadB();
    void onReadBDone();
    void issueWrite();
    void onWriteDone();
    void nextOrDone();
};

} // namespace gem5
#endif
