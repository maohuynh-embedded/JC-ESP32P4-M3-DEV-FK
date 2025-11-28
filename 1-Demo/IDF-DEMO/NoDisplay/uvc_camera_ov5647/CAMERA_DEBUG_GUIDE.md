# Camera Debug Module - Hướng dẫn sử dụng

## Tổng quan

Module Camera Debug cung cấp các công cụ debug chuyên sâu để phân tích dữ liệu camera, bao gồm:

- **Hex dump** - Hiển thị dữ liệu raw dưới dạng hex
- **Image format analyzer** - Phân tích header JPEG/H264
- **Statistics tracking** - Theo dõi FPS, bitrate, frame size
- **Performance monitoring** - Đo thời gian xử lý frame

## Cài đặt và cấu hình

### Bước 1: Kích hoạt Camera Debug

Sử dụng menuconfig để bật tính năng debug:

```bash
idf.py menuconfig
```

Đi tới menu:
```
Example Configuration --->
    Camera Debug Configuration --->
        [*] Enable Camera Debug Logging
```

### Bước 2: Chọn các chế độ debug

Sau khi bật `Enable Camera Debug Logging`, chọn các chế độ debug cần thiết:

#### **Statistics Logging** (Recommended)
```
[*] Enable Statistics Logging
```
- Hiển thị thống kê frame: FPS, bitrate, kích thước frame
- Tác động hiệu năng: **Rất thấp**
- Khuyến nghị: **Luôn bật** khi debug

#### **Image Header Analysis** (Recommended)
```
[*] Enable Image Header Analysis
```
- Phân tích header JPEG/H264
- Hiển thị thông tin: dimensions, keyframe, SPS/PPS, SOI/EOI
- Tác động hiệu năng: **Thấp**
- Khuyến nghị: **Bật** để kiểm tra format

#### **Hex Dump of Frame Header** (Use with caution)
```
[ ] Enable Hex Dump of Frame Header
```
- Hiển thị hex dump 256 bytes đầu tiên của mỗi frame
- Tác động hiệu năng: **Trung bình**
- Sinh ra nhiều log data
- Khuyến nghị: **Chỉ bật khi cần** kiểm tra raw data

#### **Full Frame Hex Dump** (⚠️ Warning!)
```
[ ] Enable Full Frame Hex Dump
```
- Hiển thị hex dump toàn bộ frame
- ⚠️ **CẢNH BÁO**: Sinh ra lượng log KHỔNG LỒ!
- Tác động hiệu năng: **Rất cao** - có thể làm crash hệ thống
- Khuyến nghị: **KHÔNG bật** trừ khi debug frame cụ thể và biết chính xác mình đang làm gì

#### **Timing Information**
```
[ ] Enable Timing Information
```
- Hiển thị timestamp và delta time giữa các frame
- Tác động hiệu năng: **Rất thấp**
- Khuyến nghị: **Bật** khi cần debug timing issues

#### **Statistics Print Interval**
```
(100) Statistics Print Interval (frames)
```
- Số frame giữa mỗi lần in thống kê
- Giá trị nhỏ = log nhiều, giá trị lớn = log ít
- Khuyến nghị: 100 frames (mặc định)

### Bước 3: Build và Flash

```bash
idf.py build flash monitor
```

## Output mẫu

### 1. Statistics Logging

```
I (12345) cam_debug: Frame #100: 45678 bytes, FPS: 29.85, Bitrate: 10925.34 kbps
I (12456) cam_debug: ========== Camera Statistics ==========
I (12456) cam_debug: Total frames:   100
I (12456) cam_debug: Dropped frames: 0
I (12456) cam_debug: Total bytes:    4567890 (4.35 MB)
I (12456) cam_debug: Frame size:     min=42341, max=48921, avg=45678 bytes
I (12456) cam_debug: FPS:            29.85
I (12456) cam_debug: Bitrate:        10925.34 kbps
I (12456) cam_debug: =======================================
```

### 2. Image Header Analysis

#### JPEG Format:
```
I (12345) cam_debug: === Frame #1 Info ===
I (12345) cam_debug: Size: 45678 bytes
I (12345) cam_debug: Format: JPEG
I (12345) cam_debug: Dimensions: 1920x1080
I (12345) cam_debug: Has SOI: Yes, Has EOI: Yes
```

#### H.264 Format:
```
I (12345) cam_debug: === Frame #1 Info ===
I (12345) cam_debug: Size: 52341 bytes
I (12345) cam_debug: Format: H.264
I (12345) cam_debug: Keyframe: Yes
I (12345) cam_debug: Has SPS: Yes, Has PPS: Yes
I (12345) cam_debug: Dimensions: 1280x720
```

### 3. Hex Dump Header (256 bytes)

```
I (12345) cam_debug: === Frame #1 Header Hex Dump (256 bytes) ===
I (12345) cam_debug: 00000000: FF D8 FF E0 00 10 4A 46 49 46 00 01 01 00 00 01  | ......JFIF......
I (12345) cam_debug: 00000010: 00 01 00 00 FF DB 00 43 00 08 06 06 07 06 05 08  | .......C........
I (12345) cam_debug: 00000020: 07 07 07 09 09 08 0A 0C 14 0D 0C 0B 0B 0C 19 12  | ................
I (12345) cam_debug: 00000030: 13 0F 14 1D 1A 1F 1E 1D 1A 1C 1C 20 24 2E 27 20  | ........... $.'.
...
```

### 4. Timing Information

```
I (12345) cam_debug: Frame #100 timing: ts=12345678 us, delta=33456 us (33.46 ms)
I (12456) cam_debug: Frame #101 timing: ts=12379134 us, delta=33456 us (33.46 ms)
```

## Use Cases

### Debug Scenario 1: Kiểm tra FPS và bitrate

**Cấu hình:**
- ✅ Statistics Logging
- ❌ Header Analysis
- ❌ Hex Dump

**Output:** Chỉ hiển thị FPS và bitrate, ít log, tác động hiệu năng minimal.

```bash
# Trong menuconfig
[*] Enable Statistics Logging
[ ] Enable Image Header Analysis
[ ] Enable Hex Dump of Frame Header
(100) Statistics Print Interval (frames)
```

### Debug Scenario 2: Phân tích format và header

**Cấu hình:**
- ✅ Statistics Logging
- ✅ Header Analysis
- ❌ Hex Dump

**Output:** Thông tin chi tiết về format, dimensions, keyframe, SPS/PPS.

```bash
# Trong menuconfig
[*] Enable Statistics Logging
[*] Enable Image Header Analysis
[ ] Enable Hex Dump of Frame Header
```

### Debug Scenario 3: Debug raw data corruption

**Cấu hình:**
- ✅ Statistics Logging
- ✅ Header Analysis
- ✅ Hex Dump Header (256 bytes)

**Output:** Hiển thị hex dump để kiểm tra data corruption trong header.

```bash
# Trong menuconfig
[*] Enable Statistics Logging
[*] Enable Image Header Analysis
[*] Enable Hex Dump of Frame Header
```

### Debug Scenario 4: Deep dive vào một frame cụ thể

**Cấu hình:**
- ✅ Full Frame Hex Dump (⚠️ chỉ để debug 1 frame)
- Sửa code để chỉ dump frame cần thiết:

```c
#ifdef CONFIG_CAMERA_DEBUG_ENABLE
    // Chỉ dump frame số 100
    if (s_debug_frame_counter == 100) {
        camera_debug_hex_dump(uvc->fb.buf, uvc->fb.len, 16);
    }
#endif
```

## Performance Impact

| Debug Level | CPU Usage | Memory Usage | Log Output | Khuyến nghị |
|------------|-----------|--------------|------------|-------------|
| STATS | +0.5% | +2KB | Vừa phải | Luôn bật |
| HEADER | +1% | +2KB | Vừa phải | Bật khi cần |
| HEX_HEADER | +5% | +2KB | Nhiều | Cẩn trọng |
| HEX_FULL | +50% | +2KB | CỰC NHIỀU | Tránh |
| TIMING | +0.5% | +1KB | Ít | An toàn |

## Tips & Best Practices

### ✅ DO:
- Bắt đầu với chỉ STATS enabled
- Tăng dần debug level khi cần
- Sử dụng print interval lớn (100-200 frames) để giảm log spam
- Tắt debug khi không cần thiết để tiết kiệm CPU

### ❌ DON'T:
- Bật HEX_FULL trong production
- Để print interval quá nhỏ (< 10 frames)
- Bật tất cả debug modes cùng lúc
- Quên tắt debug sau khi hoàn thành

## Troubleshooting

### Vấn đề: Không thấy log debug

**Giải pháp:**
1. Kiểm tra `CONFIG_CAMERA_DEBUG_ENABLE=y` trong sdkconfig
2. Kiểm tra ESP_LOG_LEVEL >= ESP_LOG_INFO
3. Rebuild hoàn toàn: `idf.py fullclean && idf.py build`

### Vấn đề: FPS giảm mạnh khi bật debug

**Giải pháp:**
1. Tắt HEX_FULL nếu đang bật
2. Tắt HEX_HEADER
3. Tăng print interval lên 200-500 frames
4. Chỉ bật STATS và HEADER

### Vấn đề: Log bị flood, không đọc được

**Giải pháp:**
1. Tăng `CONFIG_CAMERA_DEBUG_PRINT_INTERVAL` lên 500 hoặc 1000
2. Tắt HEX_HEADER và HEX_FULL
3. Chỉ bật timing cho 1 frame cụ thể

### Vấn đề: Muốn capture hex dump của chính xác 1 frame

**Giải pháp:**
Sửa code trong `uvc_example.c`:

```c
#ifdef CONFIG_CAMERA_DEBUG_ENABLE
    s_debug_frame_counter++;

    // Chỉ dump frame 100
    if (s_debug_frame_counter == 100) {
        ESP_LOGI(TAG, "=== Dumping Frame #100 ===");
        camera_debug_hex_dump(uvc->fb.buf, uvc->fb.len, 16);
    }

    // Print stats mỗi 100 frames
    if (s_debug_frame_counter % CONFIG_CAMERA_DEBUG_PRINT_INTERVAL == 0) {
        camera_debug_print_stats();
    }
#endif
```

## API Reference

Xem chi tiết trong [camera_debug.h](main/camera_debug.h):

```c
// Khởi tạo debug module
esp_err_t camera_debug_init(uint32_t debug_level);

// Xử lý frame data
esp_err_t camera_debug_process_frame(const uint8_t *data, size_t len, int64_t timestamp);

// Hex dump
void camera_debug_hex_dump(const uint8_t *data, size_t len, uint8_t bytes_per_line);

// Phân tích format
esp_err_t camera_debug_analyze_format(const uint8_t *data, size_t len, image_header_info_t *info);

// Lấy và in statistics
esp_err_t camera_debug_get_stats(camera_stats_t *stats);
void camera_debug_print_stats(void);
void camera_debug_reset_stats(void);
```

## Ví dụ mở rộng

### Custom hex dump cho vùng data cụ thể

```c
// Dump chỉ 64 bytes ở offset 512
camera_debug_hex_dump(uvc->fb.buf + 512, 64, 16);
```

### Phân tích format thủ công

```c
image_header_info_t info;
if (camera_debug_analyze_format(uvc->fb.buf, uvc->fb.len, &info) == ESP_OK) {
    if (info.format == IMG_FORMAT_H264 && info.is_keyframe) {
        ESP_LOGI(TAG, "Got H.264 keyframe!");
    }
}
```

### Lấy statistics vào code

```c
camera_stats_t stats;
camera_debug_get_stats(&stats);

if (stats.fps < 20.0) {
    ESP_LOGW(TAG, "FPS too low: %.2f", stats.fps);
}

if (stats.avg_size > 100000) {
    ESP_LOGW(TAG, "Frame size too large: %lu bytes", stats.avg_size);
}
```

## Kết luận

Module Camera Debug là công cụ mạnh mẽ để phân tích và debug camera data trên ESP32-P4. Sử dụng đúng cách sẽ giúp bạn:

- Nhanh chóng phát hiện vấn đề về format, corruption, timing
- Tối ưu hiệu năng camera và encoder
- Hiểu rõ flow của data trong hệ thống

**Khuyến nghị:** Bắt đầu với cấu hình minimal (chỉ STATS), sau đó tăng dần debug level theo nhu cầu.
