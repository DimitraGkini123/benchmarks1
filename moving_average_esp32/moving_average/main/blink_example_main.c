#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_clk_tree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "riscv/rv_utils.h"

#include "soc/spi_mem_reg.h"
#include "esp_intr_alloc.h"
#include "soc/periph_defs.h"


static const char *TAG = "BENCH";

/* 

Αυτός ο κώδικας ενεργοποιεί ένα hardware interrupt κάθε φορά που ο επεξεργαστής (ESP32-C6) κάνει πρόσβαση στη flash μέσω του MSPI controller.
Κάθε φορά που συμβαίνει τέτοια πρόσβαση (π.χ. cache miss ή ανάγνωση εντολής από flash), αυξάνεται ένας μετρητής flash_accesses.

Έτσι μετράς πόσες φορές χρειάστηκε ο πυρήνας να “καλέσει” τη flash — δηλαδή μετράς εμμέσως cache misses ή flash fetches κατά την εκτέλεση του benchmark σου.

Ο αριθμός των flash accesses είναι περίπου ίσος με τον αριθμό των I-cache misses (εντολές που έπρεπε να φορτωθούν από flash).
*/
static volatile uint32_t flash_accesses = 0;

static void IRAM_ATTR spi0_isr(void *arg)
{
    flash_accesses++;
    REG_WRITE(SPI_MEM_INT_CLR_REG(0), SPI_MEM_MST_ST_END_INT_CLR_M);
}

static void IRAM_ATTR enable_flash_monitor(void)
{
    REG_WRITE(SPI_MEM_INT_CLR_REG(0), SPI_MEM_MST_ST_END_INT_CLR_M);
    REG_SET_BIT(SPI_MEM_INT_ENA_REG(0), SPI_MEM_MST_ST_END_INT_ENA_M);

    esp_err_t err = esp_intr_alloc(
        ETS_MSPI_INTR_SOURCE,
        ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3,   // ✅ Critical: IRAM flag
        spi0_isr,
        NULL,
        NULL
    );

    if (err == ESP_OK)
        ESP_LOGI("CACHE_MON", "Flash access interrupt enabled!");
    else
        ESP_LOGE("CACHE_MON", "Failed to enable flash monitor: %d", err);
}


// -----------------------------------------------------------
// --- Moving average filter ---
// -----------------------------------------------------------

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

// -----------------------------------------------------------
// --- Simple heart-rate estimation from peaks ---
// -----------------------------------------------------------

static double compute_hr(const double *x, size_t len, double fs, double thr)
{
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

// -----------------------------------------------------------
// --- Test data ---
// -----------------------------------------------------------

static double x[100];
static double y[100];

// -----------------------------------------------------------
// --- Main benchmark ---
// -----------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Starting benchmark build at %s %s", __DATE__, __TIME__);

    enable_flash_monitor();  // <-- Ενεργοποιεί MSPI monitor

    const size_t LEN = 100;
    const int M = 20;
    const double FS = 50.0;
    const double TH = 0.6;

    for (size_t i = 0; i < LEN; i++) {
        x[i] = 0.5 + 0.5 * sin(2 * M_PI * i / 50.0);
    }

    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &freq_hz);
    double freq_mhz = freq_hz / 1e6;

    flash_accesses = 0;  // μηδενισμός πριν το benchmark

    uint32_t start_cycles = esp_cpu_get_cycle_count();
    uint64_t start_us = esp_timer_get_time();
    uint64_t start_ins = read_instret();

    // --- Run computations ---
    moving_average_filter(x, y, LEN, M);
    double hr = compute_hr(y, LEN, FS, TH);

    uint32_t end_cycles = esp_cpu_get_cycle_count();
    uint64_t end_us = esp_timer_get_time();
    uint64_t end_ins = read_instret();


    uint32_t diff_cycles = end_cycles - start_cycles;
    double elapsed_ms = (end_us - start_us) / 1000.0;
    double elapsed_us_from_cycles = diff_cycles / freq_mhz;
    uint64_t diff_ins = end_ins - start_ins;

    // --- Results ---
    ESP_LOGI(TAG, "Benchmark done!");
    ESP_LOGI(TAG, "Estimated heart rate = %.2f bpm", hr);
    ESP_LOGI(TAG, "Execution time = %.3f ms", elapsed_ms);
    ESP_LOGI(TAG, "CPU cycles = %u", diff_cycles);
    ESP_LOGI(TAG, "≈ %.3f us (from cycles @ %.1f MHz)", elapsed_us_from_cycles, freq_mhz);
    ESP_LOGI(TAG, "Flash accesses during benchmark = %u", flash_accesses);
    ESP_LOGI("PERF", "Instructions retired = %llu", diff_ins);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
