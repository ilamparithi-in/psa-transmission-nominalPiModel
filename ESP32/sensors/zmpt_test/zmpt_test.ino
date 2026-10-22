#define ZMPT_PIN 2 // Use GPIO 3 for ESP32-C3, or A0 for Arduino Uno
#define RELAY_PING

void setup() {
  Serial.begin(115200);
}

void loop() {
  int raw = analogRead(ZMPT_PIN);

  // Format for Serial Plotter
  // Serial.print("Raw:");
  Serial.print(raw);
  Serial.print("\n");
  // Serial.print(" Min_Limit:64 Max_Limit:4096\n"); // Reference bounds

  delayMicroseconds(500); // Fast enough for 50Hz/60Hz wave (sampling ~5kHz)
}
