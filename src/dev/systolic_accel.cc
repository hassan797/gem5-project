/*
 * Systolic Array Accelerator Implementation
 */

#include "dev/systolic_accel.hh"

#include <cstring>
#include <algorithm>

#include "base/trace.hh"
#include "debug/SystolicAccel.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/se_translating_port_proxy.hh"
#include "sim/process.hh"
#include "sim/system.hh"

namespace gem5 {

SystolicAccel::SystolicAccel(const SystolicAccelParams &p)
    : DmaVirtDevice(p),
      pioAddr(p.pio_addr),
      pioSize(p.pio_size),
      pioDelay(p.pio_latency),
      arrayRows(p.array_rows),
      arrayCols(p.array_cols),
      macLatency(p.mac_latency),
      regMatA(0), regMatB(0), regMatC(0),
      regDimM(0), regDimK(0), regDimN(0),
      regCmd(0), regStatus(0),
      currentState(Idle),
      tileI(0), tileJ(0), tileK(0),
      rowIdx(0), colIdx(0), kIdx(0)
{
    // Initialize PE array
    peArray.resize(arrayRows);
    for (unsigned i = 0; i < arrayRows; i++) {
        peArray[i].resize(arrayCols);
    }
    
    // Allocate DMA buffers (max tile size)
    bufWeights.resize(arrayRows * arrayCols * sizeof(uint64_t));
    bufARow.resize(arrayRows * sizeof(uint64_t));  // K elements for one row of A
    bufCTile.resize(arrayRows * arrayCols * sizeof(uint64_t));
    
    std::cout << "[SystolicAccel] Created " << arrayRows << "×" << arrayCols 
              << " PE array, MAC latency=" << macLatency << " ticks" << std::endl;
}

void
SystolicAccel::init()
{
    DmaVirtDevice::init();
}

Tick
SystolicAccel::read(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    uint64_t val = 0;
    
    switch (off) {
      case 0x00: val = regMatA; break;
      case 0x08: val = regMatB; break;
      case 0x10: val = regMatC; break;
      case 0x18: val = regDimM; break;
      case 0x20: val = regDimK; break;
      case 0x28: val = regDimN; break;
      case 0x30: val = regCmd; break;
      case 0x38: val = regStatus; break;
      default: val = 0; break;
    }
    
    pkt->setLE<uint64_t>(val);
    pkt->makeResponse();
    return pioDelay;
}

Tick
SystolicAccel::write(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    const uint64_t val = pkt->getLE<uint64_t>();
    
    switch (off) {
      case 0x00: regMatA = val; break;
      case 0x08: regMatB = val; break;
      case 0x10: regMatC = val; break;
      case 0x18: regDimM = val; break;
      case 0x20: regDimK = val; break;
      case 0x28: regDimN = val; break;
      case 0x30:
        regCmd = val;
        if (val & 0x1) {
            kick();
        }
        break;
      case 0x38: regStatus = val; break;
      default: break;
    }
    
    pkt->makeResponse();
    return pioDelay;
}

void
SystolicAccel::kick()
{
    if (currentState != Idle) {
        std::cout << "[SystolicAccel] ERROR: Already busy" << std::endl;
        return;
    }
    
    // Validate dimensions
    if (regDimM == 0 || regDimK == 0 || regDimN == 0) {
        std::cout << "[SystolicAccel] ERROR: Invalid dimensions M=" << regDimM 
                  << " K=" << regDimK << " N=" << regDimN << std::endl;
        return;
    }
    
    std::cout << "[SystolicAccel] Starting GEMM: C[" << regDimM << "×" << regDimN 
              << "] = A[" << regDimM << "×" << regDimK << "] × B[" << regDimK 
              << "×" << regDimN << "]" << std::endl;
    
    regStatus |= 0x1;  // Set busy
    regCmd &= ~0x1ULL; // Clear start bit
    
    // Reset ALL PEs at the start of a new GEMM operation
    std::cout << "[SystolicAccel] Resetting all PEs" << std::endl;
    for (unsigned i = 0; i < arrayRows; i++) {
        for (unsigned j = 0; j < arrayCols; j++) {
            peArray[i][j].reset();
        }
    }
    
    // Initialize tiling
    tileI = 0;
    tileJ = 0;
    tileK = 0;
    
    // Start by loading weights for first tile
    startWeightLoad();
}

void
SystolicAccel::startWeightLoad()
{
    currentState = LoadWeights;
    
    uint64_t kTile = getCurrentKTileSize();
    uint64_t nTile = getCurrentNTileSize();
    
    std::cout << "[SystolicAccel] Loading weights tile (" << tileI << "," << tileJ 
              << "," << tileK << "), size=" << kTile << "×" << nTile << std::endl;
    
    // Load entire B tile: B[kStart:kStart+kTile][nStart:nStart+nTile]
    // This is kTile rows × nTile columns
    // We need to load this row by row since B is row-major
    
    uint64_t kStart = tileK * arrayRows;
    uint64_t nStart = tileJ * arrayCols;
    
    // Load first row of the tile
    Addr addr = regMatB + (kStart * regDimN + nStart) * sizeof(uint64_t);
    uint64_t loadSize = nTile * sizeof(uint64_t);
    
    colIdx = 0;  // Track which k-row of B we've loaded
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onBRowLoaded(); });
    
    currentState = WaitWeightLoad;
    // Load into correct position in bufWeights (row 0 of tile)
    dmaReadVirt(addr, loadSize, cb, bufWeights.data());
}

void
SystolicAccel::onBRowLoaded()
{
    uint64_t kTile = getCurrentKTileSize();
    uint64_t nTile = getCurrentNTileSize();
    
    std::cout << "[SystolicAccel] Loaded B row " << colIdx << " of " << kTile << std::endl;
    
    colIdx++;  // Move to next row of B tile
    
    if (colIdx < kTile) {
        // Load next row of B
        uint64_t kStart = tileK * arrayRows;
        uint64_t nStart = tileJ * arrayCols;
        Addr addr = regMatB + ((kStart + colIdx) * regDimN + nStart) * sizeof(uint64_t);
        uint64_t loadSize = nTile * sizeof(uint64_t);
        
        auto cb = new DmaVirtCallback<uint64_t>(
            [this](const uint64_t &) { onBRowLoaded(); });
        
        // Load into correct position: row colIdx of tile
        // Each row is nTile elements wide
        uint8_t *dest = bufWeights.data() + (colIdx * nTile * sizeof(uint64_t));
        dmaReadVirt(addr, loadSize, cb, dest);
    } else {
        // All B rows loaded
        std::cout << "[SystolicAccel] B tile fully loaded (" << kTile << "×" << nTile << " elements)" << std::endl;
        onWeightLoadDone();
    }
}

void
SystolicAccel::onWeightLoadDone()
{
    uint64_t nTile = getCurrentNTileSize();
    
    // B tile is now fully loaded in bufWeights
    // bufWeights contains kTile × nTile elements in row-major order
    
    // Reset accumulators at start of new output tile
    if (tileK == 0) {
        for (unsigned i = 0; i < arrayRows; i++) {
            for (unsigned j = 0; j < arrayCols; j++) {
                peArray[i][j].reset();
            }
        }
    }
    
    std::cout << "[SystolicAccel] B tile loaded, starting row processing" << std::endl;
    
    // Start streaming A rows
    rowIdx = 0;
    startARowLoad();
}

void
SystolicAccel::startARowLoad()
{
    if (rowIdx >= getCurrentMTileSize()) {
        // Done with this tile, move to next K tile or finish
        tileK++;
        if (tileK * arrayRows < regDimK) {
            // More K tiles to accumulate
            startWeightLoad();
        } else {
            // Finished all K tiles, write results
            startCTileWrite();
        }
        return;
    }
    
    currentState = StreamCompute;
    
    // Load A[tileI*arrayRows + rowIdx][tileK*arrayRows : tileK*arrayRows + kTile]
    uint64_t rowGlobal = tileI * arrayRows + rowIdx;
    uint64_t kStart = tileK * arrayRows;
    uint64_t kTile = getCurrentKTileSize();
    
    // A is M×K, row-major: A[m][k] = A_base + (m*K + k)*8
    Addr addr = regMatA + (rowGlobal * regDimK + kStart) * sizeof(uint64_t);
    uint64_t loadSize = kTile * sizeof(uint64_t);
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onARowLoadDone(); });
    
    currentState = WaitCompute;
    dmaReadVirt(addr, loadSize, cb, bufARow.data());
}

void
SystolicAccel::onARowLoadDone()
{
    // Perform computation: stream A row elements through PE row
    performCompute();
}

void
SystolicAccel::performCompute()
{
    uint64_t kTile = getCurrentKTileSize();
    uint64_t nTile = getCurrentNTileSize();
    
    // For this row of A, compute: C[row][j] += sum_k A[row][k] * B[k][j]
    // bufARow contains: A[row][kStart], A[row][kStart+1], ..., A[row][kStart+kTile-1]
    // bufWeights contains: B tile in row-major order
    //   B[kStart][nStart:nStart+nTile]
    //   B[kStart+1][nStart:nStart+nTile]
    //   ...
    //   B[kStart+kTile-1][nStart:nStart+nTile]
    
    std::cout << "[SystolicAccel] performCompute: row=" << rowIdx 
              << ", kTile=" << kTile << ", nTile=" << nTile << std::endl;
    
    for (unsigned k = 0; k < kTile; k++) {
        uint64_t aValue;
        std::memcpy(&aValue, bufARow.data() + k * sizeof(uint64_t), sizeof(uint64_t));
        
        // For this k, multiply with B[kStart+k][nStart:nStart+nTile]
        // These B values are at bufWeights[k * nTile : (k+1) * nTile]
        for (unsigned j = 0; j < nTile && j < arrayCols; j++) {
            uint64_t bValue;
            std::memcpy(&bValue, bufWeights.data() + (k * nTile + j) * sizeof(uint64_t), sizeof(uint64_t));
            
            if (rowIdx == 0 && j < 4) {
                std::cout << "  [DEBUG] k=" << k << ", A[0][" << k << "]=" << aValue 
                          << ", B[" << k << "][" << j << "]=" << bValue;
                std::cout << ", PE[0][" << j << "].accum_before=" << peArray[0][j].getResult();
            }
            
            // PE[rowIdx][j] computes C[rowIdx][j]
            peArray[rowIdx][j].loadWeight(bValue);
            peArray[rowIdx][j].compute(aValue);
            
            if (rowIdx == 0 && j < 4) {
                std::cout << ", accum_after=" << peArray[0][j].getResult() << std::endl;
            }
        }
    }
    
    // Schedule compute completion (all PEs computed in parallel)
    schedule(new EventFunctionWrapper([this]() { onComputeDone(); }, name()),
             curTick() + macLatency);
}

void
SystolicAccel::onComputeDone()
{
    rowIdx++;
    startARowLoad();  // Process next row or finish
}

void
SystolicAccel::startCTileWrite()
{
    currentState = DrainResults;
    
    uint64_t mTile = getCurrentMTileSize();
    uint64_t nTile = getCurrentNTileSize();
    
    std::cout << "[SystolicAccel] Writing results tile (" << tileI << "," << tileJ 
              << "), size=" << mTile << "×" << nTile << std::endl;
    
    // Collect results from PEs
    for (unsigned i = 0; i < mTile; i++) {
        for (unsigned j = 0; j < nTile; j++) {
            uint64_t result = peArray[i][j].getResult();
            uint64_t idx = i * nTile + j;
            std::memcpy(bufCTile.data() + idx * sizeof(uint64_t), &result, sizeof(uint64_t));
            
            if (i < 2 && j < 4) {
                std::cout << "[WriteTile] PE[" << i << "][" << j << "]=" << result 
                          << " -> bufCTile[" << idx << "]" << std::endl;
            }
        }
    }
    
    // Write C[tileI*arrayRows : tileI*arrayRows+mTile][tileJ*arrayCols : tileJ*arrayCols+nTile]
    uint64_t rowStart = tileI * arrayRows;
    uint64_t colStart = tileJ * arrayCols;
    
    // C is M×N, row-major: C[m][n] = C_base + (m*N + n)*8
    Addr addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t);
    uint64_t writeSize = mTile * nTile * sizeof(uint64_t);
    
    std::cout << "[WriteTile] Writing to addr=0x" << std::hex << addr << std::dec 
              << ", size=" << writeSize << " bytes" << std::endl;
    std::cout << "[WriteTile] First 4 values: bufCTile[0]=" << *((uint64_t*)bufCTile.data())
              << ", [1]=" << *((uint64_t*)(bufCTile.data() + 8))
              << ", [2]=" << *((uint64_t*)(bufCTile.data() + 16))
              << ", [3]=" << *((uint64_t*)(bufCTile.data() + 24)) << std::endl;
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onCTileWriteDone(); });
    
    currentState = WaitDrain;
    dmaWriteVirt(addr, writeSize, cb, bufCTile.data());
}

void
SystolicAccel::onCTileWriteDone()
{
    std::cout << "[WriteTile] DMA write completed for tile (" << tileI << "," << tileJ << ")" << std::endl;
    nextTile();
}

void
SystolicAccel::nextTile()
{
    // Move to next tile in row-major order over M×N
    tileJ++;
    if (tileJ * arrayCols >= regDimN) {
        tileJ = 0;
        tileI++;
        if (tileI * arrayRows >= regDimM) {
            // All tiles done!
            finishOperation();
            return;
        }
    }
    
    // Start next tile
    tileK = 0;
    startWeightLoad();
}

void
SystolicAccel::finishOperation()
{
    std::cout << "[SystolicAccel] GEMM complete" << std::endl;
    currentState = Idle;
    regStatus &= ~0x1ULL;  // Clear busy
}

uint64_t
SystolicAccel::getCurrentMTileSize() const
{
    uint64_t remaining = regDimM - tileI * arrayRows;
    return std::min(remaining, (uint64_t)arrayRows);
}

uint64_t
SystolicAccel::getCurrentNTileSize() const
{
    uint64_t remaining = regDimN - tileJ * arrayCols;
    return std::min(remaining, (uint64_t)arrayCols);
}

uint64_t
SystolicAccel::getCurrentKTileSize() const
{
    uint64_t remaining = regDimK - tileK * arrayRows;
    return std::min(remaining, (uint64_t)arrayRows);
}

AddrRangeList
SystolicAccel::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(AddrRange(pioAddr, pioAddr + pioSize));
    return ranges;
}

TranslationGenPtr
SystolicAccel::translate(Addr vaddr, Addr size)
{
    auto process = sys->threads[0]->getProcessPtr();
    return process->pTable->translateRange(vaddr, size);
}

} // namespace gem5
