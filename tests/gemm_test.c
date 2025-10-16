// tests/gemm_test.c
#include <stdint.h>
#include <stddef.h>

// Simple print functions for gem5 SE mode
static void print_str(const char *s) {
    while (*s) {
        asm volatile("li a7, 64\n"       // sys_write
                     "li a0, 1\n"        // fd = stdout
                     "mv a1, %0\n"       // buffer
                     "li a2, 1\n"        // count = 1
                     "ecall\n"
                     : : "r"(s) : "a0", "a1", "a2", "a7");
        s++;
    }
}

static void print_num(uint64_t n) {
    char buf[32];
    int i = 0;
    if (n == 0) {
        print_str("0");
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    // Print in reverse
    while (i > 0) {
        char c = buf[--i];
        asm volatile("li a7, 64\n"
                     "li a0, 1\n"
                     "mv a1, %0\n"
                     "li a2, 1\n"
                     "ecall\n"
                     : : "r"(&c) : "a0", "a1", "a2", "a7");
    }
}

static inline void xcop_optype(uint64_t op){
    asm volatile(".insn r 0x0B,0x5,0x01, x0,%0,x0" :: "r"(op) : "memory");
}
static inline void xcop_dimk(uint64_t k){
    asm volatile(".insn r 0x0B,0x6,0x01, x0,%0,x0" :: "r"(k) : "memory");
}
static inline void xcop_dimn(uint64_t n){
    asm volatile(".insn r 0x0B,0x7,0x01, x0,%0,x0" :: "r"(n) : "memory");
}
static inline void xcop_srca(uint64_t a){
    asm volatile(".insn r 0x0B,0x0,0x01, x0,%0,x0" :: "r"(a) : "memory");
}
static inline void xcop_srcb(uint64_t b){
    asm volatile(".insn r 0x0B,0x1,0x01, x0,%0,x0" :: "r"(b) : "memory");
}
static inline void xcop_dst (uint64_t d){
    asm volatile(".insn r 0x0B,0x2,0x01, x0,%0,x0" :: "r"(d) : "memory");
}
static inline void xcop_len (uint64_t m){
    asm volatile(".insn r 0x0B,0x3,0x01, x0,%0,x0" :: "r"(m) : "memory");
}
static inline void xcop_kick(void){
    asm volatile("fence iorw, iorw\n\t"
                 ".insn r 0x0B,0x4,0x01, x0,x0,x0\n\t"
                 : : : "memory");
}

#define M 4
#define K 4
#define N 4

static uint64_t A[M][K], B[K][N], C[M][N];

int main(void) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            A[i][j] = i * K + j + 1;
        }
    }
    
    for (int i = 0; i < K; i++) {
        for (int j = 0; j < N; j++) {
            B[i][j] = (i == j) ? 1 : 0;
        }
    }
    
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            C[i][j] = 0;
        }
    }

    xcop_optype(1);
    xcop_len(M);
    xcop_dimk(K);
    xcop_dimn(N);
    xcop_srca((uint64_t)(uintptr_t)A);
    xcop_srcb((uint64_t)(uintptr_t)B);
    xcop_dst((uint64_t)(uintptr_t)C);
    xcop_kick();

    volatile uint64_t *STATUS = (uint64_t*)(uintptr_t)(0x40000000ull + 0x28);
    while (*STATUS & 1) { }

    print_str("\n=== GEMM Results ===\n");
    
    int ok = 1;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            print_str("C[");
            print_num(i);
            print_str("][");
            print_num(j);
            print_str("] = ");
            print_num(C[i][j]);
            print_str(" (expected ");
            print_num(A[i][j]);
            print_str(")\n");
            
            uint64_t expected = A[i][j];
            if (C[i][j] != expected) {
                ok = 0;
                print_str("ERROR: Mismatch!\n");
                C[0][0] = 0xDEADBEEFDEADBEEFULL;
                break;
            }
        }
        if (!ok) break;
    }
    
    if (ok) {
        C[M-1][N-1] = 0xC0FFEEC0FFEEULL;
        print_str("\n*** TEST PASSED ***\n");
    } else {
        print_str("\n*** TEST FAILED ***\n");
    }
    
    return ok ? 0 : 1;
}
