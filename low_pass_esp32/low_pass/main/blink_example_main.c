#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_clk_tree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_SAMPLES 100
#define MAX_TAPS    256

static const char *TAG = "PPG_BENCH";

// ---------------- FIR Low-pass filter -----------------
static void low_pass_fir(const double *in, double *out, size_t len,
                         const double *h, int M)
{
    for (size_t n = 0; n < len; ++n) {
        double sum = 0.0;
        int kmax = (n < (size_t)(M - 1)) ? (int)n : (M - 1);
        for (int k = 0; k <= kmax; ++k) {
            sum += h[k] * in[n - (size_t)k];
        }
        out[n] = sum;
    }
}

// ---------------- Heart-rate computation -----------------
static double compute_hr(const double *x, size_t len, double fs, double thr)
{
    if (!x || len < 3 || fs <= 0.0) return 0.0;
    int peaks = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        if (x[i] > x[i-1] && x[i] > x[i+1] && x[i] > thr) {
            peaks++;
            size_t skip = (size_t)(fs * 0.4);
            i += skip;
            if (i + 1 >= len) break;
        }
    }
    double dur = (double)len / fs;
    return dur > 0 ? (peaks / dur) * 60.0 : 0.0;
}

// ---------------- Main benchmark -----------------
void app_main(void)
{
    ESP_LOGI(TAG, "Starting PPG FIR benchmark build at %s %s", __DATE__, __TIME__);

    // ---- Παράμετροι ----
    const double FS = 100.0;
    const int REPEATS = 100;           // Πόσες φορές να τρέξει
    const double TH = 0.2;
    const double h_user[] = {0.1, 0.2, 0.4, 0.2, 0.1};
    const int M = sizeof(h_user) / sizeof(h_user[0]);

    static double x[MAX_SAMPLES];
    static double y[MAX_SAMPLES];

    // ---- Δημιουργία ψεύτικου PPG σήματος ----
    for (size_t i = 0; i < MAX_SAMPLES; i++) {
        double t = (double)i / FS;
        x[i] = 0.5 + 0.4 * sin(2.0 * M_PI * 1.2 * t)
                    + 0.05 * sin(2.0 * M_PI * 10.0 * t);
    }

    low_pass_fir(x, y, MAX_SAMPLES, h_user, M);
    volatile double sink = compute_hr(y, MAX_SAMPLES, FS, TH);


    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                 ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT,
                                 &freq_hz);
    double freq_mhz = freq_hz / 1e6;

    // ---- Benchmark ----
    uint64_t start_us = esp_timer_get_time();
    uint32_t start_cycles = esp_cpu_get_cycle_count();

    double sum = 0.0;
    for (int r = 0; r < REPEATS; ++r) {
        low_pass_fir(x, y, MAX_SAMPLES, h_user, M);
        double hr = compute_hr(y, MAX_SAMPLES, FS, TH);
        sum += hr;
        sink += y[r % MAX_SAMPLES];
    }

    uint32_t end_cycles = esp_cpu_get_cycle_count();
    uint64_t end_us = esp_timer_get_time();

    // ---- Αποτελέσματα ----
    uint32_t diff_cycles = end_cycles - start_cycles;
    double elapsed_ms = (end_us - start_us) / 1000.0;
    double elapsed_us_from_cycles = diff_cycles / freq_mhz;

    ESP_LOGI(TAG, "Benchmark done!");
    ESP_LOGI(TAG, "M=%d, FS=%.1f, REPEATS=%d", M, FS, REPEATS);
    ESP_LOGI(TAG, "Sum=%.3f, Sink=%.3f", sum, sink);
    ESP_LOGI(TAG, "Execution time = %.3f ms", elapsed_ms);
    ESP_LOGI(TAG, "CPU cycles = %u", diff_cycles);
    ESP_LOGI(TAG, "≈ %.3f us (from cycles @ %.1f MHz)", elapsed_us_from_cycles, freq_mhz);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
