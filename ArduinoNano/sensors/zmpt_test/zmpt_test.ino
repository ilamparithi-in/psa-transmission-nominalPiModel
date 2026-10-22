// Arduino Nano ZMPT101B Raw Reading Test
// 10-bit ADC (0 - 1023), 5.0V logic

#define ZMPT_PIN A0 // Arduino Nano Analog Pin A0

void setup() {
  Serial.begin(115200);
}

void loop() {
  int raw = analogRead(ZMPT_PIN);

  // Format for Serial Plotter (10-bit bounds: 0 to 1023)
  Serial.print("Raw:");
  Serial.print(raw);
  Serial.print(" Min_Limit:0 Max_Limit:1023\n");

  delayMicroseconds(500); // Fast enough for 50Hz/60Hz wave (~2kHz sampling)
}
