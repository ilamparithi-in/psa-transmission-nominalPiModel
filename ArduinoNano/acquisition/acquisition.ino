#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ZMPT101B.h>
#include <ACS712.h>
#include <ezBuzzer.h>

// -------------------------------------------------------------
// 16x2 LCD Display Configuration (I2C Backpack PCF8574)
// Shared I2C Pins: A4 (SDA), A5 (SCL)
// -------------------------------------------------------------
#define LCD_ADDR 0x27 // Standard I2C address (change to 0x3F if your module uses it)
#define LCD_COLS 16
#define LCD_ROWS 2

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Relay, Buzzer, Reset & Fault Protection Configuration
#define RELAY_PIN 2                  // Protection Relay control pin (Active-Low: LOW = Tripped/Open)
#define BUZZER_PIN 3                 // Audible Alarm Buzzer pin (Active or Passive)
#define RESET_BTN_PIN 4              // Optional External Reset pushbutton (Active-Low with INPUT_PULLUP)
#define FAULT_CURRENT_THRESHOLD 6.0f // Overcurrent fault threshold on Sending End (6.0 Amps RMS)

// Fault State & Frozen Snapshot Variables
bool isFaultLatched = false;
float freezeVs = 0.0f, freezeVr = 0.0f, freezeIs = 0.0f, freezeIr = 0.0f, freezeIf = 0.0f;
float freezeVsPk = 0.0f, freezeVrPk = 0.0f, freezeIsPk = 0.0f, freezeIrPk = 0.0f, freezeIfPk = 0.0f;

// Buzzer controller configured for PASSIVE buzzer (uses tone() at 2500 Hz for audible sound)
ezBuzzer buzzer(BUZZER_PIN, BUZZER_TYPE_PASSIVE);
// Non-blocking LCD refresh timer
unsigned long lastLcdUpdate = 0;
const unsigned long lcdInterval = 250; // Update LCD every 250ms

// -------------------------------------------------------------
// Voltage Sensor Configuration Structure (ZMPT101B)
// -------------------------------------------------------------
struct VoltageSensor {
  const char* name;
  uint8_t pin;
  float sensitivity;
  int zeroBaseline;
  ZMPT101B driver;

  // Arduino Nano: 10-bit ADC (0-1023), 5.0V reference, zero baseline ~514
  VoltageSensor(const char* n, uint8_t p, float sens, int zero = 514, float freq = 50.0f)
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
  float noiseFloor; // Quiescent RMS noise floor in Amperes (default 0.17A)
  ACS712 driver;

  // Arduino Nano: 10-bit ADC (0-1023), 5.0V reference, zero baseline ~512
  // ACS712 models: 5A = 185.0 mV/A, 20A = 100.0 mV/A, 30A = 66.0 mV/A
  // noiseFloor: compensates for inherent Hall sensor & ADC thermal noise floor at 0A
  CurrentSensor(const char* n, uint8_t p, float mVperA = 100.0f, int zero = 512, float noise = 0.17f)
    : name(n), pin(p), mVperAmp(mVperA), zeroBaseline(zero), noiseFloor(noise), driver(p, 5.0, 1023, mVperA) {}

  void begin() {
    driver.suppressNoise(true); // Smooth high-frequency ADC noise
    if (zeroBaseline > 0) {
      driver.setMidPoint(zeroBaseline);
    } else {
      driver.autoMidPoint();
      zeroBaseline = driver.getMidPoint();
    }
  }

  // Returns RMS current in Amperes (A) with quadrature noise de-embedding
  float getRms(uint8_t loopCount = 1) {
    float rawRms = driver.mA_AC_sampling(50, loopCount) / 1000.0f;
    if (rawRms <= noiseFloor) {
      return 0.0f;
    }
    // Quadrature de-embedding: I_true^2 = I_meas^2 - I_noise^2
    return sqrt(rawRms * rawRms - noiseFloor * noiseFloor);
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
    float peak = ((float)maxDeviation * (5.0f / 1023.0f)) / (mVperAmp / 1000.0f);
    float peakNoiseThreshold = noiseFloor * 1.4142f;
    if (peak <= peakNoiseThreshold) {
      return 0.0f;
    }
    return peak;
  }
};

// -------------------------------------------------------------
// Easy Sensor Lists: Add or modify sensors here in one line!
// -------------------------------------------------------------
VoltageSensor voltageSensors[] = {
  // Name, Pin, Sensitivity, Zero-Baseline
  VoltageSensor("Vs", A0, 17.00f, 514), // Sending-end Voltage (calibrated for 13.5V)
  VoltageSensor("Vr", A1, 15.56f, 514), // Receiving-end Voltage (calibrated for 13.1V)
};

CurrentSensor currentSensors[] = {
  // Name, Pin, mVperAmp, Zero-Baseline, Noise-Floor (A)
  CurrentSensor("Is", A2, 100.0f, 512, 0.17f), // Sending-end Current (20A module: 100 mV/A)
  CurrentSensor("Ir", A3, 100.0f, 512, 0.17f), // Receiving-end Current
  CurrentSensor("If", A6, 100.0f, 512, 0.17f), // Fault Bridge Current
};

const size_t NUM_VOLTAGE_SENSORS = sizeof(voltageSensors) / sizeof(voltageSensors[0]);
const size_t NUM_CURRENT_SENSORS = sizeof(currentSensors) / sizeof(currentSensors[0]);

// -------------------------------------------------------------
// LCD Display Update Functions (Flicker-Free 16-Char Buffer Overwrite)
// -------------------------------------------------------------

// Normal operation:
// Row 0: S: 13.5V  0.57A
// Row 1: R: 13.1V  0.47A
void updateLcdNormal(float vsRms, float isRms, float vrRms, float irRms) {
  char vBuf[7];
  char iBuf[7];
  char line[17];

  // Row 0: Sending End RMS
  dtostrf(vsRms, 4, 1, vBuf);
  dtostrf(isRms, 4, 2, iBuf);
  snprintf(line, sizeof(line), "S:%5sV %5sA", vBuf, iBuf);
  lcd.setCursor(0, 0);
  lcd.print(line);

  // Row 1: Receiving End RMS
  dtostrf(vrRms, 4, 1, vBuf);
  dtostrf(irRms, 4, 2, iBuf);
  snprintf(line, sizeof(line), "R:%5sV %5sA", vBuf, iBuf);
  lcd.setCursor(0, 1);
  lcd.print(line);

#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    lcd.init();
    lcd.backlight();
  }
#endif
}

// Frozen fault condition display:
// Row 0: Vs: 7.09  Vr: 1.03
// Row 1: Is: 8.29  Ir: 0.00
void updateLcdFrozenFault(float vs, float vr, float is, float ir) {
  char vsBuf[7], vrBuf[7], isBuf[7], irBuf[7];
  char line0[17], line1[17];

  dtostrf(vs, (vs >= 10.0f) ? 4 : 4, (vs >= 10.0f) ? 1 : 2, vsBuf);
  dtostrf(vr, (vr >= 10.0f) ? 4 : 4, (vr >= 10.0f) ? 1 : 2, vrBuf);
  dtostrf(is, (is >= 10.0f) ? 4 : 4, (is >= 10.0f) ? 1 : 2, isBuf);
  dtostrf(ir, (ir >= 10.0f) ? 4 : 4, (ir >= 10.0f) ? 1 : 2, irBuf);

  snprintf(line0, sizeof(line0), "Vs:%-4s  Vr:%-4s", vsBuf, vrBuf);
  snprintf(line1, sizeof(line1), "Is:%-4s  Ir:%-4s", isBuf, irBuf);

  line0[16] = '\0';
  line1[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);

#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    lcd.init();
    lcd.backlight();
  }
#endif
}

// -------------------------------------------------------------
// Setup & Loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Initialize I2C with hardware bus timeout to prevent lockup on switching transients/EMI
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true); // 25ms timeout, auto-reset hardware TWI on timeout
#endif

  // Initialize Protection Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Normal closed / un-tripped (Active-Low)

  // Initialize Reset Button (optional external button on D4 to GND; onboard RST button also resets)
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  // Initialize LCD display
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F(" PSA Acquisition"));
  lcd.setCursor(0, 1);
  lcd.print(F(" Initializing..."));

  // Initialize Voltage sensors
  for (size_t i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
    voltageSensors[i].begin();
  }

  // Initialize Current sensors
  for (size_t i = 0; i < NUM_CURRENT_SENSORS; i++) {
    currentSensors[i].begin();
  }

  // Configure buzzer frequency (2500 Hz resonant peak for maximum volume)
  buzzer.setBeepFrequency(2500);

  // Quick 80ms startup self-test chirp to confirm buzzer operation on boot
  tone(BUZZER_PIN, 2500, 80);

  delay(600);
  lcd.clear();
}

void loop() {
  // -------------------------------------------------------------
  // 1. If currently in Latched Fault Mode:
  // Hold relay OPEN, hold buzzer active, freeze LCD and Serial!
  // -------------------------------------------------------------
  if (isFaultLatched) {
    digitalWrite(RELAY_PIN, LOW); // Hold relay OPEN (Active-Low: LOW = Tripped/Open)

    // Keep buzzer active (siren alarm: 150ms ON, 80ms OFF)
    if (buzzer.getState() == BUZZER_IDLE) {
      buzzer.beep(150, 80);
    }
    buzzer.loop();

    // Check Reset Button (Pin D4 or onboard RST button)
    if (digitalRead(RESET_BTN_PIN) == LOW) {
      delay(50); // Debounce
      if (digitalRead(RESET_BTN_PIN) == LOW) {
        isFaultLatched = false;
        buzzer.stop();
        digitalWrite(RELAY_PIN, HIGH); // Re-close relay
        Serial.println(F("\n>>> FAULT CLEARED VIA RESET BUTTON. RESUMING NORMAL OPERATION <<<\n"));
        lcd.clear();
      }
    }

    // FREEZE: Do NOT perform any sensor sampling or overwrite frozen values!
    delay(10);
    return;
  }

  // -------------------------------------------------------------
  // 2. Normal Mode: Acquire Sensors
  // Sample Sending End Current FIRST for minimal detection delay (20ms)
  // -------------------------------------------------------------
  float isRms = currentSensors[0].getRms();
  float isPeak = currentSensors[0].getPeak();

  // Acquire remaining sensor values
  float vsRms = voltageSensors[0].getRms();
  float vsPeak = voltageSensors[0].getPeak();

  float vrRms = (NUM_VOLTAGE_SENSORS > 1) ? voltageSensors[1].getRms() : 0.0f;
  float vrPeak = (NUM_VOLTAGE_SENSORS > 1) ? voltageSensors[1].getPeak() : 0.0f;

  float irRms = (NUM_CURRENT_SENSORS > 1) ? currentSensors[1].getRms() : 0.0f;
  float irPeak = (NUM_CURRENT_SENSORS > 1) ? currentSensors[1].getPeak() : 0.0f;

  float ifRms = (NUM_CURRENT_SENSORS > 2) ? currentSensors[2].getRms() : 0.0f;
  float ifPeak = (NUM_CURRENT_SENSORS > 2) ? currentSensors[2].getPeak() : 0.0f;

  // -------------------------------------------------------------
  // 3. Fault Detection (Sending-End Current >= 6.0A RMS)
  // -------------------------------------------------------------
  if (isRms >= FAULT_CURRENT_THRESHOLD) {
    // 1. Immediately trip protection relay open!
    digitalWrite(RELAY_PIN, LOW); // Active-Low: LOW = Open/Tripped
    isFaultLatched = true;

    // Immediately sound audible alarm without delay
    buzzer.beep(150, 0);
    buzzer.loop();

    // 2. Save frozen snapshot values at fault condition
    freezeVs = vsRms;  freezeVr = vrRms;  freezeIs = isRms;  freezeIr = irRms;  freezeIf = ifRms;
    freezeVsPk = vsPeak; freezeVrPk = vrPeak; freezeIsPk = isPeak; freezeIrPk = irPeak; freezeIfPk = ifPeak;

    // 3. Print the EXACT single frozen serial line (Fault:1)
    Serial.print(F("Vs_RMS:"));  Serial.print(freezeVs, 2);   Serial.print(F(" "));
    Serial.print(F("Vs_Peak:")); Serial.print(freezeVsPk, 2); Serial.print(F(" "));
    Serial.print(F("Vr_RMS:"));  Serial.print(freezeVr, 2);   Serial.print(F(" "));
    Serial.print(F("Vr_Peak:")); Serial.print(freezeVrPk, 2); Serial.print(F(" "));
    Serial.print(F("Is_RMS:"));  Serial.print(freezeIs, 3);   Serial.print(F(" "));
    Serial.print(F("Is_Peak:")); Serial.print(freezeIsPk, 3); Serial.print(F(" "));
    Serial.print(F("Ir_RMS:"));  Serial.print(freezeIr, 3);   Serial.print(F(" "));
    Serial.print(F("Ir_Peak:")); Serial.print(freezeIrPk, 3); Serial.print(F(" "));
    Serial.print(F("If_RMS:"));  Serial.print(freezeIf, 3);   Serial.print(F(" "));
    Serial.print(F("If_Peak:")); Serial.print(freezeIfPk, 3); Serial.print(F(" "));
    Serial.print(F("Fault:1"));
    Serial.println();

    // 4. Freeze LCD immediately displaying Vs, Vr, Is, Ir
    updateLcdFrozenFault(freezeVs, freezeVr, freezeIs, freezeIr);
    return;
  }

  // -------------------------------------------------------------
  // 4. Normal Streaming & LCD Update
  // -------------------------------------------------------------
  Serial.print(F("Vs_RMS:"));  Serial.print(vsRms, 2);  Serial.print(F(" "));
  Serial.print(F("Vs_Peak:")); Serial.print(vsPeak, 2); Serial.print(F(" "));
  Serial.print(F("Vr_RMS:"));  Serial.print(vrRms, 2);  Serial.print(F(" "));
  Serial.print(F("Vr_Peak:")); Serial.print(vrPeak, 2); Serial.print(F(" "));
  Serial.print(F("Is_RMS:"));  Serial.print(isRms, 3);  Serial.print(F(" "));
  Serial.print(F("Is_Peak:")); Serial.print(isPeak, 3); Serial.print(F(" "));
  Serial.print(F("Ir_RMS:"));  Serial.print(irRms, 3);  Serial.print(F(" "));
  Serial.print(F("Ir_Peak:")); Serial.print(irPeak, 3); Serial.print(F(" "));
  Serial.print(F("If_RMS:"));  Serial.print(ifRms, 3);  Serial.print(F(" "));
  Serial.print(F("If_Peak:")); Serial.print(ifPeak, 3); Serial.print(F(" "));
  Serial.print(F("Fault:0"));
  Serial.println();

  unsigned long now = millis();
  if (now - lastLcdUpdate >= lcdInterval) {
    lastLcdUpdate = now;
    updateLcdNormal(vsRms, isRms, vrRms, irRms);
  }
}
