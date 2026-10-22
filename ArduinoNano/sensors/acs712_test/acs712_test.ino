// Arduino Nano ACS712 AC Current Sensor Test
// 10-bit ADC (1023 steps), 5.0V logic

#include "ACS712.h"

// Arduino Nano: 5.0 volt with a max ADC value of 1023 steps
// ACS712 5A  uses 185 mV per A
// ACS712 20A uses 100 mV per A
// ACS712 30A uses  66 mV per A

// Pin A1, 5.0V logic, 1023 resolution (10-bit), 100 mV/A (20A sensor)
ACS712 ACS(A1, 5.0, 1023, 100);

uint32_t start, stop;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println();
  Serial.println(F(__FILE__));
  Serial.print(F("ACS712_LIB_VERSION: "));
  Serial.println(ACS712_LIB_VERSION);
  Serial.println();

  ACS.autoMidPoint();
  Serial.print(F("MidPoint: "));
  Serial.print(ACS.getMidPoint());
  Serial.print(F(". Noise mV: "));
  Serial.println(ACS.getNoisemV());
}

void loop() {
  delay(100);
  start = micros();
  int mA = ACS.mA_AC_sampling();
  stop = micros();

  Serial.print(F("mA: "));
  Serial.print(mA);
  Serial.print(F(". Form factor: "));
  Serial.print(ACS.getFormFactor());
  Serial.print(F("  time: "));
  Serial.println(stop - start);

  delay(2000);
}
