#include <stdio.h>
#include <stdint.h>
#include <time.h>

// 1. Floating-point Mandelbrot Escape-Time Kernel
int64_t run_mandelbrot(int32_t width, int32_t height, int32_t max_iter) {
    int64_t total_iterations = 0;
    double f_width = (double)width;
    double f_height = (double)height;

    for (int32_t y = 0; y < height; y++) {
        double cy = -1.5 + (3.0 * (double)y) / f_height;
        for (int32_t x = 0; x < width; x++) {
            double cx = -2.0 + (3.0 * (double)x) / f_width;
            double zx = 0.0;
            double zy = 0.0;
            int32_t iter = 0;

            while (zx * zx + zy * zy <= 4.0 && iter < max_iter) {
                double tmp = zx * zx - zy * zy + cx;
                zy = 2.0 * zx * zy + cy;
                zx = tmp;
                iter++;
            }
            total_iterations += iter;
        }
    }
    return total_iterations;
}

// 2. Integer ALU & Branching Kernel
int64_t run_collatz(int32_t max_n) {
    int64_t total_steps = 0;
    for (int32_t i = 1; i <= max_n; i++) {
        int64_t n = (int64_t)i;
        int32_t steps = 0;
        while (n > 1) {
            if (n % 2 == 0) {
                n = n / 2;
            } else {
                n = n * 3 + 1;
            }
            steps++;
        }
        total_steps += (int64_t)steps;
    }
    return total_steps;
}

int main(void) {
    printf("====================================================\n");
    printf("           COMPUTE BENCHMARK: C (ISO C99/C11)       \n");
    printf("====================================================\n");

    // Benchmark 1: Mandelbrot
    printf("[1/2] Running Mandelbrot (2000x2000, 250 max_iter)...\n");
    clock_t t0 = clock();
    int64_t mandel_checksum = run_mandelbrot(2000, 2000, 250);
    clock_t t1 = clock();
    double mandel_ms = ((double)(t1 - t0) / (double)CLOCKS_PER_SEC) * 1000.0;
    printf("      -> Checksum: %lld\n", (long long)mandel_checksum);
    printf("      -> Time    : %.2f ms\n\n", mandel_ms);

    // Benchmark 2: Collatz
    printf("[2/2] Running Collatz Sequence (2,000,000 iterations)...\n");
    clock_t t2 = clock();
    int64_t collatz_checksum = run_collatz(2000000);
    clock_t t3 = clock();
    double collatz_ms = ((double)(t3 - t2) / (double)CLOCKS_PER_SEC) * 1000.0;
    printf("      -> Checksum: %lld\n", (long long)collatz_checksum);
    printf("      -> Time    : %.2f ms\n\n", collatz_ms);

    printf("Total Execution Time: %.2f ms\n", mandel_ms + collatz_ms);
    printf("====================================================\n");
    return 0;
}