/*
 * PROFILED Version - Measures exact time spent in each operation
 * This version adds detailed timing to prove compute-bound vs memory-bound
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <chrono>

// Simplified model settings (from MLPerf Tiny image classification)
constexpr int kIcInputSize = 3072;  // 32x32x3 RGB image
constexpr int kCategoryCount = 10;   // CIFAR-10 has 10 classes
constexpr int kTensorArenaSize = 100 * 1024;  // 100KB for TensorFlow Lite

// Timing accumulators (in microseconds)
struct ProfilingData {
    double memory_allocation_us = 0.0;
    double memory_deallocation_us = 0.0;
    double input_generation_us = 0.0;
    double input_loading_us = 0.0;
    double conv1_compute_us = 0.0;
    double pool1_compute_us = 0.0;
    double conv2_compute_us = 0.0;
    double pool2_compute_us = 0.0;
    double fc_compute_us = 0.0;
    double softmax_compute_us = 0.0;
    double output_formatting_us = 0.0;
    double total_inference_us = 0.0;
    int num_runs = 0;
};

ProfilingData g_profile;

// Helper to measure time
class Timer {
    std::chrono::high_resolution_clock::time_point start;
public:
    Timer() { start = std::chrono::high_resolution_clock::now(); }
    double elapsed_us() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(end - start).count();
    }
};

// Simple struct to hold our "model"
struct SimpleModel {
    uint8_t* tensor_arena;
    int8_t* input_buffer;
    float* output_buffer;
    
    SimpleModel() {
        Timer t;
        tensor_arena = new uint8_t[kTensorArenaSize];
        input_buffer = new int8_t[kIcInputSize];
        output_buffer = new float[kCategoryCount];
        g_profile.memory_allocation_us += t.elapsed_us();
        
        printf("Model initialized with %d KB tensor arena\n\n", kTensorArenaSize / 1024);
    }
    
    ~SimpleModel() {
        Timer t;
        delete[] tensor_arena;
        delete[] input_buffer;
        delete[] output_buffer;
        g_profile.memory_deallocation_us += t.elapsed_us();
    }
    
    void LoadInput(const uint8_t* data, int size) {
        Timer t;
        
        if (size != kIcInputSize) {
            printf("Error: Expected %d bytes, got %d\n", kIcInputSize, size);
            return;
        }
        
        // Convert uint8_t [0,255] to int8_t [-128,127]
        for (int i = 0; i < kIcInputSize; i++) {
            input_buffer[i] = static_cast<int8_t>(data[i] - 128);
        }
        
        g_profile.input_loading_us += t.elapsed_us();
        printf("Input loaded: %d bytes\n", kIcInputSize);
    }
    
    // Simulate neural network inference with detailed profiling
    void RunInference() {
        printf("Running inference...\n");
        Timer total_timer;
        
        // ===== STAGE 1: First convolution layer =====
        {
            Timer t;
            float* conv1_output = new float[32 * 32 * 32];
            
            for (int h = 0; h < 32; h++) {
                for (int w = 0; w < 32; w++) {
                    for (int c = 0; c < 32; c++) {
                        float sum = 0.0f;
                        // 3x3 convolution
                        for (int kh = -1; kh <= 1; kh++) {
                            for (int kw = -1; kw <= 1; kw++) {
                                int ih = h + kh;
                                int iw = w + kw;
                                if (ih >= 0 && ih < 32 && iw >= 0 && iw < 32) {
                                    for (int ic = 0; ic < 3; ic++) {
                                        int idx = (ih * 32 + iw) * 3 + ic;
                                        sum += input_buffer[idx] * 0.01f;  // Simulated weight
                                    }
                                }
                            }
                        }
                        conv1_output[(h * 32 + w) * 32 + c] = fmaxf(sum, 0.0f);  // ReLU
                    }
                }
            }
            
            double elapsed = t.elapsed_us();
            g_profile.conv1_compute_us += elapsed;
            
            // ===== STAGE 2: First pooling =====
            Timer t2;
            float* pool1_output = new float[16 * 16 * 32];
            for (int h = 0; h < 16; h++) {
                for (int w = 0; w < 16; w++) {
                    for (int c = 0; c < 32; c++) {
                        float max_val = -1e10f;
                        for (int kh = 0; kh < 2; kh++) {
                            for (int kw = 0; kw < 2; kw++) {
                                int ih = h * 2 + kh;
                                int iw = w * 2 + kw;
                                float val = conv1_output[(ih * 32 + iw) * 32 + c];
                                max_val = fmaxf(max_val, val);
                            }
                        }
                        pool1_output[(h * 16 + w) * 32 + c] = max_val;
                    }
                }
            }
            
            delete[] conv1_output;
            g_profile.pool1_compute_us += t2.elapsed_us();
            
            // ===== STAGE 3: Second convolution =====
            Timer t3;
            float* conv2_output = new float[16 * 16 * 64];
            for (int h = 0; h < 16; h++) {
                for (int w = 0; w < 16; w++) {
                    for (int c = 0; c < 64; c++) {
                        float sum = 0.0f;
                        for (int kh = -1; kh <= 1; kh++) {
                            for (int kw = -1; kw <= 1; kw++) {
                                int ih = h + kh;
                                int iw = w + kw;
                                if (ih >= 0 && ih < 16 && iw >= 0 && iw < 16) {
                                    for (int ic = 0; ic < 32; ic++) {
                                        int idx = (ih * 16 + iw) * 32 + ic;
                                        sum += pool1_output[idx] * 0.01f;
                                    }
                                }
                            }
                        }
                        conv2_output[(h * 16 + w) * 64 + c] = fmaxf(sum, 0.0f);
                    }
                }
            }
            
            delete[] pool1_output;
            g_profile.conv2_compute_us += t3.elapsed_us();
            
            // ===== STAGE 4: Second pooling =====
            Timer t4;
            float* pool2_output = new float[8 * 8 * 64];
            for (int h = 0; h < 8; h++) {
                for (int w = 0; w < 8; w++) {
                    for (int c = 0; c < 64; c++) {
                        float max_val = -1e10f;
                        for (int kh = 0; kh < 2; kh++) {
                            for (int kw = 0; kw < 2; kw++) {
                                int ih = h * 2 + kh;
                                int iw = w * 2 + kw;
                                float val = conv2_output[(ih * 16 + iw) * 64 + c];
                                max_val = fmaxf(max_val, val);
                            }
                        }
                        pool2_output[(h * 8 + w) * 64 + c] = max_val;
                    }
                }
            }
            
            delete[] conv2_output;
            g_profile.pool2_compute_us += t4.elapsed_us();
            
            // ===== STAGE 5: Fully connected layer =====
            Timer t5;
            int fc_input_size = 8 * 8 * 64;  // 4096
            for (int c = 0; c < kCategoryCount; c++) {
                float sum = 0.0f;
                for (int i = 0; i < fc_input_size; i++) {
                    sum += pool2_output[i] * 0.01f;  // Simulated weight
                }
                output_buffer[c] = sum;
            }
            
            delete[] pool2_output;
            g_profile.fc_compute_us += t5.elapsed_us();
            
            // ===== STAGE 6: Softmax =====
            Timer t6;
            float max_val = output_buffer[0];
            for (int i = 1; i < kCategoryCount; i++) {
                max_val = fmaxf(max_val, output_buffer[i]);
            }
            
            float sum_exp = 0.0f;
            for (int i = 0; i < kCategoryCount; i++) {
                output_buffer[i] = expf(output_buffer[i] - max_val);
                sum_exp += output_buffer[i];
            }
            
            for (int i = 0; i < kCategoryCount; i++) {
                output_buffer[i] /= sum_exp;
            }
            
            g_profile.softmax_compute_us += t6.elapsed_us();
        }
        
        g_profile.total_inference_us += total_timer.elapsed_us();
        g_profile.num_runs++;
        
        printf("Inference complete\n");
    }
    
    void PrintResults() {
        Timer t;
        
        const char* class_names[] = {
            "airplane", "automobile", "bird", "cat", "deer",
            "dog", "frog", "horse", "ship", "truck"
        };
        
        printf("\n=== Classification Results ===\n");
        printf("m-results-[");
        for (int i = 0; i < kCategoryCount; i++) {
            printf("%.3f", output_buffer[i]);
            if (i < kCategoryCount - 1) printf(",");
        }
        printf("]\n");
        
        int max_idx = 0;
        float max_prob = output_buffer[0];
        for (int i = 1; i < kCategoryCount; i++) {
            if (output_buffer[i] > max_prob) {
                max_prob = output_buffer[i];
                max_idx = i;
            }
        }
        
        printf("Top prediction: %s (%.1f%% confidence)\n", 
               class_names[max_idx], max_prob * 100.0f);
        
        g_profile.output_formatting_us += t.elapsed_us();
    }
};

void GenerateSyntheticInput(uint8_t* buffer, int pattern_idx) {
    Timer t;
    
    pattern_idx = pattern_idx % 3;
    
    if (pattern_idx == 0) {
        for (int i = 0; i < kIcInputSize; i++) {
            buffer[i] = (i * 7) % 256;
        }
    } else if (pattern_idx == 1) {
        for (int i = 0; i < 32 * 32; i++) {
            uint8_t val = ((i / 32) + (i % 32)) % 2 ? 200 : 50;
            buffer[i * 3 + 0] = val;
            buffer[i * 3 + 1] = val;
            buffer[i * 3 + 2] = val;
        }
    } else {
        for (int i = 0; i < kIcInputSize; i++) {
            buffer[i] = (i * 13 + pattern_idx * 17) % 256;
        }
    }
    
    g_profile.input_generation_us += t.elapsed_us();
}

void PrintProfilingReport() {
    printf("\n");
    printf("========================================================================\n");
    printf("                    DETAILED PROFILING REPORT                          \n");
    printf("========================================================================\n");
    printf("\nRuns: %d\n", g_profile.num_runs);
    printf("\n--- TIME BREAKDOWN (microseconds) ---\n\n");
    
    double total_compute = g_profile.conv1_compute_us + g_profile.pool1_compute_us +
                          g_profile.conv2_compute_us + g_profile.pool2_compute_us +
                          g_profile.fc_compute_us + g_profile.softmax_compute_us;
    
    double total_memory = g_profile.memory_allocation_us + g_profile.memory_deallocation_us;
    
    double total_io = g_profile.input_generation_us + g_profile.input_loading_us + 
                      g_profile.output_formatting_us;
    
    printf("COMPUTATION:\n");
    printf("  Conv Layer 1:        %12.2f us  (%5.1f%%)\n", 
           g_profile.conv1_compute_us, 
           100.0 * g_profile.conv1_compute_us / g_profile.total_inference_us);
    printf("  Pool Layer 1:        %12.2f us  (%5.1f%%)\n", 
           g_profile.pool1_compute_us,
           100.0 * g_profile.pool1_compute_us / g_profile.total_inference_us);
    printf("  Conv Layer 2:        %12.2f us  (%5.1f%%)\n", 
           g_profile.conv2_compute_us,
           100.0 * g_profile.conv2_compute_us / g_profile.total_inference_us);
    printf("  Pool Layer 2:        %12.2f us  (%5.1f%%)\n", 
           g_profile.pool2_compute_us,
           100.0 * g_profile.pool2_compute_us / g_profile.total_inference_us);
    printf("  Fully Connected:     %12.2f us  (%5.1f%%)\n", 
           g_profile.fc_compute_us,
           100.0 * g_profile.fc_compute_us / g_profile.total_inference_us);
    printf("  Softmax:             %12.2f us  (%5.1f%%)\n", 
           g_profile.softmax_compute_us,
           100.0 * g_profile.softmax_compute_us / g_profile.total_inference_us);
    printf("  --------------------------------\n");
    printf("  TOTAL COMPUTE:       %12.2f us  (%5.1f%%)\n\n", 
           total_compute, 100.0 * total_compute / g_profile.total_inference_us);
    
    printf("MEMORY MANAGEMENT:\n");
    printf("  Allocation:          %12.2f us  (%5.1f%%)\n", 
           g_profile.memory_allocation_us,
           100.0 * g_profile.memory_allocation_us / g_profile.total_inference_us);
    printf("  Deallocation:        %12.2f us  (%5.1f%%)\n", 
           g_profile.memory_deallocation_us,
           100.0 * g_profile.memory_deallocation_us / g_profile.total_inference_us);
    printf("  --------------------------------\n");
    printf("  TOTAL MEMORY MGT:    %12.2f us  (%5.1f%%)\n\n", 
           total_memory, 100.0 * total_memory / g_profile.total_inference_us);
    
    printf("INPUT/OUTPUT:\n");
    printf("  Input Generation:    %12.2f us  (%5.1f%%)\n", 
           g_profile.input_generation_us,
           100.0 * g_profile.input_generation_us / g_profile.total_inference_us);
    printf("  Input Loading:       %12.2f us  (%5.1f%%)\n", 
           g_profile.input_loading_us,
           100.0 * g_profile.input_loading_us / g_profile.total_inference_us);
    printf("  Output Formatting:   %12.2f us  (%5.1f%%)\n", 
           g_profile.output_formatting_us,
           100.0 * g_profile.output_formatting_us / g_profile.total_inference_us);
    printf("  --------------------------------\n");
    printf("  TOTAL I/O:           %12.2f us  (%5.1f%%)\n\n", 
           total_io, 100.0 * total_io / g_profile.total_inference_us);
    
    printf("TOTAL INFERENCE TIME:  %12.2f us\n", g_profile.total_inference_us);
    
    printf("\n========================================================================\n");
    printf("                         SUMMARY                                        \n");
    printf("========================================================================\n\n");
    
    printf("Compute:       %6.1f%% (%9.2f us)\n", 
           100.0 * total_compute / g_profile.total_inference_us, total_compute);
    printf("Memory Mgmt:   %6.1f%% (%9.2f us)\n", 
           100.0 * total_memory / g_profile.total_inference_us, total_memory);
    printf("I/O:           %6.1f%% (%9.2f us)\n", 
           100.0 * total_io / g_profile.total_inference_us, total_io);
    
    printf("\n");
    if (total_compute > total_memory * 10) {
        printf("✓ VERDICT: COMPUTE-BOUND (computation dominates by %.1fx)\n", 
               total_compute / total_memory);
    } else if (total_memory > total_compute * 2) {
        printf("✓ VERDICT: MEMORY-BOUND (memory management dominates by %.1fx)\n", 
               total_memory / total_compute);
    } else {
        printf("✓ VERDICT: BALANCED (neither clearly dominates)\n");
    }
    
    printf("\n========================================================================\n");
}

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("MLPerf Tiny Image Classification Benchmark\n");
    printf("PROFILED VERSION - Detailed Timing Analysis\n");
    printf("========================================\n\n");
    
    int num_runs = 10;
    if (argc > 1) {
        num_runs = atoi(argv[1]);
        if (num_runs <= 0) num_runs = 10;
    }
    
    printf("Configuration:\n");
    printf("  - Input size: %d bytes (32x32x3 RGB)\n", kIcInputSize);
    printf("  - Output classes: %d\n", kCategoryCount);
    printf("  - Tensor arena: %d KB\n", kTensorArenaSize / 1024);
    printf("  - Number of runs: %d\n\n", num_runs);
    
    SimpleModel model;
    uint8_t* input_data = new uint8_t[kIcInputSize];
    
    for (int run = 0; run < num_runs; run++) {
        printf("\n--- Run %d/%d ---\n", run + 1, num_runs);
        
        printf("Generating synthetic input (pattern %d)...\n", run % 3);
        GenerateSyntheticInput(input_data, run);
        
        model.LoadInput(input_data, kIcInputSize);
        model.RunInference();
        model.PrintResults();
    }
    
    delete[] input_data;
    
    PrintProfilingReport();
    
    return 0;
}
