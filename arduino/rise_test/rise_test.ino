// Hardware check: how long D4 (SDA) / D5 (SCL) take to rise after release, with the module's
// pull-ups only and with the Uno's internal pull-ups added. Counts loop iterations (~0.19 us each).
uint16_t rise(uint8_t bit, bool internal) {
  PORTD &= ~bit; DDRD |= bit;            // drive low
  delayMicroseconds(20);
  noInterrupts();
  DDRD &= ~bit;                           // release
  if (internal) PORTD |= bit;
  uint16_t n = 0;
  while (!(PIND & bit) && n < 60000) n++;
  interrupts();
  PORTD &= ~bit;
  return n;
}

void setup() {
  Serial.begin(115200);
  for (uint8_t k = 0; k < 3; k++) {
    Serial.print("SDA module-only="); Serial.print(rise(_BV(PD4), false));
    Serial.print(" +internal="); Serial.print(rise(_BV(PD4), true));
    Serial.print("   SCL module-only="); Serial.print(rise(_BV(PD5), false));
    Serial.print(" +internal="); Serial.println(rise(_BV(PD5), true));
  }
}

void loop() {}
