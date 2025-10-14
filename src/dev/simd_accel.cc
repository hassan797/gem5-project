/*
 * SIMD accelerator: element-wise multiply using DMA. Matches the
 * SimpleCoprocessor pattern (BasicPioDevice + DmaDevice, EventFunctionWrapper
 * callbacks, per-element DMA operations).
 */

#include "dev/simd_accel.hh"

#include <cstring>

#include "base/trace.hh"
#include "debug/SimdAccel.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/se_translating_port_proxy.hh"
#include "sim/process.hh"
#include "sim/system.hh"

namespace gem5 {

SimdAccel::SimdAccel(const SimdAccelParams &p)
        : DmaVirtDevice(p),
      pioAddr(p.pio_addr),
      pioSize(p.pio_size),
      pioDelay(p.pio_latency)
{ }

void
SimdAccel::init()
{
    DmaVirtDevice::init();
}

Tick
SimdAccel::read(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    uint64_t val = 0;
    switch (off) {
      case 0x00: val = regSrcA; break;
      case 0x08: val = regSrcB; break;
      case 0x10: val = regDst;  break;
      case 0x18: val = regLen;  break;
      case 0x20: val = regCmd;  break;
      case 0x28: val = regStatus; break;
      default:   val = 0; break;
    }

    pkt->setUintX(val, ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}

Tick
SimdAccel::write(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    const uint64_t val = pkt->getUintX(ByteOrder::little);

    switch (off) {
      case 0x00: regSrcA = val; break;
      case 0x08: regSrcB = val; break;
      case 0x10: regDst  = val; break;
      case 0x18: regLen  = val; break;
      case 0x20: regCmd  = val; kick(); break;
      default: break;
    }
    pkt->makeResponse();
    return pioDelay;
}

void
SimdAccel::kick()
{
    if (!(regCmd & 0x1) || regLen == 0)
        return;

    DPRINTF(SimdAccel, "KICK: Starting operation with regLen=%d\n", regLen);
    regStatus |= 0x1;      // busy
    regCmd &= ~0x1ULL;     // clear start
    idx = 0;
    issueReadA();
}

void
SimdAccel::issueReadA()
{
    DPRINTF(SimdAccel, "issueReadA: idx=%d, regLen=%d\n", idx, regLen);
    if (idx >= regLen) {
        DPRINTF(SimdAccel, "issueReadA: idx >= regLen, calling nextOrDone\n");
        nextOrDone();
        return;
    }
    const Addr a = regSrcA + idx * 8;
    DPRINTF(SimdAccel, "issueReadA: reading A[%d] from addr 0x%x\n", idx, a);
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onReadADone(); });
    dmaReadVirt(a, /*size*/8, cb, bufA.data());
}

void
SimdAccel::onReadADone()
{
    std::memcpy(&tmpA, bufA.data(), 8);
    DPRINTF(SimdAccel, "onReadADone: A[%d]=%d\n", idx, tmpA);
    issueReadB();
}

void
SimdAccel::issueReadB()
{
    const Addr b = regSrcB + idx * 8;
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onReadBDone(); });
    dmaReadVirt(b, /*size*/8, cb, bufB.data());
}

void
SimdAccel::onReadBDone()
{
    std::memcpy(&tmpB, bufB.data(), 8);
    tmpR = tmpA * tmpB;                 // core math
    std::memcpy(bufR.data(), &tmpR, 8);
    DPRINTF(SimdAccel, "onReadBDone: B[%d]=%d, result=%d\n", idx, tmpB, tmpR);
    issueWrite();
}

void
SimdAccel::issueWrite()
{
    const Addr d = regDst + idx * 8;
    DPRINTF(SimdAccel, "issueWrite: writing C[%d]=%d to address 0x%x\n", idx, tmpR, d);
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onWriteDone(); });
    dmaWriteVirt(d, /*size*/8, cb, bufR.data());
}

void
SimdAccel::onWriteDone()
{
    DPRINTF(SimdAccel, "onWriteDone: idx=%d before increment, regLen=%d\n", idx, regLen);
    idx++;
    DPRINTF(SimdAccel, "onWriteDone: idx=%d after increment\n", idx);
    if (idx < regLen) {
        DPRINTF(SimdAccel, "onWriteDone: continuing to next element\n");
        issueReadA();     // next element
    } else {
        DPRINTF(SimdAccel, "onWriteDone: all elements done, finishing\n");
        nextOrDone();
    }
}

void
SimdAccel::nextOrDone()
{
    DPRINTF(SimdAccel, "nextOrDone: accelerator finished, clearing busy flag\n");
    regStatus &= ~0x1ULL; // not busy
}

AddrRangeList
SimdAccel::getAddrRanges() const
{
    // Return the address range for this PIO device
    AddrRangeList ranges;
    ranges.push_back(AddrRange(pioAddr, pioAddr + pioSize));
    return ranges;
}

TranslationGenPtr
SimdAccel::translate(Addr vaddr, Addr size)
{
    // For SE mode, use the process page table to translate virtual addresses
    // This allows DMA to work with the process's virtual address space
    auto process = sys->threads[0]->getProcessPtr();
    return process->pTable->translateRange(vaddr, size);
}


} // namespace gem5
