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

// Relay, Buzzer, Button & Fault Protection Configuration
#define RELAY_PIN 2                  // Protection Relay control pin (Active-Low: LOW = Tripped/Open)
#define BUZZER_PIN 3                 // Audible Alarm Buzzer pin (Active or Passive)
#define BTN_PIN 4                    // Tactile pushbutton to toggle Serial Plotter / RMS mode (Active-Low: pulls to GND)
#define FAULT_CURRENT_THRESHOLD 6.0f // Overcurrent fault threshold on Sending End (6.0 Amps RMS)

// Serial Output Mode & Button State
bool isPlotterMode = false;          // false = Periodic RMS reporting, true = Serial Plotter (ADC counts)
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Fault State, Clearing Time & Frozen Snapshot Variables
bool isFaultLatched = false;
unsigned long faultLatchTime = 0;    // Millisecond timestamp when fault was latched
float tripTimeMs = 0.0f;             // Supply disconnection time in milliseconds
float tripCycles = 0.0f;             // Disconnection time in 50Hz cycles
float freezeVs = 0.0f, freezeVr = 0.0f, freezeIs = 0.0f, freezeIr = 0.0f, freezeIf = 0.0f;
float freezeVsPk = 0.0f, freezeVrPk = 0.0f, freezeIsPk = 0.0f, freezeIrPk = 0.0f, freezeIfPk = 0.0f;

// Buzzer controller configured for PASSIVE buzzer (uses tone() at 2500 Hz for audible sound)
ezBuzzer buzzer(BUZZER_PIN, BUZZER_TYPE_PASSIVE);
// Non-blocking LCD refresh timers
unsigned long lastLcdUpdate = 0;
unsigned long lastFrozenLcdUpdate = 0;
const unsigned long lcdInterval = 250; // Update LCD every 250ms

void handleModeButton() {
  int reading = digitalRead(BTN_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    static int buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Button clicked (pulled to GND)
        isPlotterMode = !isPlotterMode;
        tone(BUZZER_PIN, 2500, 60); // Short beep (60ms) to indicate mode change

        if (isPlotterMode) {
          lcd.setCursor(0, 0);
          lcd.print(F(" Serial Plotter "));
          lcd.setCursor(0, 1);
          lcd.print(F("  Streaming S/R "));
        } else {
          lastLcdUpdate = 0; // Trigger immediate LCD update in RMS mode
        }
      }
    }
  }
  lastButtonState = reading;
}

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

  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    Wire.begin();
    Wire.setWireTimeout(3000, true);
    lcd.init();
    lcd.backlight();
  }
}

// Frozen fault condition display (matching normal operation order: Vs, Is on Row 0; Vr, Ir on Row 1):
// Row 0: !S: 7.1V 8.11A !
// Row 1: !R: 0.9V 0.00A !
void updateLcdFrozenFault(float vs, float is, float vr, float ir) {
  char vsBuf[7], vrBuf[7], isBuf[7], irBuf[7];
  char line0[17], line1[17];

  dtostrf(vs, 4, 1, vsBuf);
  dtostrf(vr, 4, 1, vrBuf);
  dtostrf(is, 4, (is >= 10.0f) ? 1 : 2, isBuf);
  dtostrf(ir, 4, (ir >= 10.0f) ? 1 : 2, irBuf);

  snprintf(line0, sizeof(line0), "!S:%4sV %4sA !", vsBuf, isBuf);
  snprintf(line1, sizeof(line1), "!R:%4sV %4sA !", vrBuf, irBuf);

  line0[16] = '\0';
  line1[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);

  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    Wire.begin();
    Wire.setWireTimeout(3000, true);
    lcd.init();
    lcd.backlight();
  }
}

// Supply disconnection / clearing time display (switched every 5s / 2s during fault):
// Row 0: Trip:    12.4 ms
// Row 1: Cycles:   0.62 cy
void updateLcdTripTime(float timeMs, float cycles) {
  char msBuf[8];
  char cycBuf[8];
  char line0[17], line1[17];

  dtostrf(timeMs, 5, 1, msBuf);
  dtostrf(cycles, 5, 2, cycBuf);

  snprintf(line0, sizeof(line0), "Trip:   %5s ms", msBuf);
  snprintf(line1, sizeof(line1), "Cycles: %5s cy", cycBuf);

  line0[16] = '\0';
  line1[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);

  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    Wire.begin();
    Wire.setWireTimeout(3000, true);
    lcd.init();
    lcd.backlight();
  }
}

// -------------------------------------------------------------
// Setup & Loop
// -------------------------------------------------------------
void setup() {
  uint8_t resetReason = MCUSR;
  MCUSR = 0; // Clear flags for subsequent resets

  Serial.begin(115200);

  // Diagnostic: Print the exact hardware cause of the reboot
  Serial.print(F("\n--- RESET DETECTED (MCUSR: 0x"));
  Serial.print(resetReason, HEX);
  Serial.println(F(") ---"));
  if (resetReason & (1 << BORF)) {
    Serial.println(F("[DIAGNOSTIC] Cause: BROWN-OUT RESET (BOD) -> 5V rail dipped below threshold!"));
  }
  if (resetReason & (1 << EXTRF)) {
    Serial.println(F("[DIAGNOSTIC] Cause: EXTERNAL RESET -> RST pin pulled low or reset button pressed."));
  }
  if (resetReason & (1 << PORF)) {
    Serial.println(F("[DIAGNOSTIC] Cause: POWER-ON RESET -> Fresh power applied."));
  }
  if (resetReason & (1 << WDRF)) {
    Serial.println(F("[DIAGNOSTIC] Cause: WATCHDOG RESET."));
  }
  Serial.println(F("-------------------------------------------------"));

  // Initialize I2C with hardware bus timeout to prevent lockup on switching transients/EMI
  Wire.begin();
  Wire.setWireTimeout(3000, true); // 3ms timeout, auto-reset hardware TWI on timeout

  // Initialize Protection Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Normal closed / un-tripped (Active-Low)

  // Initialize Mode Toggle Button on Pin D4 (tactile pushbutton pulls pin to GND when pressed)
  pinMode(BTN_PIN, INPUT_PULLUP);

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
  handleModeButton();

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

    // 7-second repeat cycle: Display fault parameters for 5s, trip time for 2s
    unsigned long now = millis();
    unsigned long elapsed = now - faultLatchTime;
    unsigned long phase = elapsed % 7000; // 0..4999 = parameters (5s), 5000..6999 = trip time (2s)

    if (now - lastFrozenLcdUpdate >= 250) {
      lastFrozenLcdUpdate = now;
      if (phase < 5000) {
        updateLcdFrozenFault(freezeVs, freezeIs, freezeVr, freezeIr);
      } else {
        updateLcdTripTime(tripTimeMs, tripCycles);
      }
    }

    delay(5);
    return;
  }

  // -------------------------------------------------------------
  // 2. Serial Plotter Mode:
  // Simultaneous ADC counts of sending end (Vs: A0) and receiving end (Vr: A1) VOLTAGE ONLY.
  // Notation: S and R for maximum speed.
  // -------------------------------------------------------------
  if (isPlotterMode) {
    int rawS = analogRead(voltageSensors[0].pin);
    int rawR = analogRead(voltageSensors[1].pin);

    Serial.print(F("S:"));
    Serial.print(rawS);
    Serial.print(F(" R:"));
    Serial.println(rawR);
    return;
  }

  // -------------------------------------------------------------
  // 3. Normal Mode: Acquire Sensors
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
    uint32_t tFaultDetect = micros();

    // 1. Immediately trip protection relay open!
    digitalWrite(RELAY_PIN, LOW); // Active-Low: LOW = Open/Tripped
    isFaultLatched = true;
    faultLatchTime = millis();

    // Immediately sound audible alarm without delay
    buzzer.beep(150, 0);
    buzzer.loop();

    // 2. Measure Supply Disconnection Time (until Vs collapses to 0)
    // High-speed polling on Vs (pin A0) to capture exact moment AC collapses into resting baseline noise
    uint32_t tLastActive = tFaultDetect;
    uint32_t tMeasureStart = micros();
    const int vsBaseline = voltageSensors[0].zeroBaseline;
    while (micros() - tMeasureStart < 100000UL) { // Up to 100ms window (5 full 50Hz cycles)
      int raw = analogRead(A0);
      int dev = abs(raw - vsBaseline);
      if (dev > 10) { // Still active AC voltage (> 0.8V peak)
        tLastActive = micros();
      } else if (micros() - tLastActive > 15000UL) {
        // Confirmed zero: flatlined within resting noise for >15ms (3/4 of a 50Hz period)
        break;
      }
    }

    tripTimeMs = (float)(tLastActive - tFaultDetect) / 1000.0f;
    tripCycles = tripTimeMs / 20.0f; // 20.0ms per 50Hz cycle

    // 3. Save frozen snapshot values at fault condition
    freezeVs = vsRms;  freezeVr = vrRms;  freezeIs = isRms;  freezeIr = irRms;  freezeIf = ifRms;
    freezeVsPk = vsPeak; freezeVrPk = vrPeak; freezeIsPk = isPeak; freezeIrPk = irPeak; freezeIfPk = ifPeak;

    // 4. Print the frozen serial line with Fault:1 and clearing time
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
    Serial.print(F("Fault:1"));  Serial.print(F(" "));
    Serial.print(F("Trip_ms:")); Serial.print(tripTimeMs, 1); Serial.print(F(" "));
    Serial.print(F("Cycles:"));  Serial.print(tripCycles, 2);
    Serial.println();

    // 5. Freeze LCD immediately displaying Vs, Is, Vr, Ir with fault indicator
    updateLcdFrozenFault(freezeVs, freezeIs, freezeVr, freezeIr);
    return;
  }

  // -------------------------------------------------------------
  // 4. Normal Streaming & LCD Update
  // -------------------------------------------------------------
  handleModeButton();
  if (isPlotterMode) return;

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
