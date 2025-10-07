/*
 * SIMD accelerator: element-wise multiply using DMA. Matches the
 * SimpleCoprocessor pattern (BasicPioDevice + DmaDevice, EventFunctionWrapper
 * callbacks, per-element DMA operations).
 */

#include "dev/simd_accel.hh"

#include <cstring>

#include "base/trace.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"

namespace gem5 {

SimdAccel::SimdAccel(const SimdAccelParams &p)
        : BasicPioDevice(p, p.pio_size),
            DmaDevice(reinterpret_cast<const DmaDevice::Params&>(p)),
      evReadADone([this]{ onReadADone(); }, BasicPioDevice::name()+".readA"),
      evReadBDone([this]{ onReadBDone(); }, BasicPioDevice::name()+".readB"),
      evWriteDone([this]{ onWriteDone(); }, BasicPioDevice::name()+".write")
{ }

void
SimdAccel::init()
{
    BasicPioDevice::init();
    DmaDevice::init();
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

    regStatus |= 0x1;      // busy
    regCmd &= ~0x1ULL;     // clear start
    idx = 0;
    issueReadA();
}

void
SimdAccel::issueReadA()
{
    if (idx >= regLen) { nextOrDone(); return; }
    const Addr a = regSrcA + idx * 8;
    dmaRead(a, /*size*/8, &evReadADone, bufA.data());
}

void
SimdAccel::onReadADone()
{
    std::memcpy(&tmpA, bufA.data(), 8);
    issueReadB();
}

void
SimdAccel::issueReadB()
{
    const Addr b = regSrcB + idx * 8;
    dmaRead(b, /*size*/8, &evReadBDone, bufB.data());
}

void
SimdAccel::onReadBDone()
{
    std::memcpy(&tmpB, bufB.data(), 8);
    tmpR = tmpA * tmpB;                 // core math
    std::memcpy(bufR.data(), &tmpR, 8);
    issueWrite();
}

void
SimdAccel::issueWrite()
{
    const Addr d = regDst + idx * 8;
    dmaWrite(d, /*size*/8, &evWriteDone, bufR.data());
}

void
SimdAccel::onWriteDone()
{
    idx++;
    if (idx < regLen)
        issueReadA();     // next element
    else
        nextOrDone();
}

void
SimdAccel::nextOrDone()
{
    regStatus &= ~0x1ULL; // not busy
}

AddrRangeList
SimdAccel::getAddrRanges() const
{
    // Let BasicPioDevice compute the ranges from pioAddr/pioSize.
    // This is the v25 style: BasicPioDevice provides the implementation
    // but we must declare the override here to satisfy the vtable.
    return BasicPioDevice::getAddrRanges();
}

SimdAccel*
SimdAccelParams::create() const
{
    return new SimdAccel(*this);
}

} // namespace gem5
