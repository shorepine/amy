// reload_stress: hardware repro for the patch-reload crash (#1185, #1190)
// on the ESP32-P4X-Function-EV-Board.
//
// Mirrors the #1185 report: AMY renders in its own tasks (I2S out, multicore,
// multithread), and a task on the other core reloads a synth's patch by
// sending wire-format messages (amy_add_message -> parser ->
// patches_load_patch), holding notes so the old voices are live when they are
// released. #1185 saw a Store access fault in reset_osc on about one reload in
// six, and 30 clean reloads with its fix.
//
// Every reload is logged, so the reload that faults can be read off the
// serial log. After a panic the board reboots and starts a fresh run, so
// leaving it running collects many runs' worth of reloads-to-first-fault;
// capture.py tallies them.
//
// Nothing needs to be connected to the I2S pins, but I2S must be enabled:
// without it AMY renders on the caller's thread and the race cannot happen.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "amy.h"

// Modes (set with idf.py -DSTRESS_MODE=1 build, see CMakeLists.txt):
//   0  PARALLEL: the #1185 protocol - 30 reloads, 500 ms apart.
//   1  STRESS:   1000 reloads back to back, to shake out the rarer
//                render-side race (a FREE_OSC run by the sending thread's
//                flush while the render tasks are reading the osc).
#ifndef STRESS_MODE
#define STRESS_MODE 0
#endif
#if STRESS_MODE == 0
#define STRESS_RELOADS 30
#define STRESS_GAP_MS 500
#else
#define STRESS_RELOADS 1000
#define STRESS_GAP_MS 0
#endif

// AMY's fill-buffer task runs on core 1 (AMY_FILL_BUFFER_TASK_COREID) and the
// render is split across both cores; the reloads come from core 0, as the
// application core did in #1185.
#define STRESS_CORE 0

// I2S output as wired on the ESP32-P4X-Function-EV-Board this is run on:
// output only, so no MCLK or DIN (they stay at amy_default_config's -1).
// Nothing needs to be connected, but the I2S DMA must run: it paces the
// render tasks as in a real app.
#define I2S_BCLK 21
#define I2S_LRC 22
#define I2S_DOUT 23

// Patches with different oscs-per-voice, so the new voice lands on osc
// numbers the old one is freeing: Juno 1, DX7 130, Juno 20.
static const char *patches[] = {"i1iv6K1Z", "i1iv6K130Z", "i1iv4K20Z"};

static unsigned long ms_since(int64_t t0_us) {
    return (unsigned long)((esp_timer_get_time() - t0_us) / 1000);
}

static void stress_task(void *arg) {
    (void)arg;
    char m[32];
    int64_t t0 = esp_timer_get_time();
    for (int i = 1; i <= STRESS_RELOADS; ++i) {
        amy_add_message((char *)patches[i % 3]);
        for (int k = 0; k < 4; ++k) {
            snprintf(m, sizeof m, "i1n%dl1Z", 48 + 3 * k);
            amy_add_message(m);
        }
        if (STRESS_MODE == 0 || i % 50 == 0)
            printf("RELOAD %d ms=%lu heap=%u\n", i, ms_since(t0),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
        if (STRESS_GAP_MS) vTaskDelay(pdMS_TO_TICKS(STRESS_GAP_MS));
    }
    printf("STRESS_DONE reloads=%d ms=%lu\n", STRESS_RELOADS, ms_since(t0));
    vTaskDelete(NULL);
}

void app_main(void) {
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_I2S;
    c.i2s_bclk = I2S_BCLK;
    c.i2s_lrc = I2S_LRC;
    c.i2s_dout = I2S_DOUT;
    amy_start(c);
    printf("STRESS_START mode=%d reloads=%d core=%d gap_ms=%d reset_reason=%d\n",
           STRESS_MODE, STRESS_RELOADS, STRESS_CORE, STRESS_GAP_MS,
           (int)esp_reset_reason());
    xTaskCreatePinnedToCore(stress_task, "reload_stress", 8192, NULL, 5, NULL,
                            STRESS_CORE);
}
