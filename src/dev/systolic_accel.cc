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
      kTileBatch(p.k_tile_batch),
      macLatency(p.mac_latency),
      regMatA(0), regMatB(0), regMatC(0),
      regDimM(0), regDimK(0), regDimN(0),
      regCmd(0), regStatus(0),
      currentState(Idle),
      tileI(0), tileJ(0), tileK(0),
      kBatchStart(0), kBatchSize(0), kBatchOffset(0),
      rowIdx(0), colIdx(0), kIdx(0)
{
    // Initialize PE array
    peArray.resize(arrayRows);
    for (unsigned i = 0; i < arrayRows; i++) {
        peArray[i].resize(arrayCols);
    }
    
    // Allocate DMA buffers (batched for multiple K-tiles)
    bufWeights.resize(arrayRows * arrayCols * kTileBatch * sizeof(uint64_t));
    bufARow.resize(arrayRows * sizeof(uint64_t));  // K elements for one row of A
    bufCTile.resize(arrayRows * arrayCols * sizeof(uint64_t));
    
    std::cout << "[SystolicAccel] Created " << arrayRows << "×" << arrayCols 
              << " PE array, MAC latency=" << macLatency << " ticks, K-tile batch=" 
              << kTileBatch << std::endl;
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
    
    // Calculate which batch of K-tiles we're loading
    // kBatchStart is the starting K-tile index for this batch
    kBatchStart = tileK;
    
    // How many K-tiles remain?
    uint64_t totalKTiles = (regDimK + arrayRows - 1) / arrayRows;
    uint64_t kTilesRemaining = totalKTiles - kBatchStart;
    
    // Load min(kTileBatch, remaining K-tiles)
    kBatchSize = std::min((uint64_t)kTileBatch, kTilesRemaining);
    kBatchOffset = 0;
    
    uint64_t nTile = getCurrentNTileSize();
    
    // Load batched K-tiles: B[kBatchStart*arrayRows : (kBatchStart+kBatchSize)*arrayRows][nStart:nStart+nTile]
    uint64_t kStart = kBatchStart * arrayRows;
    uint64_t nStart = tileJ * arrayCols;
    uint64_t kElements = kBatchSize * arrayRows;  // Total K elements in this batch
    
    // std::cout << "[SystolicAccel] Loading weight batch: K-tiles [" << kBatchStart 
    //           << ":" << (kBatchStart + kBatchSize) << "], " << kElements << "×" << nTile 
    //           << " elements" << std::endl;
    
    // Load first row of the batch
    Addr addr = regMatB + (kStart * regDimN + nStart) * sizeof(uint64_t);
    uint64_t loadSize = nTile * sizeof(uint64_t);
    
    colIdx = 0;  // Track which k-row we've loaded
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onBRowLoaded(); });
    
    currentState = WaitWeightLoad;
    // Load into correct position in bufWeights (row 0 of tile)
    dmaReadVirt(addr, loadSize, cb, bufWeights.data());
}

void
SystolicAccel::onBRowLoaded()
{
    uint64_t nTile = getCurrentNTileSize();
    uint64_t kElements = kBatchSize * arrayRows;  // Total K rows in this batch
    
    // std::cout << "[SystolicAccel] Loaded B row " << colIdx << " of " << kElements << std::endl;
    
    colIdx++;  // Move to next row of B batch
    
    if (colIdx < kElements) {
        // Load next row of B batch
        uint64_t kStart = kBatchStart * arrayRows;
        uint64_t nStart = tileJ * arrayCols;
        Addr addr = regMatB + ((kStart + colIdx) * regDimN + nStart) * sizeof(uint64_t);
        uint64_t loadSize = nTile * sizeof(uint64_t);
        
        auto cb = new DmaVirtCallback<uint64_t>(
            [this](const uint64_t &) { onBRowLoaded(); });
        
        // Load into correct position: row colIdx of batch
        // Each row is nTile elements wide
        uint8_t *dest = bufWeights.data() + (colIdx * nTile * sizeof(uint64_t));
        dmaReadVirt(addr, loadSize, cb, dest);
    } else {
        // All B rows for this batch loaded
        // std::cout << "[SystolicAccel] B batch fully loaded (" << kElements << "×" << nTile << " elements)" << std::endl;
        onWeightLoadDone();
    }
}

void
SystolicAccel::onWeightLoadDone()
{
    // B batch is now fully loaded in bufWeights
    // bufWeights contains (kBatchSize * arrayRows) × nTile elements in row-major order
    
    // Reset accumulators at start of new output tile
    if (tileK == 0) {
        for (unsigned i = 0; i < arrayRows; i++) {
            for (unsigned j = 0; j < arrayCols; j++) {
                peArray[i][j].reset();
            }
        }
    }
    
    // std::cout << "[SystolicAccel] B batch loaded, starting row processing" << std::endl;
    
    // Start processing first K-tile in the batch
    kBatchOffset = 0;
    rowIdx = 0;
    startARowLoad();
}

void
SystolicAccel::startARowLoad()
{
    if (rowIdx >= getCurrentMTileSize()) {
        // Done with all rows for this K-tile in batch
        kBatchOffset++;
        tileK++;
        
        if (kBatchOffset < kBatchSize) {
            // More K-tiles in current batch to process
            rowIdx = 0;
            startARowLoad();
        } else if (tileK * arrayRows < regDimK) {
            // Finished batch, need to load next batch
            startWeightLoad();
        } else {
            // Finished all K tiles, write results
            startCTileWrite();
        }
        return;
    }
    
    currentState = StreamCompute;
    
    // Load A[tileI*arrayRows + rowIdx][(kBatchStart+kBatchOffset)*arrayRows : (kBatchStart+kBatchOffset+1)*arrayRows]
    uint64_t rowGlobal = tileI * arrayRows + rowIdx;
    uint64_t kStart = (kBatchStart + kBatchOffset) * arrayRows;
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
    // bufWeights contains: Batched B data in row-major order
    //   The current K-tile data starts at offset (kBatchOffset * arrayRows * nTile)
    
    // std::cout << "[SystolicAccel] performCompute: row=" << rowIdx 
    //           << ", kBatchOffset=" << kBatchOffset << ", kTile=" << kTile << ", nTile=" << nTile << std::endl;
    
    // Offset to current K-tile within the batched buffer
    uint64_t bufferOffset = kBatchOffset * arrayRows * nTile;
    
    for (unsigned k = 0; k < kTile; k++) {
        uint64_t aValue;
        std::memcpy(&aValue, bufARow.data() + k * sizeof(uint64_t), sizeof(uint64_t));
        
        // For this k, multiply with B[kStart+k][nStart:nStart+nTile]
        // These B values are at bufWeights[bufferOffset + k * nTile : bufferOffset + (k+1) * nTile]
        for (unsigned j = 0; j < nTile && j < arrayCols; j++) {
            uint64_t bValue;
            uint64_t bufferIdx = bufferOffset + k * nTile + j;
            std::memcpy(&bValue, bufWeights.data() + bufferIdx * sizeof(uint64_t), sizeof(uint64_t));
            
            // Debug output disabled for performance
            // if (rowIdx == 0 && j < 4) {
            //     std::cout << "  [DEBUG] k=" << k << ", A[0][" << k << "]=" << aValue 
            //               << ", B[" << k << "][" << j << "]=" << bValue;
            //     std::cout << ", PE[0][" << j << "].accum_before=" << peArray[0][j].getResult();
            // }
            
            // PE[rowIdx][j] computes C[rowIdx][j]
            peArray[rowIdx][j].loadWeight(bValue);
            peArray[rowIdx][j].compute(aValue);
            
            // if (rowIdx == 0 && j < 4) {
            //     std::cout << ", accum_after=" << peArray[0][j].getResult() << std::endl;
            // }
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
    
    // std::cout << "[SystolicAccel] Writing results tile (" << tileI << "," << tileJ 
    //           << "), size=" << mTile << "×" << nTile << std::endl;
    
    // Collect results from PEs into buffer
    for (unsigned i = 0; i < mTile; i++) {
        for (unsigned j = 0; j < nTile; j++) {
            uint64_t result = peArray[i][j].getResult();
            uint64_t idx = i * nTile + j;
            std::memcpy(bufCTile.data() + idx * sizeof(uint64_t), &result, sizeof(uint64_t));
            
            // if (i < 2 && j < 4) {
            //     std::cout << "[WriteTile] PE[" << i << "][" << j << "]=" << result 
            //               << " -> bufCTile[" << idx << "]" << std::endl;
            // }
        }
    }
    
    // Start writing row-by-row (to handle non-contiguous memory layout)
    writeRowIdx = 0;
    currentState = WaitDrain;
    
    // Write first row
    uint64_t rowStart = tileI * arrayRows + writeRowIdx;
    uint64_t colStart = tileJ * arrayCols;
    Addr addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t);
    uint64_t rowSize = nTile * sizeof(uint64_t);
    
    // std::cout << "[WriteTile] Writing row " << writeRowIdx << " to addr=0x" 
    //           << std::hex << rowAddr << std::dec << ", size=" << writeSize << " bytes" << std::endl;
    
    auto cb = new DmaVirtCallback<uint64_t>(
        [this](const uint64_t &) { onCTileWriteDone(); });
    
    dmaWriteVirt(addr, rowSize, cb, bufCTile.data() + writeRowIdx * nTile * sizeof(uint64_t));
}

void
SystolicAccel::onCTileWriteDone()
{
    uint64_t mTile = getCurrentMTileSize();
    uint64_t nTile = getCurrentNTileSize();
    
    writeRowIdx++;
    
    // Check if we have more rows to write
    if (writeRowIdx < mTile) {
        // Write next row
        uint64_t rowStart = tileI * arrayRows + writeRowIdx;
        uint64_t colStart = tileJ * arrayCols;
        Addr addr = regMatC + (rowStart * regDimN + colStart) * sizeof(uint64_t);
        uint64_t rowSize = nTile * sizeof(uint64_t);
        
        // std::cout << "[WriteTile] Writing row " << writeRowIdx << " to addr=0x" 
        //           << std::hex << rowAddr << std::dec << ", size=" << writeSize << " bytes" << std::endl;
        
        auto cb = new DmaVirtCallback<uint64_t>(
            [this](const uint64_t &) { onCTileWriteDone(); });
        
        dmaWriteVirt(addr, rowSize, cb, bufCTile.data() + writeRowIdx * nTile * sizeof(uint64_t));
    } else {
        // All rows written
        // std::cout << "[WriteTile] DMA write completed for tile (" << tileI << "," << tileJ << ")" << std::endl;
        nextTile();
    }
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
