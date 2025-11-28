# ========================================
# UVC Camera Application Build Configuration
# ========================================

# Choose ONE architecture:
# Option 1: Original single-threaded (simple, for reference)
# Option 2: Reference architecture (table-driven, production-ready)

# ========== OPTION 1: Original Architecture (BACKUP) ==========
# Uncomment to use original single-threaded version:
# set(srcs "uvc_example.c")

# ========== OPTION 2: Reference Architecture (ACTIVE) ==========
set(srcs
    "main.c"                # Main entry point (clean, simple)
    "os_startup.c"          # OS services - task/queue creation
    "os_cfg.c"              # Task configuration table
    "uvc_app_common.c"      # Common utilities and HW init
)

# ========== Task Implementations ==========
# All 5 tasks following reference architecture pattern
list(APPEND srcs
    "camera_task.c"         # Camera capture task (init/main/term)
    "encoding_task.c"       # Encoding task (init/main/term)
    "uvc_stream_task.c"     # UVC streaming task (init/main/term)
    "monitor_task.c"        # System monitor task (init/main/term)
    "event_handler_task.c"  # Event handler task (init/main/term)
)

# ========== Debug Module ==========
if(CONFIG_CAMERA_DEBUG_ENABLE)
    list(APPEND srcs "camera_debug.c")
endif()

# ========== Register Component ==========
idf_component_register(SRCS ${srcs})
