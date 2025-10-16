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
      case 0x30: regOpType = val; break;  // Operation type
      case 0x38: regDimK = val; break;    // K dimension for GEMM
      case 0x40: regDimN = val; break;    // N dimension for GEMM
      default: break;
    }
    pkt->makeResponse();
    return pioDelay;
}

void
SimdAccel::kick()
{
    if (!(regCmd & 0x1))
        return;

    regStatus |= 0x1;      // busy
    regCmd &= ~0x1ULL;     // clear start
    
    // Dispatch based on operation type
    if (regOpType == 0) {
        // Element-wise multiply
        if (regLen == 0) return;
        DPRINTF(SimdAccel, "KICK: Starting element-wise multiply with regLen=%d\n", regLen);
        idx = 0;
        issueReadA();
    } else if (regOpType == 1) {
        // GEMM: C[M×N] = A[M×K] × B[K×N]
        gemmM = regLen;  // M stored in regLen
        gemmK = regDimK;
        gemmN = regDimN;
        
        if (gemmM == 0 || gemmK == 0 || gemmN == 0) {
            DPRINTF(SimdAccel, "KICK: Invalid GEMM dimensions M=%d K=%d N=%d\n", 
                    gemmM, gemmK, gemmN);
            regStatus &= ~0x1ULL;
            return;
        }
        
        DPRINTF(SimdAccel, "KICK: Starting GEMM M=%d K=%d N=%d\n", gemmM, gemmK, gemmN);
        kickGemm();
    } else {
        DPRINTF(SimdAccel, "KICK: Unknown operation type %d\n", regOpType);
        regStatus &= ~0x1ULL;
    }
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

// ========== GEMM Implementation: C[M×N] = A[M×K] × B[K×N] ==========

void
SimdAccel::kickGemm()
{
    // Initialize GEMM state
    gemmI = 0;
    gemmJ = 0;
    gemmKIdx = 0;
    gemmAccum = 0;
    
    DPRINTF(SimdAccel, "kickGemm: Starting C[%d][%d] computation\n", gemmI, gemmJ);
    
    // Start first dot product: C[0][0] = sum(A[0][k] * B[k][0])
    gemmReadA();
}

void
SimdAccel::gemmReadA()
{
    // Read A[i][k] where i=gemmI, k=gemmKIdx
    // A is stored in row-major: A[i][k] is at offset (i * gemmK + k) * 8 bytes
    const Addr addr = regSrcA + (gemmI * gemmK + gemmKIdx) * 8;
    
    DPRINTF(SimdAccel, "gemmReadA: Reading A[%d][%d] from addr 0x%x\n", 
            gemmI, gemmKIdx, addr);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnReadADone(); });
    dmaReadVirt(addr, 8, cb, bufA.data());
}

void
SimdAccel::gemmOnReadADone()
{
    std::memcpy(&tmpA, bufA.data(), 8);
    DPRINTF(SimdAccel, "gemmOnReadADone: A[%d][%d]=%d\n", gemmI, gemmKIdx, tmpA);
    gemmReadB();
}

void
SimdAccel::gemmReadB()
{
    // Read B[k][j] where k=gemmKIdx, j=gemmJ
    // B is stored in row-major: B[k][j] is at offset (k * gemmN + j) * 8 bytes
    const Addr addr = regSrcB + (gemmKIdx * gemmN + gemmJ) * 8;
    
    DPRINTF(SimdAccel, "gemmReadB: Reading B[%d][%d] from addr 0x%x\n", 
            gemmKIdx, gemmJ, addr);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnReadBDone(); });
    dmaReadVirt(addr, 8, cb, bufB.data());
}

void
SimdAccel::gemmOnReadBDone()
{
    std::memcpy(&tmpB, bufB.data(), 8);
    DPRINTF(SimdAccel, "gemmOnReadBDone: B[%d][%d]=%d\n", gemmKIdx, gemmJ, tmpB);
    
    // Accumulate: accum += A[i][k] * B[k][j]
    gemmAccum += tmpA * tmpB;
    
    DPRINTF(SimdAccel, "gemmOnReadBDone: accum=%d after A*B=%d*%d\n", 
            gemmAccum, tmpA, tmpB);
    
    // Move to next k
    gemmKIdx++;
    
    if (gemmKIdx < gemmK) {
        // Continue dot product for current C[i][j]
        gemmReadA();
    } else {
        // Finished computing C[i][j], write it out
        tmpR = gemmAccum;
        std::memcpy(bufR.data(), &tmpR, 8);
        
        DPRINTF(SimdAccel, "gemmOnReadBDone: Completed C[%d][%d]=%d, writing\n", 
                gemmI, gemmJ, tmpR);
        
        gemmWriteC();
    }
}

void
SimdAccel::gemmWriteC()
{
    // Write C[i][j]
    // C is stored in row-major: C[i][j] is at offset (i * gemmN + j) * 8 bytes
    const Addr addr = regDst + (gemmI * gemmN + gemmJ) * 8;
    
    DPRINTF(SimdAccel, "gemmWriteC: Writing C[%d][%d]=%d to addr 0x%x\n", 
            gemmI, gemmJ, tmpR, addr);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnWriteDone(); });
    dmaWriteVirt(addr, 8, cb, bufR.data());
}

void
SimdAccel::gemmOnWriteDone()
{
    DPRINTF(SimdAccel, "gemmOnWriteDone: Finished C[%d][%d]\n", gemmI, gemmJ);
    
    // Move to next output element
    gemmJ++;
    if (gemmJ < gemmN) {
        // Next column in same row
        gemmKIdx = 0;
        gemmAccum = 0;
        DPRINTF(SimdAccel, "gemmOnWriteDone: Moving to C[%d][%d]\n", gemmI, gemmJ);
        gemmReadA();
    } else {
        // Move to next row
        gemmI++;
        gemmJ = 0;
        
        if (gemmI < gemmM) {
            // Next row
            gemmKIdx = 0;
            gemmAccum = 0;
            DPRINTF(SimdAccel, "gemmOnWriteDone: Moving to C[%d][%d]\n", gemmI, gemmJ);
            gemmReadA();
        } else {
            // All done!
            DPRINTF(SimdAccel, "gemmOnWriteDone: GEMM complete!\n");
            nextOrDone();
        }
    }
}


} // namespace gem5
