#include <U8g2lib.h>

// Page mode (_1): draws in 128-byte strips, so the Uno's 2 KB RAM isn't eaten by a full 1 KB frame buffer.
// Software I2C because the OLED is wired to D4 (SDA) / D5 (SCL), not the Uno's hardware I2C pins A4/A5.
U8G2_SSD1306_128X64_NONAME_1_SW_I2C oled(U8G2_R0, /* clock=*/5, /* data=*/4);

void setup() {
  Serial.begin(115200);
  oled.begin();
  oled.firstPage();
  do {
    oled.drawFrame(0, 0, 128, 64);
    oled.setFont(u8g2_font_ncenB14_tr);
    oled.drawStr(34, 30, "Hello");
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(34, 50, "Arduino Uno");
  } while (oled.nextPage());
  Serial.println("hello shown");
}

void loop() {}
