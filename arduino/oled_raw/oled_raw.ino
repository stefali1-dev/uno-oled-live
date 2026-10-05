// Hardware check: sends the SSD1306 power-on sequence by bit-banged I2C on D4/D5, checks every byte is ACKed,
// then blinks "all pixels on" (0xA5) / "show RAM" (0xA4) once a second so the result is visible.
const byte SDA_PIN = 4, SCL_PIN = 5, ADDR = 0x3C;

void release(byte p) { pinMode(p, INPUT_PULLUP); }  // internal pull-up, so the bus works even if the module's are missing
void pull(byte p) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
void tick() { delayMicroseconds(10); }
void start() { release(SDA_PIN); release(SCL_PIN); tick(); pull(SDA_PIN); tick(); pull(SCL_PIN); tick(); }
void stop() { pull(SDA_PIN); tick(); release(SCL_PIN); tick(); release(SDA_PIN); tick(); }

bool writeByte(byte b) {
  for (byte i = 0; i < 8; i++) {
    if (b & 0x80) release(SDA_PIN); else pull(SDA_PIN);
    b <<= 1;
    tick(); release(SCL_PIN); tick(); pull(SCL_PIN);
  }
  release(SDA_PIN); tick(); release(SCL_PIN); tick();
  bool ack = digitalRead(SDA_PIN) == LOW;
  pull(SCL_PIN); tick();
  return ack;
}

// Sends one command byte; returns true if address, control byte and command were all ACKed.
bool command(byte c) {
  start();
  bool ok = writeByte(ADDR << 1) & writeByte(0x00) & writeByte(c);
  stop();
  return ok;
}

const byte INIT[] = {
  0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
  0xDA, 0x12, 0x81, 0xFF, 0xD9, 0xF1, 0xDB, 0x40, 0xA6, 0xAF,
};

void setup() {
  Serial.begin(115200);
  pinMode(SDA_PIN, INPUT); pinMode(SCL_PIN, INPUT);
  Serial.print("idle SDA="); Serial.print(digitalRead(SDA_PIN));
  Serial.print(" SCL="); Serial.println(digitalRead(SCL_PIN));
  int nacks = 0;
  for (byte i = 0; i < sizeof(INIT); i++) {
    if (!command(INIT[i])) { nacks++; Serial.print("NACK on 0x"); Serial.println(INIT[i], HEX); }
  }
  Serial.print("init bytes: "); Serial.print(sizeof(INIT));
  Serial.print(" nacks: "); Serial.println(nacks);
}

void loop() {
  static bool on = false;
  on = !on;
  bool ok = command(on ? 0xA5 : 0xA4);
  Serial.print(on ? "ALL PIXELS ON" : "pixels from RAM");
  Serial.println(ok ? "" : "  (NACK)");
  delay(1000);
}
