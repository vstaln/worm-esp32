// i2c_hunt — find which GPIO pair the OLED is on (if any).
// Tries every pair of candidate pins, checking only the two SSD1306
// addresses (0x3C, 0x3D). Prints each hit.
// Skips UART0 pins (1,3), flash pins (6-11), strapping pins (0,2,5,12,15)
// and PSRAM-capable pins (16,17) to stay safe.
#include <Wire.h>

const int pins[] = {4, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const int nPins = sizeof(pins) / sizeof(pins[0]);

void tryPair(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(400000);
  for (int addr = 0x3C; addr <= 0x3D; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(">>> HIT: SDA=GPIO%d, SCL=GPIO%d, address 0x%02X\n", sda, scl, addr);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n== I2C pin hunt (0x3C/0x3D on all GPIO pairs) ==");
  for (int i = 0; i < nPins; i++)
    for (int j = 0; j < nPins; j++) {
      if (i == j) continue;
      tryPair(pins[i], pins[j]);
    }
  Serial.println("== hunt done ==");
}

void loop() {
  delay(1000);
}
