# UVC Camera Project Structure

## Overview
ESP32-P4 UVC camera application with ISP-enabled image processing and multi-task FreeRTOS architecture.

## Directory Structure

```
uvc_camera_ov5647/
├── components/              # Project components
│   ├── video/              # ESP video driver (ISP enabled, patched)
│   ├── os/                 # OS services component
│   ├── camera/             # Camera utilities component
│   └── uvc/                # UVC streaming component
├── main/                   # Application entry point
│   ├── main.c             # Main entry point
│   ├── CMakeLists.txt     # Main component build config
│   └── Kconfig.projbuild  # Project configuration
├── managed_components/     # ESP Component Registry dependencies
│   ├── espressif__esp_cam_sensor/
│   ├── espressif__esp_h264/
│   ├── espressif__esp_ipa/
│   ├── espressif__esp_sccb_intf/
│   ├── espressif__tinyusb/
│   └── espressif__usb_device_uvc/
└── sdkconfig.defaults.esp32p4  # ESP32-P4 default configuration

```

## Component Details

### 1. components/os/ - OS Services Component
**Purpose**: FreeRTOS task management infrastructure

**Files**:
- `os_cfg.c` - Task configuration table (taskcfg_tb)
- `os_startup.c` - OS startup and task creation
- `monitor_task.c` - System monitoring task (heap, stack)
- `event_handler_task.c` - Event handling task
- `include/os_interface.h` - Public API
- `include/os_service.h` - Service definitions

**Dependencies**: `freertos`, `esp_event`, `uvc` (PRIV_REQUIRES)

**Responsibility**: 
- Provides table-driven task creation framework
- Implements monitor task for system health
- Handles application events

### 2. components/camera/ - Camera Utilities Component
**Purpose**: Camera debugging and utilities

**Files**:
- `camera_debug.c` - Debug utilities (conditional on CONFIG_CAMERA_DEBUG_ENABLE)
- `include/camera_debug.h` - Debug API

**Dependencies**: `esp_timer`, `driver`, `video` (all PRIV_REQUIRES)

**Responsibility**:
- Provides camera debugging utilities
- Frame timing analysis
- Camera performance monitoring

### 3. components/uvc/ - UVC Streaming Component
**Purpose**: UVC device implementation and video streaming

**Files**:
- `uvc_stream_task.c` - UVC streaming task with camera+encode+stream
- `uvc_app_common.c` - Common utilities and hardware initialization
- `include/uvc_app_common.h` - Common API and context

**Dependencies**:
- Public: `freertos`, `video`, `espressif__usb_device_uvc`, `espressif__tinyusb`, `os`, `camera`
- Private: `esp_timer`, `driver`, `esp_event`

**Responsibility**:
- Implements UVC streaming task
- Camera frame capture and H.264 encoding
- Hardware initialization (video subsystem, USB)
- Application context management (g_app_ctx)

### 4. components/video/ - ESP Video Driver (Customized)
**Purpose**: ESP video driver with ISP pipeline controller enabled (originally espressif/esp_video)

**Source**: Based on ESP Component Registry `espressif/esp_video` v0.8.0

**Key Customizations**:
- Added `IRAM_ATTR` to ISP callbacks in `src/device/esp_video_isp_device.c:388,746`
- Added ISP initialization debug logging in `src/esp_video_init.c:240-267`
- Removed examples and documentation files
- Renamed from `espressif__esp_video` to `video` for project ownership

**ISP Features**:
- ISP pipeline controller auto-initialization
- IPA (Image Processing Algorithm) configuration for OV5647
- Auto white balance, auto exposure, color correction
- Sharpen, denoise, gamma correction

**Dependencies**: `esp_driver_cam`, `esp_driver_isp`, `espressif__esp_cam_sensor`, `espressif__esp_h264`, `esp_driver_jpeg`, `espressif__esp_ipa` (optional)

## Build Configuration

### sdkconfig.defaults.esp32p4
Complete ESP32-P4 configuration file containing all critical settings. This file ensures consistent builds across different environments.

**Key Configuration Categories:**

1. **Camera Sensor** - OV5647 (1920x1080@30fps) and SC2336 support
2. **ISP** - Pipeline controller enabled with IRAM-safe callbacks
3. **UVC** - Multi-resolution support (1920x1080, 1280x720, etc.)
4. **SPIRAM** - 128MB PSRAM @ 200MHz with XIP support
5. **FreeRTOS** - Dual-core, 100Hz tick, extended memory support
6. **USB** - OTG support with optimized transfer settings

**Critical ISP Settings:**
```
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y  # Auto-init ISP with IPA config
CONFIG_ISP_ISR_IRAM_SAFE=y                          # ISP callbacks in IRAM
CONFIG_ISP_CTRL_FUNC_IN_IRAM=y                      # ISP control functions in IRAM
```

**Memory Configuration:**
```
CONFIG_SPIRAM=y                                     # Enable external PSRAM
CONFIG_SPIRAM_SPEED_200M=y                          # PSRAM @ 200MHz
CONFIG_SPIRAM_XIP_FROM_PSRAM=y                      # Execute instructions from PSRAM
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y                  # Fetch instructions to PSRAM
CONFIG_SPIRAM_RODATA=y                              # Store read-only data in PSRAM
```

**To regenerate sdkconfig from defaults:**
```bash
rm sdkconfig
idf.py reconfigure
```

## Component Dependencies Graph

```
main
 ├─→ os
 │    └─→ uvc (PRIV)
 └─→ uvc
      ├─→ os
      ├─→ camera
      ├─→ video
      ├─→ espressif__usb_device_uvc
      └─→ espressif__tinyusb

video (customized component)
 ├─→ esp_driver_cam
 ├─→ esp_driver_isp
 ├─→ espressif__esp_cam_sensor
 ├─→ espressif__esp_h264
 ├─→ esp_driver_jpeg
 └─→ espressif__esp_ipa (optional, for ISP pipeline controller)
```

## Task Architecture

### Task Configuration Table (os_cfg.c)
| Task Name    | Priority | Stack Size | Core | Function                          |
|--------------|----------|------------|------|-----------------------------------|
| uvc_stream   | 5        | 8 KB       | 0    | Camera capture + encode + stream  |
| monitor      | 1        | 4 KB       | 0    | System monitoring                 |
| event        | 2        | 4 KB       | 0    | Event handling                    |

### Task Lifecycle
Each task follows init → main → terminate pattern:
- `initUvcStreamTask()` - Initialize hardware, allocate resources
- `mainUvcStreamTask()` - Main task loop (event-driven or polling)
- `terUvcStreamTask()` - Cleanup and shutdown

## Build Information

**Binary Size**: 0x82b80 bytes (533,376 bytes)
**Free Space**: 49% of app partition (0x7d480 bytes / 513,152 bytes)

**Build Command**:
```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
```

## ISP (Image Signal Processor)

### ISP Pipeline Controller
The ISP pipeline controller is automatically initialized when:
1. `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y` is set
2. Camera sensor has ISP info defined (OV5647 has it)
3. IPA configuration exists for the sensor

### IPA Configuration (OV5647)
Located in: `build/esp-idf/espressif__esp_ipa/esp_video_ipa_config.c`

Modules:
- **AWB** (Auto White Balance) - Color temperature adjustment
- **AEN** (Auto Enhancement) - Sharpen, gamma, contrast
- **ADN** (Auto Denoise) - Noise reduction
- **ACC** (Auto Color Correction) - CCM, saturation
- **IAN** (ISP Analytics) - Statistics collection

### Output Format
- **Input**: Bayer RAW from OV5647 sensor
- **ISP Processing**: Demosaicing, white balance, color correction
- **Output**: YUV422P (planar 4:2:2) for H.264 encoder

## Key Design Patterns

1. **Table-Driven Task Creation**: All tasks defined in `taskcfg_tb[]` array
2. **Separation of Concerns**: Each component has single responsibility
3. **Event-Driven Architecture**: Tasks communicate via FreeRTOS queues
4. **Hardware Abstraction**: V4L2 API for camera/encoder control

## Adding New Components

### Component Structure
When adding a new component to `components/`, follow this structure:

```
components/new_component/
├── include/               # Public headers (exported to other components)
│   └── new_component.h   # Main public interface
├── src/                  # Source files (optional, can be in root)
│   └── new_component.c
├── private_include/      # Private headers (internal only)
│   └── internal.h
└── CMakeLists.txt        # Component build configuration
```

### CMakeLists.txt Template

```cmake
idf_component_register(
    SRCS
        "src/new_component.c"
        "src/helper.c"
    INCLUDE_DIRS
        "include"           # Public headers
    PRIV_INCLUDE_DIRS
        "private_include"   # Private headers (optional)
    REQUIRES
        freertos            # Public dependencies
        other_component
    PRIV_REQUIRES
        driver              # Private dependencies
        esp_timer
)

# Optional: Add compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NEW_COMPONENT_VERSION_MAJOR=1
    NEW_COMPONENT_VERSION_MINOR=0
)
```

### Header File Format

#### Public Interface Header (include/component_name.h)

```c
/**
 * @file component_name.h
 * @brief Component Name - Public API
 *
 * This file provides the public interface for [component purpose].
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Component configuration structure
 */
typedef struct {
    uint32_t param1;        /**< Description of param1 */
    bool     enable_flag;   /**< Description of flag */
} component_config_t;

/**
 * @brief Initialize component
 *
 * @param config Pointer to configuration structure
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid argument
 *     - ESP_FAIL: Initialization failed
 */
esp_err_t component_init(const component_config_t *config);

/**
 * @brief Start component operation
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Component not initialized
 */
esp_err_t component_start(void);

/**
 * @brief Stop component operation
 */
void component_stop(void);

#ifdef __cplusplus
}
#endif
```

#### Private Header (private_include/internal.h)

```c
/**
 * @file internal.h
 * @brief Component Name - Internal Definitions
 *
 * Internal definitions not exposed to other components.
 */

#pragma once

#include "component_name.h"

// Internal constants
#define INTERNAL_BUFFER_SIZE  1024
#define MAX_RETRY_COUNT       3

// Internal structures
typedef struct {
    uint8_t buffer[INTERNAL_BUFFER_SIZE];
    size_t  length;
} internal_buffer_t;

// Internal functions
esp_err_t internal_helper(void);
```

### Header Guard Conventions

**Use `#pragma once`** instead of traditional include guards for cleaner code:

```c
// ✅ Preferred
#pragma once

// ❌ Avoid (unless required for compatibility)
#ifndef COMPONENT_NAME_H
#define COMPONENT_NAME_H
// ...
#endif
```

### Naming Conventions

1. **Component names**: lowercase with underscores (`os`, `camera`, `uvc`, `video`)
2. **Header files**: `component_name.h` for public, `internal.h` or descriptive name for private
3. **Function prefixes**: `component_name_` for public API (e.g., `os_startup()`, `uvc_init()`)
4. **Type suffixes**:
   - `_t` for typedefs (e.g., `app_context_t`)
   - `_st` for structs in table-driven architectures (e.g., `taskcfg_st`)
   - `_cb` for callback function types

### Documentation Requirements

Each public header must include:
1. **File-level documentation**: Brief description of component purpose
2. **Function documentation**: Doxygen-style comments with:
   - `@brief` - Short description
   - `@param` - Parameter descriptions
   - `@return` - Return value descriptions with possible error codes
3. **Type documentation**: Description of structures and their fields

### Example: Adding a New "display" Component

```bash
# 1. Create directory structure
mkdir -p components/display/include
mkdir -p components/display/src
mkdir -p components/display/private_include

# 2. Create CMakeLists.txt
cat > components/display/CMakeLists.txt << 'EOF'
idf_component_register(
    SRCS
        "src/display.c"
        "src/display_driver.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "private_include"
    REQUIRES
        freertos
        driver
    PRIV_REQUIRES
        esp_timer
)
EOF

# 3. Create public header
cat > components/display/include/display.h << 'EOF'
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
void display_update(const uint8_t *frame_data, size_t len);

#ifdef __cplusplus
}
#endif
EOF

# 4. Create implementation
# (Add your implementation in src/display.c)

# 5. Use in other components
# Add "display" to REQUIRES or PRIV_REQUIRES in other CMakeLists.txt
```

### Component Dependencies Rules

1. **REQUIRES**: Use for public dependencies (headers used in your public API)
   - Example: If your header includes `<freertos/FreeRTOS.h>`, add `freertos` to REQUIRES

2. **PRIV_REQUIRES**: Use for private dependencies (only used in .c files)
   - Example: If only your .c files use ESP-TIMER, add `esp_timer` to PRIV_REQUIRES

3. **Avoid circular dependencies**:
   - If A requires B and B requires A, refactor to extract shared interfaces
   - Use PRIV_REQUIRES when possible to break circular public dependencies

### Testing New Components

```bash
# 1. Clean build to ensure CMake picks up new component
rm -rf build
idf.py reconfigure

# 2. Build
idf.py build

# 3. Verify component is registered
idf.py reconfigure 2>&1 | grep "new_component"
```

## References
- ESP-IDF version: 5.5.1
- ESP32-P4 with hardware ISP and H.264 encoder
- ESP-IDF Component System: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#component-cmakelists-files
