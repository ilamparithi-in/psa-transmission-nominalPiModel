/**
 * Arduino Nano ZMPT101B Sensitivity Calibration
 * Estimates the right sensitivity value for the sensor on 10-bit ADC / 5V logic.
 */

#include <ZMPT101B.h>

#define ACTUAL_VOLTAGE 14.4f // Change this based on actual measured AC voltage

#define START_VALUE 0.0f
#define STOP_VALUE 1000.0f
#define STEP_VALUE 0.25f
#define TOLLERANCE 0.1f

#define MAX_TOLLERANCE_VOLTAGE (ACTUAL_VOLTAGE + TOLLERANCE)
#define MIN_TOLLERANCE_VOLTAGE (ACTUAL_VOLTAGE - TOLLERANCE)

// ZMPT101B sensor output connected to Arduino Nano analog pin A0 (50 Hz mains)
ZMPT101B voltageSensor(A0, 50.0);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.print(F("The Actual Voltage: "));
  Serial.println(ACTUAL_VOLTAGE);

  float sensitivityValue = START_VALUE;
  voltageSensor.setSensitivity(sensitivityValue);
  float voltageNow = voltageSensor.getRmsVoltage();

  Serial.println(F("Starting sensitivity calibration..."));

  while (voltageNow > MAX_TOLLERANCE_VOLTAGE || voltageNow < MIN_TOLLERANCE_VOLTAGE) {
    if (sensitivityValue < STOP_VALUE) {
      sensitivityValue += STEP_VALUE;
      voltageSensor.setSensitivity(sensitivityValue);
      voltageNow = voltageSensor.getRmsVoltage();
      Serial.print(sensitivityValue);
      Serial.print(F(" => "));
      Serial.println(voltageNow);
    } else {
      Serial.println(F("Unfortunately the sensitivity value cannot be determined."));
      return;
    }
  }

  Serial.print(F("Closest voltage within tolerance: "));
  Serial.println(voltageNow);
  Serial.print(F("Sensitivity Value: "));
  Serial.println(sensitivityValue, 6);
}

void loop() {}
