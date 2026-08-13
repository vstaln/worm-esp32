// wirecheck — live wiring diagnostic.
// Every cycle it reports the electrical state of the two data pins and
// rescans for the OLED, so you can watch what changes as you poke or
// re-seat each wire. Serial at 115200.
//
// Reading guide:
//   D21/D22 = HI  -> line idles high (nothing dragging it down):
//                    either no wire on that pin, or module powered.
//   D21/D22 = lo  -> something is pulling the pin low: a wire IS on the
//                    pin and the module is unpowered (VDD/GND problem)
//                    or the line is shorted to GND.
//   [OLED at 0x..] -> module found and communicating. Fix complete.
#include <Wire.h>

void oneCycle() {
  // Line states with the ESP32 internal pull-up (same as idle I2C bus).
  pinMode(21, INPUT_PULLUP);
  pinMode(22, INPUT_PULLUP);
  delay(2);
  int s21 = digitalRead(21), s22 = digitalRead(22);
  Serial.printf("D21=%s  D22=%s   ", s21 ? "HI " : "lo ", s22 ? "HI " : "lo ");

  // Is the OLED answering on either pin orientation?
  int hit = 0;
  Wire.begin(21, 22);
  Wire.setClock(100000);
  for (int a = 0x3C; a <= 0x3D; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("[OLED @ 0x%02X on D21/D22] ", a); hit = 1; }
  }
  Wire.begin(22, 21);
  Wire.setClock(100000);
  for (int a = 0x3C; a <= 0x3D; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("[OLED @ 0x%02X on D22/D21] ", a); hit = 1; }
  }
  if (!hit) Serial.print("[no OLED]");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("== wirecheck: D21/D22 states + OLED probe, once per second ==");
}

void loop() {
  oneCycle();
  delay(1000);
}
