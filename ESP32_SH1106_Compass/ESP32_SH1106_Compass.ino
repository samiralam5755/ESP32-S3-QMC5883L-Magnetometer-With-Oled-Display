/*
  ==================================================================================
  ESP32-S3 Animated Compass - QMC5883P / HP5883 (0x2C) + SH1106 1.3" OLED
  ==================================================================================
  Verified Hardware Configuration:
  - Compass Sensor: HP5883 / QMC5883P (0x2C), QMC5883L (0x0D), HMC5883L (0x1E)
  - OLED Display: SH1106 1.3" at I2C Address 0x3C (or 0x3D)
  - I2C Pins: SDA = GPIO 4, SCL = GPIO 5
  ==================================================================================
  Features:
  - Fixed East/West & North/South orientation mapping
  - Animated 360° Compass Dial with Rotating Marks & Sharp North Pointer
  - Real-time Hard-Iron (Offset) & Soft-Iron (Scale) Calibration
  - Live Serial Monitor Configuration:
      '1' to '8' -> Instant orientation presets
      'i' -> Toggle North/South inversion (Invert X)
      'j' -> Toggle East/West inversion (Invert Y)
      's' -> Toggle Swap X and Y axes
      'c' -> Trigger 8-second Calibration
  ==================================================================================
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ── Pin Configuration ────────────────────────────────────────────────────────
#define SDA_PIN        4
#define SCL_PIN        5
#define OLED_ADDR      0x3C
#define SCREEN_W       128
#define SCREEN_H       64

Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledAvailable = false;

// ── Sensor State ─────────────────────────────────────────────────────────────
enum SensorType {
  SENSOR_NONE = 0,
  SENSOR_HP5883,    // 0x2C (QMC5883P / HP5883)
  SENSOR_QMC5883L,  // 0x0D (Standard QMC5883L)
  SENSOR_HMC5883L   // 0x1E (Honeywell HMC5883L)
};

SensorType detectedSensor = SENSOR_NONE;
uint8_t compassAddress = 0;
uint8_t chipID = 0;

// ── Axis Orientation Mapping ─────────────────────────────────────────────────
// Preset 2: invertX = false, invertY = true (Corrects East/West flip)
bool invertX = false; 
bool invertY = true;  
bool swapXY  = false; 

// ── Calibration Limits ───────────────────────────────────────────────────────
int16_t minX = 32767, maxX = -32768;
int16_t minY = 32767, maxY = -32768;
int16_t minZ = 32767, maxZ = -32768;
bool hasCalibrationData = false;

// ── Exponential Moving Average (EMA) Smoothing ────────────────────────────────
float smoothHeading = 0.0f;
const float EMA_ALPHA = 0.20f; // 0.10 = very smooth, 0.30 = fast response

// ── Declination Angle (Optional: Local Magnetic Declination in degrees) ───────
float declinationAngle = 0.0f; 

// ── Cardinal & Intercardinal Directions ───────────────────────────────────────
const char* cardinals[] = {
  "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
};

const char* cardinalsFull[] = {
  "North (N)", "North-North-East (NNE)", "North-East (NE)", "East-North-East (ENE)",
  "East (E)", "East-South-East (ESE)", "South-East (SE)", "South-South-East (SSE)",
  "South (S)", "South-South-West (SSW)", "South-West (SW)", "West-South-West (WSW)",
  "West (W)", "West-North-West (WNW)", "North-West (NW)", "North-North-West (NNW)"
};

// Dial Geometry
#define CX 36
#define CY 32
#define CR 28

// ─────────────────────────────────────────────────────────────────────────────
// I2C Helper Functions
// ─────────────────────────────────────────────────────────────────────────────
bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return (Wire.endTransmission() == 0);
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)addr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compass Sensor Auto-Detection & Initialization
// ─────────────────────────────────────────────────────────────────────────────
bool initCompass() {
  // 1. Check HP5883 / QMC5883P (Address 0x2C)
  Wire.beginTransmission(0x2C);
  if (Wire.endTransmission() == 0) {
    compassAddress = 0x2C;
    detectedSensor = SENSOR_HP5883;
    chipID = readReg(0x2C, 0x00);

    // Soft reset
    writeReg(0x2C, 0x0A, 0x80);
    delay(20);

    // Set/Reset Period = 1
    writeReg(0x2C, 0x0B, 0x01);
    delay(10);

    // Set Continuous Mode (200Hz ODR, 512 OSR)
    writeReg(0x2C, 0x0A, 0x0D);
    delay(30);
    writeReg(0x2C, 0x09, 0x1D);
    delay(20);

    Serial.printf("-> Detected HP5883 / QMC5883P at 0x2C (Chip ID: 0x%02X)\n", chipID);
    return true;
  }

  // 2. Check QMC5883L (Address 0x0D)
  Wire.beginTransmission(0x0D);
  if (Wire.endTransmission() == 0) {
    compassAddress = 0x0D;
    detectedSensor = SENSOR_QMC5883L;
    chipID = readReg(0x0D, 0x0D);

    writeReg(0x0D, 0x0A, 0x80);
    delay(20);
    writeReg(0x0D, 0x0B, 0x01);
    delay(10);
    writeReg(0x0D, 0x09, 0x1D);
    delay(30);

    Serial.printf("-> Detected QMC5883L at 0x0D (Chip ID: 0x%02X)\n", chipID);
    return true;
  }

  // 3. Check HMC5883L (Address 0x1E)
  Wire.beginTransmission(0x1E);
  if (Wire.endTransmission() == 0) {
    compassAddress = 0x1E;
    detectedSensor = SENSOR_HMC5883L;

    writeReg(0x1E, 0x00, 0x70);
    writeReg(0x1E, 0x01, 0x20);
    writeReg(0x1E, 0x02, 0x00);
    delay(30);

    Serial.println("-> Detected HMC5883L at 0x1E");
    return true;
  }

  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw Sensor Reading
// ─────────────────────────────────────────────────────────────────────────────
bool readCompassRaw(int16_t &x, int16_t &y, int16_t &z) {
  if (detectedSensor == SENSOR_HP5883) {
    Wire.beginTransmission(0x2C);
    Wire.write(0x01);
    uint8_t err = Wire.endTransmission(false);
    if (err != 0) {
      Wire.beginTransmission(0x2C);
      Wire.write(0x01);
      if (Wire.endTransmission(true) != 0) return false;
    }

    if (Wire.requestFrom((uint8_t)0x2C, (uint8_t)6) < 6) return false;
    uint8_t xl = Wire.read(), xh = Wire.read();
    uint8_t yl = Wire.read(), yh = Wire.read();
    uint8_t zl = Wire.read(), zh = Wire.read();

    int16_t rx = (int16_t)((xh << 8) | xl);
    int16_t ry = (int16_t)((yh << 8) | yl);
    int16_t rz = (int16_t)((zh << 8) | zl);

    if (rx == 0 && ry == 0 && rz == 0) {
      writeReg(0x2C, 0x0A, 0x0D);
      return false;
    }

    x = rx;
    y = ry;
    z = rz;
    return true;
  }
  else if (detectedSensor == SENSOR_QMC5883L) {
    Wire.beginTransmission(0x0D);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) {
      Wire.beginTransmission(0x0D);
      Wire.write(0x00);
      if (Wire.endTransmission(true) != 0) return false;
    }

    if (Wire.requestFrom((uint8_t)0x0D, (uint8_t)6) < 6) return false;
    uint8_t xl = Wire.read(), xh = Wire.read();
    uint8_t yl = Wire.read(), yh = Wire.read();
    uint8_t zl = Wire.read(), zh = Wire.read();

    int16_t rx = (int16_t)((xh << 8) | xl);
    int16_t ry = (int16_t)((yh << 8) | yl);
    int16_t rz = (int16_t)((zh << 8) | zl);

    if (rx == 0 && ry == 0 && rz == 0) return false;

    x = rx;
    y = ry;
    z = rz;
    return true;
  }
  else if (detectedSensor == SENSOR_HMC5883L) {
    Wire.beginTransmission(0x1E);
    Wire.write(0x03);
    if (Wire.endTransmission(false) != 0) {
      Wire.beginTransmission(0x1E);
      Wire.write(0x03);
      if (Wire.endTransmission(true) != 0) return false;
    }

    if (Wire.requestFrom((uint8_t)0x1E, (uint8_t)6) < 6) return false;
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t zh = Wire.read(), zl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();

    x = (int16_t)((xh << 8) | xl);
    y = (int16_t)((yh << 8) | yl);
    z = (int16_t)((zh << 8) | zl);
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Calibration Update & Computation
// ─────────────────────────────────────────────────────────────────────────────
void updateCalibration(int16_t rx, int16_t ry, int16_t rz) {
  if (rx == 0 && ry == 0 && rz == 0) return;

  if (rx < minX) minX = rx;
  if (rx > maxX) maxX = rx;
  if (ry < minY) minY = ry;
  if (ry > maxY) maxY = ry;
  if (rz < minZ) minZ = rz;
  if (rz > maxZ) maxZ = rz;

  if (maxX > minX && maxY > minY) {
    hasCalibrationData = true;
  }
}

float calculateHeading(int16_t rawX, int16_t rawY, float &calX_out, float &calY_out) {
  updateCalibration(rawX, rawY, 0);

  if (!hasCalibrationData || (maxX <= minX) || (maxY <= minY)) {
    calX_out = (float)rawX;
    calY_out = (float)rawY;
    float hX = invertX ? -((float)rawX) : ((float)rawX);
    float hY = invertY ? -((float)rawY) : ((float)rawY);
    if (swapXY) { float t = hX; hX = hY; hY = t; }
    float h = degrees(atan2(hY, hX)) + declinationAngle;
    if (h < 0.0f) h += 360.0f;
    if (h >= 360.0f) h -= 360.0f;
    return h;
  }

  // 1. Hard-Iron Offset (Center bias)
  float offX = (float)(maxX + minX) / 2.0f;
  float offY = (float)(maxY + minY) / 2.0f;

  // 2. Soft-Iron Scaling (Ellipsoid distortion to circle)
  float spanX = (float)(maxX - minX) / 2.0f;
  float spanY = (float)(maxY - minY) / 2.0f;

  if (spanX < 1.0f) spanX = 1.0f;
  if (spanY < 1.0f) spanY = 1.0f;

  float avgSpan = (spanX + spanY) / 2.0f;
  float scaleX = avgSpan / spanX;
  float scaleY = avgSpan / spanY;

  // Apply calibration
  float calX = ((float)rawX - offX) * scaleX;
  float calY = ((float)rawY - offY) * scaleY;

  calX_out = calX;
  calY_out = calY;

  // Apply Axis Inversions & Swap
  float headX = invertX ? -calX : calX;
  float headY = invertY ? -calY : calY;
  if (swapXY) {
    float temp = headX;
    headX = headY;
    headY = temp;
  }

  float heading = degrees(atan2(headY, headX)) + declinationAngle;
  if (heading < 0.0f) heading += 360.0f;
  if (heading >= 360.0f) heading -= 360.0f;

  return heading;
}

// ─────────────────────────────────────────────────────────────────────────────
// Interactive Calibration Routine
// ─────────────────────────────────────────────────────────────────────────────
void runCalibration() {
  Serial.println("\n+--------------------------------------------------------------+");
  Serial.println("|               COMPASS CALIBRATION IN PROGRESS                |");
  Serial.println("+--------------------------------------------------------------+");
  Serial.println("|  Rotate sensor slowly in 360 deg & Figure-8 motion           |");
  Serial.println("+--------------------------------------------------------------+\n");

  minX = minY = minZ = 32767;
  maxX = maxY = maxZ = -32768;
  hasCalibrationData = false;

  uint32_t startTime = millis();
  const uint32_t DURATION = 8000; // 8 seconds
  int lastPct = -1;

  while (millis() - startTime < DURATION) {
    int16_t rx = 0, ry = 0, rz = 0;
    if (readCompassRaw(rx, ry, rz)) {
      updateCalibration(rx, ry, rz);
    }

    uint32_t elapsed = millis() - startTime;
    int pct = (elapsed * 100) / DURATION;

    if (oledAvailable) {
      display.clearDisplay();
      display.setTextColor(SH110X_WHITE);
      display.setTextSize(1);
      display.setCursor(14, 2);
      display.print("CALIBRATION MODE");
      display.drawFastHLine(0, 12, SCREEN_W, SH110X_WHITE);

      display.setCursor(10, 18);
      display.print("Rotate in 8-shape");

      display.setCursor(6, 32);
      display.printf("X:[%d,%d]", (minX == 32767 ? 0 : minX), (maxX == -32768 ? 0 : maxX));
      display.setCursor(6, 42);
      display.printf("Y:[%d,%d]", (minY == 32767 ? 0 : minY), (maxY == -32768 ? 0 : maxY));

      // Progress bar
      display.drawRect(6, 54, 116, 8, SH110X_WHITE);
      display.fillRect(8, 56, (pct * 112) / 100, 4, SH110X_WHITE);

      display.display();
    }
    
    if (pct != lastPct && pct % 10 == 0) {
      lastPct = pct;
      Serial.printf("Calibrating [%3d%%] -> X:[%6d to %6d] Y:[%6d to %6d]\n",
                    pct, (minX == 32767 ? 0 : minX), (maxX == -32768 ? 0 : maxX),
                         (minY == 32767 ? 0 : minY), (maxY == -32768 ? 0 : maxY));
    }
    delay(20);
  }

  Serial.println("[OK] CALIBRATION COMPLETE!");
  Serial.printf("  Offset X : %+6.1f | Span X: %d\n", (float)(maxX + minX)/2.0f, maxX - minX);
  Serial.printf("  Offset Y : %+6.1f | Span Y: %d\n\n", (float)(maxY + minY)/2.0f, maxY - minY);
}

// ─────────────────────────────────────────────────────────────────────────────
// OLED Drawing Functions
// ─────────────────────────────────────────────────────────────────────────────
void drawCompassDial(float headingDeg) {
  if (!oledAvailable) return;

  // Outer circles
  display.drawCircle(CX, CY, CR,     SH110X_WHITE);
  display.drawCircle(CX, CY, CR - 1, SH110X_WHITE);

  // 16 tick marks
  for (int i = 0; i < 16; i++) {
    float angle = radians(i * 22.5f - headingDeg);
    float s = sin(angle), c = cos(angle);
    int len = (i % 4 == 0) ? 5 : 2;
    int x0 = CX + (int)((CR - 1) * s),       y0 = CY - (int)((CR - 1) * c);
    int x1 = CX + (int)((CR - 1 - len) * s), y1 = CY - (int)((CR - 1 - len) * c);
    display.drawLine(x0, y0, x1, y1, SH110X_WHITE);
  }

  // 4 Cardinal Labels (N, E, S, W) rotating with dial
  const char* dirs4[] = {"N", "E", "S", "W"};
  const float dAng[]  = {0.0f, 90.0f, 180.0f, 270.0f};
  for (int i = 0; i < 4; i++) {
    float angle = radians(dAng[i] - headingDeg);
    int lx = CX + (int)((CR - 8) * sin(angle)) - 2;
    int ly = CY - (int)((CR - 8) * cos(angle)) - 3;
    display.setCursor(lx, ly);
    display.setTextSize(1);
    display.print(dirs4[i]);
  }

  // Fixed North Needle pointer at center
  int tipY = CY - 18, baseY = CY + 8, sideX = 4, midY = CY - 2;
  display.fillTriangle(CX, tipY,     CX - sideX, midY, CX + sideX, midY, SH110X_WHITE);
  display.fillTriangle(CX, baseY,    CX - sideX, midY, CX + sideX, midY, SH110X_WHITE);
  display.fillTriangle(CX, baseY + 1, CX - sideX + 1, midY, CX + sideX - 1, midY, SH110X_BLACK);
  display.fillCircle(CX, CY, 2, SH110X_WHITE);
}

void drawHUD(float headingDeg, int16_t rx, int16_t ry) {
  if (!oledAvailable) return;
  int idx = (int)((headingDeg + 11.25f) / 22.5f) % 16;
  const char* cardName = cardinals[idx];

  // Big digital degree readout
  display.setTextSize(2);
  display.setCursor(76, 2);
  char buf[8];
  snprintf(buf, sizeof(buf), "%3d", (int)headingDeg);
  display.print(buf);
  display.setTextSize(1);
  display.print("\xF8"); // Degree symbol

  // Cardinal direction text
  display.setTextSize(1);
  int nameX = 76 + (36 - (int)strlen(cardName) * 6) / 2;
  display.setCursor(nameX, 22);
  display.print(cardName);

  // Vertical separator
  display.drawFastVLine(72, 0, SCREEN_H, SH110X_WHITE);

  // Raw X & Y values
  display.setCursor(76, 36);
  display.printf("X:%4d", rx);
  display.setCursor(76, 46);
  display.printf("Y:%4d", ry);

  // 360 degree visual progress bar
  display.drawFastHLine(74, 56, 52, SH110X_WHITE);
  int barW = (int)(headingDeg / 360.0f * 50.0f);
  if (barW > 0) display.fillRect(74, 58, barW, 4, SH110X_WHITE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 QMC5883P / HP5883 Compass & OLED Starting ===");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // Initialize SH1106 OLED Display
  if (display.begin(OLED_ADDR, true)) {
    oledAvailable = true;
    Wire.setClock(100000); // Maintain 100kHz after OLED init
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(18, 20);
    display.print("HP5883 / 0x2C");
    display.setCursor(20, 34);
    display.print("ANIMATED COMPASS");
    display.display();
    delay(800);
  } else {
    Serial.println("Note: OLED Display not found at 0x3C (Running in Serial Mode).");
  }

  // Initialize Compass Sensor
  if (!initCompass()) {
    Serial.println("ERROR: Compass sensor not found (Checked 0x2C, 0x0D, 0x1E)!");
    if (oledAvailable) {
      display.clearDisplay();
      display.setCursor(10, 24);
      display.print("Sensor Not Found!");
      display.display();
    }
    while (!initCompass()) {
      delay(2000);
    }
  }
  Serial.printf("SUCCESS: Compass sensor detected at 0x%02X!\n", compassAddress);
  Serial.println("\n--- Live Commands ---");
  Serial.println("  '1' -> Preset 1: Default (atan2(Y, X))");
  Serial.println("  '2' -> Preset 2: Invert Y (atan2(-Y, X)) [CURRENT]");
  Serial.println("  '3' -> Preset 3: Invert X (atan2(Y, -X))");
  Serial.println("  '4' -> Preset 4: Invert Both (atan2(-Y, -X))");
  Serial.println("  '5' -> Preset 5: Swap X/Y (atan2(X, Y))");
  Serial.println("  '6' -> Preset 6: Swap X/Y + Invert X (atan2(X, -Y))");
  Serial.println("  '7' -> Preset 7: Swap X/Y + Invert Y (atan2(-X, Y))");
  Serial.println("  '8' -> Preset 8: Swap X/Y + Invert Both (atan2(-X, -Y))");
  Serial.println("  'c' -> Recalibrate sensor");
  Serial.println("---------------------\n");

  // Run initial calibration
  runCalibration();

  int16_t x = 0, y = 0, z = 0;
  float calX, calY;
  if (readCompassRaw(x, y, z)) {
    smoothHeading = calculateHeading(x, y, calX, calY);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────────────────
int16_t lastX = 0, lastY = 0, lastZ = 0;
uint32_t lastPrint = 0;

void loop() {
  // Serial Commands Handler
  if (Serial.available()) {
    char c = Serial.read();
    while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n')) {
      Serial.read();
    }

    if (c == '1') {
      invertX = false; invertY = false; swapXY = false;
      Serial.println("\n[Preset 1 Activated]: Standard (atan2(Y, X))");
    } else if (c == '2') {
      invertX = false; invertY = true; swapXY = false;
      Serial.println("\n[Preset 2 Activated]: Invert Y (atan2(-Y, X))");
    } else if (c == '3') {
      invertX = true; invertY = false; swapXY = false;
      Serial.println("\n[Preset 3 Activated]: Invert X (atan2(Y, -X))");
    } else if (c == '4') {
      invertX = true; invertY = true; swapXY = false;
      Serial.println("\n[Preset 4 Activated]: Invert Both (atan2(-Y, -X))");
    } else if (c == '5') {
      invertX = false; invertY = false; swapXY = true;
      Serial.println("\n[Preset 5 Activated]: Swap X/Y (atan2(X, Y))");
    } else if (c == '6') {
      invertX = true; invertY = false; swapXY = true;
      Serial.println("\n[Preset 6 Activated]: Swap X/Y + Invert X (atan2(X, -Y))");
    } else if (c == '7') {
      invertX = false; invertY = true; swapXY = true;
      Serial.println("\n[Preset 7 Activated]: Swap X/Y + Invert Y (atan2(-X, Y))");
    } else if (c == '8') {
      invertX = true; invertY = true; swapXY = true;
      Serial.println("\n[Preset 8 Activated]: Swap X/Y + Invert Both (atan2(-X, -Y))");
    } else if (c == 'i' || c == 'I') {
      invertX = !invertX;
      Serial.printf("\n[Toggle]: Invert X (N/S) = %s\n", invertX ? "ON" : "OFF");
    } else if (c == 'j' || c == 'J') {
      invertY = !invertY;
      Serial.printf("\n[Toggle]: Invert Y (E/W) = %s\n", invertY ? "ON" : "OFF");
    } else if (c == 's' || c == 'S') {
      swapXY = !swapXY;
      Serial.printf("\n[Toggle]: Swap X/Y = %s\n", swapXY ? "ON" : "OFF");
    } else if (c == 'c' || c == 'C') {
      runCalibration();
    }
  }

  int16_t rx = 0, ry = 0, rz = 0;
  if (readCompassRaw(rx, ry, rz)) {
    lastX = rx;
    lastY = ry;
    lastZ = rz;
  }

  float calX = 0, calY = 0;
  float heading = calculateHeading(lastX, lastY, calX, calY);

  // EMA angle smoothing with wrap handling around 0/360°
  float diff = heading - smoothHeading;
  if (diff >  180.0f) diff -= 360.0f;
  if (diff < -180.0f) diff += 360.0f;
  smoothHeading += EMA_ALPHA * diff;
  if (smoothHeading <    0.0f) smoothHeading += 360.0f;
  if (smoothHeading >= 360.0f) smoothHeading -= 360.0f;

  // Render to OLED Display
  if (oledAvailable) {
    display.clearDisplay();
    drawCompassDial(smoothHeading);
    drawHUD(smoothHeading, lastX, lastY);
    display.display();
  }

  // Print to Serial Monitor at 4 Hz
  if (millis() - lastPrint > 250) {
    lastPrint = millis();
    int idx = (int)((smoothHeading + 11.25f) / 22.5f) % 16;
    Serial.printf("HEADING: %5.1f deg | DIR: %-4s (%-20s) | Invert:[X:%s Y:%s Swap:%s]\n",
                  smoothHeading, cardinals[idx], cardinalsFull[idx],
                  invertX ? "YES" : "NO", invertY ? "YES" : "NO", swapXY ? "YES" : "NO");
  }

  delay(20);
}
