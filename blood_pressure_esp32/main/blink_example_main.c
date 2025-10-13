#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_clk_tree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define N_SAMPLES 500
#define FS 100.0

static const char *TAG = "BENCH";

// --- Moving average filter ---
static void moving_average(const double *in, double *out, size_t len, int M)
{
    for (size_t n = 0; n < len; n++) {
        double sum = 0.0; int c = 0;
        for (int k = 0; k < M; k++) {
            if (n >= (size_t)k) { sum += in[n - k]; c++; }
        }
        out[n] = c ? (sum / c) : 0.0;
    }
}
static int find_peaks(const double *x, size_t len, double thr, int *idx_out, int max_idx)
{
    int n_peaks = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        if (x[i] > thr && x[i] > x[i - 1] && x[i] > x[i + 1]) {
            if (n_peaks < max_idx) idx_out[n_peaks++] = (int)i;
            i += 5; // μικρό skip για να μη βρει διπλά peaks
        }
    }
    return n_peaks;
}

static double compute_hr(const int *peaks, int n_peaks)
{
    if (n_peaks < 2) return 0.0;
    double mean_interval = 0.0;
    for (int i = 1; i < n_peaks; i++) {
        mean_interval += (peaks[i] - peaks[i - 1]);
    }
    mean_interval /= (n_peaks - 1);
    double hr = 60.0 * FS / mean_interval;
    return hr;
}

static double compute_ptt(const int *p1, int n1, const int *p2, int n2)
{
    if (n1 == 0 || n2 == 0) return 0.0;
    int delay = p2[0] - p1[0];   // απλή εκτίμηση (πρώτα peaks)
    return delay / FS;
}

static double compute_bp(double ptt, double a, double b)
{
    return a * ptt + b;
}


// --- Test data ---
static double x[100];
static double y[100];

void app_main(void)
{
    ESP_LOGI(TAG, "Starting benchmark build at %s %s", __DATE__, __TIME__);


    //construct fake PPG signals ( for wrist and finger)
    static double ppg_wrist[N_SAMPLES];
    static double ppg_finger[N_SAMPLES];
    const double delay_sec = 0.05; // 50 ms καθυστέρηση (PTT)
    const int delay_samples = (int)(delay_sec * FS);

    for (int i = 0; i < N_SAMPLES; i++) {
        double t = i / FS;
        double base = 0.5 + 0.5 * sin(2 * M_PI * 1.2 * t);  // 1.2 Hz (~72 bpm)
        ppg_wrist[i] = base + 0.05 * sin(2 * M_PI * 10 * t);
        int j = i - delay_samples;
        ppg_finger[i] = (j >= 0) ? ppg_wrist[j] : 0.0;
    }

    static double fw[N_SAMPLES], ff[N_SAMPLES]; //filtered signals
    moving_average(ppg_wrist, fw, N_SAMPLES, 5);
    moving_average(ppg_finger, ff, N_SAMPLES, 5);

    
    // ---- Benchmark parameters ----
    const int REPEATS = 1000;
    double a = -50.0, b = 130.0;   
    //warm up
    //0.8  --> theshold ( only peaks above 0.8 count as peaks)
    int idx1[64], idx2[64];
    int n1 = find_peaks(fw, N_SAMPLES, 0.8, idx1, 64);//peaks wrist
    int n2 = find_peaks(ff, N_SAMPLES, 0.8, idx2, 64);//peaks finger
    double ptt = compute_ptt(idx1, n1, idx2, n2);
    volatile double bp = compute_bp(ptt, a, b);

    // ---- CPU frequency ----
    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &freq_hz);
    double freq_mhz = freq_hz / 1e6;

    // ---- Start timers ----
    uint64_t start_us = esp_timer_get_time();
    uint32_t start_cycles = esp_cpu_get_cycle_count();

    // ---- Benchmark loop ----
    double sum_bp = 0.0, sum_hr = 0.0;
    for (int r = 0; r < REPEATS; r++) {
        n1 = find_peaks(fw, N_SAMPLES, 0.8, idx1, 64);
        n2 = find_peaks(ff, N_SAMPLES, 0.8, idx2, 64);
        double hr = compute_hr(idx1, n1);
        double ptt_val = compute_ptt(idx1, n1, idx2, n2);
        double bp_val = compute_bp(ptt_val, a, b);
        sum_hr += hr;
        sum_bp += bp_val;
    }

    // ---- Stop timers ----
    uint32_t end_cycles = esp_cpu_get_cycle_count();
    uint64_t end_us = esp_timer_get_time();

    uint32_t diff_cycles = end_cycles - start_cycles;
    double elapsed_ms = (end_us - start_us) / 1000.0;
    double elapsed_us_from_cycles = diff_cycles / freq_mhz;

    // ---- Results ----
    ESP_LOGI(TAG, "Benchmark done!");
    ESP_LOGI(TAG, "Samples=%d, Repeats=%d", N_SAMPLES, REPEATS);
    ESP_LOGI(TAG, "Mean HR=%.1f bpm, Mean BP=%.2f mmHg", sum_hr / REPEATS, sum_bp / REPEATS);
    ESP_LOGI(TAG, "Execution time = %.3f ms", elapsed_ms);
    ESP_LOGI(TAG, "CPU cycles = %u", diff_cycles);
    ESP_LOGI(TAG, "≈ %.3f us (from cycles @ %.1f MHz)", elapsed_us_from_cycles, freq_mhz);

    while (1) vTaskDelay(pdMS_TO_TICKS(2000));
}