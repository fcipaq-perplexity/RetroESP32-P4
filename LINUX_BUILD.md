# Building firmware on Linux

These scripts provide Linux equivalents of the top-level Windows firmware
build, merge and flash scripts. Existing Windows scripts are unchanged.
The separate tools/build_*_papp.ps1 native-application recipes are not ported
by this patch.

## Prerequisites

Install ESP-IDF with ESP32-P4 support and its Linux prerequisites, then activate
that installation in the same terminal:

```bash
source /path/to/esp-idf/export.sh
python3 -m esptool version
```

Use the ESP-IDF Python environment; `PYTHON` can override the interpreter used
by the shell entry points. The repository does not pin an ESP-IDF version, so
this patch does not claim compatibility with a particular release.

## Build

Run from any working directory (paths below assume the repository root):

```bash
bash build_all.sh                  # LCD: launcher + 12 emulator apps + merge
bash build_all_hdmi.sh             # HDMI: same apps with HDMI config overlay
bash build_all.sh --clean          # reset only LCD Linux build/config files
```

Each project uses `build_linux_lcd` or `build_linux_hdmi` and a separate
`sdkconfig.linux.lcd` or `sdkconfig.linux.hdmi`. Existing `build` directories
and `sdkconfig` files are never deleted. Both build variants use the project's
own defaults; HDMI additionally uses launcher/sdkconfig.hdmi.defaults.
Any failed build/copy stops the entire command before merging.

LCD binaries are staged in `firmware`, HDMI binaries in `firmware_hdmi`.
The merged outputs retain the Windows names: `RetroESP32_P4_v1.bin` and
`RetroESP32_P4_HDMI_v1.bin`. A merge replaces the output only on success.

## Merge existing staged binaries

```bash
bash generate_merged_bin.sh
bash generate_merged_bin_hdmi.sh
```

Unlike the Windows merge scripts, these commands only read the corresponding
staging directory: they do not copy potentially wrong-variant files from
shared build directories. All 16 binary inputs must be present and nonempty;
partial images are deliberately rejected. Offsets and flash settings match
the existing scripts (ESP32-P4, DIO, 16 MB, 80 MHz). The unused ota_9 partition
is not populated, matching the existing build workflow.

## Flash (explicitly requested only)

Close any serial monitor holding the device first. No processes are killed.
Ensure your user has permission to access the selected serial device.

```bash
bash flash_all.sh --port /dev/ttyACM0
bash flash_all.sh --variant hdmi --port /dev/ttyACM0
ESP_PORT=/dev/ttyUSB0 bash flash_all.sh --baud 460800
```

Flashing writes all staged partitions and changes device contents. Build and
merge commands never flash. Ports are examples, not autodetected defaults.

## Validation

Run `python3 -m unittest discover -s tests -p 'test_linux_firmware.py'`.
Tests mock external commands; they do not compile firmware or access hardware.
A real ESP-IDF build and device smoke test remain necessary.
