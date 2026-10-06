#!/bin/sh
# Compile a sketch and run it headless in Wokwi. Usage: ./sim.sh <sketch dir> [wokwi-cli args]
# e.g. ./sim.sh btc --screenshot-part oled --screenshot-time 2500 --screenshot-file build/shot.png
set -e
cd "$(dirname "$0")"
sketch=$1; shift
name=$(basename "$sketch")
arduino-cli compile -b arduino:avr:uno --output-dir "build/$name" "$sketch"
cp "build/$name/$name.ino.hex" build/sim.hex && cp "build/$name/$name.ino.elf" build/sim.elf
exec ~/.wokwi/bin/wokwi-cli --timeout 3000 --serial-log-file build/serial.log "$@" .
