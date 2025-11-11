/*
 * SIMD accelerator: DMA-based coprocessor for array operations.
 * Supports:
 *   - Element-wise multiplication (opType=0)
 *   - GEMM matrix multipl    std::memcpy(&tm    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    tmpR[0] = tmpA[0] * tmpB[0];
    std::memcpy(bufR.data(), &tmpR[0], sizeof(uint64_t));
    
    DPRvoid
SimdAccel::gemmWriteC()
{
    // Write final result C[i][j] back to memory
    // C is row-major: offset = (row * num_cols + col) * sizeof(element)
    const Addr addr = regDst + (gemmI * gemmN + gemmJ) * sizeof(uint64_t);
    
    DPRINTF(SimdAccel, "gemmWriteC: Writing C[%d][%d]=%lu to addr 0x%lx\n", 
            gemmI, gemmJ, tmpR[0], addr);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnWriteDone(); });
    dmaWriteVirt(addr, sizeof(uint64_t), cb, bufR.data());
}l, "onReadBDone: B[%d]=%lu, result=%lu\n", idx, tmpB[0], tmpR[0]);
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
    : DmaVirtDevice(p),
      pioAddr(p.pio_addr),
      pioSize(p.pio_size),
      pioDelay(p.pio_latency),
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

    // Always print MMIO writes to verify instructions are reaching device
    std::cout << "[SimdAccel] MMIO Write: offset=0x" << std::hex << off 
              << " value=0x" << val << std::dec << std::endl;

    switch (off) {
      case 0x00: 
        regSrcA = val; 
        std::cout << "[SimdAccel] Set regSrcA = 0x" << std::hex << val << std::dec << std::endl;
        break;
      case 0x08: 
        regSrcB = val; 
        std::cout << "[SimdAccel] Set regSrcB = 0x" << std::hex << val << std::dec << std::endl;
        break;
      case 0x10: 
        regDst  = val; 
        std::cout << "[SimdAccel] Set regDst = 0x" << std::hex << val << std::dec << std::endl;
        break;
      case 0x18: 
        regLen  = val; 
        std::cout << "[SimdAccel] Set regLen = " << val << std::endl;
        break;
      case 0x20: 
        regCmd  = val; 
        std::cout << "[SimdAccel] Set regCmd, calling kick()..." << std::endl;
        kick(); 
        break;
      case 0x30: 
        regOpType = val; 
        std::cout << "[SimdAccel] Set regOpType = " << val << std::endl;
        break;
      case 0x38: 
        regDimK = val; 
        std::cout << "[SimdAccel] Set regDimK = " << val << std::endl;
        break;
      case 0x40: 
        regDimN = val; 
        std::cout << "[SimdAccel] Set regDimN = " << val << std::endl;
        break;
      default: 
        std::cout << "[SimdAccel] Unknown offset 0x" << std::hex << off << std::dec << std::endl;
        break;
    }
    pkt->makeResponse();
    return pioDelay;
}

void
SimdAccel::kick()
{
    std::cout << "[SimdAccel] kick() called! regCmd=0x" << std::hex << regCmd << std::dec << std::endl;
    
    if (!(regCmd & 0x1))
        return;

    regStatus |= 0x1;      // Set busy flag
    regCmd &= ~0x1ULL;     // Clear start bit
    
    std::cout << "[SimdAccel] Starting operation: opType=" << regOpType 
              << " len=" << regLen << std::endl;
    
    // Dispatch based on operation type
    if (regOpType == 0) {
        // Element-wise multiply operation
        if (regLen == 0) {
            DPRINTF(SimdAccel, "KICK: Invalid length 0 for element-wise operation\n");
            std::cout << "[SimdAccel] ERROR: Invalid length 0" << std::endl;
            regStatus &= ~0x1ULL;
            return;
        }
        DPRINTF(SimdAccel, "KICK: Starting element-wise multiply with regLen=%d\n", regLen);
        std::cout << "[SimdAccel] Starting element-wise multiply, len=" << regLen << std::endl;
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
    
    // Determine batch size: process up to numLanes elements at once
    uint64_t remaining = regLen - idx;
    uint64_t batchSize = (remaining < numLanes) ? remaining : numLanes;
    
    const Addr a = regSrcA + idx * sizeof(uint64_t);
    std::cout << "[SimdAccel] BATCHED READ A: idx=" << idx << ", batchSize=" << batchSize 
              << ", addr=0x" << std::hex << a << std::dec << std::endl;
    DPRINTF(SimdAccel, "issueReadA: reading %d elements starting at A[%d] from addr 0x%lx\n", 
            batchSize, idx, a);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this, batchSize](const uint64_t &) { onReadADone(batchSize); });
    dmaReadVirt(a, batchSize * sizeof(uint64_t), cb, bufA.data());
}

void
SimdAccel::onReadADone(uint64_t batchSize)
{
    // Copy all elements from DMA buffer
    for (uint64_t i = 0; i < batchSize; i++) {
        std::memcpy(&tmpA[i], bufA.data() + i * sizeof(uint64_t), sizeof(uint64_t));
        DPRINTF(SimdAccel, "onReadADone: A[%d]=%lu\n", idx + i, tmpA[i]);
    }
    issueReadB(batchSize);
}

void
SimdAccel::issueReadB(uint64_t batchSize)
{
    const Addr b = regSrcB + idx * sizeof(uint64_t);
    DPRINTF(SimdAccel, "issueReadB: reading %d elements starting at B[%d] from addr 0x%lx\n",
            batchSize, idx, b);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this, batchSize](const uint64_t &) { onReadBDone(batchSize); });
    dmaReadVirt(b, batchSize * sizeof(uint64_t), cb, bufB.data());
}

void
SimdAccel::onReadBDone(uint64_t batchSize)
{
    std::cout << "[SimdAccel] BATCHED COMPUTE: Processing " << batchSize 
              << " elements in parallel (SIMD lanes)" << std::endl;
    
    // Copy all elements from DMA buffer and compute in parallel
    for (uint64_t i = 0; i < batchSize; i++) {
        std::memcpy(&tmpB[i], bufB.data() + i * sizeof(uint64_t), sizeof(uint64_t));
        // Perform SIMD multiply - all lanes compute in parallel
        tmpR[i] = tmpA[i] * tmpB[i];
        std::memcpy(bufR.data() + i * sizeof(uint64_t), &tmpR[i], sizeof(uint64_t));
        DPRINTF(SimdAccel, "onReadBDone: B[%d]=%lu, result=%lu\n", idx + i, tmpB[i], tmpR[i]);
    }
    
    DPRINTF(SimdAccel, "onReadBDone: computed %d elements in parallel, scheduling write after %lu ticks\n", 
            batchSize, computeLatency);
    
    // KEY: All batchSize elements (up to 4) are computed in parallel
    // So the latency is the SAME whether we process 1 or 4 elements!
    schedule(new EventFunctionWrapper([this, batchSize]() { issueWrite(batchSize); }, name()), 
             curTick() + computeLatency);
}

void
SimdAccel::issueWrite(uint64_t batchSize)
{
    const Addr d = regDst + idx * sizeof(uint64_t);
    DPRINTF(SimdAccel, "issueWrite: writing %d elements starting at C[%d] to address 0x%lx\n", 
            batchSize, idx, d);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this, batchSize](const uint64_t &) { onWriteDone(batchSize); });
    dmaWriteVirt(d, batchSize * sizeof(uint64_t), cb, bufR.data());
}

void
SimdAccel::onWriteDone(uint64_t batchSize)
{
    DPRINTF(SimdAccel, "onWriteDone: completed %d elements [%d-%d]\n", 
            batchSize, idx, idx + batchSize - 1);
    
    // Advance index by the number of elements we just processed
    idx += batchSize;
    
    if (idx < regLen) {
        DPRINTF(SimdAccel, "onWriteDone: continuing with next batch\n");
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
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnReadADone(); });
    dmaReadVirt(addr, sizeof(uint64_t), cb, bufA.data());
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
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { gemmOnReadBDone(); });
    dmaReadVirt(addr, sizeof(uint64_t), cb, bufB.data());
}

void
SimdAccel::gemmOnReadBDone()
{
    std::memcpy(&tmpB[0], bufB.data(), sizeof(uint64_t));
    DPRINTF(SimdAccel, "gemmOnReadBDone: B[%d][%d]=%lu\n", gemmKIdx, gemmJ, tmpB[0]);
    
    // Multiply-accumulate: gemmAccum += A[i][k] * B[k][j]
    // This computation takes computeLatency ticks (same for 1 or numLanes elements)
    gemmAccum += tmpA[0] * tmpB[0];
    
    DPRINTF(SimdAccel, "gemmOnReadBDone: accum=%lu after A*B=%lu*%lu, delaying %lu ticks for compute\n", 
            gemmAccum, tmpA[0], tmpB[0], computeLatency);
    
    // Schedule next step after compute latency
    schedule(new EventFunctionWrapper([this]() { gemmComputeDone(); }, name()), 
             curTick() + computeLatency);
}

void
SimdAccel::gemmComputeDone()
{
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
