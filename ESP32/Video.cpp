// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: ESP32 video output via LT8912B HDMI bridge (sim_display component)
//
// Display: 640×480 RGB888 (pclk=24MHz, 60Hz) — CEA-861 VIC=1 (VGA 640×480).
// SAM framebuffer: visiblearea=1 → 544×208 pixels (512×192 + 8px SAM border).
// Scaling: 1×H, 2×V → 544×416, centred in 640×480 (OFF_X=48, OFF_Y=32).
// Aspect ratio: 4:3 exact.
//
// DST_STRIDE = 640×3 = 1920 bytes per display row. Framebuffer: 900KB.
// Offsets are computed dynamically from fb.Width()/fb.Height() so visiblearea
// can be changed at runtime without recompiling.
// m_prev is sized for the maximum framebuffer (SAM_MAX_W × SAM_MAX_H).
//
// pGuiScreen (OSD): 560×420 — rendered 1×H 1×V, centred in 640×480.

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
static constexpr int DST_W      = 640;
static constexpr int DST_H      = 480;
static constexpr int DST_STRIDE = DST_W * 3;   // 1920 bytes per row

// Maximum SAM framebuffer size (visiblearea=2: 576×268).
// m_prev is allocated for this maximum so any visiblearea fits.
static constexpr int SAM_MAX_W  = 576;
static constexpr int SAM_MAX_H  = 268;

// Offsets are computed at runtime from fb.Width()/fb.Height():
//   OFF_X = (DST_W - src_w) / 2
//   OFF_Y = (DST_H - src_h * 2) / 2
// visiblearea=1: src=544×208 → scaled 544×416 → OFF_X=48 OFF_Y=32

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

    // DRAM row buffer: one full display row (1920 bytes).
    // Border pixels zeroed at Init() — stay black permanently.
    alignas(4) uint8_t m_row_buf[DST_W * 3];   // 1920 bytes

    // Previous frame snapshot for dirty detection (DRAM).
    // Sized for maximum SAM framebuffer (SAM_MAX_W × SAM_MAX_H = 576×268).
    alignas(4) uint8_t m_prev[SAM_MAX_W * SAM_MAX_H];

    bool m_initialized  = false;
    bool m_was_gui      = false;  // true if previous frame was GUI/OSD
    int  m_clear_borders = 0;     // countdown: clear border rows for N frames after OSD exit
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
    // Offsets depend on visiblearea — approximate with frame dimensions.
    // For visiblearea=1: off_x=48, off_y=32.
    int fw = Frame::Width();
    int fh = Frame::Height();
    int ox = (DST_W - fw) / 2;
    int oy = (DST_H - fh * 2) / 2;
    x = x - ox;
    y = (y - oy) / 2;
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

    // Pre-zero m_row_buf — border pixels stay black permanently.
    memset(m_row_buf, 0, sizeof(m_row_buf));

    BuildPalette();
    memset(m_prev, 0xFF, sizeof(m_prev));
    m_initialized = true;
    ESP_LOGI(TAG, "ESP32Video: %dx%d VGA, visiblearea=%d (dynamic offsets)",
             DST_W, DST_H, GetOption(visiblearea));
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
        // DPI at 24MHz sends bytes in R,B,G order to LT8912B (hardware quirk).
        // Compensate by swapping G and B channels in the palette.
        m_palette[i].r = palette[i].red;
        m_palette[i].g = palette[i].blue;
        m_palette[i].b = palette[i].green;
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



    const int src_w = fb.Width();   // 544 normal (visiblearea=1) or 560 GUI
    const int src_h = fb.Height();  // 208 normal (visiblearea=1) or 420 GUI

    // Normal: 1×H 2×V → src_w × (src_h*2), centred in DST_W×DST_H.
    // GUI:    1×H 1×V → src_w × src_h,      centred in DST_W×DST_H.
    // Distinguish by height: GUI (pGuiScreen) is always taller than SAM_MAX_H.
    const bool is_gui = (src_h > SAM_MAX_H);

    // Dynamic offsets based on actual framebuffer size.
    const int off_x = (DST_W - src_w) / 2;                // 48 for 544, 40 for 560
    const int off_y = is_gui ? (DST_H - src_h) / 2        // GUI: 1×V centering
                             : (DST_H - src_h * 2) / 2;   // normal: 2×V centering

    // Detect GUI→normal transition.
    if (!is_gui && m_was_gui) {
        // Invalidate m_prev → forces full repaint of all 192 SAM lines.
        // Set m_clear_borders=2 so the game blit writes to BOTH FBs for
        // the next 2 frames, overwriting OSD pixels in whichever FB is back.
        memset(m_prev, 0xFF, sizeof(m_prev));
        m_clear_borders = 2;
    }
    m_was_gui = is_gui;

    if (is_gui)
    {
        // GUI: src_w x src_h -> 1xH 1xV, centred in 640x480.
        // src_w=560, src_h=420 -> gui_off_x=40, gui_off_y=30.
        const int gui_off_x = (src_w < DST_W) ? (DST_W - src_w) / 2 : 0;
        const int gui_off_y = (src_h < DST_H) ? (DST_H - src_h) / 2 : 0;
        const int gui_w = (src_w <= DST_W) ? src_w : DST_W;
        const int rows  = (src_h <= DST_H - gui_off_y) ? src_h : DST_H - gui_off_y;
        for (int sy = 0; sy < rows; ++sy)
        {
            const uint8_t* src_line = fb.GetLine(sy);
            uint8_t* p = m_row_buf + gui_off_x * 3;
            for (int sx = 0; sx < gui_w; ++sx) {
                const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
            }
            memcpy(dst + (gui_off_y + sy) * DST_STRIDE, m_row_buf, DST_STRIDE);
        }
        sim_display_flush_region((size_t)gui_off_y * DST_STRIDE,
                                 (size_t)rows * DST_STRIDE);
        sim_display_swap();
    }
    else
    {
        // Normal: src_w×src_h → 1×H 2×V → src_w×(src_h*2), centred dynamically.
        // Write dirty lines to BOTH framebuffers to keep double-buffer in sync.
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

            // Expand 1×H into active area of m_row_buf (borders stay black)
            if (vdiag) s_expand_us -= esp_timer_get_time();
            uint8_t* p = m_row_buf + off_x * 3;
            for (int sx = 0; sx < src_w; ++sx)
            {
                const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
                *p++ = e.r; *p++ = e.g; *p++ = e.b;
            }
            if (vdiag) s_expand_us += esp_timer_get_time();

            // Copy 2×V into both framebuffers
            if (vdiag) s_copy_us -= esp_timer_get_time();
            int dy0 = off_y + sy * 2;
            memcpy(dst + dy0       * DST_STRIDE, m_row_buf, DST_STRIDE);
            memcpy(dst + (dy0 + 1) * DST_STRIDE, m_row_buf, DST_STRIDE);
            if (other) {
                memcpy(other + dy0       * DST_STRIDE, m_row_buf, DST_STRIDE);
                memcpy(other + (dy0 + 1) * DST_STRIDE, m_row_buf, DST_STRIDE);
            }
            if (vdiag) s_copy_us += esp_timer_get_time();

            if (first_dirty < 0) first_dirty = sy;
            last_dirty = sy;
        }

        if (vdiag) vt1 = esp_timer_get_time();
        if (first_dirty >= 0)
        {
            int disp_first = off_y + first_dirty * 2;
            int disp_last  = off_y + last_dirty  * 2 + 2;
            size_t flush_bytes = (size_t)(disp_last - disp_first) * DST_STRIDE;

            static uint32_t s_vid_frame = 0; s_vid_frame++;
            if ((s_vid_frame % 250) == 0) {
                int64_t t0d = esp_timer_get_time();
                sim_display_flush_region((size_t)disp_first * DST_STRIDE, flush_bytes);
                int64_t t1d = esp_timer_get_time();
                ESP_LOGI("vidperf", "dirty=%d/%d msync=%lldus flush_kb=%u",
                         last_dirty - first_dirty + 1, src_h,
                         (long long)(t1d - t0d),
                         (unsigned)(flush_bytes / 1024));
            } else {
                sim_display_flush_region((size_t)disp_first * DST_STRIDE, flush_bytes);
            }
        }
        // On OSD exit: clean border pixels left by OSD in both FBs.
        if (m_clear_borders > 0) {
            --m_clear_borders;
            const size_t top_bytes = (size_t)off_y * DST_STRIDE;
            const size_t bot_off   = (size_t)(off_y + src_h * 2) * DST_STRIDE;
            const size_t bot_bytes = (size_t)DST_H * DST_STRIDE - bot_off;
            memset(dst,           0, top_bytes);
            memset(dst + bot_off, 0, bot_bytes);
            for (int row = off_y; row < off_y + src_h * 2; ++row) {
                memset(dst + row * DST_STRIDE,                           0, off_x * 3);
                memset(dst + row * DST_STRIDE + (off_x + src_w) * 3,    0, (DST_W - off_x - src_w) * 3);
            }
            esp_cache_msync(dst, (size_t)DST_W * DST_H * 3,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                            ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
                            ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }

        // Swap once per frame — present back buffer regardless of dirty count.
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
