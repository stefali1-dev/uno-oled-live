#include <avr/pgmspace.h>
#include "globe_data.h"

// ISS tracker: a spinning globe, lit by the real sun position, with the ISS orbit drawn as a 3D ring
// (hidden behind the globe) and the ISS as a sprite with a pulsing ping. iss/feed.py sends:
//   "I px py pz bx by bz sunlat sunlon;L0;...;L7\n"
// p = ISS position, b = its direction of motion (earth frame, x = lon 0, z = north, unit x127),
// sun lat/lon in 1/10 degree, L0..L7 = text for the 8 rows of the right panel.
// The OLED is on D4 (SDA) / D5 (SCL) = PD4 / PD5, driven by a bit-banged I2C.

const uint8_t SDA_BIT = _BV(PD4), SCL_BIT = _BV(PD5);
const uint8_t OLED_ADDR = 0x3C;
const uint16_t FRAME_MS = 80;  // ~12 fps: a frame measures ~80 ms on the board; 1/256 turn per frame = ~20 s per turn
const uint32_t BAUD = 38400;
const uint8_t GLOBE_COLS = 80, TEXT_X = 80, TEXT_COLS = 128 - TEXT_X;
const uint8_t RING_N = 128;    // orbit ring points
const uint8_t RING_R = 33;     // orbit radius in pixels: exaggerated so the ring clears the globe
const uint8_t TRAIL = 36;      // ring points behind the ISS drawn solid: its recent track

// Open-drain: "high" = input with internal pull-up, "low" = output driving 0. Never drives the line high.
// The lines rise slowly on this wiring, so "high" waits until the pin really reads high.
inline void waitHigh(uint8_t bit) { for (uint8_t n = 1; !(PIND & bit) && n; n++) {} }
inline void sdaRelease() { DDRD &= ~SDA_BIT; PORTD |= SDA_BIT; }
inline void sdaHigh() { sdaRelease(); waitHigh(SDA_BIT); }
inline void sdaLow() { PORTD &= ~SDA_BIT; DDRD |= SDA_BIT; }
inline void sclHigh() { DDRD &= ~SCL_BIT; PORTD |= SCL_BIT; waitHigh(SCL_BIT); __builtin_avr_delay_cycles(8); }
inline void sclLow() { PORTD &= ~SCL_BIT; DDRD |= SCL_BIT; }

void i2cWrite(uint8_t b) {
  for (uint8_t m = 0x80; m; m >>= 1) {
    if (b & m) sdaHigh(); else sdaLow();
    sclHigh();
    sclLow();
  }
  sdaRelease();  // ACK clock: the display holds SDA low here, so don't wait for high; the ACK is ignored
  sclHigh();
  sclLow();
}

void i2cBegin(uint8_t control) {
  sdaHigh(); sclHigh(); sdaLow(); sclLow();
  i2cWrite(OLED_ADDR << 1);
  i2cWrite(control);  // 0x00 = commands follow, 0x40 = display data follows
}

void i2cEnd() { sdaLow(); sclHigh(); sdaHigh(); }

const uint8_t INIT[] = {
  0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8,
  0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
};

// Writes n bytes to one 8-row page starting at column col (page addressing mode).
void sendPage(uint8_t page, uint8_t col, const uint8_t *data, uint8_t n) {
  i2cBegin(0x00);
  i2cWrite(0xB0 | page);
  i2cWrite(col & 0x0F);
  i2cWrite(0x10 | col >> 4);
  i2cEnd();
  i2cBegin(0x40);
  while (n--) i2cWrite(*data++);
  i2cEnd();
}

int8_t cosTab(uint8_t a) { return pgm_read_byte(COS_TAB + a); }
int8_t sinTab(uint8_t a) { return pgm_read_byte(COS_TAB + (uint8_t)(a - 64)); }

// State from the feed.
bool haveIss = false;
int8_t issP[3], issB[3];
int8_t sunA[64], sunB[64];  // per texture row: sin(lat)sin(sunlat), cos(lat)cos(sunlat), x127
uint8_t sunLon = 0;         // subsolar longitude as a texture column
char lines[8][9] = {"ISS", "", "WAITING", "FOR", "FEED"};
bool textDirty = true;

void setSun(float latDeg, float lonDeg) {
  float d = radians(latDeg);
  for (uint8_t r = 0; r < 64; r++) {
    float lat = radians(90 - (r + 0.5) * 180 / 64);
    sunA[r] = 127 * sin(lat) * sin(d);
    sunB[r] = 127 * cos(lat) * cos(d);
  }
  sunLon = (uint16_t)(lonDeg / 360 * 256) & 255;
}

void onMessage(char *msg) {
  if (msg[0] != 'I') return;
  char *p = msg + 1;
  int16_t v[8];
  for (uint8_t i = 0; i < 8; i++) v[i] = strtol(p, &p, 10);
  if (*p != ';') return;
  for (uint8_t i = 0; i < 3; i++) { issP[i] = v[i]; issB[i] = v[3 + i]; }
  setSun(v[6] / 10.0, v[7] / 10.0);
  for (uint8_t i = 0; i < 8; i++) {
    p++;  // skip ';'
    uint8_t n = 0;
    while (*p && *p != ';') {
      if (n < 8) lines[i][n++] = *p;
      p++;
    }
    lines[i][n] = 0;
  }
  haveIss = true;
  textDirty = true;
  Serial.println(F("ok"));  // lets feed.py confirm delivery
}

// Collects one line; applied at the next frame start so a frame never mixes old and new data.
char rx[128];
uint8_t rxLen = 0;
bool rxReady = false;

void readSerial() {
  while (!rxReady && Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      rx[rxLen] = 0;
      rxReady = true;
    } else if (ch != '\r' && rxLen < sizeof(rx) - 1) {
      rx[rxLen++] = ch;
    }
  }
}

void applySerial() {
  if (!rxReady) return;
  onMessage(rx);
  rxLen = 0;
  rxReady = false;
}

// Globe: each pixel looks up the land bitmap at (its row, its longitude + spin) and is lit when the
// sun-dependent brightness reaches its precomputed dither threshold.
const uint8_t *pixelPtr;  // walks PIXELS row by row through the frame

void renderGlobe(uint8_t *buf, uint8_t page, uint8_t spin) {
  for (uint8_t y = page * 8; y < page * 8 + 8; y++) {
    uint8_t x = pgm_read_byte(ROW_X0 + y), n = pgm_read_byte(ROW_N + y), mask = 1 << (y & 7);
    for (; n; n--, x++) {
      uint8_t lon = pgm_read_byte(pixelPtr++) + spin;
      uint8_t row = pgm_read_byte(pixelPtr++), thr = pgm_read_byte(pixelPtr++);
      if (!thr) { buf[x] |= mask; continue; }
      bool land = pgm_read_byte(LAND + row * 32 + (lon >> 3)) & (1 << (lon & 7));
      int16_t s = sunA[row] + ((sunB[row] * cosTab(lon - sunLon)) >> 7);
      uint8_t b = land ? (s > 0 ? LAND_DAY + ((s * (255 - LAND_DAY)) >> 7) : LAND_NIGHT)
                       : (s > 0 ? (s * OCEAN_DAY) >> 7 : 0);
      if (b >= thr) buf[x] |= mask;
    }
  }
}

// Orbit ring in screen coordinates; x = 255 marks a point hidden behind the globe,
// ringFront marks points on the near side (drawn solid, with a black border over the globe).
uint8_t ringX[RING_N], ringY[RING_N];
bool ringFront[RING_N];
int8_t tiltC, tiltS;

void computeRing(uint8_t spin) {
  int8_t cl = cosTab(spin), sl = sinTab(spin);
  const int16_t hide = (GLOBE_R + 2) * (GLOBE_R + 2);
  for (uint8_t k = 0; k < RING_N; k++) {
    uint8_t a = k * (256 / RING_N);
    int8_t c = cosTab(a), s = sinTab(a);
    int16_t e[3];
    for (uint8_t i = 0; i < 3; i++) e[i] = (c * issP[i] + s * issB[i]) >> 7;
    // Earth frame -> view: turn the view-center meridian onto +x, then tilt towards the viewer.
    int16_t xr = ((int32_t)e[0] * cl + (int32_t)e[1] * sl) >> 7;
    int16_t right = ((int32_t)-e[0] * sl + (int32_t)e[1] * cl) >> 7;
    int16_t toward = (xr * tiltC + e[2] * tiltS) >> 7;
    int16_t up = (e[2] * tiltC - xr * tiltS) >> 7;
    int16_t dx = right * RING_R / 127, dy = up * RING_R / 127;
    bool hidden = toward < 0 && dx * dx + dy * dy < hide;
    ringX[k] = hidden ? 255 : GLOBE_CX + dx;
    ringY[k] = GLOBE_CY - dy;
    ringFront[k] = toward >= 0;
  }
}

void setPixel(uint8_t *buf, uint8_t page, int16_t x, int16_t y, bool on) {
  if (x < 0 || x >= GLOBE_COLS || (y >> 3) != page || y < 0) return;
  if (on) buf[x] |= 1 << (y & 7);
  else buf[x] &= ~(1 << (y & 7));
}

// ISS sprite: solar panels on a truss, 7x3.
const uint8_t ISS_SPRITE[3] = {0b1101011, 0b1111111, 0b1101011};

void drawOverlay(uint8_t *buf, uint8_t page, uint8_t tick) {
  // Near side: clear a border first, then draw, so the ring reads against the dithered globe.
  for (uint8_t k = 1; k < RING_N; k++) {
    if (ringX[k] == 255 || !ringFront[k]) continue;
    setPixel(buf, page, ringX[k], ringY[k] - 1, false);
    setPixel(buf, page, ringX[k], ringY[k] + 1, false);
  }
  for (uint8_t k = 1; k < RING_N; k++) {
    if (ringX[k] == 255) continue;
    bool trail = k >= RING_N - TRAIL;
    if (ringFront[k] || trail || k % 3 == 0) setPixel(buf, page, ringX[k], ringY[k], true);
  }
  if (ringX[0] == 255) return;
  int16_t x0 = ringX[0] - 3, y0 = ringY[0] - 1;
  // Ping: a circle that grows from the ISS and restarts every 16 frames.
  uint8_t r = 4 + (tick & 15) / 2;
  for (uint8_t i = 0; i < 16; i++)
    setPixel(buf, page, ringX[0] + cosTab(i * 16) * r / 127, ringY[0] + sinTab(i * 16) * r / 127, true);
  for (int8_t y = -1; y <= 3; y++)
    for (int8_t x = -1; x <= 7; x++) {
      bool on = y >= 0 && y < 3 && x >= 0 && x < 7 && (ISS_SPRITE[y] >> (6 - x) & 1);
      setPixel(buf, page, x0 + x, y0 + y, on);  // black border keeps the ISS readable on land
    }
}

void drawText() {
  uint8_t buf[TEXT_COLS];
  for (uint8_t page = 0; page < 8; page++) {
    memset(buf, 0, sizeof(buf));
    uint8_t x = 0;
    for (const char *c = lines[page]; *c && x + 5 <= TEXT_COLS; c++, x += 6) {
      const char *hit = strchr_P(FONT_CHARS, *c);
      if (hit) memcpy_P(buf + x, FONT + (hit - FONT_CHARS) * 5, 5);
    }
    sendPage(page, TEXT_X, buf, TEXT_COLS);
  }
}

void setup() {
  Serial.begin(BAUD);
  tiltC = 127 * cos(GLOBE_TILT);
  tiltS = 127 * sin(GLOBE_TILT);
  setSun(0, 0);
  i2cBegin(0x00);
  for (uint8_t i = 0; i < sizeof(INIT); i++) i2cWrite(INIT[i]);
  i2cEnd();
  uint8_t zero[128] = {0};
  for (uint8_t page = 0; page < 8; page++) sendPage(page, 0, zero, 128);
  Serial.println(F("iss ready"));
}

void loop() {
  static uint8_t spin = 0, tick = 0;
  static uint32_t next = 0;
  while ((int32_t)(millis() - next) < 0) readSerial();
  next = millis() + FRAME_MS;
  applySerial();

  if (haveIss) computeRing(spin);
  pixelPtr = PIXELS;
  uint8_t buf[GLOBE_COLS];
  for (uint8_t page = 0; page < 8; page++) {
    memset(buf, 0, sizeof(buf));
    renderGlobe(buf, page, spin);
    if (haveIss) drawOverlay(buf, page, tick);
    readSerial();
    sendPage(page, 0, buf, GLOBE_COLS);
  }
  if (textDirty) {
    drawText();
    textDirty = false;
  }
  spin++;
  tick++;
}
