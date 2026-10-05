// Hardware check: lists pins pulled high by something external (e.g. the OLED's I2C pull-ups).
void setup() {
  Serial.begin(115200);
  const char *names[] = {"D2","D3","D4","D5","D6","D7","D8","D9","D10","D11","D12","D13","A0","A1","A2","A3","A4","A5"};
  const byte pins[] = {2,3,4,5,6,7,8,9,10,11,12,13,A0,A1,A2,A3,A4,A5};
  for (byte i = 0; i < sizeof(pins); i++) {
    pinMode(pins[i], INPUT);
    delay(2);
    Serial.print(names[i]);
    Serial.print("=");
    Serial.print(digitalRead(pins[i]));
    Serial.print(" ");
  }
  Serial.println();
}

void loop() {}
