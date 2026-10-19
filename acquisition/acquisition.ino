#include <ZMPT101B.h>

// Sensor configuration structure
struct VoltageSensor {
  const char* name;
  uint8_t pin;
  float sensitivity;
  int zeroBaseline;
  ZMPT101B driver;

  VoltageSensor(const char* n, uint8_t p, float sens, int zero = 2282, float freq = 50.0f)
    : name(n), pin(p), sensitivity(sens), zeroBaseline(zero), driver(p, freq) {}

  void begin() {
    driver.setSensitivity(sensitivity);
  }

  float getRms(uint8_t loopCount = 1) {
    return driver.getRmsVoltage(loopCount);
  }

  float getPeak() {
    int maxDeviation = 0;
    uint32_t tStart = micros();
    while (micros() - tStart < 20000) { // 20ms = 1 period of 50Hz
      int raw = analogRead(pin);
      int dev = abs(raw - zeroBaseline);
      if (dev > maxDeviation) {
        maxDeviation = dev;
      }
    }
    return (float)maxDeviation * (3.3f / 4095.0f) * sensitivity;
  }
};

// -------------------------------------------------------------
// Easy Sensor List: Add any new sensor here in one line!
// -------------------------------------------------------------
VoltageSensor sensors[] = {
  // Name, Pin, Sensitivity, Zero-Baseline
  // VoltageSensor("V1", A3, 26.50f, 2282),
  VoltageSensor("V2", 0,  23.75f, 2281),
};

const size_t NUM_SENSORS = sizeof(sensors) / sizeof(sensors[0]);

void setup() {
  Serial.begin(921600);

  for (size_t i = 0; i < NUM_SENSORS; i++) {
    sensors[i].begin();
  }
}

void loop() {
  for (size_t i = 0; i < NUM_SENSORS; i++) {
    float vRMS = sensors[i].getRms();
    float vPeak = sensors[i].getPeak();

    // Print labeled values (e.g. V1_RMS:14.40 V1_Peak:20.36)
    Serial.print(sensors[i].name);
    Serial.print("_RMS:");
    Serial.print(vRMS, 2);
    Serial.print(" ");
    Serial.print(sensors[i].name);
    Serial.print("_Peak:");
    Serial.print(vPeak, 2);

    if (i < NUM_SENSORS - 1) {
      Serial.print(" ");
    }
  }
  Serial.println();

  delay(100);
}
