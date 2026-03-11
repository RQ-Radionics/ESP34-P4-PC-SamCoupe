// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: ESP32 video output via LT8912B HDMI bridge (sim_display component)
//
// Display: 1024×768 RGB888 (pclk=60MHz).
// SAM framebuffer: visiblearea=0 → 512×192 pixels, 1 byte/pixel (palette index).
// Scaling: 2×H, 4×V → 1024×768 fullscreen (same 1.33:1 aspect ratio as original).
// No padding — SAM image fills the entire display area. OFF_X=0, OFF_Y=0.
//
// DST_STRIDE = 1024×3 = 3072 bytes per display row.
// Each SAM row is expanded to a 3072-byte DRAM row_buf (1024 pixels × RGB888,
// each SAM pixel doubled horizontally), then copied 4× into the framebuffer.
//
// Dirty line tracking: each SAM row is compared with the previous frame.
// Only changed rows are expanded and written to PSRAM. The flush region
// covers only the span [first_dirty .. last_dirty] (inclusive).
//
// pGuiScreen (OSD): 512×384 — rendered 2×H 2×V → 1024×768 fullscreen.

#include "SimCoupe.h"
#include "Video.h"
#include "Frame.h"
#include "SAMIO.h"

#include "sim_display.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "video";

// ── Display geometry ──────────────────────────────────────────────────────────
// Must match sdkconfig.defaults CONFIG_SIM_DISPLAY_HACT/VACT.
static constexpr int DST_W      = 1024;
static constexpr int DST_H      = 768;
static constexpr int DST_STRIDE = DST_W * 3;   // 3840 bytes per row

// SAM active area: 512×192 → 2×H 4×V → 1024×768 fullscreen
static constexpr int SAM_W      = 512;
static constexpr int SAM_H      = 192;
static constexpr int SCALED_W   = SAM_W * 2;   // 1024 (2× horizontal)
static constexpr int SCALED_H   = SAM_H * 4;   // 768  (4× vertical)
static constexpr int OFF_X      = 0;            // no horizontal padding
static constexpr int OFF_Y      = 0;            // no vertical padding

// ── ESP32Video: IVideoBase implementation ────────────────────────────────────

class ESP32Video final : public IVideoBase
{
public:
    bool Init() override;
    Rect DisplayRect() const override;
    void ResizeWindow(int height) const override { /* fixed display */ }
    std::pair<int, int> MouseRelative() override { return {0, 0}; }
    void OptionsChanged() override { BuildPalette(); memset(m_prev, 0xFF, sizeof(m_prev)); }
    void Update(const FrameBuffer& fb) override;

private:
    void BuildPalette();

    // RGB888 palette: 128 entries as packed structs for sequential access
    struct PaletteEntry { uint8_t r, g, b; };
    PaletteEntry m_palette[128]{};

    // DRAM row buffer: palette expand writes here, then 4×memcpy to PSRAM.
    // 2×H scaling: each SAM pixel written twice → DST_W pixels = 3072 bytes.
    alignas(4) uint8_t m_row_buf[DST_W * 3];   // one expanded display row (3072 bytes)

    // Previous frame snapshot for dirty detection (DRAM, 512×192 = 98304 bytes).
    // When a row is dirty it is written to BOTH framebuffers so they stay in
    // sync — dirty tracking is then safe with double buffering.
    alignas(4) uint8_t m_prev[SAM_W * SAM_H];

    bool m_initialized = false;
    bool m_was_gui     = false;  // true if previous frame was GUI/OSD
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
    x = x / 2;   // 2× horizontal scale
    y = y / 4;   // 4× vertical scale
}

void ResizeWindow(int /*height*/) { /* fixed */ }

Rect DisplayRect()
{
    if (s_video)
        return s_video->DisplayRect();
    return {0, 0, DST_W, DST_H};
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
    void* fb0 = nullptr;
    void* fb1 = nullptr;
    if (sim_display_get_framebuffer(&fb0, &fb1) != ESP_OK || !fb0)
    {
        ESP_LOGE(TAG, "sim_display not initialized");
        return false;
    }

    // Zero framebuffer(s) — ensures clean state on first frame.
    size_t fb_size = DST_W * DST_H * 3;
    memset(fb0, 0, fb_size);
    esp_cache_msync(fb0, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (fb1) {
        memset(fb1, 0, fb_size);
        esp_cache_msync(fb1, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }

    BuildPalette();
    memset(m_prev, 0xFF, sizeof(m_prev));
    m_initialized = true;
    ESP_LOGI(TAG, "ESP32Video: %dx%d display, SAM %dx%d -> 2xH 4xV -> fullscreen (dirty-track, 1024x768@60Hz)",
             DST_W, DST_H, SAM_W, SAM_H);
    return true;
}

Rect ESP32Video::DisplayRect() const
{
    return {0, 0, DST_W, DST_H};
}

void ESP32Video::BuildPalette()
{
    auto palette = IO::Palette();
    for (size_t i = 0; i < palette.size() && i < 128; ++i)
    {
        m_palette[i].r = palette[i].red;
        m_palette[i].g = palette[i].green;
        m_palette[i].b = palette[i].blue;
    }
}

void ESP32Video::Update(const FrameBuffer& fb)
{
    if (!m_initialized)
        return;

    // ── Video timing diagnostic (5 frames after frame 400) ───────────────────
    static int  s_vdiag_skip  = 400;
    static int  s_vdiag_count = 0;
    int64_t vt0 = 0, vt1 = 0, vt2 = 0;
    int64_t s_expand_us = 0, s_copy_us = 0;
    bool vdiag = (s_vdiag_skip == 0 && s_vdiag_count < 5);
    if (s_vdiag_skip > 0) s_vdiag_skip--;
    if (vdiag) vt0 = esp_timer_get_time();

    uint8_t* dst = static_cast<uint8_t*>(sim_display_get_back_buffer());
    if (!dst)
        return;



    const int src_w = fb.Width();   // 512 (normal) or 512 (GUI)
    const int src_h = fb.Height();  // 192 (normal) or 384 (GUI)

    // GUI screen (512×384): rendered 2×H 2×V → 1024×768 fullscreen.
    // Normal framebuffer (512×192): rendered 2×H 4×V → 1024×768 fullscreen.
    const bool is_gui = (src_h > SAM_H);

    // Detect GUI→normal transition: invalidate m_prev so dirty-line tracking
    // forces a full repaint of the SAM area, overwriting any GUI pixels left
    // in the framebuffer from the previous OSD frame.
    if (!is_gui && m_was_gui) {
        memset(m_prev, 0xFF, sizeof(m_prev));
    }
    m_was_gui = is_gui;

    if (is_gui)
    {
        // GUI: 512×384 source → 2×H 2×V → 1024×768 fullscreen.
        // Each source pixel written twice horizontally into m_row_buf (3072 B),
        // then each row copied twice vertically into the framebuffer.
        // src_h=384, 2×V → 768 display rows = DST_H exactly.
        const int rows = (src_h <= DST_H / 2) ? src_h : DST_H / 2;
        for (int sy = 0; sy < rows; ++sy)
        {
            const uint8_t* src_line = fb.GetLine(sy);
            // Expand 2×H into m_row_buf
            uint8_t* p = m_row_buf;
            for (int sx = 0; sx < src_w; ++sx)
            {
                const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
            }
            // Copy 2×V into framebuffer (no offset — fullscreen)
            int dy0 = sy * 2;
            memcpy(dst + dy0       * DST_STRIDE, m_row_buf, DST_STRIDE);
            memcpy(dst + (dy0 + 1) * DST_STRIDE, m_row_buf, DST_STRIDE);
        }
        // Flush entire screen and present.
        sim_display_flush_region(0, (size_t)DST_H * DST_STRIDE);
        sim_display_swap();
    }
    else
    {
        // Normal framebuffer: 512×192 → 2×H 4×V → 1024×768 fullscreen.
        //
        // Each SAM pixel written twice horizontally into m_row_buf (3072 B),
        // then the row copied 4× vertically into the framebuffer.
        //
        // Dirty-line tracking: only expand+blit rows that changed.
        // Each dirty row is written to BOTH framebuffers (back and front)
        // so both stay in sync — safe with double buffering.
        void* fb0_ptr = nullptr;
        void* fb1_ptr = nullptr;
        sim_display_get_framebuffer(&fb0_ptr, &fb1_ptr);
        uint8_t* other = (dst == (uint8_t*)fb0_ptr) ? (uint8_t*)fb1_ptr : (uint8_t*)fb0_ptr;

        int first_dirty = -1;
        int last_dirty  = -1;

        for (int sy = 0; sy < src_h; ++sy)
        {
            const uint8_t* src_line = fb.GetLine(sy);
            uint8_t* prev_line = m_prev + sy * src_w;

            if (memcmp(src_line, prev_line, src_w) == 0)
                continue;

            memcpy(prev_line, src_line, src_w);

            // Expand 2×H into m_row_buf
            if (vdiag) s_expand_us -= esp_timer_get_time();
            uint8_t* p = m_row_buf;
            for (int sx = 0; sx < src_w; ++sx)
            {
                const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
            }
            if (vdiag) s_expand_us += esp_timer_get_time();

            // Copy 4×V into framebuffer (no horizontal offset — fullscreen)
            if (vdiag) s_copy_us -= esp_timer_get_time();
            int dy0 = sy * 4;
            // Write to back buffer
            memcpy(dst + dy0       * DST_STRIDE, m_row_buf, DST_STRIDE);
            memcpy(dst + (dy0 + 1) * DST_STRIDE, m_row_buf, DST_STRIDE);
            memcpy(dst + (dy0 + 2) * DST_STRIDE, m_row_buf, DST_STRIDE);
            memcpy(dst + (dy0 + 3) * DST_STRIDE, m_row_buf, DST_STRIDE);
            // Write to front buffer too — keeps both in sync
            if (other) {
                memcpy(other + dy0       * DST_STRIDE, m_row_buf, DST_STRIDE);
                memcpy(other + (dy0 + 1) * DST_STRIDE, m_row_buf, DST_STRIDE);
                memcpy(other + (dy0 + 2) * DST_STRIDE, m_row_buf, DST_STRIDE);
                memcpy(other + (dy0 + 3) * DST_STRIDE, m_row_buf, DST_STRIDE);
            }
            if (vdiag) s_copy_us += esp_timer_get_time();

            if (first_dirty < 0) first_dirty = sy;
            last_dirty = sy;
        }

        if (vdiag) vt1 = esp_timer_get_time();
        if (first_dirty >= 0)
        {
            int disp_first = first_dirty * 4;
            int disp_last  = last_dirty  * 4 + 4;
            sim_display_flush_region((size_t)disp_first * DST_STRIDE,
                                     (size_t)(disp_last - disp_first) * DST_STRIDE);
        }
        // Swap once per frame — present back buffer regardless of dirty count.
        // (On a fully static frame with no dirty lines the display content is
        //  unchanged, but we still swap so s_back_buf stays in sync.)
        sim_display_swap();
        if (vdiag) {
            vt2 = esp_timer_get_time();
            ESP_LOGI(TAG, "video frame %d: expand=%lld us  copy2psram=%lld us  flush=%lld us  total=%lld us",
                     s_vdiag_count,
                     (long long)s_expand_us,
                     (long long)s_copy_us,
                     (long long)(vt2 - vt1),
                     (long long)(vt2 - vt0));
            s_vdiag_count++;
        }
    }
}
