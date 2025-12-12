/*
 * Systolic Array Accelerator for GEMM: C[M×N] = A[M×K] × B[K×N]
 * 
 * Simplified systolic array with:
 * - 2D grid of Processing Elements (PEs)
 * - Weight-stationary dataflow
 * - DMA-based data movement
 * - MMIO control interface
 * 
 * No Aladdin dependencies - uses DmaVirtDevice like SimdAccel
 */

#ifndef __DEV_SYSTOLIC_ACCEL_HH__
#define __DEV_SYSTOLIC_ACCEL_HH__

#include <vector>
#include <array>

#include "base/types.hh"
#include "dev/dma_virt_device.hh"
#include "params/SystolicAccel.hh"
#include "sim/eventq.hh"

namespace gem5 {

// Simple Processing Element (PE)
// Performs: accum = accum + (a_in * b_in)
class ProcessingElement {
  public:
    ProcessingElement() : weight(0), accumulator(0) {}
    
    void reset() {
        weight = 0;
        accumulator = 0;
    }
    
    void loadWeight(uint64_t w) {
        weight = w;
    }
    
    void compute(uint64_t a_value) {
        // MAC: accum += a * weight
        accumulator += a_value * weight;
    }
    
    uint64_t getResult() const {
        return accumulator;
    }
    
    void setResult(uint64_t val) {
        accumulator = val;
    }
    
  private:
    uint64_t weight;      // Stored weight (B matrix element)
    uint64_t accumulator; // Accumulated result (C matrix element)
};

class SystolicAccel : public DmaVirtDevice
{
  public:
    SystolicAccel(const SystolicAccelParams &p);
    
    void init() override;
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
    AddrRangeList getAddrRanges() const override;
    TranslationGenPtr translate(Addr vaddr, Addr size) override;
    
  private:
    // MMIO configuration
    Addr pioAddr;
    Addr pioSize;
    Tick pioDelay;
    
    // Systolic array dimensions
    const unsigned arrayRows;    // Number of PE rows (M dimension)
    const unsigned arrayCols;    // Number of PE columns (N dimension)
    
    // Timing parameters
    Tick macLatency;  // Latency for one MAC operation across all PEs
    
    // MMIO registers (similar to SimdAccel)
    Addr     regMatA;      // Base address of A matrix (M×K)
    Addr     regMatB;      // Base address of B matrix (K×N)
    Addr     regMatC;      // Base address of C matrix (M×N)
    uint64_t regDimM;      // M dimension (rows of A, rows of C)
    uint64_t regDimK;      // K dimension (cols of A, rows of B)
    uint64_t regDimN;      // N dimension (cols of B, cols of C)
    uint64_t regCmd;       // Command register (bit 0 = start)
    uint64_t regStatus;    // Status register (bit 0 = busy)
    
    // PE array
    std::vector<std::vector<ProcessingElement>> peArray;
    
    // Computation state
    enum State {
        Idle,
        LoadWeights,      // Loading B matrix into PEs
        WaitWeightLoad,
        StreamCompute,    // Streaming A rows through PEs
        WaitCompute,
        DrainResults,     // Writing C matrix back
        WaitDrain
    };
    State currentState;
    
    // Tiling indices (for when matrix is larger than PE array)
    uint64_t tileI;        // Current M tile
    uint64_t tileJ;        // Current N tile
    uint64_t tileK;        // Current K tile (for accumulation)
    
    // Within-tile indices
    uint64_t rowIdx;       // Current row being processed in tile
    uint64_t colIdx;       // Current column being processed in tile
    uint64_t kIdx;         // Current K step
    
    // DMA buffers
    std::vector<uint8_t> bufWeights;   // Buffer for loading weights
    std::vector<uint8_t> bufARow;      // Buffer for A matrix row
    std::vector<uint8_t> bufCTile;     // Buffer for C matrix tile
    
    // Helper functions
    void kick();
    void startWeightLoad();
    void onBRowLoaded();         // Callback for loading each row of B tile
    void onWeightLoadDone();
    void startARowLoad();
    void onARowLoadDone();
    void performCompute();
    void onComputeDone();
    void startCTileWrite();
    void onCTileWriteDone();
    void nextTile();
    void finishOperation();
    
    // Tile size helpers
    uint64_t getCurrentMTileSize() const;
    uint64_t getCurrentNTileSize() const;
    uint64_t getCurrentKTileSize() const;
};

} // namespace gem5

#endif // __DEV_SYSTOLIC_ACCEL_HH__
