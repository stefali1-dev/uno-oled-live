#include <U8g2lib.h>
#include <avr/pgmspace.h>
#include "coin_frames.h"

// BTC ticker: spinning coin on the left, price on the right with odometer-rolling digits,
// 24 h change and a 24 h sparkline. btc/feed.py sends the data over USB serial as
//   "P <price> <change in 1/100 %> <70 spark chars 'a'..'r'>\n"
// The OLED is on D4 (SDA) / D5 (SCL) = PD4 / PD5, driven by a fast bit-banged I2C.

const uint8_t SDA_BIT = _BV(PD4), SCL_BIT = _BV(PD5);
const uint16_t FRAME_MS = 40;            // 25 fps
// A frame takes ~35 ms to draw, so serial is also read between page strips; 38400 baud
// keeps the 64-byte RX buffer from overflowing in between.
const uint32_t BAUD = 38400;
const uint32_t STALE_MS = 120000UL;      // no update for 2 min -> data marked stale
const uint8_t PANEL_X = 58;              // right panel starts here; the coin owns columns 0..55
const uint8_t SPARK_N = 70;  // spark chars 'a'..'r' = 18 levels, bottom of the screen up
const uint8_t PRICE_BASE = 31, DIGIT_H = 17, ROLL_FRAMES = 10;

// Open-drain: "high" = input with internal pull-up, "low" = output driving 0. Never drives the line high.
// The lines rise slowly and unevenly on this wiring (measured 0.2-24 us), so "high" waits until the
// pin really reads high. Without that the display misread bits and drew the price in wrong places.
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

// U8g2 transport callbacks: route its I2C bytes through the fast bit-bang above.
uint8_t byteCb(u8x8_t *u8x8, uint8_t msg, uint8_t n, void *data) {
  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      sdaHigh(); sclHigh(); sdaLow(); sclLow();
      i2cWrite(u8x8_GetI2CAddress(u8x8));
      break;
    case U8X8_MSG_BYTE_SEND:
      for (uint8_t *p = (uint8_t *)data; n--; ) i2cWrite(*p++);
      break;
    case U8X8_MSG_BYTE_END_TRANSFER:
      sdaLow(); sclHigh(); sdaHigh();
      break;
  }
  return 1;
}

uint8_t gpioCb(u8x8_t *, uint8_t msg, uint8_t n, void *) {
  if (msg == U8X8_MSG_DELAY_MILLI) delay(n);
  return 1;
}

U8G2 oled;

// Ticker state, filled by the serial feed.
bool haveData = false;
uint32_t lastUpdate = 0;
int16_t change = 0;                        // 24 h change in 1/100 %
char spark[SPARK_N + 1];
char shown[9] = "", previous[9] = "";      // formatted prices, e.g. "85,682"
uint8_t roll = ROLL_FRAMES;                // frames into the odometer roll; ROLL_FRAMES = settled
bool rollUp = true;
uint32_t price = 0;
bool panelDirty = true;                    // panel needs a full redraw (new data, roll, stale change)

void formatPrice(uint32_t p, char *out) {
  char digits[11];
  ultoa(p, digits, 10);
  uint8_t n = strlen(digits), j = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (i && (n - i) % 3 == 0) out[j++] = ',';
    out[j++] = digits[i];
  }
  out[j] = 0;
}

void onMessage(char *line) {
  if (line[0] != 'P') return;
  char *end;
  uint32_t p = strtoul(line + 2, &end, 10);
  int16_t c = strtol(end, &end, 10);
  while (*end == ' ') end++;
  if (!p || strlen(end) < SPARK_N) return;

  memcpy(spark, end, SPARK_N);
  change = c;
  lastUpdate = millis();
  char next[9];
  formatPrice(p, next);
  if (haveData && strcmp(next, shown) != 0 && strlen(next) == strlen(shown)) {
    strcpy(previous, shown);
    roll = 0;
    rollUp = p > price;
  }
  strcpy(shown, next);
  price = p;
  haveData = true;
  panelDirty = true;
  Serial.print("ok ");  // lets feed.py confirm delivery
  Serial.println(p);
}

// Collects one line; it is applied at the next frame start (applySerial) so a frame never
// mixes old and new data between its page strips.
char rx[96];
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

// Draws the price right-aligned; digits that changed roll in from below (price up) or above (down).
void drawPrice() {
  oled.setFont(u8g2_font_logisoso16_tn);
  int16_t x = 127 - oled.getStrWidth(shown);
  const uint8_t top = PRICE_BASE - DIGIT_H + 1;
  // Distance the new digit still has to travel; shrinks quadratically (ease out).
  uint8_t t = ROLL_FRAMES - roll;
  int8_t off = (int16_t)DIGIT_H * t * t / (ROLL_FRAMES * ROLL_FRAMES);
  int8_t dir = rollUp ? 1 : -1;
  for (uint8_t i = 0; shown[i]; i++) {
    char cur[2] = {shown[i], 0};
    if (roll < ROLL_FRAMES && previous[i] != shown[i]) {
      char old[2] = {previous[i], 0};
      oled.setClipWindow(x, top, x + 12, PRICE_BASE + 1);
      oled.drawStr(x, PRICE_BASE + dir * off - dir * DIGIT_H, old);
      x += oled.drawStr(x, PRICE_BASE + dir * off, cur);
      oled.setMaxClipWindow();
    } else {
      x += oled.drawStr(x, PRICE_BASE, cur);
    }
  }
  oled.setFont(u8g2_font_5x7_tr);
}

// True if rows y0..y1 overlap the 8-row page strip being drawn. Page mode redraws the panel
// once per strip, so skipping what's off-strip cuts the frame time.
bool onPage(uint8_t y0, uint8_t y1) {
  uint8_t top = oled.getU8g2()->tile_curr_row * 8;
  return y1 >= top && y0 < top + 8;
}

bool isFresh() { return haveData && millis() - lastUpdate < STALE_MS; }

void drawPanel(bool fresh) {
  oled.setFont(u8g2_font_5x7_tr);
  if (onPage(0, 7)) {
    oled.drawStr(PANEL_X + 2, 7, "BTC/USD");
    if (fresh) oled.drawDisc(124, 3, 2);
    else oled.drawCircle(124, 3, 2);
  }

  if (!haveData) {
    if (onPage(19, 35)) {
      oled.drawStr(PANEL_X + 2, 26, "waiting for");
      oled.drawStr(PANEL_X + 2, 35, "btc/feed.py");
    }
    return;
  }
  if (onPage(PRICE_BASE - DIGIT_H, PRICE_BASE + 1)) drawPrice();

  // 24 h change with an up/down arrow.
  const uint8_t y = 42;
  if (onPage(y - 7, y)) drawChange(y);

  // Sparkline of the last 24 h, newest point marked.
  if (onPage(44, 63)) {
    uint8_t prevY = 0;
    for (uint8_t i = 0; i < SPARK_N; i++) {
      uint8_t py = 63 - (spark[i] - 'a');
      if (i) oled.drawLine(PANEL_X + i - 1, prevY, PANEL_X + i, py);
      prevY = py;
    }
    oled.drawDisc(PANEL_X + SPARK_N - 1, prevY, 1);
  }
}

void drawChange(uint8_t y) {
  if (change >= 0) oled.drawTriangle(PANEL_X + 2, y, PANEL_X + 8, y, PANEL_X + 5, y - 5);
  else oled.drawTriangle(PANEL_X + 2, y - 5, PANEL_X + 8, y - 5, PANEL_X + 5, y);
  // "+1.23% 24h", built by hand: sprintf alone would overflow the Uno's flash.
  char text[16];
  int16_t a = abs(change);
  text[0] = change >= 0 ? '+' : '-';
  itoa(a / 100, text + 1, 10);
  char *p = text + strlen(text);
  *p++ = '.';
  *p++ = '0' + a % 100 / 10;
  *p++ = '0' + a % 10;
  strcpy(p, "% 24h");
  oled.drawStr(PANEL_X + 12, y, text);
}

void setup() {
  Serial.begin(BAUD);
  u8g2_Setup_ssd1306_i2c_128x64_noname_1(oled.getU8g2(), U8G2_R0, byteCb, gpioCb);
  oled.begin();
  Serial.println("btc ready");
}

void loop() {
  static uint8_t frame = 0;
  static uint32_t next = 0;
  while ((int32_t)(millis() - next) < 0) readSerial();
  next = millis() + FRAME_MS;
  applySerial();

  // The display keeps what it was sent, so most frames only send the coin (~15 ms); the
  // panel is redrawn through U8g2 (~100 ms) only when it changes.
  static bool drawnFresh = false;
  bool fresh = isFresh();
  if (fresh != drawnFresh || roll < ROLL_FRAMES) panelDirty = true;

  if (panelDirty) {
    oled.firstPage();
    do {
      readSerial();
      drawPanel(fresh);
      // Page mode: copy this 8-row strip of the pre-rendered coin straight into the page buffer.
      uint8_t page = oled.getU8g2()->tile_curr_row;
      memcpy_P(oled.getBufferPtr(), COIN_DATA[frame] + page * COIN_W, COIN_W);
    } while (oled.nextPage());
    panelDirty = false;
    drawnFresh = fresh;
  } else {
    uint8_t strip[COIN_W];
    for (uint8_t page = 0; page < 8; page++) {
      readSerial();
      memcpy_P(strip, COIN_DATA[frame] + page * COIN_W, COIN_W);
      u8x8_DrawTile(oled.getU8x8(), 0, page, COIN_W / 8, strip);
    }
  }

  frame = (frame + 1) % COIN_FRAMES;
  if (roll < ROLL_FRAMES) roll++;
}
