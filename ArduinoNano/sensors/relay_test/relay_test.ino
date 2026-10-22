// Arduino Nano Relay and ZMPT101B Test
// 10-bit ADC (0-1023), 5V logic

#define ZMPT_PIN   A0
#define RELAY_PIN  2
#define BUTTON_PIN 3

bool isRunning = false;        // Set to true if you want it to run immediately on boot
bool relayState = false;       // false = OFF, true = ON (Active-Low relay: LOW = ON, HIGH = OFF)
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastToggleTime = 0;
const unsigned long toggleInterval = 500; // Relay toggle interval in ms

unsigned long lastSampleMicros = 0;
const unsigned long sampleIntervalMicros = 500; // ~2kHz sampling for 50Hz wave

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // OFF initially (Active-Low)

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void handleButtonAndRelay() {
  int reading = digitalRead(BUTTON_PIN);

  // Check for button press (falling edge) with debounce
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    static int buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;

      // Button pressed (LOW)
      if (buttonState == LOW) {
        isRunning = !isRunning;
        if (!isRunning) {
          relayState = false;
          digitalWrite(RELAY_PIN, HIGH); // Ensure relay is OFF when paused
        } else {
          relayState = true;
          digitalWrite(RELAY_PIN, LOW);  // Turn ON immediately when started
          lastToggleTime = millis();
        }
      }
    }
  }
  lastButtonState = reading;

  // Toggle relay every toggleInterval ms when running
  if (isRunning) {
    if (millis() - lastToggleTime >= toggleInterval) {
      relayState = !relayState;
      digitalWrite(RELAY_PIN, relayState ? LOW : HIGH); // LOW is ON (active-low)
      lastToggleTime = millis();
    }
  }
}

void loop() {
  // Non-blocking button and relay control
  handleButtonAndRelay();

  // Non-blocking analog sampling at ~2kHz
  unsigned long currentMicros = micros();
  if (currentMicros - lastSampleMicros >= sampleIntervalMicros) {
    lastSampleMicros = currentMicros;

    int raw = analogRead(ZMPT_PIN);

    // Format for Serial Plotter (10-bit ADC: 0 to 1023)
    Serial.print(F("Raw:"));
    Serial.print(raw);
    Serial.print(F(" Min_Limit:0 Max_Limit:1023\n"));
  }
}
