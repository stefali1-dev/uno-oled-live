#!/bin/sh
# Build the sim firmware from src/ and run it headless. Extra args go to wokwi-cli,
# e.g. ./sim.sh --screenshot-part oled --screenshot-time 7500 --screenshot-file build/shot.png
set -e
cd "$(dirname "$0")"
.venv/bin/python tools/build_sim.py
exec ~/.wokwi/bin/wokwi-cli --timeout 8000 --serial-log-file build/serial.log "$@" .
