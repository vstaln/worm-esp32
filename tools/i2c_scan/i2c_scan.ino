// i2c_scan v2 — SSD1306 bus diagnostic.
// Scans at 100 kHz for reliability, checks bus line states (stuck-low
// detection), full address range 0x01-0x7F, both pin orientations.
#include <Wire.h>

static int err4Count;

void busState(const char *label, int sda, int scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delay(2);
  int s = digitalRead(sda), c = digitalRead(scl);
  Serial.printf("  %s -> SDA pin %d reads %s, SCL pin %d reads %s\n",
                label, sda, s ? "HIGH" : "LOW", scl, c ? "HIGH" : "LOW");
}

void scan(const char *label, int sda, int scl) {
  Serial.printf("\n--- %s (SDA=%d, SCL=%d, 100 kHz) ---\n", label, sda, scl);
  busState("idle", sda, scl);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  int found = 0, nack = 0;
  err4Count = 0;
  for (int addr = 1; addr < 128; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      found++;
      Serial.printf("  FOUND device at 0x%02X (decimal %d)%s\n", addr, addr,
                    (addr == 0x3C || addr == 0x3D) ? "  <-- SSD1306 expected here" : "");
    } else if (err == 2) {
      nack++;
    } else if (err == 4 || err == 5) {
      err4Count++;
    }
  }
  Serial.printf("  (%d found; %d NACKs; %d bus errors)\n", found, nack, err4Count);
  if (found == 0 && err4Count > 50) {
    Serial.printf("  !! BUS LOOKS STUCK — many bus errors; check for a short on SDA/SCL\n");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n== I2C bus diagnostic v2 ==");
  scan("Orientation A (SDA=21, SCL=22)", 21, 22);
  scan("Orientation B (SDA=22, SCL=21)", 22, 21);
  Serial.println("\n== scan done ==");
}

void loop() {
  delay(1000);
}
