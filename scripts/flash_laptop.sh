#!/bin/sh
# Flash Worm Life to the ESP32 from the Void laptop.
# Usage: ./flash_laptop.sh [serial_port]
#   serial_port defaults to the first /dev/ttyUSB* or /dev/ttyACM* found.
# Requires: esptool (pip install esptool) on the laptop.
set -e
PORT="$1"
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)
fi
if [ -z "$PORT" ]; then
    echo "error: no ESP32 serial port found. Plug in the ESP32 via USB." >&2
    exit 1
fi
echo "==> flashing $PORT with build/worm_life_merged.bin"
esptool.py --chip esp32 --port "$PORT" --baud 921600 \
    write_flash -z 0x0 build/worm_life_merged.bin
echo "==> done. The worm should appear on the OLED within seconds."
