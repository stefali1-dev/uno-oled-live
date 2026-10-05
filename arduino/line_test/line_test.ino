// Hardware check: drives each I2C line low and reads the other, to find shorts or stuck lines.
void show(const char *label) {
  Serial.print(label);
  Serial.print(" A4="); Serial.print(digitalRead(A4));
  Serial.print(" A5="); Serial.println(digitalRead(A5));
}

void setup() {
  Serial.begin(115200);
  pinMode(A4, INPUT_PULLUP); pinMode(A5, INPUT_PULLUP); delay(5);
  show("internal pull-ups:  ");
  pinMode(A4, INPUT); pinMode(A5, INPUT); delay(5);
  show("both released:      ");
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delay(5);
  show("A4 driven low:      ");
  pinMode(A4, INPUT); delay(5);
  show("released again:     ");
  pinMode(A5, OUTPUT); digitalWrite(A5, LOW); delay(5);
  show("A5 driven low:      ");
  pinMode(A5, INPUT);
  unsigned long t = micros();
  while (!digitalRead(A5) && micros() - t < 5000) {}
  Serial.print("A5 rise time us:    "); Serial.println(micros() - t);
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(50); pinMode(A4, INPUT);
  t = micros();
  while (!digitalRead(A4) && micros() - t < 5000) {}
  Serial.print("A4 rise time us:    "); Serial.println(micros() - t);
}

void loop() {}
