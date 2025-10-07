#include "dev/coproc/SimpleCoprocessor.hh"

#include <cstring>

#include "base/trace.hh"

#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"

namespace gem5 {

SimpleCoprocessor::SimpleCoprocessor(const SimpleCoprocessorParams &p)
    // v25: BasicPioDevice takes (p, size). Address comes from params.
        : BasicPioDevice(p, p.pio_size),
            DmaDevice(reinterpret_cast<const DmaDevice::Params&>(p)),
      // Disambiguate name() due to MI
      evReadADone([this]{ onReadADone(); }, BasicPioDevice::name()+".readA"),
      evReadBDone([this]{ onReadBDone(); }, BasicPioDevice::name()+".readB"),
      evWriteDone([this]{ onWriteDone(); }, BasicPioDevice::name()+".write")
{ }

void SimpleCoprocessor::init()
{
    BasicPioDevice::init();
}

// MMIO reads
Tick SimpleCoprocessor::read(PacketPtr pkt)
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

    // v25 API: pass ByteOrder, not size
    pkt->setUintX(val, ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}

// MMIO writes
Tick SimpleCoprocessor::write(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    // v25 API: pass ByteOrder
    const uint64_t val = pkt->getUintX(ByteOrder::little);

    switch (off) {
      case 0x00: regSrcA = val; break;
      case 0x08: regSrcB = val; break;
      case 0x10: regDst  = val; break;
      case 0x18: regLen  = val; break;
      case 0x20: regCmd  = val; kick(); break;   // start
      default: break;
    }
    pkt->makeResponse();
    return pioDelay;
}

void SimpleCoprocessor::kick()
{
    if (!(regCmd & 0x1) || regLen == 0)
        return;

    regStatus |= 0x1;      // busy
    regCmd &= ~0x1ULL;     // clear start
    idx = 0;
    issueReadA();
}

void SimpleCoprocessor::issueReadA()
{
    if (idx >= regLen) { nextOrDone(); return; }
    const Addr a = regSrcA + idx * 8;

    // v25 DMA API: (Addr, size, Event*, uint8_t*, [opts], delay)
    dmaRead(a, /*size*/8, &evReadADone, bufA.data());
}

void SimpleCoprocessor::onReadADone()
{
    std::memcpy(&tmpA, bufA.data(), 8);
    issueReadB();
}

void SimpleCoprocessor::issueReadB()
{
    const Addr b = regSrcB + idx * 8;
    dmaRead(b, /*size*/8, &evReadBDone, bufB.data());
}

void SimpleCoprocessor::onReadBDone()
{
    std::memcpy(&tmpB, bufB.data(), 8);
    tmpR = tmpA * tmpB;                 // core math
    std::memcpy(bufR.data(), &tmpR, 8);
    issueWrite();
}

void SimpleCoprocessor::issueWrite()
{
    const Addr d = regDst + idx * 8;
    dmaWrite(d, /*size*/8, &evWriteDone, bufR.data());
}

void SimpleCoprocessor::onWriteDone()
{
    idx++;
    if (idx < regLen)
        issueReadA();     // next element
    else
        nextOrDone();
}

void SimpleCoprocessor::nextOrDone()
{
    regStatus &= ~0x1ULL; // not busy
}

AddrRangeList
SimpleCoprocessor::getAddrRanges() const
{
    return BasicPioDevice::getAddrRanges();
}


} // namespace gem5
