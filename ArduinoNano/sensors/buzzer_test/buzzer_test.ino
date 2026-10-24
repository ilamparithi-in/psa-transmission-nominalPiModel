// Arduino Nano Buzzer Frequency Sweep Test
// Pin: D3
//
// This sketch tests your buzzer on Pin D3 by:
// 1. Sweeping through audible frequencies (300 Hz - 4000 Hz) upwards and downwards.
// 2. Printing the current frequency to Serial Monitor (115200 baud).
// 3. Testing alarm siren pulses.
//
// NOTE:
// - If your buzzer changes pitch across the sweep, it is a PASSIVE buzzer.
// - If it only clicks or buzzes at one constant pitch regardless of frequency, it is an ACTIVE buzzer.

#define BUZZER_PIN 3

const int START_FREQ = 300;   // Starting frequency in Hz
const int END_FREQ   = 4000;  // Maximum frequency in Hz
const int STEP_FREQ  = 25;    // Step size in Hz
const int STEP_DELAY = 15;    // Milliseconds per step

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(BUZZER_PIN, OUTPUT);

  Serial.println(F("========================================"));
  Serial.println(F(" Arduino Nano Buzzer Test (Pin D3)"));
  Serial.println(F(" Sweeping from 300 Hz to 4000 Hz..."));
  Serial.println(F("========================================"));
  delay(1000);
}

void loop() {
  // -------------------------------------------------------------
  // Test 1: Upward Frequency Sweep (300 Hz -> 4000 Hz)
  // -------------------------------------------------------------
  Serial.println(F(">>> Sweeping UP (300 Hz -> 4000 Hz)..."));
  for (int freq = START_FREQ; freq <= END_FREQ; freq += STEP_FREQ) {
    tone(BUZZER_PIN, freq);
    delay(STEP_DELAY);
  }

  // -------------------------------------------------------------
  // Test 2: Downward Frequency Sweep (4000 Hz -> 300 Hz)
  // -------------------------------------------------------------
  Serial.println(F(">>> Sweeping DOWN (4000 Hz -> 300 Hz)..."));
  for (int freq = END_FREQ; freq >= START_FREQ; freq -= STEP_FREQ) {
    tone(BUZZER_PIN, freq);
    delay(STEP_DELAY);
  }

  noTone(BUZZER_PIN);
  delay(500);

  // -------------------------------------------------------------
  // Test 3: Common Alarm Frequencies (Stepped)
  // -------------------------------------------------------------
  int testFreqs[] = { 1000, 1500, 2000, 2500, 3000 };
  Serial.println(F(">>> Testing Key Frequencies (listen for peak volume):"));
  for (int i = 0; i < 5; i++) {
    Serial.print(F("Tone: "));
    Serial.print(testFreqs[i]);
    Serial.println(F(" Hz"));

    tone(BUZZER_PIN, testFreqs[i]);
    delay(300);
    noTone(BUZZER_PIN);
    delay(150);
  }

  delay(500);

  // -------------------------------------------------------------
  // Test 4: Fault Alarm Siren Simulation (Dual-Tone Alternating)
  // -------------------------------------------------------------
  Serial.println(F(">>> Testing Dual-Tone Fault Alarm Siren..."));
  for (int i = 0; i < 6; i++) {
    tone(BUZZER_PIN, 2500); // High pitch
    delay(120);
    tone(BUZZER_PIN, 1800); // Low pitch
    delay(120);
  }

  noTone(BUZZER_PIN);
  Serial.println(F("--- Cycle Complete. Pausing 2 seconds ---\n"));
  delay(2000);
}
