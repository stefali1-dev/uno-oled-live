#include <Wire.h>

// Hardware check: reports idle I2C line levels and every address that answers on the bus.
void setup() {
  Serial.begin(115200);
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  Serial.print("SDA(A4) idle=");
  Serial.print(digitalRead(A4));
  Serial.print(" SCL(A5) idle=");
  Serial.println(digitalRead(A5));

  Wire.begin();
  Wire.setWireTimeout(3000, true);
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("found 0x");
      Serial.println(addr, HEX);
      found++;
    } else if (err == 5) {
      Serial.println("bus timeout: check wiring");
      break;
    }
  }
  Serial.print("devices: ");
  Serial.println(found);
}

void loop() {}
