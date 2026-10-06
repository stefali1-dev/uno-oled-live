"""Feeds the BTC price to the Uno over USB serial every 30 s, from Coinbase's public API (no key).

Usage: btc/feed.py [port]   (default /dev/ttyUSB0; --once prints one message and exits)
"""
import json
import sys
import time
import urllib.request

API = "https://api.exchange.coinbase.com/products/BTC-USD"
SPARK_N, SPARK_LEVELS = 70, 18  # must match btc.ino
BAUD = 38400  # must match btc.ino
INTERVAL_S = 30


def get(path):
    req = urllib.request.Request(API + path, headers={"User-Agent": "pico-btc"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def message():
    stats = get("/stats")
    last, open_24h = float(stats["last"]), float(stats["open"])
    # 15-minute candles, newest first: [time, low, high, open, close, volume]. 96 of them = 24 h.
    closes = [c[4] for c in reversed(get("/candles?granularity=900")[:96])] + [last]
    points = [closes[round(i * (len(closes) - 1) / (SPARK_N - 1))] for i in range(SPARK_N)]
    lo, hi = min(points), max(points)
    spark = "".join(chr(ord("a") + round((p - lo) / ((hi - lo) or 1) * (SPARK_LEVELS - 1))) for p in points)
    change = round((last - open_24h) / open_24h * 10000)
    return f"P {round(last)} {change} {spark}\n"


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
                price = msg.split()[1]
                acked = f"ok {price}" in s.read(200).decode(errors="replace")
                print(time.strftime("%H:%M:%S"), price, "board ok" if acked else "NO REPLY from board", flush=True)
            except (OSError, ValueError, KeyError) as e:
                print("fetch failed:", e, flush=True)
            time.sleep(INTERVAL_S - 1)


main()
