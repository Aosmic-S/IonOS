#!/bin/bash
# Erase NVS partition (resets WiFi credentials, settings)
PORT=${1:-/dev/ttyUSB0}
echo "Erasing NVS partition on $PORT..."
python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 -p "$PORT" -b 460800 \
    erase_region 0x9000 0x6000
echo "Done. NVS cleared."
