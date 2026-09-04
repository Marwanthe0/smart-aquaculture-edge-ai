/*  ============================================================
    FPMS — Fish Pond Management System
    ESP32 Node  v6.0

    Actuators:
      Feeder Motor  → GPIO 23 (Relay, Active LOW)
      Servo Gate    → GPIO 18 (MG 996R Servo, PWM) [Configurable via SERVO_PIN]
      Oxygen Pump   → GPIO 22 (Relay, Active LOW)

    Sensors:
      GPIO  4  DS18B20 temperature
      GPIO 34  pH (analog)
      GPIO 33  Turbidity (analog, 0–100 NTU)
      GPIO 32  Raindrop (analog)

    FEEDING SEQUENCE:
      - START: (1) Open Servo Gate → (2) Turn ON Feeder Motor
      - STOP:  (1) Close Servo Gate → (2) Turn OFF Feeder Motor
    ============================================================ */

#include "water_quality_model.h"
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <WiFi.h>
#include <time.h>

// DSP Signal Conditioning: Exponential Moving Average (EMA) low-pass filter
// Alpha range: 0.0 to 1.0 (lower values provide stronger smoothing against
// electrical noise)
const float DSP_ALPHA = 0.25f;
float filteredPh = 7.0f;
float filteredTurbidity = 15.0f;
bool dspInitialized = false;
// Apply single-pole low-pass filtering to raw sensor voltage/readings
float applyLowPassFilter(float currentSample, float &previousFiltered) {
  if (!dspInitialized) {
    previousFiltered = currentSample;
    return currentSample;
  }
  previousFiltered =
      (DSP_ALPHA * currentSample) + ((1.0f - DSP_ALPHA) * previousFiltered);
  return previousFiltered;
}

const char *WIFI_SSID = "RAIHAN";
const char *WIFI_PASSWORD = "32145678";

const char *BACKEND_IP =
    "10.205.158.15";           // ← PC's LAN IP (run `ipconfig` in cmd to check)
const int BACKEND_PORT = 5000; // ← Backend server port
const char *DEVICE_ID = "pond-01";

// ── Backend API Endpoints (Auto-constructed from BACKEND_IP & BACKEND_PORT) ──
String BASE_URL;
String SENSOR_URL;
String BATCH_URL;
String MOTOR_URL;
String FEEDING_URL;

// ── Pin numbers ───────────────────────────────────────────────
#define FEEDER_PIN 23
#define OXYGEN_PIN 22
#define TEMP_PIN 4
#define PH_PIN 34
#define TURBIDITY_PIN 33 // Analog input (0–100 NTU)
#define RAIN_PIN 32

// ── Feeder Servo Gate (MG 996R — 360° Continuous Rotation) ─────────────────
#define SERVO_PIN 21         // ← GPIO for MG 996R Servo signal
const int SERVO_STOP = 90;   // 90 = STOP (0 RPM)
const int SERVO_FORWARD = 0; // 0 = Full speed forward
const int SERVO_REVERSE = 0; // 180 = Full speed backward / reverse

// Timing variables (tune each direction independently for perfect gate
// alignment):
const int SERVO_OPEN_MS =
    940; // ← Milliseconds to spin forward (Go / Open gate)
const int SERVO_CLOSE_MS =
    1000; // ← Milliseconds to spin backward (Come back / Return / Close gate)
const int SERVO_HALF_TURN_MS = 840; // Kept for backwards compatibility

Servo gateServo;

// ── DS18B20 ───────────────────────────────────────────────────
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// ── Timing ────────────────────────────────────────────────────
unsigned long lastSensorTime = 0;
unsigned long lastPollTime = 0;

// ── Actuator states ───────────────────────────────────────────
bool feederIsOn = false;
bool oxygenIsOn = false;

// ── Feeder schedule tracking ──────────────────────────────────
bool feederRunning = false;
unsigned long feederOnAt = 0;
int feederDurationMs = 60000;

// ─────────────────────────────────────────────────────────────
// Print what the actuators are doing right now
// ─────────────────────────────────────────────────────────────
void printMotorStatus() {
  Serial.println("------ Actuator Status ------");
  if (oxygenIsOn) {
    Serial.println("Oxygen Pump  (GPIO 22) : ON");
  } else {
    Serial.println("Oxygen Pump  (GPIO 22) : OFF");
  }
  if (feederIsOn) {
    Serial.println("Feeder Motor (GPIO 23) : ON");
    Serial.print("Servo Gate   (GPIO ");
    Serial.print(SERVO_PIN);
    Serial.println(") : OPEN");
  } else {
    Serial.println("Feeder Motor (GPIO 23) : OFF");
    Serial.print("Servo Gate   (GPIO ");
    Serial.print(SERVO_PIN);
    Serial.println(") : CLOSED");
  }
  Serial.println("-----------------------------");
}

// ─────────────────────────────────────────────────────────────
// Servo Gate Helpers:
//   openGate  → spins forward for SERVO_OPEN_MS (gate opens)
//   closeGate → spins backward for SERVO_CLOSE_MS (gate comes back / closes)
// ─────────────────────────────────────────────────────────────
void spinServoHalfTurn(const char *label) {
  Serial.print("SERVO: ");
  Serial.print(label);
  Serial.println(" — spinning forward...");
  gateServo.write(SERVO_FORWARD); // Start spinning forward
  delay(SERVO_OPEN_MS);           // Spin for open duration
  gateServo.write(SERVO_STOP);    // Stop immediately
  delay(200);                     // Let motor settle
  Serial.print("SERVO: ");
  Serial.print(label);
  Serial.println(" — complete, stopped.");
}

void openGate() {
  Serial.println("SERVO: OPEN GATE — spinning forward (going)...");
  gateServo.write(SERVO_FORWARD); // Spin forward (0)
  delay(SERVO_OPEN_MS);           // Go duration
  gateServo.write(SERVO_STOP);    // Stop
  delay(200);
  Serial.println("SERVO: OPEN GATE — complete, stopped.");
}

void closeGate() {
  Serial.println("SERVO: CLOSE GATE — spinning backward (coming back)...");
  gateServo.write(SERVO_REVERSE); // Spin reverse (180 = full speed backward)
  delay(SERVO_CLOSE_MS);          // Come back duration
  gateServo.write(SERVO_STOP);    // Stop
  delay(200);
  Serial.println("SERVO: CLOSE GATE — complete, stopped.");
}

// ─────────────────────────────────────────────────────────────
// Feeding Sequences (First Servo -> Then Feeder Motor)
// ─────────────────────────────────────────────────────────────

// Sequence 1: Start feeding
// 1. First rotate Servo to OPEN gate
// 2. Then start Feeder Motor (Relay GPIO 23)
void startFeedingSequence(const char *reason) {
  if (feederIsOn)
    return;

  Serial.print("\n>>> START FEEDING [");
  Serial.print(reason);
  Serial.println("] >>>");

  // Step 1: Open servo gate first
  openGate();

  // Step 2: Start feeding motor
  pinMode(FEEDER_PIN, OUTPUT);
  digitalWrite(FEEDER_PIN, LOW); // Active LOW relay
  feederIsOn = true;
  Serial.println("FEEDER: GPIO 23 → OUTPUT LOW → Feeder Motor turned ON");
  printMotorStatus();
}

// Sequence 2: Stop feeding
// 1. First rotate Servo to CLOSE gate
// 2. Then stop Feeder Motor and abandon pin (high-Z safety)
void stopFeedingSequence(const char *reason) {
  if (!feederIsOn)
    return;

  Serial.print("\n<<< STOP FEEDING [");
  Serial.print(reason);
  Serial.println("] <<<");

  // Step 1: Close servo gate first
  closeGate();

  // Step 2: Stop feeding motor and abandon pin
  pinMode(FEEDER_PIN, INPUT); // High-Z floating pin for relay safety
  feederIsOn = false;
  Serial.println(
      "FEEDER: GPIO 23 → INPUT → Feeder Motor turned OFF (pin abandoned)");
  printMotorStatus();
}

// ─────────────────────────────────────────────────────────────
// ADC average (fast non-blocking sampling)
// ─────────────────────────────────────────────────────────────
int readADCAvg(int pin) {
  long total = 0;
  for (int i = 0; i < 8; i++) {
    total += analogRead(pin);
    delayMicroseconds(500);
  }
  return total / 8;
}

// ─────────────────────────────────────────────────────────────
// POST ALL sensor readings in ONE single fast batch HTTP call
// ─────────────────────────────────────────────────────────────
int postBatchReadings(float tempC, float phVal, float turbNTU, float wetness) {
  HTTPClient http;
  http.begin(BATCH_URL.c_str());
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1500); // Fast 1.5s timeout

  String body = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"readings\":[";
  if (tempC != DEVICE_DISCONNECTED_C) {
    body += "{\"type\":\"temperature\",\"value\":" + String(tempC, 2) +
            ",\"unit\":\"C\"},";
  }
  body +=
      "{\"type\":\"ph\",\"value\":" + String(phVal, 2) + ",\"unit\":\"pH\"},";
  body += "{\"type\":\"turbidity\",\"value\":" + String(turbNTU, 1) +
          ",\"unit\":\"NTU\"},";
  body +=
      "{\"type\":\"rain\",\"value\":" + String(wetness, 1) + ",\"unit\":\"%\"}";
  body += "]}";

  int code = http.POST(body);
  http.end();

  Serial.print("  BATCH POST (All 4 Sensors) → HTTP ");
  Serial.println(code);
  return code;
}

// ─────────────────────────────────────────────────────────────
// POST one sensor value to backend (Fallback)
// ─────────────────────────────────────────────────────────────
int postReading(const char *type, float value, const char *unit) {
  HTTPClient http;
  http.begin(SENSOR_URL.c_str());
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1500);
  String body = "{\"type\":\"";
  body += type;
  body += "\",\"value\":";
  body += String(value, 2);
  body += ",\"unit\":\"";
  body += unit;
  body += "\",\"deviceId\":\"";
  body += DEVICE_ID;
  body += "\"}";
  int code = http.POST(body);
  http.end();

  Serial.print("  POST ");
  Serial.print(type);
  Serial.print(" → HTTP ");
  Serial.println(code);
  return code;
}

// ─────────────────────────────────────────────────────────────
// GET JSON from a URL
// ─────────────────────────────────────────────────────────────
bool getJson(const char *url, JsonDocument &doc) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(1500);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  return (err == DeserializationError::Ok);
}

// ─────────────────────────────────────────────────────────────
// Convert "08:00 AM" to minutes since midnight
// ─────────────────────────────────────────────────────────────
int timeStringToMinutes(const char *s) {
  int h = 0;
  int m = 0;
  char ap[3] = "AM";
  if (sscanf(s, "%d:%d %2s", &h, &m, ap) != 3) {
    return -1;
  }
  if (strcmp(ap, "PM") == 0 && h != 12)
    h += 12;
  if (strcmp(ap, "AM") == 0 && h == 12)
    h = 0;
  return h * 60 + m;
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== FPMS v6.0 starting (Feeder Motor + Servo Gate) ===");

  // Build Base URL & Endpoint URLs dynamically from BACKEND_IP and BACKEND_PORT
  BASE_URL = "http://" + String(BACKEND_IP) + ":" + String(BACKEND_PORT);
  SENSOR_URL = BASE_URL + "/api/sensors/reading";
  BATCH_URL = BASE_URL + "/api/sensors/batch";
  MOTOR_URL = BASE_URL + "/api/controls/motor";
  FEEDING_URL = BASE_URL + "/api/controls/feeding/state";

  Serial.println("Target Backend Endpoints:");
  Serial.print("  Base URL    : ");
  Serial.println(BASE_URL);
  Serial.print("  Batch POST  : ");
  Serial.println(BATCH_URL);
  Serial.print("  Motor GET   : ");
  Serial.println(MOTOR_URL);
  Serial.print("  Feeding GET : ");
  Serial.println(FEEDING_URL);

  // Abandon both relay pins at boot — relay coils are dead
  pinMode(FEEDER_PIN, INPUT);
  feederIsOn = false;
  Serial.println("GPIO 23 (Feeder)      → INPUT at boot (OFF)");

  pinMode(OXYGEN_PIN, INPUT);
  oxygenIsOn = false;
  Serial.println("GPIO 22 (Oxygen Pump) → INPUT at boot (OFF)");

  // Initialize MG 996R Servo Gate
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  gateServo.setPeriodHertz(50); // Standard 50Hz servo PWM
  gateServo.attach(SERVO_PIN, 500,
                   2400);      // Attach with 500us - 2400us pulse widths
  gateServo.write(SERVO_STOP); // 90 = STOP at boot (no spinning)
  Serial.print("Servo Gate (MG 996R)  → Attached to GPIO ");
  Serial.print(SERVO_PIN);
  Serial.println(" (CLOSED at boot)");

  printMotorStatus();

  // Turbidity is digital input
  pinMode(TURBIDITY_PIN, INPUT);

  // Start temperature sensor with non-blocking conversion
  tempSensor.begin();
  tempSensor.setWaitForConversion(
      false);                       // Non-blocking: eliminates 750ms delay!
  tempSensor.requestTemperatures(); // Request first conversion
  Serial.println("DS18B20 ready on GPIO 4 (Async Mode)");

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA); // Station mode only — required for hotspot connection
  WiFi.setAutoReconnect(true); // ESP32 handles reconnects internally
  WiFi.disconnect(false);      // Clear stale connection state
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
    configTime(6 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("NTP time sync started (UTC+6)");
  } else {
    int wifiStatus = WiFi.status();
    Serial.print("WiFi FAILED! Status code: ");
    Serial.println(wifiStatus);
    Serial.print("  Meaning: ");
    switch (wifiStatus) {
    case 0:
      Serial.println("WL_IDLE_STATUS — WiFi is idle. Try reset.");
      break;
    case 1:
      Serial.println("WL_NO_SSID_AVAIL — Hotspot NOT FOUND. Check hotspot name "
                     "(case-sensitive) or 2.4GHz band.");
      break;
    case 2:
      Serial.println("WL_SCAN_COMPLETED — Scan done but not connected.");
      break;
    case 3:
      Serial.println("WL_CONNECTED — Connected (should not reach here).");
      break;
    case 4:
      Serial.println("WL_CONNECT_FAILED — WRONG PASSWORD or auth failure.");
      break;
    case 5:
      Serial.println(
          "WL_CONNECTION_LOST — Connection was established then lost.");
      break;
    case 6:
      Serial.println("WL_DISCONNECTED — Hotspot is 5GHz (ESP32 only supports "
                     "2.4GHz). Fix: set hotspot to 2.4GHz band.");
      break;
    default:
      Serial.println("Unknown status.");
      break;
    }
  }
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ════════════════════════════════════════════════
  // WIFI RECONNECT — only if disconnected, once per 30s
  // ════════════════════════════════════════════════
  static unsigned long lastWifiAttempt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttempt >= 30000) {
      lastWifiAttempt = now;
      Serial.println("[WIFI] Disconnected — attempting reconnect...");
      WiFi.disconnect();
      delay(100);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      int retries = 0;
      while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        retries++;
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WIFI] Reconnected! IP: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.print("[WIFI] Reconnect failed (status=");
        Serial.print(WiFi.status());
        Serial.println("). Retry in 30s.");
      }
    }
  }

  // ════════════════════════════════════════════════
  // SENSOR BLOCK — runs every 3 seconds
  // ════════════════════════════════════════════════
  if (now - lastSensorTime >= 3000) {
    lastSensorTime = now;
    Serial.println("================================");

    // Temperature (Instant non-blocking read)
    float tempC = tempSensor.getTempCByIndex(0);
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("TEMP: sensor disconnected");
    } else {
      Serial.print("TEMP: ");
      Serial.print(tempC);
      Serial.println(" C");
    }

    // pH Calibration
    const float PH_7_VOLTAGE = 2.000f;
    int phRaw = readADCAvg(PH_PIN);
    float phV = phRaw * (3.3f / 4095.0f);
    float phVal = 7.0f + (PH_7_VOLTAGE - phV) * 5.70f;
    phVal = constrain(phVal, 0.0f, 14.0f);

    Serial.print("PH:   ADC=");
    Serial.print(phRaw);
    Serial.print(" V=");
    Serial.print(phV, 3);
    Serial.print(" pH=");
    Serial.print(phVal, 2);
    if (phVal < 6.5) {
      Serial.println(" (ACIDIC)");
    } else if (phVal <= 8.5) {
      Serial.println(" (OPTIMAL)");
    } else {
      Serial.println(" (ALKALINE)");
    }

    // Turbidity — analog on GPIO 33
    int turbRaw = readADCAvg(TURBIDITY_PIN);
    float turbV = turbRaw * (3.3f / 4095.0f);
    float turbNTU =
        constrain((1.0f - (turbRaw / 4095.0f)) * 100.0f, 0.0f, 100.0f);
    Serial.print("TURB: ADC=");
    Serial.print(turbRaw);
    Serial.print(" V=");
    Serial.print(turbV, 3);
    Serial.print(" NTU=");
    Serial.print(turbNTU, 1);
    if (turbNTU < 20.0f) {
      Serial.println("  [CLEAR]");
    } else if (turbNTU < 60.0f) {
      Serial.println("  [MODERATE]");
    } else {
      Serial.println("  [TURBID]");
    }

    // Rain
    int rainRaw = readADCAvg(RAIN_PIN);
    float wetness = ((4095.0f - rainRaw) / 4095.0f) * 100.0f;
    Serial.print("RAIN: ");
    Serial.print(wetness);
    if (wetness < 5.0) {
      Serial.println("% (DRY)");
    } else if (wetness < 30.0) {
      Serial.println("% (LIGHT RAIN)");
    } else if (wetness < 60.0) {
      Serial.println("% (MODERATE RAIN)");
    } else {
      Serial.println("% (HEAVY RAIN)");
    }

    // POST ALL 4 SENSORS IN 1 SINGLE FAST BATCH CALL (< 60ms)
    if (WiFi.status() == WL_CONNECTED) {
      postBatchReadings(tempC, phVal, turbNTU, wetness);
    }

    // Trigger next temperature reading asynchronously for next cycle
    tempSensor.requestTemperatures();

    // Always print actuator status after sensor readings
    printMotorStatus();
  }

  // ════════════════════════════════════════════════
  // ACTUATOR POLL BLOCK — runs every 3 seconds
  // ════════════════════════════════════════════════
  if (WiFi.status() == WL_CONNECTED && now - lastPollTime >= 3000) {
    lastPollTime = now;

    // ────────────────────────────────────────────
    // OXYGEN PUMP — GPIO 22
    // ────────────────────────────────────────────
    JsonDocument oxygenDoc;
    if (getJson(MOTOR_URL.c_str(), oxygenDoc)) {

      bool enabled = oxygenDoc["enabled"] | false;
      bool connActive = oxygenDoc["connectionActive"] | true;

      if (enabled == true && connActive == true) {
        if (oxygenIsOn == false) {
          pinMode(OXYGEN_PIN, OUTPUT);
          digitalWrite(OXYGEN_PIN, LOW);
          oxygenIsOn = true;
          Serial.println("OXYGEN: GPIO 22 → OUTPUT LOW → Pump turned ON");
          printMotorStatus();
        }
      } else {
        if (oxygenIsOn == true) {
          pinMode(OXYGEN_PIN, INPUT);
          oxygenIsOn = false;
          Serial.println(
              "OXYGEN: GPIO 22 → INPUT → Pump turned OFF (pin abandoned)");
          printMotorStatus();
        }
      }
    }

    // ────────────────────────────────────────────
    // FEEDER MOTOR & SERVO GATE — GPIO 23 & GPIO 18
    // ────────────────────────────────────────────
    JsonDocument feederDoc;
    if (getJson(FEEDING_URL.c_str(), feederDoc)) {

      bool manualMode = feederDoc["manualMode"] | false;
      bool manualActive = feederDoc["manualActive"] | false;
      int durMin = feederDoc["durationMinutes"] | 1;
      feederDurationMs = durMin * 60 * 1000;

      if (manualMode == true) {
        // ── MANUAL MODE ────────────────────────────
        feederRunning = false; // Reset scheduled tracker when in manual mode

        if (manualActive == true) {
          // User requested Feeder ON: First Servo Gate opens, then Motor starts
          if (feederIsOn == false) {
            startFeedingSequence("Manual Mode");
          }
        } else {
          // User requested Feeder OFF: First Servo Gate closes, then Motor
          // stops
          if (feederIsOn == true) {
            stopFeedingSequence("Manual Mode");
          }
        }

      } else {
        // ── SCHEDULED MODE ─────────────────────────

        // If feeder is currently running, check if duration is complete
        if (feederRunning == true) {
          unsigned long timeRunning = millis() - feederOnAt;
          if (timeRunning >= (unsigned long)feederDurationMs) {
            // Duration completed: First close servo gate, then stop motor
            stopFeedingSequence("Scheduled Timeout");
            feederRunning = false;
            Serial.print("FEEDER: Scheduled run complete (ran ");
            Serial.print(durMin);
            Serial.println(" min)");
          }
        }

        // If feeder is not running, check if current time matches scheduled
        // time
        if (feederRunning == false) {
          struct tm timeInfo;
          if (getLocalTime(&timeInfo)) {
            int nowMinutes = timeInfo.tm_hour * 60 + timeInfo.tm_min;
            JsonArray scheduledTimes = feederDoc["times"];
            for (JsonVariant entry : scheduledTimes) {
              int scheduledMinutes =
                  timeStringToMinutes(entry.as<const char *>());
              if (scheduledMinutes >= 0 && nowMinutes == scheduledMinutes &&
                  timeInfo.tm_sec < 30) {
                // Scheduled time reached: First open servo gate, then start
                // motor
                startFeedingSequence("Scheduled Trigger");
                feederRunning = true;
                feederOnAt = millis();
                Serial.print("FEEDER: Scheduled start for ");
                Serial.print(durMin);
                Serial.println(" min");
                break;
              }
            }
          }
        }

        // Print current scheduled feeder state every poll
        if (feederRunning == true) {
          Serial.println("FEEDER: Scheduled run in progress...");
        } else {
          Serial.println("FEEDER: Scheduled, waiting for next time slot...");
        }
      }
    }
  }
}
