"""Feeds the ISS position to the Uno over USB serial every 5 s, from wheretheiss.at (no key).

Usage: iss/feed.py [port]   (default /dev/ttyUSB0; --once prints one message and exits)
"""
import json
import math
import sys
import time
import urllib.request

API = "https://api.wheretheiss.at/v1/satellites/25544/positions?units=kilometers&timestamps={},{}"
BAUD = 38400  # must match iss.ino
INTERVAL_S = 5


def unit(lat, lon):
    lat, lon = math.radians(lat), math.radians(lon)
    return [math.cos(lat) * math.cos(lon), math.cos(lat) * math.sin(lon), math.sin(lat)]


def message():
    now = int(time.time())
    req = urllib.request.Request(API.format(now, now + 60), headers={"User-Agent": "pico-btc"})
    with urllib.request.urlopen(req, timeout=10) as r:
        cur, later = json.load(r)
    p, q = unit(cur["latitude"], cur["longitude"]), unit(later["latitude"], later["longitude"])
    # Direction of motion: the part of the later position perpendicular to the current one.
    dot = sum(a * b for a, b in zip(p, q))
    b = [qi - dot * pi for pi, qi in zip(p, q)]
    norm = math.sqrt(sum(v * v for v in b))
    b = [v / norm for v in b]

    lat, lon = cur["latitude"], cur["longitude"]
    lines = [
        "ISS",
        "",
        f"{abs(lat):.1f} {'N' if lat >= 0 else 'S'}",
        f"{abs(lon):.1f} {'E' if lon >= 0 else 'W'}",
        f"{cur['altitude']:.0f} KM",
        f"{cur['velocity'] / 3600:.2f}KM/S",
        "",
        "SUNLIT" if cur["visibility"] == "daylight" else "IN SHADE",
    ]
    nums = [round(127 * v) for v in p + b] + [round(cur["solar_lat"] * 10), round(cur["solar_lon"] % 360 * 10)]
    return "I " + " ".join(map(str, nums)) + ";" + ";".join(lines) + "\n"


def main():
    if "--once" in sys.argv:
        print(message(), end="")
        return
    import serial

    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    with serial.Serial(port, BAUD, timeout=0) as s:
        time.sleep(2)  # opening the port resets the Uno
        while True:
            try:
                msg = message()
                s.write(msg.encode())
                time.sleep(1)
                acked = "ok" in s.read(200).decode(errors="replace")
                where = " ".join(msg.split(";")[3:5])
                print(time.strftime("%H:%M:%S"), where, "board ok" if acked else "NO REPLY from board", flush=True)
            except (OSError, ValueError, KeyError) as e:
                print("fetch failed:", e, flush=True)
            time.sleep(INTERVAL_S - 1)


main()
