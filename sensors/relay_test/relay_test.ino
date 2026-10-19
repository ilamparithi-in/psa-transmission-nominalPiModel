#define ZMPT_PIN 3 // Use GPIO 3 for ESP32-C3, or A0 for Arduino Uno
#define RELAY_PIN 2
#define BUTTON_PIN 9 // BOOT button on ESP32-C3

void relayTask(void *pvParameters) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  bool isRunning =
      false; // Set to true if you want it to run immediately on boot
  bool relayState = false; // false = OFF, true = ON
  bool lastButtonState = HIGH;
  TickType_t lastToggleTick = xTaskGetTickCount();

  while (true) {
    bool currentButtonState = digitalRead(BUTTON_PIN);

    // Detect button press (falling edge)
    if (lastButtonState == HIGH && currentButtonState == LOW) {
      isRunning = !isRunning;
      if (!isRunning) {
        relayState = false;
        digitalWrite(RELAY_PIN, HIGH); // Ensure relay is OFF when paused
      } else {
        relayState = true;
        digitalWrite(RELAY_PIN,
                     LOW); // Turn ON immediately when started (active-low)
        lastToggleTick = xTaskGetTickCount();
      }
      vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
      lastButtonState = LOW;
      continue;
    }
    lastButtonState = currentButtonState;

    if (isRunning) {
      if ((xTaskGetTickCount() - lastToggleTick) >= pdMS_TO_TICKS(500)) {
        relayState = !relayState;
        digitalWrite(RELAY_PIN,
                     relayState ? LOW : HIGH); // LOW is ON (active-low)
        lastToggleTick = xTaskGetTickCount();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(921600);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // OFF initially (NC)

  xTaskCreate(relayTask,   // Task function
              "relayTask", // Task name
              2048,        // Stack size
              NULL,        // Task input parameter
              1,           // Priority
              NULL         // Task handle
  );
}

void loop() {
  int raw = analogRead(ZMPT_PIN);

  // Format for Serial Plotter
  Serial.print("Raw:");
  Serial.print(raw);
  Serial.print(" Min_Limit:64 Max_Limit:4096\n"); // Reference bounds

  delayMicroseconds(500); // Fast enough for 50Hz/60Hz wave (sampling ~2kHz)
}
