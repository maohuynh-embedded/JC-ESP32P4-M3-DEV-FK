# UVC Camera Application - Architecture Guide

## Tổng quan

Project này hiện có **3 kiến trúc khác nhau**. Bạn cần chọn **MỘT** kiến trúc để sử dụng.

## 3 Kiến trúc có sẵn

### 1. ⭐ **Reference Architecture** (RECOMMENDED - ĐANG ACTIVE)

**File chính:** `main.c`, `os_startup.c`, `os_cfg.c`, `uvc_app_common.c`

**Đặc điểm:**
- ✅ **Production-ready** - Based on real-world project
- ✅ **Table-driven** - Easy configuration
- ✅ **Scalable** - Easy to add/remove tasks
- ✅ **Clean** - 3-phase lifecycle (init/main/term)
- ✅ **Maintainable** - One file per task

**Khi nào dùng:**
- Production deployment
- Large, complex applications
- Team development
- Long-term maintenance

**Build:**
```bash
# Đã active trong CMakeLists.txt
idf.py build flash monitor
```

---

### 2. 📦 **Original Single-Thread** (BACKUP)

**File chính:** `uvc_example.c`

**Đặc điểm:**
- ✅ Simple và straightforward
- ✅ Ít file, dễ hiểu cho beginners
- ❌ Không có task separation
- ❌ Khó mở rộng

**Khi nào dùng:**
- Quick prototyping
- Learning ESP32-P4 UVC
- Simple use cases

**Build:**
```bash
# Edit CMakeLists.txt:
# Comment out "Option 2" section
# Uncomment: set(srcs "uvc_example.c")

idf.py build flash monitor
```

---

### 3. 🚀 **FreeRTOS Multi-Task (Old Approach)** (REFERENCE)

**File chính:** `app_main_freertos.c`, `app_tasks.c`

**Đặc điểm:**
- ✅ Multi-task architecture
- ✅ Good performance
- ⚠️ Less structured than reference architecture
- ⚠️ Monolithic app_tasks.c file

**Khi nào dùng:**
- Reference for task implementation
- Understanding FreeRTOS tasks
- Migration baseline

**Status:** Không build-able hiện tại (reference only)

---

## So sánh chi tiết

| Aspect | Original | FreeRTOS Multi-Task | Reference Architecture |
|--------|----------|---------------------|----------------------|
| **Complexity** | Low | Medium | Medium-High |
| **Files** | 1 main file | 2 files | 6+ files |
| **Tasks** | 0 (main loop) | 5 tasks | 5 tasks (table-driven) |
| **Configuration** | Hardcoded | Hardcoded | Table-based |
| **Scalability** | ⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Maintainability** | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Production Ready** | No | Yes | ✅ **Yes** |
| **Learning Curve** | Easy | Medium | Medium |
| **Code Reuse** | Low | Medium | High |
| **Best for** | Prototyping | Development | Production |

---

## Current Active Architecture: Reference Pattern

### Cấu trúc file hiện tại:

```
main/
├── main.c                  ✅ Main entry (simple, calls os_startup)
├── os_interface.h          ✅ Public API (task/queue IDs)
├── os_service.h            ✅ Task config structures
├── os_startup.c            ✅ Task/queue creation logic
├── os_cfg.c                ✅ Task configuration table
├── uvc_app_common.h        ✅ Shared definitions
├── uvc_app_common.c        ✅ Common utilities + HW init
│
├── camera_debug.h/c        ✅ Debug module
│
├── uvc_example.c           📦 BACKUP - Original version
├── app_main_freertos.c     📚 REFERENCE - Old multi-task
├── app_tasks.h/c           📚 REFERENCE - Old task impl
└── main_refactored.c       📚 OBSOLETE - merged into main.c
```

### Flow hiện tại:

```
app_main() (main.c)
   │
   ▼
os_startup() (os_startup.c)
   │
   ├─── os_init_stuff() (os_cfg.c)
   │      ├─ uvc_app_hw_init()
   │      └─ uvc_app_debug_init()
   │
   ├─── For each task in taskcfg_tb[]:
   │      └─ call initfunc()
   │
   ├─── Create queues
   │
   └─── Create tasks
          └─ Each task runs mainfunc()
```

---

## Chuyển đổi giữa các architectures

### Từ Original → Reference (Hiện tại ACTIVE):

**Already done!** Chỉ cần:
```bash
idf.py build flash monitor
```

### Từ Reference → Original:

**Edit `main/CMakeLists.txt`:**
```cmake
# Comment out Reference Architecture section (lines 14-19)
# Uncomment Original Architecture:
set(srcs "uvc_example.c")

if(CONFIG_CAMERA_DEBUG_ENABLE)
    list(APPEND srcs "camera_debug.c")
endif()

idf_component_register(SRCS ${srcs})
```

Then rebuild:
```bash
idf.py fullclean
idf.py build flash monitor
```

---

## Task Implementation Status

Reference Architecture đang sử dụng:

| Component | Status | File | Notes |
|-----------|--------|------|-------|
| Main entry | ✅ Complete | main.c | Clean, simple |
| OS services | ✅ Complete | os_startup.c, os_cfg.c | Fully functional |
| Common utils | ✅ Complete | uvc_app_common.c | HW init, utilities |
| Debug module | ✅ Complete | camera_debug.c | Optional, works |
| Camera task | ⚠️ TODO | camera_task.c | Needs creation |
| Encoding task | ⚠️ TODO | encoding_task.c | Needs creation |
| UVC stream task | ⚠️ TODO | uvc_stream_task.c | Needs creation |
| Monitor task | ⚠️ TODO | monitor_task.c | Needs creation |
| Event handler task | ⚠️ TODO | event_handler_task.c | Needs creation |

**Note:** Các task implementations có thể copy từ `app_tasks.c` và refactor theo pattern reference (init/main/term functions).

---

## Next Steps

### Option A: Sử dụng Reference Architecture như hiện tại

1. **Tạo task implementation files:**
   - Copy logic từ `app_tasks.c`
   - Split thành 5 files riêng biệt
   - Follow init/main/term pattern

2. **Update CMakeLists.txt:**
   - Uncomment task files (lines 24-30)

3. **Build & Test:**
   ```bash
   idf.py build flash monitor
   ```

### Option B: Quay lại Original Architecture

1. **Edit `main/CMakeLists.txt`** như hướng dẫn ở trên
2. **Rebuild:**
   ```bash
   idf.py fullclean && idf.py build flash monitor
   ```

---

## Recommended: Reference Architecture

**Lý do:**
1. ✅ **Proven** - Based on production code
2. ✅ **Scalable** - Easy to extend
3. ✅ **Maintainable** - Clean structure
4. ✅ **Professional** - Industry best practices
5. ✅ **Debuggable** - Clear task separation

**Trade-off:** Cần tạo 5 task files (nhưng có template sẵn trong `app_tasks.c`)

---

## Documentation

- **[ARCHITECTURE_MIGRATION.md](ARCHITECTURE_MIGRATION.md)** - Chi tiết migration guide
- **[FREERTOS_ARCHITECTURE.md](FREERTOS_ARCHITECTURE.md)** - FreeRTOS architecture explained
- **[CAMERA_DEBUG_GUIDE.md](CAMERA_DEBUG_GUIDE.md)** - Debug module usage

---

## Questions?

**Q: Tại sao có 3 architectures?**
A: Evolution của code:
1. Original (prototype)
2. FreeRTOS Multi-Task (improvement)
3. Reference Pattern (production-ready, based on real project)

**Q: Nên dùng cái nào?**
A: **Reference Architecture** (Option 2) - Đang active

**Q: Có thể dùng Original không?**
A: Có, nhưng không recommended cho production.

**Q: Task files chưa có, code có chạy được không?**
A: Không, cần tạo task implementation files hoặc quay lại Original architecture.

**Q: Làm sao tạo task files?**
A: Copy từ `app_tasks.c`, split thành 5 files, refactor theo init/main/term pattern. Có template trong ARCHITECTURE_MIGRATION.md.
