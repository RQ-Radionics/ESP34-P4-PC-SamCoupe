// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: ESP32 video output via LT8912B HDMI bridge (sim_display component)
//
// The SAM Coupé framebuffer is 512×384 pixels, 1 byte/pixel (palette index 0-127).
// The display is 1024×768 — exactly 2× in both dimensions.
// We do a palette lookup to RGB888 and write directly into the sim_display back buffer.
//
// sim_display back buffer layout: 1024×768 × 3 bytes/pixel = 2,359,296 bytes
// (RGB888, row-major, no padding)

#include "SimCoupe.h"
#include "Video.h"
#include "Frame.h"
#include "SAMIO.h"

#include "sim_display.h"
#include "esp_log.h"
#include "esp_cache.h"
#include <algorithm>

static const char* TAG = "video";

// ── ESP32Video: IVideoBase implementation ────────────────────────────────────

class ESP32Video final : public IVideoBase
{
public:
    bool Init() override;
    Rect DisplayRect() const override;
    void ResizeWindow(int height) const override { /* fixed display */ }
    std::pair<int, int> MouseRelative() override { return {0, 0}; }
    void OptionsChanged() override { BuildPalette(); }
    void Update(const FrameBuffer& fb) override;

private:
    void BuildPalette();

    // RGB888 palette: 128 entries × 3 bytes (R, G, B)
    uint8_t m_palette_r[128]{};
    uint8_t m_palette_g[128]{};
    uint8_t m_palette_b[128]{};

    bool m_initialized = false;
};

// ── Video namespace (called by SimCoupe core) ─────────────────────────────────

static std::unique_ptr<IVideoBase> s_video;

namespace Video
{

bool Init()
{
    Exit();
    auto v = std::make_unique<ESP32Video>();
    if (!v->Init())
        return false;
    s_video = std::move(v);
    return true;
}

void Exit()
{
    s_video.reset();
}

void NativeToSam(int& x, int& y)
{
    // 2× scaling: divide by 2
    x /= 2;
    y /= 2;
}

void ResizeWindow(int /*height*/) { /* fixed */ }

Rect DisplayRect()
{
    if (s_video)
        return s_video->DisplayRect();
    return {0, 0, 1024, 768};
}

std::pair<int, int> MouseRelative()
{
    if (s_video)
        return s_video->MouseRelative();
    return {0, 0};
}

void OptionsChanged()
{
    if (s_video)
        s_video->OptionsChanged();
}

void Update(const FrameBuffer& fb)
{
    if (s_video)
        s_video->Update(fb);
}

} // namespace Video

// ── UI::CreateVideo (called from UI.cpp) ──────────────────────────────────────
// Defined here to avoid a separate translation unit.

#include "UI.h"

std::unique_ptr<IVideoBase> UI::CreateVideo()
{
    auto v = std::make_unique<ESP32Video>();
    if (v->Init())
        return v;
    return nullptr;
}

// ── ESP32Video implementation ─────────────────────────────────────────────────

bool ESP32Video::Init()
{
    // sim_display_init() must already have been called by app_main
    void* fb0 = nullptr;
    void* fb1 = nullptr;
    if (sim_display_get_framebuffer(&fb0, &fb1) != ESP_OK || !fb0)
    {
        ESP_LOGE(TAG, "sim_display not initialized");
        return false;
    }

    BuildPalette();
    m_initialized = true;
    ESP_LOGI(TAG, "ESP32Video initialized (512x384 -> 1024x768 2x)");
    return true;
}

Rect ESP32Video::DisplayRect() const
{
    return {0, 0, 1024, 768};
}

void ESP32Video::BuildPalette()
{
    auto palette = IO::Palette();
    for (size_t i = 0; i < palette.size() && i < 128; ++i)
    {
        m_palette_r[i] = palette[i].red;
        m_palette_g[i] = palette[i].green;
        m_palette_b[i] = palette[i].blue;
    }
}

void ESP32Video::Update(const FrameBuffer& fb)
{
    if (!m_initialized)
        return;

    uint8_t* dst = static_cast<uint8_t*>(sim_display_get_back_buffer());
    if (!dst)
        return;

    const int src_w = fb.Width();
    const int src_h = fb.Height();
    const int dst_stride = 1024 * 3; // bytes per output row

    // Detect if this is the GUI screen (512×768) or SAM framebuffer (512×384)
    // GUI screen is created as width × (height*2), so height is doubled
    if (src_h == 768 && src_w == 512)
    {
        // This is pGuiScreen (512×768) — already 2× scaled vertically
        // Just copy directly to display (1024×768), scaling horizontally 2×
        for (int sy = 0; sy < src_h && sy < 768; ++sy)
        {
            const uint8_t* src_line = fb.GetLine(sy);
            uint8_t* dst_row = dst + sy * dst_stride;

            for (int sx = 0; sx < src_w; ++sx)
            {
                uint8_t idx = src_line[sx] & 0x7F;
                uint8_t r = m_palette_r[idx];
                uint8_t g = m_palette_g[idx];
                uint8_t b = m_palette_b[idx];

                // Write 2 pixels horizontally
                *dst_row++ = r; *dst_row++ = g; *dst_row++ = b;
                *dst_row++ = r; *dst_row++ = g; *dst_row++ = b;
            }
        }
    }
    else
    {
        // This is pFrameBuffer (512×384) — scale 2× in both dimensions
        const int scale_h = std::min(src_h, 384);
        for (int sy = 0; sy < scale_h; ++sy)
        {
            const uint8_t* src_line = fb.GetLine(sy);

            // Two output rows for this source row
            uint8_t* dst_row0 = dst + (sy * 2)     * dst_stride;
            uint8_t* dst_row1 = dst + (sy * 2 + 1) * dst_stride;

            uint8_t* p0 = dst_row0;
            uint8_t* p1 = dst_row1;

            for (int sx = 0; sx < src_w; ++sx)
            {
                uint8_t idx = src_line[sx] & 0x7F;
                uint8_t r = m_palette_r[idx];
                uint8_t g = m_palette_g[idx];
                uint8_t b = m_palette_b[idx];

                // Write pixel at (2*sx, 2*sy)
                *p0++ = r; *p0++ = g; *p0++ = b;
                // Write pixel at (2*sx+1, 2*sy)
                *p0++ = r; *p0++ = g; *p0++ = b;

                // Write pixel at (2*sx, 2*sy+1)
                *p1++ = r; *p1++ = g; *p1++ = b;
                // Write pixel at (2*sx+1, 2*sy+1)
                *p1++ = r; *p1++ = g; *p1++ = b;
            }
        }
    }

    sim_display_flush();
}
