# Contributing to UVC Camera Project

## Project Organization

This project follows ESP-IDF component-based architecture with custom components in `components/` directory.

See [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for detailed architecture documentation.

## Code Style Guidelines

### 1. Component Structure
- Place new components in `components/`
- Use `include/` for public headers
- Use `private_include/` for internal headers (optional)
- Use `src/` for implementation files (optional, can be in root)

### 2. Naming Conventions

**Files:**
- Component names: lowercase with underscores (`os`, `camera`, `uvc`)
- Header files: `component_name.h` for public API
- Source files: `component_name.c` or descriptive names

**Functions:**
- Public API: `component_name_function()` (e.g., `os_startup()`, `uvc_init()`)
- Private/static: `function_name()` or `_function_name()`

**Types:**
- Structs: `component_name_t` or `description_t`
- Table structs: `name_st` (e.g., `taskcfg_st`)
- Callbacks: `name_cb`
- Enums: `component_name_enum_t`

**Constants:**
- Macros: `COMPONENT_NAME_CONSTANT` (uppercase)
- Config: `CONFIG_*` for Kconfig options

### 3. Header File Template

All public headers must use this format:

```c
/**
 * @file component_name.h
 * @brief Component Name - One line description
 *
 * Detailed description of component purpose.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function description
 *
 * @param param1 Description
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Failure
 */
esp_err_t component_function(int param1);

#ifdef __cplusplus
}
#endif
```

### 4. Documentation Requirements

**All public APIs must have:**
- Doxygen-style comments
- `@brief` description
- `@param` for each parameter
- `@return` with all possible return values

**Example:**
```c
/**
 * @brief Initialize UVC streaming task
 *
 * Sets up camera, encoder, and USB subsystems.
 *
 * @param arg Unused, required by FreeRTOS task signature
 *
 * @note This function is called by os_startup() during system initialization
 */
void initUvcStreamTask(void *arg);
```

## Adding a New Component

### Quick Start

```bash
# 1. Create component structure
mkdir -p components/mycomp/include
mkdir -p components/mycomp/src

# 2. Create CMakeLists.txt
cat > components/mycomp/CMakeLists.txt << 'CMEOF'
idf_component_register(
    SRCS
        "src/mycomp.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
)
CMEOF

# 3. Create header
cat > components/mycomp/include/mycomp.h << 'HEOF'
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mycomp_init(void);

#ifdef __cplusplus
}
#endif
HEOF

# 4. Create implementation
cat > components/mycomp/src/mycomp.c << 'CEOF'
#include "mycomp.h"
#include "esp_log.h"

static const char *TAG = "mycomp";

esp_err_t mycomp_init(void) {
    ESP_LOGI(TAG, "Initializing mycomp");
    return ESP_OK;
}
CEOF

# 5. Add to dependencies in other components
# Edit other component's CMakeLists.txt and add "mycomp" to REQUIRES

# 6. Build
rm -rf build
idf.py build
```

### Component Dependencies

**REQUIRES vs PRIV_REQUIRES:**

- **REQUIRES**: Public dependencies (headers included in your public API)
  ```cmake
  REQUIRES freertos driver mycomponent
  ```
  Use when your `.h` files include headers from these components.

- **PRIV_REQUIRES**: Private dependencies (only used in `.c` files)
  ```cmake
  PRIV_REQUIRES esp_timer nvs_flash
  ```
  Use when only your implementation (`.c`) uses these components.

**Avoid Circular Dependencies:**
- If component A requires B, B should NOT require A
- Use PRIV_REQUIRES to break cycles when possible
- Refactor shared code into a third component if needed

## Build System

### Clean Build
```bash
rm -rf build sdkconfig
idf.py reconfigure
idf.py build
```

### Configuration
All project config is in `sdkconfig.defaults.esp32p4`. To modify:
```bash
idf.py menuconfig
# Make changes
# Config is automatically saved to sdkconfig
# Copy important changes to sdkconfig.defaults.esp32p4
```

### Flash and Monitor
```bash
idf.py flash monitor
# or specify port
idf.py -p /dev/ttyUSB0 flash monitor
```

## Code Quality

### Before Committing

1. **Build successfully**
   ```bash
   idf.py build
   ```

2. **Check for warnings**
   ```bash
   idf.py build 2>&1 | grep -i warning
   ```

3. **Test on hardware** (if applicable)
   ```bash
   idf.py flash monitor
   ```

4. **Update documentation** if adding new features
   - Update README.md for user-facing changes
   - Update PROJECT_STRUCTURE.md for architecture changes

### Logging

Use ESP-IDF logging macros with appropriate levels:

```c
static const char *TAG = "component_name";

ESP_LOGE(TAG, "Error: %s", error_msg);    // Errors
ESP_LOGW(TAG, "Warning: %s", warn_msg);   // Warnings  
ESP_LOGI(TAG, "Info: %s", info_msg);      // Info (default level)
ESP_LOGD(TAG, "Debug: %s", debug_msg);    // Debug
ESP_LOGV(TAG, "Verbose: %s", verb_msg);   // Verbose
```

### Error Handling

Always check and handle errors:

```c
// ✅ Good
esp_err_t ret = component_init(&config);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize: %s", esp_err_to_name(ret));
    return ret;
}

// ❌ Bad
component_init(&config);  // Ignoring return value
```

## Git Workflow

### Commit Messages

Follow conventional commit format:

```
type(scope): brief description

Detailed description (optional)

- List of changes
- Another change
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code refactoring
- `docs`: Documentation changes
- `style`: Code style changes (formatting)
- `perf`: Performance improvements
- `test`: Adding tests
- `build`: Build system changes

**Examples:**
```
feat(uvc): add multi-resolution support

- Added 1280x720 @ 15fps mode
- Added 480x320 @ 30fps mode
- Updated UVC descriptors

fix(isp): correct IRAM attribute for callbacks

The ISP callbacks were failing due to missing IRAM_ATTR.
Added attribute to isp_sharpen_stats_done() and isp_stats_done().

docs(readme): update hardware requirements

Added OV5647 camera specification and I2C pin configuration.
```

## Testing

### Unit Testing (Future)
When adding tests, place them in `components/component_name/test/`:
```
components/mycomp/
├── include/
├── src/
└── test/
    └── test_mycomp.c
```

### Hardware Testing Checklist
- [ ] Camera initializes correctly
- [ ] ISP pipeline starts
- [ ] H.264 encoding works
- [ ] USB enumeration successful
- [ ] UVC streaming at all resolutions
- [ ] No memory leaks (check with monitor task)
- [ ] Stable operation for 10+ minutes

## Questions?

- Check [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for architecture details
- Check [README.md](README.md) for quick start guide
- Review existing components (`os`, `camera`, `uvc`, `video`) for examples
- ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/

## License

See individual component licenses. Custom components in this project follow the project's main license.
