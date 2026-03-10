/*
 * main.cpp — SimCoupe ESP32-P4-PC entry point
 *
 * Boot sequence:
 *   1. NVS init (app_main, LP DRAM stack — required by NVS)
 *   2. Spawn simcoupe_task (256 KB PSRAM stack)
 *   3. HDMI init  — sim_display_init()
 *   4. Audio init — sim_audio_init_with_bus(sim_display_get_i2c_bus())
 *   5. SD card    — sim_sdcard_mount()
 *   6. Keyboard   — sim_kbd_init() via Input::Init() inside Main::Init()
 *   7. SimCoupe   — Main::Init(0, nullptr) → CPU::Run() (never returns)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

// Hardware drivers
#include "sim_display.h"
#include "sim_audio.h"
#include "sim_sdcard.h"

// SimCoupe core
#include "SimCoupe.h"
#include "Main.h"
#include "CPU.h"

static const char* TAG = "simcoupe";

// Stack for the emulation task — must be in internal DRAM, NOT PSRAM.
//
// WHY NOT PSRAM: sim_display_flush() calls esp_cache_msync(DIR_C2M) to
// writeback the framebuffer. On ESP32-P4 this can transiently invalidate
// cache lines covering the active task stack if it lives in PSRAM, corrupting
// local variables and return addresses mid-function. Symptom: Load access
// fault inside fs::directory_iterator() when the GUI file browser refreshes
// its listing after Enter is pressed (SamCoupe-8mh).
//
// 32 KB DRAM is sufficient: SimCoupe's large buffers (framebuffer, ROM, RAM)
// are heap-allocated in PSRAM. The call stack depth is modest (~2-3 KB peak).
#define SIMCOUPE_TASK_STACK_BYTES  (32 * 1024)

static void simcoupe_task(void* /*arg*/)
{
    ESP_LOGI(TAG, "SimCoupe task started");

    // ── 1. HDMI display ──────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Initializing display...");
    esp_err_t err = sim_display_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sim_display_init failed: 0x%x — halting", err);
        vTaskSuspend(nullptr);
    }
    ESP_LOGI(TAG, "Display OK");

    // ── 2. Audio (shares I2C bus with display) ───────────────────────────────
    ESP_LOGI(TAG, "Initializing audio...");
    i2c_master_bus_handle_t i2c_bus = sim_display_get_i2c_bus();
    err = sim_audio_init_with_bus(i2c_bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sim_audio_init_with_bus failed: 0x%x — continuing without audio", err);
        // Non-fatal: emulator can run silently
    }
    else
    {
        sim_audio_set_volume(80);
        ESP_LOGI(TAG, "Audio OK");
    }

    // ── 3. SD card ───────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Mounting SD card...");
    err = sim_sdcard_mount();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "sim_sdcard_mount failed: 0x%x — no disk images available", err);
        // Non-fatal: emulator can run without SD card (ROM only)
    }
    else
    {
        ESP_LOGI(TAG, "SD card mounted at /sdcard");
    }

    // ── 4. SimCoupe init + emulation loop ────────────────────────────────────
    // Main::Init() calls OSD::Init(), Frame::Init(), CPU::Init(), UI::Init(),
    // Sound::Init(), Input::Init() (which calls sim_kbd_init()), Video::Init().
    // Options::Load(0, nullptr) uses defaults + /sdcard/simcoupe/SimCoupe.cfg.
    ESP_LOGI(TAG, "Starting SimCoupe...");

    // argv[0] must be a valid string (Options::Load uses it as exe path)
    static const char* argv0 = "simcoupe";
    static char* argv[] = { const_cast<char*>(argv0), nullptr };
    int argc = 1;

    if (Main::Init(argc, argv))
    {
        ESP_LOGI(TAG, "SimCoupe running");
        CPU::Run();   // Never returns under normal operation
    }
    else
    {
        ESP_LOGE(TAG, "Main::Init failed");
    }

    Main::Exit();
    ESP_LOGE(TAG, "SimCoupe exited — restarting in 5s");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

extern "C" void app_main(void)
{
    // NVS init must run on the LP DRAM stack (app_main default).
    // Erase and reinit if the partition is full or version changed.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS erasing and reinitializing");
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK)
        ESP_LOGE(TAG, "NVS init failed: 0x%x (non-fatal)", nvs_err);

    ESP_LOGI(TAG, "SimCoupe ESP32-P4-PC starting (free heap: %lu bytes)",
             (unsigned long)esp_get_free_heap_size());

    // Spawn the emulation task on Core 1, pinned away from USB (Core 0).
    //
    // Priority 15: above USB lib (10) and USB client (5), below IPC/timer (22+).
    // This ensures the Z80 emulation loop is never preempted by USB host tasks.
    //
    // Core 1: USB lib and USB client are pinned to Core 0. Pinning simcoupe
    // to Core 1 gives the Z80 a dedicated core with no USB scheduling noise.
    //
    // Stack in internal DRAM (see SIMCOUPE_TASK_STACK_BYTES comment above).
    // xTaskCreatePinnedToCore with a DRAM stack does NOT need EXT_MEM flag.
    BaseType_t ret = xTaskCreatePinnedToCore(
        simcoupe_task,
        "simcoupe",
        SIMCOUPE_TASK_STACK_BYTES,
        nullptr,
        15,
        nullptr,
        1);  /* Core 1 — dedicated to Z80 emulation */

    if (ret != pdPASS)
        ESP_LOGE(TAG, "Failed to create simcoupe task!");
}
