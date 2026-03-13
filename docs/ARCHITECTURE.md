# Architecture: SimCoupe ESP32-P4-PC Port

## Overview

SimCoupe is an SDL2-based emulator. This port replaces all SDL subsystems with ESP-IDF native drivers while keeping the SimCoupe core (Z80, memory, I/O, sound synthesis) unchanged.

```
┌─────────────────────────────────────────────────────────┐
│                    SimCoupe Core                        │
│  CPU (Z80) · Frame · Memory · SAMIO · Sound · GUI       │
│  (unchanged from upstream — Base/ and SAASound/)        │
└────────────────┬────────────────────────────────────────┘
                 │ calls platform HAL
┌────────────────▼────────────────────────────────────────┐
│               ESP32 HAL  (ESP32/)                       │
│  Video.cpp  Audio.cpp  Input.cpp  OSD.cpp  UI.cpp       │
│  (replaces SDL/  — same interface, ESP-IDF backend)     │
└──────┬──────────┬──────────┬──────────┬─────────────────┘
       │          │          │          │
  sim_display  sim_audio  sim_kbd   sim_sdcard
  (component)  (component)(component)(component)
       │          │          │          │
   LT8912B     ES8311    USB HID     SDMMC
   HDMI bridge  codec    keyboard    FAT32
```

## FreeRTOS Task Layout

```
Core 0                          Core 1
──────────────────────          ──────────────────────
prio 22+  esp_timer / ipc       prio 22+  esp_timer / ipc
prio 14   sound_task            prio 15   simcoupe_task
prio 13   (video_task, unused)            Z80 + Frame::End
prio 10   usb_lib_task                    + IO::FrameUpdate
prio  5   usb_client_task
prio  0   IDLE0                 prio  0   IDLE1
```

**simcoupe_task** (Core 1, prio 15, 32 KB DRAM stack):
- Runs the main emulation loop: `CPU::Run()` → `ExecuteChunk()` → `Frame::End()` → `IO::FrameUpdate()`
- Pinned to Core 1 to avoid USB scheduling interference

**sound_task** (Core 0, prio 14, 4 KB stack):
- Runs `Sound::FrameUpdate()` (SAASound synthesis) overlapped with Z80 execution
- Semaphore pipeline: `s_sound_start` (Core1→Core0) / `s_sound_done` (Core0→Core1)

## Data Flows

### Video

```
Z80 writes to SAM RAM (PSRAM)
        │
        ▼
Frame::UpdateLine()  ← called per scan line during Z80 execution
        │  writes palette indices to FrameBuffer (PSRAM heap)
        ▼
Frame::End() → Video::Update(FrameBuffer)
        │
        ▼  ESP32Video::Update()  [ESP32/Video.cpp]
        │
        ├─ dirty check: memcmp(row, prev_row, 512)  ← DRAM, fast
        │   skip if unchanged (typical: 0/192 dirty on static screens)
        │
        ├─ expand: palette[index] → RGB888 → m_row_buf  ← DRAM
        │
        ├─ blit: memcpy(m_row_buf → PSRAM display buffer) × 2 rows
        │
        └─ flush: esp_cache_msync(DIR_C2M) → LT8912B DMA → HDMI
```

Display: 512×480 RGB888 @ 30 MHz pixel clock (~71 Hz). SAM active area (512×192) is scaled 2× vertically to 512×384, centred with 48 px black top/bottom padding.

### Audio

```
Z80 writes to SAA-1099 registers (via IO::Out())
        │
        ▼
Sound::FrameUpdate()  [Core 0, sound_task]
        │
        ▼  SAASound::GenerateMany(buffer, 441 samples)
        │   CSAADevice::_TickAndOutputStereo() × 441 × 4  (4× oversample)
        │   scale_for_output(): float arithmetic, DC-blocking highpass
        │
        ▼
Audio::AddData(buffer, 441×4 bytes)  [ESP32/Audio.cpp]
        │
        ▼  sim_audio_write() → I2S DMA ring buffer
        │   ES8311 codec → 3.5mm audio jack
        └─ blocks until DMA has room (natural backpressure = timing)
```

Sample rate: 22050 Hz stereo 16-bit. I2S MCLK: 5,644,800 Hz. DMA frame: 441 samples.

### Keyboard

```
USB keyboard → FE1.1s hub → ESP32-P4 USB host
        │
        ▼  usb_client_task (Core 0)
        │   HID report → PS/2 Set 2 scancode bytes → FreeRTOS queue
        │
        ▼  Input::Update()  [ESP32/Input.cpp]
        │   PS/2 state machine: IDLE / BREAK / EXT / EXT_BREAK
        │   scancode → HK_ host key code
        │
        ▼  Keyboard::SetKey(hk, pressed, mods)
        │   HK_ → SAM key matrix row/column
        └─ SimCoupe reads matrix via IO::In(port)
```

## Memory Map

| Region | Size | Location | Contents |
|--------|------|----------|----------|
| SAM RAM | 4.5 MB | PSRAM | Z80 address space (all pages) |
| Display FB × 2 | 737 KB × 2 | PSRAM | RGB888 double-buffer (512×480×3) |
| FrameBuffer | 98 KB | PSRAM heap | SAM pixel data (512×192, palette indices) |
| Contention tables | 3 × 117 KB | DRAM | Z80 memory contention timing |
| FreqTable | 8 KB | DRAM | SAA-1099 frequency lookup (2048 entries) |
| Dirty-line prev buf | 98 KB | DRAM | Previous frame for dirty detection |
| simcoupe_task stack | 32 KB | DRAM | Must be DRAM (see main.cpp comment) |
| sound_task stack | 4 KB | DRAM | SAASound synthesis |

## Key Design Decisions

### Why DRAM for contention tables and FreqTable?
PSRAM latency is ~100 ns vs ~5 ns for internal DRAM. The Z80 contention tables are accessed on every memory/port operation; FreqTable on every oscillator tick. Moving them to DRAM gave a measurable speedup.

### Why 512×480 instead of 640×480?
The SAM Coupé active area is exactly 512 pixels wide. Using 512×480 eliminates horizontal padding, reduces the display framebuffer by 20% (737 KB vs 921 KB), and makes adjacent display rows contiguous in PSRAM (simplifying the blit).

### Why dirty-line tracking?
The PSRAM bus is the main bottleneck. Writing 589 KB of RGB888 data per frame takes ~13 ms. On static or mostly-static screens (BASIC prompt, menus), most rows are unchanged. Comparing each 512-byte row with the previous frame (DRAM→DRAM, fast) and skipping unchanged rows reduces PSRAM writes dramatically — to near zero on fully static screens.

### Why 4× oversample instead of 64×?
SAASound's default 64× oversample runs the SAA-1099 chip model 64 times per output sample. At 22050 Hz that is 28,224 chip ticks per frame. At 4× it is 1,764 — a 16× reduction. The audible difference at 22050 Hz output is negligible.

### Why float instead of double in SAASound?
The ESP32-P4 RISC-V core has a hardware FPU for single-precision (`float`) but emulates double-precision in software (~10× slower). `scale_for_output()` is called 441 times per frame; switching to `float` gives a large speedup with no audible quality loss.

### Why sound on Core 0 and Z80 on Core 1?
`Sound::FrameUpdate()` (SAASound synthesis) takes ~8 ms. The Z80 takes ~12.5 ms. Running them on separate cores with a semaphore pipeline overlaps most of the sound work with Z80 execution, keeping the total frame time close to max(12.5, 8) ≈ 12.5 ms rather than 12.5 + 8 = 20.5 ms.

### Why NOT overlap video with Z80?
Attempted and reverted. Both the Z80 (accessing SAM RAM in PSRAM) and the video blit (writing display buffer in PSRAM) share the MSPI bus. Overlapping them causes severe bus contention: Z80 slows from 12.5 ms to 21.5 ms, negating the benefit. Dirty-line tracking is the correct solution — reduce PSRAM writes rather than overlap them.
