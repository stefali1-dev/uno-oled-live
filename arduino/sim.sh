#!/bin/sh
# Compile a sketch and run it headless in Wokwi. Usage: ./sim.sh <sketch> [wokwi-cli args]
# e.g. ./sim.sh hello --screenshot-part oled --screenshot-time 2500 --screenshot-file build/shot.png
# Upload the same build: arduino-cli upload -b arduino:avr:uno -p /dev/ttyUSB0 --input-dir build/<sketch> <sketch>
set -e
cd "$(dirname "$0")"
sketch=$1; shift
arduino-cli compile -b arduino:avr:uno --output-dir "build/$sketch" "$sketch"
cp "build/$sketch/$sketch.ino.hex" build/sim.hex && cp "build/$sketch/$sketch.ino.elf" build/sim.elf
exec ~/.wokwi/bin/wokwi-cli --timeout 3000 --serial-log-file build/serial.log "$@" .
