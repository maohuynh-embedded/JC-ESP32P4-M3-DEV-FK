# Architecture Migration Guide

## Tổng quan

Đã refactor UVC Camera application theo kiến trúc reference project từ `/mnt/e/4_WEEV/01_SRC/01_Embedded_sub_esp32_wifi_node`.

## Kiến trúc mới

### So sánh với reference project

| Component | Reference Project | UVC Camera App (New) |
|-----------|------------------|---------------------|
| **Main entry** | `main.c` → `os_startup()` | `main_refactored.c` → `os_startup()` |
| **OS Service** | `os_service.h`, `os_startup.c`, `os_cfg.c` | ✅ Same structure |
| **Task table** | `taskcfg_tb[]` in `os_cfg.c` | ✅ Same pattern |
| **Common utils** | `appcommon.h/c` | `uvc_app_common.h/c` |
| **Logging** | Centralized tags in appcommon | ✅ Centralized tags |
| **Init pattern** | `init` → `main` → `ter` | ✅ Same 3-phase lifecycle |

### Cấu trúc file mới

```
main/
├── main_refactored.c          # Main entry point (đơn giản, gọi os_startup)
├── os_interface.h              # Public API (task/queue IDs)
├── os_service.h                # Task config structure
├── os_startup.c                # Task/queue creation logic
├── os_cfg.c                    # Task configuration table
├── uvc_app_common.h            # Shared definitions
├── uvc_app_common.c            # Common utilities & HW init
│
├── camera_task.c               # Camera capture task (init/main/term)
├── encoding_task.c             # Encoding task (init/main/term)
├── uvc_stream_task.c           # UVC streaming task (init/main/term)
├── monitor_task.c              # System monitor task (init/main/term)
├── event_handler_task.c        # Event handler task (init/main/term)
│
├── camera_debug.h/c            # Debug module (giữ nguyên)
└── CMakeLists.txt              # Build configuration
```

## Chi tiết kiến trúc

### 1. Main Entry Point Pattern

#### Reference:
```c
void app_main(void) {
    os_startup();  // Tạo tất cả tasks

    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        // Optional: monitoring
    }
}
```

#### UVC App (New):
```c
void app_main(void) {
    os_startup();  // Tạo tất cả tasks

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        #ifdef DEBUG_MENLEAK
        monitor_heap();
        #endif
    }
}
```

**✅ Đúng pattern!**

---

### 2. OS Startup Flow

```
app_main()
   │
   ▼
os_startup()
   ├── os_init_stuff()           // Global initialization
   ├── For each task:
   │   └── call initfunc()       // Task-specific init
   ├── Create queues
   └── Create tasks
       └── Each task runs mainfunc()
```

**Reference:**
```c
void os_init_stuff(void) {
    appcommon_init();
    apptimer_init();
}
```

**UVC App:**
```c
void os_init_stuff(void) {
    uvc_app_hw_init();      // Init camera/encoder hardware
    uvc_app_debug_init();   // Init debug module
}
```

---

### 3. Task Configuration Table

#### Reference Pattern:
```c
const taskcfg_st taskcfg_tb[NUMOFTASK] = {
    /*taskname    initfunc   mainfunc    terfunc     stack   priority  core*/
    {"appconsole", initAC,    mainAC,     terAC,      1280,   10,       1},
    {"pm",         initPM,    mainPM,     terPM,      3328,   10,       1},
    {"mqtt",       initMQTT,  mainMQTT,   terMQTT,    8192,   10,       1},
};
```

#### UVC App (New):
```c
const taskcfg_st taskcfg_tb[NUMOFTASK] = {
    /*taskname      initfunc            mainfunc            terfunc             stack   priority  core*/
    {"camera",      initCameraTask,     mainCameraTask,     terCameraTask,      4096,   5,        1},
    {"encoding",    initEncodingTask,   mainEncodingTask,   terEncodingTask,    8192,   4,        1},
    {"uvc_stream",  initUvcStreamTask,  mainUvcStreamTask,  terUvcStreamTask,   4096,   3,        0},
    {"monitor",     initMonitorTask,    mainMonitorTask,    terMonitorTask,     3072,   1,        0},
    {"event",       initEventHandlerTask, mainEventHandlerTask, terEventHandlerTask, 2048, 2,    0},
};
```

**Key points:**
- ✅ 3-phase lifecycle: init → main → terminate
- ✅ Table-driven configuration
- ✅ Core affinity assignment
- ✅ Priority-based scheduling

---

### 4. Task & Queue ID Enums

#### Reference:
```c
typedef enum {
    TASK_APPCONSOLE,
    TASK_PM,
    TASK_NW,
    TASK_MQTT,
    NUMOFTASK
} os_task_id_en;

typedef enum {
    QUEUE_PM,
    QUEUE_NW,
    NUMOFQUEUE
} os_queue_id_en;
```

#### UVC App:
```c
typedef enum {
    TASK_CAMERA_CAPTURE,
    TASK_ENCODING,
    TASK_UVC_STREAM,
    TASK_MONITOR,
    TASK_EVENT_HANDLER,
    NUMOFTASK
} os_task_id_en;

typedef enum {
    QUEUE_RAW_FRAME,
    QUEUE_ENCODED_FRAME,
    QUEUE_SYSTEM_EVENT,
    NUMOFQUEUE
} os_queue_id_en;
```

**✅ Cùng pattern!**

---

### 5. Centralized Logging

#### Reference Pattern:
```c
// appcommon.h
#define MQTT_TAG    tagNameTable[APPTAG_MQTT]
#define MQTT_INFO(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, MQTT_TAG, format, ##__VA_ARGS__)
#define MQTT_ERROR(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, MQTT_TAG, format, ##__VA_ARGS__)
```

#### UVC App (Simplified):
```c
// uvc_app_common.h
#define CAM_TAG     "camera"
#define ENC_TAG     "encoding"
#define UVC_TAG     "uvc_stream"
#define MON_TAG     "monitor"
#define EVT_TAG     "event"

// Usage:
ESP_LOGI(CAM_TAG, "Camera initialized");
ESP_LOGE(ENC_TAG, "Encoding failed");
```

**Note:** Có thể mở rộng thành tag table như reference nếu cần control logging per component.

---

### 6. Global Context Access

#### Reference:
```c
// Không có global context cố định, mỗi module tự manage
```

#### UVC App:
```c
// uvc_app_common.h
extern app_context_t g_app_ctx;

// Access từ bất kỳ đâu:
g_app_ctx.total_frames_captured++;
xEventGroupSetBits(g_app_ctx.system_events, EVENT_STREAMING_ACTIVE);
```

**Difference:** UVC app có shared state phức tạp hơn nên cần global context.

---

### 7. Task Lifecycle Pattern

#### Init Phase (initfunc):
```c
void initCameraTask(void *arg) {
    // Allocate resources
    // Setup hardware
    // Initialize local state
    ESP_LOGI(CAM_TAG, "Camera task initialized");
}
```

#### Main Loop (mainfunc):
```c
void mainCameraTask(void *arg) {
    ESP_LOGI(CAM_TAG, "Camera task started");

    // Wait for system ready
    xEventGroupWaitBits(g_app_ctx.system_events, EVENT_CAMERA_READY, ...);

    while (1) {
        // Check shutdown
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            break;
        }

        // Main work
        // ...

        vTaskDelay(1);
    }

    vTaskDelete(NULL);
}
```

#### Terminate Phase (terfunc):
```c
void terCameraTask(void *arg) {
    // Free resources
    // Close file descriptors
    ESP_LOGI(CAM_TAG, "Camera task terminated");
}
```

**✅ Giống hệt reference pattern!**

---

## Migration Steps

### Bước 1: Backup existing code
```bash
cd main/
cp uvc_example.c uvc_example_original.c
cp app_main_freertos.c app_main_freertos_backup.c
```

### Bước 2: Sử dụng new architecture
```bash
# Rename main_refactored.c → main.c
mv main_refactored.c main.c
```

### Bước 3: Update CMakeLists.txt
```cmake
set(srcs
    "main.c"
    "os_startup.c"
    "os_cfg.c"
    "uvc_app_common.c"
    "camera_task.c"
    "encoding_task.c"
    "uvc_stream_task.c"
    "monitor_task.c"
    "event_handler_task.c"
)

if(CONFIG_CAMERA_DEBUG_ENABLE)
    list(APPEND srcs "camera_debug.c")
endif()

idf_component_register(SRCS ${srcs})
```

### Bước 4: Tạo các task implementation files

Mỗi task cần 3 functions:
- `initXxxTask(void *arg)`
- `mainXxxTask(void *arg)`
- `terXxxTask(void *arg)`

Tham khảo implementation trong các file tương ứng.

---

## Lợi ích của kiến trúc mới

### 1. Modularity
- ✅ Mỗi task là một file riêng
- ✅ Clear separation of concerns
- ✅ Dễ thêm/xóa tasks

### 2. Maintainability
- ✅ Table-driven config → easy to modify priorities/stacks
- ✅ Centralized initialization trong `os_init_stuff()`
- ✅ Clean shutdown với `os_terminate_stuff()`

### 3. Scalability
- ✅ Thêm task mới: chỉ cần add vào table
- ✅ Thêm queue mới: add vào enum và create trong os_startup

### 4. Consistency
- ✅ All tasks follow same pattern: init → main → term
- ✅ Same logging convention
- ✅ Same resource management

### 5. Debugging
- ✅ Clear task names trong vTaskList()
- ✅ Centralized monitoring trong app_main loop
- ✅ Stack watermark checking dễ dàng

---

## Best Practices (theo reference)

### ✅ DO:
1. **Always follow 3-phase lifecycle**
   ```c
   initfunc:  Allocate, setup
   mainfunc:  Main loop
   terfunc:   Cleanup
   ```

2. **Use table-driven configuration**
   - Không hardcode priorities/stacks trong code
   - Tất cả config ở `taskcfg_tb[]`

3. **Centralize global init**
   - Hardware init → `os_init_stuff()`
   - Không init trong main.c

4. **Check return values**
   ```c
   APP_RETURN_ON_ERROR(esp_video_init(...), TAG, "Init failed");
   ```

5. **Use consistent logging**
   ```c
   ESP_LOGI(CAM_TAG, "message");  // Not printf()
   ```

### ❌ DON'T:
1. **Không tạo tasks trong main.c**
   - Tasks được tạo trong `os_startup()`

2. **Không hardcode queue handles**
   - Use `os_getQueueHandler(QUEUE_ID)`

3. **Không skip terminate functions**
   - Always cleanup resources

4. **Không mix init và main logic**
   - Init ở `initfunc`, main loop ở `mainfunc`

---

## Example: Adding a new task

### 1. Add to task enum (os_interface.h):
```c
typedef enum {
    TASK_CAMERA_CAPTURE,
    TASK_ENCODING,
    TASK_NEW_TASK,  // <-- Add here
    NUMOFTASK
} os_task_id_en;
```

### 2. Add to task table (os_cfg.c):
```c
extern void initNewTask(void *arg);
extern void mainNewTask(void *arg);
extern void terNewTask(void *arg);

const taskcfg_st taskcfg_tb[NUMOFTASK] = {
    // ... existing tasks ...
    {"newtask", initNewTask, mainNewTask, terNewTask, 4096, 3, 0},
};
```

### 3. Create implementation (new_task.c):
```c
#include "uvc_app_common.h"

void initNewTask(void *arg) {
    // Init code
}

void mainNewTask(void *arg) {
    while (1) {
        // Main work
        vTaskDelay(100);
    }
    vTaskDelete(NULL);
}

void terNewTask(void *arg) {
    // Cleanup
}
```

### 4. Update CMakeLists.txt:
```cmake
list(APPEND srcs "new_task.c")
```

**Done!** Task sẽ tự động được tạo trong `os_startup()`.

---

## Comparison với old architecture

| Aspect | Old (app_tasks.c) | New (Reference Pattern) |
|--------|------------------|------------------------|
| **Task creation** | Manual xTaskCreatePinnedToCore() | Table-driven os_startup() |
| **Configuration** | Scattered #defines | Centralized taskcfg_tb[] |
| **Initialization** | In app_main() | 3-phase: init/main/term |
| **Logging** | Mixed tags | Centralized tags |
| **Task access** | Direct handles | os_getTaskHandler() |
| **Queue access** | Direct handles | os_getQueueHandler() |
| **Modularity** | Monolithic app_tasks.c | One file per task |
| **Maintainability** | Medium | High |
| **Scalability** | Medium | Excellent |

---

## Conclusion

Kiến trúc mới được refactor hoàn toàn theo reference project pattern:

✅ **Table-driven task management**
✅ **3-phase lifecycle (init/main/term)**
✅ **Centralized configuration**
✅ **Clean separation of concerns**
✅ **Easy to extend and maintain**

Đây là một **production-ready architecture** đã được verify trong reference project thực tế.
