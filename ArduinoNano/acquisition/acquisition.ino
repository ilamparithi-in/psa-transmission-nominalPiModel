#include <ZMPT101B.h>
#include <ACS712.h>

// -------------------------------------------------------------
// Voltage Sensor Configuration Structure (ZMPT101B)
// -------------------------------------------------------------
struct VoltageSensor {
  const char* name;
  uint8_t pin;
  float sensitivity;
  int zeroBaseline;
  ZMPT101B driver;

  // Arduino Nano: 10-bit ADC (0-1023), 5.0V reference, zero baseline ~512
  VoltageSensor(const char* n, uint8_t p, float sens, int zero = 512, float freq = 50.0f)
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
    // 5.0V logic, 10-bit ADC (1023 steps)
    return (float)maxDeviation * (5.0f / 1023.0f) * sensitivity;
  }
};

// -------------------------------------------------------------
// Current Sensor Configuration Structure (ACS712)
// -------------------------------------------------------------
struct CurrentSensor {
  const char* name;
  uint8_t pin;
  float mVperAmp;
  int zeroBaseline;
  ACS712 driver;

  // Arduino Nano: 10-bit ADC (0-1023), 5.0V reference, zero baseline ~512
  // ACS712 models: 5A = 185.0 mV/A, 20A = 100.0 mV/A, 30A = 66.0 mV/A
  // Pass zero = 0 to trigger automatic midpoint calibration on boot
  CurrentSensor(const char* n, uint8_t p, float mVperA = 100.0f, int zero = 512)
    : name(n), pin(p), mVperAmp(mVperA), zeroBaseline(zero), driver(p, 5.0, 1023, mVperA) {}

  void begin() {
    if (zeroBaseline > 0) {
      driver.setMidPoint(zeroBaseline);
    } else {
      driver.autoMidPoint();
      zeroBaseline = driver.getMidPoint();
    }
  }

  // Returns RMS current in Amperes (A)
  float getRms(uint8_t loopCount = 1) {
    return driver.mA_AC_sampling(50, loopCount) / 1000.0f;
  }

  // Returns Peak current in Amperes (A)
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
    // (5.0V / 1023.0f) / (mVperAmp / 1000.0f) = Amperes
    return ((float)maxDeviation * (5.0f / 1023.0f)) / (mVperAmp / 1000.0f);
  }
};

// -------------------------------------------------------------
// Easy Sensor Lists: Add any new sensor here in one line!
// -------------------------------------------------------------
VoltageSensor voltageSensors[] = {
  // Name, Pin, Sensitivity, Zero-Baseline
  VoltageSensor("Vs", A0, 26.50f, 514), // Sending-end Voltage
  VoltageSensor("Vr", A1, 23.75f, 514), // Receiving-end Voltage
};

CurrentSensor currentSensors[] = {
  // Name, Pin, mVperAmp, Zero-Baseline
  CurrentSensor("Is", A2, 100.0f, 512), // Sending-end Current (20A module: 100 mV/A)
  CurrentSensor("Ir", A3, 100.0f, 512), // Receiving-end Current
  CurrentSensor("If", A6, 100.0f, 512), // Fault Bridge Current
};

const size_t NUM_VOLTAGE_SENSORS = sizeof(voltageSensors) / sizeof(voltageSensors[0]);
const size_t NUM_CURRENT_SENSORS = sizeof(currentSensors) / sizeof(currentSensors[0]);

void setup() {
  Serial.begin(115200);

  for (size_t i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
    voltageSensors[i].begin();
  }

  for (size_t i = 0; i < NUM_CURRENT_SENSORS; i++) {
    currentSensors[i].begin();
  }
}

void loop() {
  // 1. Read and print Voltage sensors
  for (size_t i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
    float vRMS = voltageSensors[i].getRms();
    float vPeak = voltageSensors[i].getPeak();

    // Labeled output: Vs_RMS:14.40 Vs_Peak:20.36
    Serial.print(voltageSensors[i].name);
    Serial.print(F("_RMS:"));
    Serial.print(vRMS, 2);
    Serial.print(F(" "));
    Serial.print(voltageSensors[i].name);
    Serial.print(F("_Peak:"));
    Serial.print(vPeak, 2);
    Serial.print(F(" "));
  }

  // 2. Read and print Current sensors
  for (size_t i = 0; i < NUM_CURRENT_SENSORS; i++) {
    float iRMS = currentSensors[i].getRms();
    float iPeak = currentSensors[i].getPeak();

    // Labeled output: Is_RMS:0.450 Is_Peak:0.636
    Serial.print(currentSensors[i].name);
    Serial.print(F("_RMS:"));
    Serial.print(iRMS, 3);
    Serial.print(F(" "));
    Serial.print(currentSensors[i].name);
    Serial.print(F("_Peak:"));
    Serial.print(iPeak, 3);

    if (i < NUM_CURRENT_SENSORS - 1) {
      Serial.print(F(" "));
    }
  }
  Serial.println();

  delay(100);
}
