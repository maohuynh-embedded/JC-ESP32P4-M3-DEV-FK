# ESP32-P4 UVC Camera with ISP

ESP32-P4 USB Video Class (UVC) camera application with hardware ISP image processing.

## Features

- **Hardware ISP**: Auto white balance, exposure, color correction
- **H.264 Encoding**: Hardware-accelerated video encoding
- **UVC Compliant**: Works as USB webcam on any OS
- **Multi-Resolution**: 1920x1080@30fps, 1280x720@30fps, and more
- **FreeRTOS Multi-Task**: Table-driven task architecture

## Hardware Requirements

- ESP32-P4 Development Board (JC-ESP32P4-M3-DEV)
- OV5647 5MP Camera Module (MIPI CSI interface)
- USB connection to host PC

## Quick Start

```bash
# Setup ESP-IDF environment
. ~/esp/esp-idf/export.sh

# Build
idf.py build

# Flash
idf.py flash monitor

# Or flash to specific port
idf.py -p /dev/ttyUSB0 flash monitor
```

## Camera Configuration

The project is configured for OV5647 camera at 1920x1080@30fps with ISP enabled.

**I2C Pins (SCCB):**
- SDA: GPIO 7
- SCL: GPIO 8
- I2C Port: 0
- Frequency: 100kHz

**Supported Resolutions:**
1. 1920x1080 @ 30fps (default)
2. 1280x720 @ 15fps
3. 480x320 @ 30fps
4. 320x240 @ 30fps

## Project Structure

See [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for detailed documentation.

```
components/
├── video/     # ESP video driver (ISP enabled, customized from espressif/esp_video)
├── os/        # OS services (task management)
├── camera/    # Camera utilities
└── uvc/       # UVC streaming implementation
```

## Configuration

All configuration is in `sdkconfig.defaults.esp32p4`. To modify:

```bash
idf.py menuconfig
```

Key configs can be found under:
- Component config → Camera configuration
- Component config → ESP Video
- Component config → USB Device UVC

## ISP Features

The ISP (Image Signal Processor) provides:
- **Auto White Balance (AWB)**: Adaptive color temperature
- **Auto Exposure (AEN)**: Dynamic exposure control
- **Color Correction (ACC)**: CCM matrix, saturation
- **Sharpen**: Edge enhancement
- **Denoise (ADN)**: Noise reduction

ISP is automatically initialized on startup with sensor-specific IPA configuration.

## USB Connection

After flashing, connect ESP32-P4 USB to your computer. The device will appear as a UVC camera:

**Linux:**
```bash
# List video devices
ls /dev/video*

# Test with ffplay
ffplay /dev/video0

# Test with VLC
vlc v4l2:///dev/video0
```

**Windows:**
- Open Camera app or any video capture software
- Select "ESP32-P4 UVC Camera"

**macOS:**
- Open Photo Booth or QuickTime Player
- Select ESP32-P4 camera from device list

## Memory Usage

```
Binary size: ~535 KB
SPIRAM usage: ~430 KB (0.32% of 128MB)
DIRAM usage: ~113 KB (19.59% of 576KB)
```

## Troubleshooting

**Camera not detected:**
- Check I2C connections (GPIO 7, 8)
- Verify camera power supply
- Check `idf.py monitor` for initialization logs

**No video output:**
- Ensure USB is connected properly
- Check USB enumeration in host OS
- Verify ISP initialization in logs: "ISP pipeline controller initialized successfully"

**Poor image quality:**
- ISP may need warm-up time
- Adjust lighting conditions
- Check sensor focus

## Contributing

Want to add features or fix bugs? See [CONTRIBUTING.md](CONTRIBUTING.md) for:
- Code style guidelines
- Component development guide
- Header file templates
- Git workflow

## Documentation

- **[README.md](README.md)** - This file, quick start guide
- **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - Detailed architecture, component guide
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Development guidelines, code standards

## License

See component licenses in `managed_components/` and custom components.

## References

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [ESP32-P4 Technical Reference Manual](https://www.espressif.com/en/support/documents/technical-documents)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)
