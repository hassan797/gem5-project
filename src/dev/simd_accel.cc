/*
 * SIMD accelerator: DMA-based coprocessor for array operations.
 * Supports:
 *   - Element-wise multiplication (opType=0)
 *   - GEMM matrix multipl    std::memcpy(&tm    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    tmpR[0] = tmpA[0] * tmpB[0];
    std::memcpy(bufR.data(), &tmpR[0], sizeof(uint64_t));
    
    DPRINTF(SimdAccel, "onReadBDone: B[%d]=%lu, result=%lu\n", idx, tmpB[0], tmpR[0]);
    issueWrite();, bufA.data(), sizeof(uint64_t));
    DPRINTF(SimdAccel, "onReadADone: A[%d]=%lu\n", idx, tmpA[0]);
    issueReadB();tion (opType=1): C[M×N] = A[M×K] × B[K×N]
 * 
 * Uses DmaVirtDevice for virtual address translation and asynchronous DMA.
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
    : BasicPioDevice(p, p.pio_size),
      DmaDevice(p),
      numLanes(p.num_lanes),
      computeLatency(p.compute_latency)
{
    // Resize buffers for SIMD lanes
    tmpA.resize(numLanes);
    tmpB.resize(numLanes);
    tmpR.resize(numLanes);
    bufA.resize(numLanes * sizeof(uint64_t));
    bufB.resize(numLanes * sizeof(uint64_t));
    bufR.resize(numLanes * sizeof(uint64_t));
    
    DPRINTF(SimdAccel, "SimdAccel created with %d SIMD lanes, compute latency = %lu ticks\n",
            numLanes, computeLatency);
}

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

    regStatus |= 0x1;      // Set busy flag
    regCmd &= ~0x1ULL;     // Clear start bit
    
    // Dispatch based on operation type
    if (regOpType == 0) {
        // Element-wise multiply operation
        if (regLen == 0) {
            DPRINTF(SimdAccel, "KICK: Invalid length 0 for element-wise operation\n");
            regStatus &= ~0x1ULL;
            return;
        }
        DPRINTF(SimdAccel, "KICK: Starting element-wise multiply with regLen=%d\n", regLen);
        idx = 0;
        issueReadA();
        
    } else if (regOpType == 1) {
        // GEMM operation: C[M×N] = A[M×K] × B[K×N]
        gemmM = regLen;  // M dimension stored in regLen register
        gemmK = regDimK; // K dimension from regDimK register
        gemmN = regDimN; // N dimension from regDimN register
        
        // Validate dimensions
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

// ========== Element-wise Multiply Implementation ==========

void
SimdAccel::issueReadA()
{
    DPRINTF(SimdAccel, "issueReadA: idx=%d, regLen=%d\n", idx, regLen);
    if (idx >= regLen) {
        DPRINTF(SimdAccel, "issueReadA: idx >= regLen, calling nextOrDone\n");
        nextOrDone();
        return;
    }
    const Addr a = regSrcA + idx * sizeof(uint64_t);
    DPRINTF(SimdAccel, "issueReadA: reading A[%d] from addr 0x%lx\n", idx, a);
    auto *cb = new EventFunctionWrapper([this]() { onReadADone(); }, name());
    dmaRead(a, sizeof(uint64_t), cb, bufA.data());
}

void
SimdAccel::onReadADone()
{
    std::memcpy(&tmpA[0], bufA.data(), sizeof(uint64_t));
    DPRINTF(SimdAccel, "onReadADone: A[%d]=%lu\n", idx, tmpA[0]);
    issueReadB();
}

void
SimdAccel::issueReadB()
{
    const Addr b = regSrcB + idx * sizeof(uint64_t);
    auto *cb = new EventFunctionWrapper([this]() { onReadBDone(); }, name());
    dmaRead(b, sizeof(uint64_t), cb, bufB.data());
}

void
SimdAccel::onReadBDone()
{
    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    tmpR[0] = tmpA[0] * tmpB[0];  // Element-wise multiplication
    std::memcpy(bufR.data(), &tmpR[0], sizeof(uint64_t));
    DPRINTF(SimdAccel, "onReadBDone: B[%d]=%lu, result=%lu\n", idx, tmpB[0], tmpR[0]);
    issueWrite();
}

void
SimdAccel::issueWrite()
{
    const Addr d = regDst + idx * sizeof(uint64_t);
    DPRINTF(SimdAccel, "issueWrite: writing C[%d]=%lu to address 0x%lx\n", idx, tmpR[0], d);
    auto *cb = new EventFunctionWrapper([this]() { onWriteDone(); }, name());
    dmaWrite(d, sizeof(uint64_t), cb, bufR.data());
}

void
SimdAccel::onWriteDone()
{
    DPRINTF(SimdAccel, "onWriteDone: completed element %d\n", idx);
    idx++;
    
    if (idx < regLen) {
        DPRINTF(SimdAccel, "onWriteDone: continuing to next element\n");
        issueReadA();
    } else {
        DPRINTF(SimdAccel, "onWriteDone: all elements done, finishing\n");
        nextOrDone();
    }
}

void
SimdAccel::nextOrDone()
{
    DPRINTF(SimdAccel, "nextOrDone: operation complete, clearing busy flag\n");
    regStatus &= ~0x1ULL;  // Clear busy flag
}

AddrRangeList
SimdAccel::getAddrRanges() const
{
    // BasicPioDevice handles the address range registration
    return BasicPioDevice::getAddrRanges();
}

// ========== GEMM Implementation: C[M×N] = A[M×K] × B[K×N] ==========
//
// Computes matrix multiplication using a triple-nested loop:
//   for i in 0..M-1:
//     for j in 0..N-1:
//       for k in 0..K-1:
//         C[i][j] += A[i][k] * B[k][j]
//
// State machine processes one element C[i][j] at a time:
//   1. Read A[i][k] for all k (inner loop)
//   2. Read B[k][j] for same k
//   3. Accumulate: gemmAccum += A[i][k] * B[k][j]
//   4. When k loop completes, write gemmAccum to C[i][j]
//   5. Move to next (i,j) position

void
SimdAccel::kickGemm()
{
    // Initialize state for C[0][0]
    gemmI = 0;       // Row index
    gemmJ = 0;       // Column index
    gemmKIdx = 0;    // Accumulation index
    gemmAccum = 0;   // Accumulator for dot product
    
    DPRINTF(SimdAccel, "kickGemm: Starting C[%d][%d] computation\n", gemmI, gemmJ);
    gemmReadA();
}

void
SimdAccel::gemmReadA()
{
    // Read A[i][k] where i=gemmI, k=gemmKIdx
    // A is row-major: offset = (row * num_cols + col) * sizeof(element)
    const Addr addr = regSrcA + (gemmI * gemmK + gemmKIdx) * sizeof(uint64_t);
    
    DPRINTF(SimdAccel, "gemmReadA: Reading A[%d][%d] from addr 0x%lx\n", 
            gemmI, gemmKIdx, addr);
    
    auto *cb = new EventFunctionWrapper([this]() { gemmOnReadADone(); }, name());
    dmaRead(addr, sizeof(uint64_t), cb, bufA.data());
}

void
SimdAccel::gemmOnReadADone()
{
    std::memcpy(&tmpA[0], bufA.data(), sizeof(uint64_t));
    DPRINTF(SimdAccel, "gemmOnReadADone: A[%d][%d]=%lu\n", gemmI, gemmKIdx, tmpA[0]);
    gemmReadB();
}

void
SimdAccel::gemmReadB()
{
    // Read B[k][j] where k=gemmKIdx, j=gemmJ
    // B is row-major: offset = (row * num_cols + col) * sizeof(element)
    const Addr addr = regSrcB + (gemmKIdx * gemmN + gemmJ) * sizeof(uint64_t);
    
    DPRINTF(SimdAccel, "gemmReadB: Reading B[%d][%d] from addr 0x%lx\n", 
            gemmKIdx, gemmJ, addr);
    
    auto *cb = new EventFunctionWrapper([this]() { gemmOnReadBDone(); }, name());
    dmaRead(addr, sizeof(uint64_t), cb, bufB.data());
}

void
SimdAccel::gemmOnReadBDone()
{
    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    DPRINTF(SimdAccel, "gemmOnReadBDone: B[%d][%d]=%lu\n", gemmKIdx, gemmJ, tmpB[0]);
    
    // Multiply-accumulate: gemmAccum += A[i][k] * B[k][j]
    gemmAccum += tmpA[0] * tmpB[0];
    
    DPRINTF(SimdAccel, "gemmOnReadBDone: accum=%lu after A*B=%lu*%lu\n", 
            gemmAccum, tmpA[0], tmpB[0]);
    
    // Move to next k
    gemmKIdx++;
    
    if (gemmKIdx < gemmK) {
        // Continue k-loop: still more elements in dot product for C[i][j]
        gemmReadA();
    } else {
        // Finished k-loop: dot product for C[i][j] is complete
        // Prepare result for writing
        tmpR[0] = gemmAccum;
        std::memcpy(bufR.data(), &tmpR[0], sizeof(uint64_t));
        
        DPRINTF(SimdAccel, "gemmOnReadBDone: Completed C[%d][%d]=%lu, writing\n", 
                gemmI, gemmJ, tmpR[0]);
        
        gemmWriteC();
    }
}

void
SimdAccel::gemmWriteC()
{
    // Write final result C[i][j] back to memory
    // C is row-major: offset = (row * num_cols + col) * sizeof(element)
    const Addr addr = regDst + (gemmI * gemmN + gemmJ) * sizeof(uint64_t);
    
    DPRINTF(SimdAccel, "gemmWriteC: Writing C[%d][%d]=%lu to addr 0x%lx\n", 
            gemmI, gemmJ, tmpR[0], addr);
    
    auto *cb = new EventFunctionWrapper([this]() { gemmOnWriteDone(); }, name());
    dmaWrite(addr, sizeof(uint64_t), cb, bufR.data());
}

void
SimdAccel::gemmOnWriteDone()
{
    DPRINTF(SimdAccel, "gemmOnWriteDone: Finished writing C[%d][%d]\n", gemmI, gemmJ);
    
    // Nested loop iteration: advance j, then i
    // for (i = 0; i < M; i++)
    //     for (j = 0; j < N; j++)
    //         compute C[i][j]
    
    gemmJ++;
    if (gemmJ < gemmN) {
        // Move to next column in same row
        gemmKIdx = 0;
        gemmAccum = 0;
        DPRINTF(SimdAccel, "gemmOnWriteDone: Moving to C[%d][%d]\n", gemmI, gemmJ);
        gemmReadA();
    } else {
        // Finished current row, move to next row
        gemmI++;
        gemmJ = 0;
        
        if (gemmI < gemmM) {
            // Start next row
            gemmKIdx = 0;
            gemmAccum = 0;
            DPRINTF(SimdAccel, "gemmOnWriteDone: Moving to C[%d][%d]\n", gemmI, gemmJ);
            gemmReadA();
        } else {
            // All M×N output elements computed - GEMM complete!
            DPRINTF(SimdAccel, "gemmOnWriteDone: GEMM complete! Processed %dx%d matrix\n", 
                    gemmM, gemmN);
            nextOrDone();
        }
    }
}


} // namespace gem5
