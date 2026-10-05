// Hardware check: bit-banged I2C scan with A4/A5 as SDA/SCL, then swapped, to catch crossed wires.
byte sda, scl;

void release(byte p) { pinMode(p, INPUT); }        // let the pull-up take the line high
void pull(byte p) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
void tick() { delayMicroseconds(10); }

void start() { release(sda); release(scl); tick(); pull(sda); tick(); pull(scl); tick(); }
void stop() { pull(sda); tick(); release(scl); tick(); release(sda); tick(); }

bool writeByte(byte b) {
  for (byte i = 0; i < 8; i++) {
    if (b & 0x80) release(sda); else pull(sda);
    b <<= 1;
    tick(); release(scl); tick(); pull(scl);
  }
  release(sda); tick(); release(scl); tick();
  bool ack = digitalRead(sda) == LOW;
  pull(scl); tick();
  return ack;
}

void scan(byte sdaPin, byte sclPin, const char *label) {
  sda = sdaPin; scl = sclPin;
  Serial.print(label);
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    start();
    if (writeByte(addr << 1)) { Serial.print(" 0x"); Serial.print(addr, HEX); found++; }
    stop();
  }
  Serial.print(" -> devices: ");
  Serial.println(found);
}

void setup() {
  Serial.begin(115200);
  scan(4, 5, "SDA=D4 SCL=D5:");
  scan(5, 4, "SDA=D5 SCL=D4 (swapped):");
}

void loop() {}
