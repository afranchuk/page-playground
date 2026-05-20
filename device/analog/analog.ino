#include <Wire.h>

uint8_t which = 0;

void receive(int numBytes) {
  while (Wire.available()) {
    which = Wire.read();
  }
}

void request() {
  int val = analogRead(A0 + which);
  uint8_t buf[2] = {uint8_t((val >> 8) & 0xff), uint8_t(val & 0xff)};
  Wire.write(buf, 2);
}

void setup() {
  Wire.setSDA(D0);
  Wire.setSCL(D1);
  Wire.begin(0x20);
  Wire.onReceive(receive);
  Wire.onRequest(request);
}

void loop() {
  delay(100);
}
