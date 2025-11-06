/*
 * Simplified Image Classification Benchmark for gem5
 * Based on MLPerf Tiny benchmark, adapted for syscall emulation mode
 * 
 * This version removes embedded system dependencies and creates
 * a standalone C++ program that can run in gem5 simulation.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>

// Simplified model settings (from MLPerf Tiny image classification)
constexpr int kIcInputSize = 3072;  // 32x32x3 RGB image
constexpr int kCategoryCount = 10;   // CIFAR-10 has 10 classes
constexpr int kTensorArenaSize = 100 * 1024;  // 100KB for TensorFlow Lite

// Simple struct to hold our "model" (we'll simulate inference for now)
struct SimpleModel {
    uint8_t* tensor_arena;
    int8_t* input_buffer;
    float* output_buffer;
    
    SimpleModel() {
        tensor_arena = new uint8_t[kTensorArenaSize];
        input_buffer = new int8_t[kIcInputSize];
        output_buffer = new float[kCategoryCount];
        
        // Initialize output to zeros
        for (int i = 0; i < kCategoryCount; i++) {
            output_buffer[i] = 0.0f;
        }
        
        printf("Model initialized with %d KB tensor arena\n", kTensorArenaSize / 1024);
    }
    
    ~SimpleModel() {
        delete[] tensor_arena;
        delete[] input_buffer;
        delete[] output_buffer;
    }
    
    // Simulate loading input data
    void LoadInput(const uint8_t* data, size_t size) {
        if (size != kIcInputSize) {
            printf("ERROR: Expected %d bytes, got %zu bytes\n", kIcInputSize, size);
            return;
        }
        
        // Convert uint8_t [0,255] to int8_t [-128,127]
        for (int i = 0; i < kIcInputSize; i++) {
            input_buffer[i] = static_cast<int8_t>(data[i] - 128);
        }
        
        printf("Input loaded: %d bytes\n", kIcInputSize);
    }
    
    // Simulate neural network inference
    // This is a PLACEHOLDER - in real implementation this would be TFLite
    void RunInference() {
        printf("Running inference...\n");
        
        // Simulate convolution and fully connected layers
        // This creates realistic memory access patterns and computation
        
        // Stage 1: Simulate first conv layer (32x32x3 -> 32x32x32)
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
        
        // Stage 2: Simulate pooling (32x32 -> 16x16)
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
                            if (val > max_val) max_val = val;
                        }
                    }
                    pool1_output[(h * 16 + w) * 32 + c] = max_val;
                }
            }
        }
        
        // Stage 3: Simulate fully connected layer (flatten -> 10 classes)
        for (int i = 0; i < kCategoryCount; i++) {
            float sum = 0.0f;
            for (int j = 0; j < 16 * 16 * 32; j++) {
                sum += pool1_output[j] * 0.001f;  // Simulated weight
            }
            output_buffer[i] = sum;
        }
        
        // Apply softmax
        float max_logit = output_buffer[0];
        for (int i = 1; i < kCategoryCount; i++) {
            if (output_buffer[i] > max_logit) max_logit = output_buffer[i];
        }
        
        float sum_exp = 0.0f;
        for (int i = 0; i < kCategoryCount; i++) {
            output_buffer[i] = expf(output_buffer[i] - max_logit);
            sum_exp += output_buffer[i];
        }
        
        for (int i = 0; i < kCategoryCount; i++) {
            output_buffer[i] /= sum_exp;
        }
        
        delete[] conv1_output;
        delete[] pool1_output;
        
        printf("Inference complete\n");
    }
    
    void PrintResults() {
        printf("\n=== Classification Results ===\n");
        const char* class_names[] = {
            "airplane", "automobile", "bird", "cat", "deer",
            "dog", "frog", "horse", "ship", "truck"
        };
        
        printf("m-results-[");
        for (int i = 0; i < kCategoryCount; i++) {
            printf("%.3f", output_buffer[i]);
            if (i < kCategoryCount - 1) printf(",");
        }
        printf("]\n\n");
        
        // Find top prediction
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
    }
};

// Generate synthetic input data (simulates a 32x32 RGB image)
void GenerateSyntheticInput(uint8_t* buffer, int pattern) {
    printf("Generating synthetic input (pattern %d)...\n", pattern);
    
    for (int i = 0; i < kIcInputSize; i++) {
        // Create different patterns based on input
        switch (pattern % 3) {
            case 0:
                // Gradient pattern
                buffer[i] = static_cast<uint8_t>((i * 255) / kIcInputSize);
                break;
            case 1:
                // Checkerboard pattern
                buffer[i] = ((i / 32) % 2) ? 200 : 50;
                break;
            case 2:
                // Random-ish pattern
                buffer[i] = static_cast<uint8_t>((i * 17 + pattern) % 256);
                break;
        }
    }
}

int main(int argc, char** argv) {
    printf("========================================\n");
    printf("MLPerf Tiny Image Classification Benchmark\n");
    printf("Simplified version for gem5 simulation\n");
    printf("========================================\n\n");
    
    // Determine number of inference runs
    int num_runs = 10;  // Default
    if (argc > 1) {
        num_runs = atoi(argv[1]);
        if (num_runs <= 0) num_runs = 10;
    }
    
    printf("Configuration:\n");
    printf("  - Input size: %d bytes (32x32x3 RGB)\n", kIcInputSize);
    printf("  - Output classes: %d\n", kCategoryCount);
    printf("  - Tensor arena: %d KB\n", kTensorArenaSize / 1024);
    printf("  - Number of runs: %d\n\n", num_runs);
    
    // Initialize model
    SimpleModel model;
    
    // Allocate input buffer
    uint8_t* input_data = new uint8_t[kIcInputSize];
    
    // Run benchmark multiple times
    for (int run = 0; run < num_runs; run++) {
        printf("\n--- Run %d/%d ---\n", run + 1, num_runs);
        
        // Generate input
        GenerateSyntheticInput(input_data, run);
        
        // Load input into model
        model.LoadInput(input_data, kIcInputSize);
        
        // Run inference
        model.RunInference();
        
        // Print results
        model.PrintResults();
    }
    
    delete[] input_data;
    
    printf("\n========================================\n");
    printf("Benchmark complete! Ran %d inferences.\n", num_runs);
    printf("========================================\n");
    
    return 0;
}
