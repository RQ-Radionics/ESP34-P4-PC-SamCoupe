// Part of SimCoupe - A SAM Coupe emulator
//
// Video.cpp: ESP32 video output via LT8912B HDMI bridge (sim_display component)
//
// Display: 512×480 RGB888 (pclk=30MHz, ~71Hz refresh).
// SAM framebuffer: visiblearea=0 → 512×192 pixels, 1 byte/pixel (palette index).
// Scaling: 1×H, 2×V → 512×384 image centred vertically in 512×480.
// Padding: 0px left/right (exact fit), 48px black top/bottom.
//
// With DST_W=512 = SAM_W, there is NO horizontal padding.
// DST_STRIDE = 512×3 = 1536 bytes = exactly one expanded SAM row.
// Two vertically-doubled rows (dy0, dy1) are contiguous in the framebuffer.
//
// Async pipeline (Core 1 / Core 0 overlap):
//   Core 1: ExecuteChunk(N) → Frame::End(N) → Video::Update(fb):
//             wait video_done → snapshot fb → give video_start → return
//   Core 0: video_task: wait video_start → expand+copy+flush → give video_done
//
// This hides the 13.6ms blit behind the 12.5ms Z80 execution of the next frame.
// Core 1 waits for video_done at the TOP of Video::Update() — before Frame::End()
// calls Update() for frame N+1 — so the snapshot buffer is never written while
// Core 0 is still reading it.
//
// pGuiScreen (OSD): 512×384 — rendered synchronously (rare, not on hot path).
//
// Framebuffer bandwidth: 512×480×3 = 737,280 bytes (vs 921,600 at 640×480).

#include "SimCoupe.h"
#include "Video.h"
#include "Frame.h"
#include "SAMIO.h"

#include "sim_display.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "video";

// ── Display geometry ──────────────────────────────────────────────────────────
// Must match sdkconfig.defaults CONFIG_SIM_DISPLAY_HACT/VACT.
static constexpr int DST_W      = 512;
static constexpr int DST_H      = 480;
static constexpr int DST_STRIDE = DST_W * 3;   // 1536 bytes per row

// SAM active area: 512×192 → 2×V → 512×384, centred in 512×480
static constexpr int SAM_W      = 512;
static constexpr int SAM_H      = 192;
static constexpr int SCALED_H   = SAM_H * 2;   // 384
static constexpr int OFF_Y      = (DST_H - SCALED_H) / 2;  // 48

// ── Async video task (Core 0) ─────────────────────────────────────────────────

// Snapshot buffer: Core 1 copies pFrameBuffer here, then Core 0 reads it.
// Size: SAM_W × SAM_H = 512 × 192 = 98,304 bytes. Allocated in DRAM.
static constexpr int SNAPSHOT_BYTES = SAM_W * SAM_H;
static uint8_t s_snapshot[SNAPSHOT_BYTES];
static int     s_snapshot_w = SAM_W;
static int     s_snapshot_h = SAM_H;

static SemaphoreHandle_t s_video_start = nullptr;
static SemaphoreHandle_t s_video_done  = nullptr;

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

    // Called from video_task on Core 0
    void DoUpdate();

private:
    void BuildPalette();
    void BlitNormal(uint8_t* dst);
    void BlitGui(uint8_t* dst, const FrameBuffer& fb);

    // RGB888 palette: 128 entries as packed structs for sequential access
    struct PaletteEntry { uint8_t r, g, b; };
    PaletteEntry m_palette[128]{};

    // DRAM row buffer: palette expand writes here, then 2×memcpy to PSRAM.
    alignas(4) uint8_t m_row_buf[DST_STRIDE];  // one expanded row (1536 bytes)

    bool m_initialized = false;
};

// ── Video namespace (called by SimCoupe core) ─────────────────────────────────

static std::unique_ptr<ESP32Video> s_video;

// Forward declaration for video_task
static void video_task(void* arg);

static void video_task_init()
{
    s_video_start = xSemaphoreCreateBinary();
    s_video_done  = xSemaphoreCreateBinary();
    configASSERT(s_video_start);
    configASSERT(s_video_done);

    // Pre-give s_video_done so the very first frame doesn't stall.
    xSemaphoreGive(s_video_done);

    // Pin to Core 0, priority 13 (below sound_task prio 14).
    // Both video and sound run on Core 0; sound has priority so it starts
    // immediately when signalled, video fills the remaining Core 0 time.
    xTaskCreatePinnedToCore(
        video_task,
        "video_task",
        4096,       // 4KB stack — blit loop is not deep
        nullptr,
        13,         // priority 13
        nullptr,
        0           // Core 0
    );
}

static void video_task(void* /*arg*/)
{
    for (;;) {
        xSemaphoreTake(s_video_start, portMAX_DELAY);
        // Use s_video at call time — safe because Core 1 holds s_video_done
        // (i.e., waits for us) before it can destroy/replace s_video.
        if (s_video)
            s_video->DoUpdate();
        xSemaphoreGive(s_video_done);
    }
}

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
    // Note: video_task is not stopped on exit (same pattern as sound_task).
    // In practice, Exit() is only called on shutdown/reinit.
    s_video.reset();
}

void NativeToSam(int& x, int& y)
{
    // No horizontal offset (DST_W == SAM_W); undo 2×V scaling and top pad
    y = (y - OFF_Y) / 2;
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

    // Zero both framebuffers once — top/bottom padding rows stay black forever.
    // DoUpdate() only writes the active area [OFF_Y .. OFF_Y+SCALED_H).
    size_t fb_size = DST_W * DST_H * 3;
    memset(fb0, 0, fb_size);
    memset(fb1, 0, fb_size);
    esp_cache_msync(fb0, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync(fb1, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    BuildPalette();
    m_initialized = true;

    // Start the async video task on Core 0 — only once across all Init() calls.
    static bool s_task_started = false;
    if (!s_task_started) {
        video_task_init();
        s_task_started = true;
    }

    ESP_LOGI(TAG, "ESP32Video: %dx%d display, SAM %dx%d -> 1xH 2xV -> pad top/bot %dpx (async Core 0)",
             DST_W, DST_H, SAM_W, SAM_H, OFF_Y);
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

// ── Video::Update — called from Core 1 (Frame::End) ──────────────────────────
//
// For the normal framebuffer (512×192):
//   1. Wait for Core 0 to finish blitting the PREVIOUS frame (video_done).
//   2. Snapshot the FrameBuffer into s_snapshot (DRAM→DRAM, ~0.3ms).
//   3. Signal Core 0 to start blitting this frame (video_start).
//   4. Return immediately — Core 1 proceeds with ExecuteChunk(N+1).
//
// For the GUI screen (512×384, rare):
//   Rendered synchronously — GUI is not on the hot path.
//
void ESP32Video::Update(const FrameBuffer& fb)
{
    if (!m_initialized)
        return;

    const int src_w = fb.Width();
    const int src_h = fb.Height();
    const bool is_gui = (src_h > SAM_H);

    if (is_gui)
    {
        // GUI path: synchronous, rare. Wait for any in-flight blit to finish
        // so we don't race on the display framebuffer.
        if (s_video_done)
            xSemaphoreTake(s_video_done, portMAX_DELAY);

        uint8_t* dst = static_cast<uint8_t*>(sim_display_get_back_buffer());
        if (dst)
            BlitGui(dst, fb);

        if (s_video_done)
            xSemaphoreGive(s_video_done);
        return;
    }

    // Normal path: async.
    // Step 1: wait for Core 0 to finish the previous frame's blit.
    if (s_video_start)
        xSemaphoreTake(s_video_done, portMAX_DELAY);

    // Step 2: snapshot the FrameBuffer (DRAM→DRAM).
    // The snapshot is SAM_W × src_h bytes. src_h is always SAM_H=192 here.
    const int snap_h = (src_h <= SAM_H) ? src_h : SAM_H;
    for (int y = 0; y < snap_h; ++y)
        memcpy(s_snapshot + y * src_w, fb.GetLine(y), src_w);
    s_snapshot_w = src_w;
    s_snapshot_h = snap_h;

    // Step 3: signal Core 0 to blit.
    if (s_video_start)
        xSemaphoreGive(s_video_start);
    // Core 1 returns here and immediately starts ExecuteChunk(N+1).
}

// ── ESP32Video::DoUpdate — called from video_task on Core 0 ──────────────────
//
// Reads s_snapshot (DRAM), expands palette, writes to PSRAM display buffer.
//
void ESP32Video::DoUpdate()
{
    uint8_t* dst = static_cast<uint8_t*>(sim_display_get_back_buffer());
    if (!dst)
        return;

    // ── Video timing diagnostic (5 frames after frame 400) ───────────────────
    static int  s_vdiag_skip  = 400;
    static int  s_vdiag_count = 0;
    bool vdiag = (s_vdiag_skip == 0 && s_vdiag_count < 5);
    if (s_vdiag_skip > 0) s_vdiag_skip--;

    int64_t t0 = vdiag ? esp_timer_get_time() : 0;
    BlitNormal(dst);
    int64_t t1 = vdiag ? esp_timer_get_time() : 0;
    sim_display_flush_region((size_t)OFF_Y * DST_STRIDE,
                             (size_t)SCALED_H * DST_STRIDE);
    if (vdiag) {
        int64_t t2 = esp_timer_get_time();
        ESP_LOGI(TAG, "video frame %d: blit=%lld us  flush=%lld us  total=%lld us",
                 s_vdiag_count,
                 (long long)(t1 - t0),
                 (long long)(t2 - t1),
                 (long long)(t2 - t0));
        s_vdiag_count++;
    }
}

void ESP32Video::BlitNormal(uint8_t* dst)
{
    const int src_w = s_snapshot_w;
    const int src_h = s_snapshot_h;

    for (int sy = 0; sy < src_h; ++sy)
    {
        const uint8_t* src_line = s_snapshot + sy * src_w;

        // Expand one SAM row into DRAM row_buf
        uint8_t* p = m_row_buf;
        for (int sx = 0; sx < src_w; ++sx)
        {
            const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
            *p++ = e.r; *p++ = e.g; *p++ = e.b;
        }

        // Copy to both consecutive PSRAM rows
        int dy0 = OFF_Y + sy * 2;
        memcpy(dst + dy0 * DST_STRIDE,       m_row_buf, DST_STRIDE);
        memcpy(dst + (dy0+1) * DST_STRIDE,   m_row_buf, DST_STRIDE);
    }
}

void ESP32Video::BlitGui(uint8_t* dst, const FrameBuffer& fb)
{
    const int src_w = fb.Width();
    const int src_h = fb.Height();
    const int rows = (src_h <= DST_H - OFF_Y) ? src_h : DST_H - OFF_Y;
    for (int sy = 0; sy < rows; ++sy)
    {
        const uint8_t* src_line = fb.GetLine(sy);
        uint8_t* p = dst + (OFF_Y + sy) * DST_STRIDE;
        for (int sx = 0; sx < src_w; ++sx)
        {
            const PaletteEntry& e = m_palette[src_line[sx] & 0x7F];
            *p++ = e.r; *p++ = e.g; *p++ = e.b;
        }
    }
    sim_display_flush_region((size_t)OFF_Y * DST_STRIDE,
                             (size_t)rows * DST_STRIDE);
}
