#!/bin/bash
PORT=${1:-/dev/ttyUSB0}
echo "IonOS Monitor — $PORT (Ctrl+] to exit)"
idf.py -p "$PORT" monitor
