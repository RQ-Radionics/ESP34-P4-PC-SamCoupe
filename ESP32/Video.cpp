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

    // Display is always 1024×768 RGB888.
    // The SAM framebuffer (pFrameBuffer) is src_w × src_h pixels (palette indexed).
    // The GUI screen (pGuiScreen) is src_w × (src_h*2) — already vertically doubled.
    //
    // We scale 2× horizontally always. Vertically:
    //   - pFrameBuffer: scale 2× (each source row → 2 display rows)
    //   - pGuiScreen:   1× (already doubled by Frame::End memcpy)
    //
    // The framebuffer width varies with visiblearea setting:
    //   visiblearea=0: 512px  → 1024px (exact fit)
    //   visiblearea=1: 544px  → 1088px (overflows 64px)
    //   visiblearea=2: 576px  → 1152px (overflows 128px)
    //   visiblearea=3: 608px  → 1216px (overflows 192px)
    //
    // To handle all cases correctly: centre the scaled image in 1024×768,
    // clipping pixels that fall outside the display bounds.

    const int DST_W = 1024;
    const int DST_H = 768;
    const int dst_stride = DST_W * 3;

    const int src_w = fb.Width();
    const int src_h = fb.Height();

    // Determine if this is pGuiScreen (height already doubled) or pFrameBuffer.
    // pGuiScreen is created as (width, height*2) where height = frame height.
    // pFrameBuffer is created as (width, height).
    // We detect by checking if src_h > DST_H/2 — GUI screen is always taller.
    const bool is_gui = (src_h > DST_H / 2);

    // Scaled output dimensions
    const int scaled_w = src_w * 2;
    const int scaled_h = is_gui ? src_h : src_h * 2;

    // Centre offset: how many display pixels to skip on left/top
    // (negative if scaled image is smaller than display — pad with black)
    const int off_x = (DST_W - scaled_w) / 2;  // may be negative (clip) or positive (pad)
    const int off_y = (DST_H - scaled_h) / 2;

    // Source pixel range to render (clip to display bounds)
    // src_x_start: first source column whose scaled output is >= 0
    // src_x_end:   first source column whose scaled output is >= DST_W
    const int src_x_start = (off_x < 0) ? (-off_x + 1) / 2 : 0;
    const int src_x_end   = std::min(src_w, (DST_W - std::max(off_x, 0) + 1) / 2);

    // Destination X of first rendered source pixel
    const int dst_x_start = std::max(off_x, 0) + (src_x_start * 2);

    // Clear left/right padding columns if image is narrower than display
    if (off_x > 0)
    {
        for (int dy = 0; dy < DST_H; ++dy)
        {
            memset(dst + dy * dst_stride, 0, off_x * 3);
            int right_start = (off_x + scaled_w) * 3;
            if (right_start < dst_stride)
                memset(dst + dy * dst_stride + right_start, 0, dst_stride - right_start);
        }
    }

    // Render source rows
    const int src_y_start = (off_y < 0) ? (-off_y + (is_gui ? 1 : 0)) / (is_gui ? 1 : 2) : 0;
    const int src_y_end   = is_gui
        ? std::min(src_h, DST_H - std::max(off_y, 0))
        : std::min(src_h, (DST_H - std::max(off_y, 0) + 1) / 2);

    for (int sy = src_y_start; sy < src_y_end; ++sy)
    {
        const uint8_t* src_line = fb.GetLine(sy);

        if (is_gui)
        {
            // GUI screen: 1 source row → 1 display row
            int dy = off_y + sy;
            if (dy < 0 || dy >= DST_H) continue;

            uint8_t* dst_row = dst + dy * dst_stride + dst_x_start * 3;

            // Clear left pad for this row if needed
            if (off_x > 0)
                memset(dst + dy * dst_stride, 0, off_x * 3);

            for (int sx = src_x_start; sx < src_x_end; ++sx)
            {
                uint8_t idx = src_line[sx] & 0x7F;
                uint8_t r = m_palette_r[idx];
                uint8_t g = m_palette_g[idx];
                uint8_t b = m_palette_b[idx];
                *dst_row++ = r; *dst_row++ = g; *dst_row++ = b;
                *dst_row++ = r; *dst_row++ = g; *dst_row++ = b;
            }
        }
        else
        {
            // Normal framebuffer: 1 source row → 2 display rows
            int dy0 = off_y + sy * 2;
            int dy1 = dy0 + 1;

            uint8_t* dst_row0 = (dy0 >= 0 && dy0 < DST_H) ? dst + dy0 * dst_stride + dst_x_start * 3 : nullptr;
            uint8_t* dst_row1 = (dy1 >= 0 && dy1 < DST_H) ? dst + dy1 * dst_stride + dst_x_start * 3 : nullptr;

            for (int sx = src_x_start; sx < src_x_end; ++sx)
            {
                uint8_t idx = src_line[sx] & 0x7F;
                uint8_t r = m_palette_r[idx];
                uint8_t g = m_palette_g[idx];
                uint8_t b = m_palette_b[idx];

                if (dst_row0) { *dst_row0++ = r; *dst_row0++ = g; *dst_row0++ = b; *dst_row0++ = r; *dst_row0++ = g; *dst_row0++ = b; }
                if (dst_row1) { *dst_row1++ = r; *dst_row1++ = g; *dst_row1++ = b; *dst_row1++ = r; *dst_row1++ = g; *dst_row1++ = b; }
            }
        }
    }

    // Clear top/bottom padding rows if image is shorter than display
    if (off_y > 0)
    {
        for (int dy = 0; dy < off_y && dy < DST_H; ++dy)
            memset(dst + dy * dst_stride, 0, dst_stride);
        int bottom_start = off_y + scaled_h;
        for (int dy = bottom_start; dy < DST_H; ++dy)
            memset(dst + dy * dst_stride, 0, dst_stride);
    }

    sim_display_flush();
}
