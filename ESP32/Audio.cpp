// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: ESP32 audio output via ES8311 codec (sim_audio component)
//
// SimCoupe calls:
//   Audio::Init()         — called once at startup
//   Audio::Exit()         — called at shutdown
//   Audio::AddData()      — called every frame with stereo int16_t PCM
//
// We delegate to sim_audio which owns the ES8311 + I2S driver.
// sim_display_init() must have been called first so we can share its I2C bus.

#include "SimCoupe.h"
#include "Audio.h"
#include "Options.h"
#include "Sound.h"

#include "sim_audio.h"
#include "sim_display.h"

#include "esp_log.h"

static const char* TAG = "audio";

static bool s_initialized    = false;
static bool s_externally_owned = false;  // true if main.cpp owns the audio lifecycle

bool Audio::Init()
{
    // If sim_audio was already initialized by main.cpp (sharing the display
    // I2C bus), adopt it without re-initializing — and mark it as externally
    // owned so Audio::Exit() won't tear it down (Sound::Init calls Exit first,
    // which would delete the I2S channel while DMA is still running).
    if (sim_audio_get_tx_handle() != nullptr)
    {
        s_initialized     = true;
        s_externally_owned = true;
        return true;
    }

    Exit();

    // sim_display_init() must already have been called — share its I2C bus
    i2c_master_bus_handle_t bus = sim_display_get_i2c_bus();
    esp_err_t err;
    if (bus)
    {
        err = sim_audio_init_with_bus(bus);
    }
    else
    {
        // Fallback: create own I2C bus (display not yet initialized)
        err = sim_audio_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sim_audio_init failed: 0x%x", err);
        return false;
    }

    sim_audio_set_volume(80);
    s_initialized     = true;
    s_externally_owned = false;
    return true;
}

void Audio::Exit()
{
    if (!s_initialized)
        return;

    if (s_externally_owned)
    {
        // main.cpp owns the I2S/codec lifecycle — don't tear it down.
        // Sound::Init() calls Exit() before re-initializing; if we called
        // sim_audio_deinit() here the DMA ISR would fire with a deleted
        // channel handle (msg_queue == NULL) and assert in FreeRTOS.
        ESP_LOGD(TAG, "Audio::Exit — externally owned, skipping deinit");
        s_initialized = false;
        return;
    }

    sim_audio_deinit();
    s_initialized = false;
}

// Called every frame with stereo 16-bit PCM.
// pData_ points to int16_t samples (L, R, L, R, ...).
// len_bytes is the byte count (num_samples * 2).
// Returns a fill ratio [0.0, 1.0] — we approximate from the write result.
float Audio::AddData(uint8_t* pData_, int len_bytes)
{
    if (!s_initialized || !pData_ || len_bytes <= 0)
        return 0.0f;

    int num_samples = len_bytes / sizeof(int16_t);
    int written = sim_audio_write(
        reinterpret_cast<const int16_t*>(pData_),
        num_samples,
        /*timeout_ms=*/20);   // ~1 frame at 50 Hz

    if (written < 0)
        return 0.0f;

    // Return approximate fill ratio based on how much was accepted
    // (sim_audio_write blocks until the DMA ring has room, so this is
    //  mostly informational — SimCoupe uses it for speed throttling)
    return static_cast<float>(written) / static_cast<float>(num_samples);
}
