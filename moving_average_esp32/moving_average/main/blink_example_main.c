#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BENCH";

// --- Inline functions to read RISC-V hardware counters ---
/*static inline uint64_t read_cycle(void) {
    uint64_t value;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(value));
    return value;
}
static inline uint64_t read_instret(void) {
    uint64_t value;
    __asm__ volatile ("csrr %0, minstret" : "=r"(value));
    return value;
}
*/


// --- Moving average filter ---
static void moving_average_filter(const double *in, double *out, size_t len, int M)
{
    for (size_t n = 0; n < len; n++) {
        double sum = 0.0; int c = 0;
        for (int k = 0; k < M; k++) {
            if (n >= (size_t)k) { sum += in[n - k]; c++; }
        }
        out[n] = c ? (sum / c) : 0.0;
    }
}

// --- Simple heart-rate estimation from peaks ---
static double compute_hr(const double *x, size_t len, double fs, double thr){
    if (!x || len < 3 || fs <= 0.0) return 0.0;
    int peaks = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        if (x[i] > x[i - 1] && x[i] > x[i + 1] && x[i] > thr) {
            peaks++;
            size_t skip = (size_t)(fs * 0.4);
            i += skip;
            if (i + 1 >= len) break;
        }
    }
    double dur = (double)len / fs;
    return dur > 0 ? (peaks / dur) * 60.0 : 0.0;
}

static double x[100];
static double y[100];
void app_main(void)
{
    ESP_LOGI(TAG, "Starting software benchmark (time-based)...");

    // Simulated PPG signal
    const size_t LEN = 100;
    const int M = 5;
    const double FS = 50.0;   // Sampling frequency
    const double TH = 0.6;    // Peak threshold

    for (size_t i = 0; i < LEN; i++) {
        x[i] = 0.5 + 0.5 * sin(2 * M_PI * i / 50.0);
    }

    // Start benchmark timer
    uint64_t start_us = esp_timer_get_time();

    // Run computations
    moving_average_filter(x, y, LEN, M);
    double hr = compute_hr(y, LEN, FS, TH);

    // Stop timer
    uint64_t end_us = esp_timer_get_time();
    double elapsed_ms = (end_us - start_us) / 1000.0;

    // Print results
    ESP_LOGI(TAG, "Benchmark done!");
    ESP_LOGI(TAG, "Estimated heart rate = %.2f bpm", hr);
    ESP_LOGI(TAG, "Execution time = %.3f ms", elapsed_ms);

    // Loop to keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}