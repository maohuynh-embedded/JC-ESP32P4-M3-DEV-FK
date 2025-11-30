set(srcs)

# Camera debug module (conditional)
if(CONFIG_CAMERA_DEBUG_ENABLE)
    list(APPEND srcs "camera_debug.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_timer
        driver
        video
)
