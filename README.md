# Smart Aquaculture: Edge-AI & IoT Autonomous Pond Management System

A hybrid edge-cloud aquaculture management system deployed on the ESP-32 microcontroller. The platform combines on-device machine learning inference (TinyML) and digital signal conditioning with real-time web telemetry for automated aeration and feeding control.

---

### Project Context and Academic Attribution

This project originated as an undergraduate capstone project at Jashore University of Science and Technology (JUST), Department of Computer Science and Engineering (Group 2, Batch 15).

While the baseline full-stack telemetry and actuator scheduler were developed collaboratively, this repository represents an advanced, standalone research extension developed by **Shafikul Islam Marwan**. This version integrates:
* An on-device TinyML decision tree inference engine running directly on the ESP-32 without cloud dependencies.
* Digital Signal Processing (DSP) exponential moving average low-pass filtering for analog sensor noise suppression.
* Autonomous fail-safe emergency aeration routines triggered directly at the hardware edge.

#### Core Team and Roles
* **Shafikul Islam Marwan** ([@Marwanthe0](https://github.com/Marwanthe0)): Hardware architecture, circuit schematics, sensor calibration (pH, turbidity, temp, rain), DSP signal filtering, and TinyML C++ firmware synthesis.
* **Md Raihan** ([@mdraihan27](https://github.com/mdraihan27)): Backend REST API architecture, MongoDB persistence, and Socket.IO real-time telemetry streaming.
* **Rubyat** ([@rubyat43](https://github.com/rubyat43)): Frontend web console, Next.js 16 layouts, and telemetry data visualization.

---

### Edge-AI & TinyML Implementation: How It Works

The system moves beyond traditional telemetry by embedding an intelligence layer directly onto the ESP-32's dual-core Xtensa processor. 

```
  [Raw Sensor Voltages]
           │
           ▼
  [DSP Low-Pass Filter]  ──> Suppresses high-frequency analog noise (alpha = 0.25)
           │
           ▼
  [TinyML Decision Engine]  ──> Evaluates (Temp, pH, Turbidity) in < 1 ms
           │
     ┌─────┴────────────────────────┐
     ▼                              ▼
[Normal State: 0 or 1]      [Critical State: 2]
  - Dispatch via HTTP         - Autonomous Relay Engagement (GPIO 22 -> Active LOW)
  - Stream via Socket.IO      - Immediate Emergency Aeration (Zero Cloud Latency)
```

#### 1. Training & Synthesis Pipeline (`tinyml/train_and_export.py`)
Rather than deploying heavy inference runtimes (e.g., full TensorFlow or ONNX engines) that exhaust microcontroller RAM, this project utilizes a custom synthesis pipeline:
* **Model Selection:** A depth-bounded Decision Tree Classifier (`max_depth = 4`) trained on freshwater aquaculture physiological standards.
* **C++ Code Synthesis:** The script traverses the binary tree structure recursively and exports the decision boundaries as a clean, self-contained C++ header file (`water_quality_model.h`).
* **Zero Overhead:** The generated code uses compile-time `inline` branching with zero dynamic heap allocations (`malloc`), consuming under 1 KB of flash memory.

#### 2. On-Device Inference Logic (`water_quality_model.h`)
The generated C++ function directly maps sensor floating-point inputs against trained decision thresholds:

```cpp
// Executed directly on ESP-32 every 3 seconds:
inline int predict_water_quality(float temperature, float ph, float turbidity) {
    if (turbidity <= 75.0360f) {
        if (ph <= 9.4999f) {
            if (ph <= 5.5005f) {
                return 2; // Critical: Severe Acidity
            } else {
                if (temperature <= 34.0274f) {
                    return 1; // Warning: Sub-optimal temperature
                } else {
                    return 2; // Critical: Extreme Thermal Stress
                }
            }
        } else {
            return 2; // Critical: Severe Alkalinity
        }
    } else {
        return 2; // Critical: High Silt / Extreme Turbidity
    }
}
```

#### 3. Closed-Loop Hardware Autonomy
* In conventional IoT systems, if the local Wi-Fi drops or the backend server shuts down, automated safeguards cease to function.
* With this Edge-AI integration, the ESP-32 continuously checks the inference output. If a `CRITICAL (Class 2)` state is detected, the firmware executes an immediate hardware fail-safe:
  ```cpp
  if (waterCondition == 2 && !oxygenIsOn) {
      pinMode(OXYGEN_PIN, OUTPUT);
      digitalWrite(OXYGEN_PIN, LOW); // Trigger active-low relay immediately
      oxygenIsOn = true;
  }
  ```
  This ensures that oxygen pumps run autonomously even during complete network or server blackouts.

---

### DSP Sensor Signal Conditioning

Low-cost analog probes (pH, turbidity) are vulnerable to electromagnetic interference and voltage ripples from nearby motors. The firmware applies a single-pole Exponential Moving Average (EMA) low-pass filter:

$$y[n] = \alpha \cdot x[n] + (1 - \alpha) \cdot y[n-1]$$

With $\alpha = 0.25$, transient spikes are suppressed while maintaining responsiveness to true biological water drift.

---

### Hardware Interfacing and Pinout

| Component / Sensor | Interface / Protocol | ESP32 GPIO | Operating Voltage |
|---|---|---|---|
| DS18B20 Temp Sensor | Digital 1-Wire | `GPIO 4` | 3.3V / 5V |
| Analog pH Sensor | Analog (ADC1) | `GPIO 34` | 5V (Calibrated to 3.3V) |
| Analog Turbidity Sensor | Analog (ADC1, 0 to 100 NTU) | `GPIO 33` | 5V |
| Raindrop Moisture Sensor | Analog (ADC1) | `GPIO 32` | 3.3V / 5V |
| Oxygen Aeration Pump | Relay Module (Active LOW) | `GPIO 22` | 5V Coil / 220V AC Load |
| Feeder Dispenser Motor | Relay Module (Active LOW) | `GPIO 23` | 5V Coil / 12V DC Load |
| Feeder Gate Servo | MG 996R PWM (50Hz) | `GPIO 21` | 5V to 6V External |

![Hardware Setup](docs/assets/hardware-setup.jpg)

*Figure 1: Complete Prototype Hardware Assembly with ESP-32 and Sensor Array.*

---

### Getting Started

#### Prerequisites
* Node.js (v18+) and MongoDB installed locally or hosted remotely.
* Arduino IDE (v2.0+) with the ESP32 board package installed via Boards Manager.
* Required Arduino libraries:
  * `ArduinoJson` (Benoit Blanchon)
  * `DallasTemperature` (Miles Burton)
  * `OneWire` (Jim Studt, Tom Pollard)
  * `ESP32Servo` (Kevin Harrington, John K. Bennet)

---

#### 1. Backend Service Setup
The backend service handles persistent storage in MongoDB, exposes REST endpoints for batch telemetry, and broadcasts live readings over WebSockets.

1. Navigate to the `backend` directory:
   ```bash
   cd backend
   npm install
   ```
2. Create a `.env` file following `.env.example` with your MongoDB connection string and server port.
3. Start the service in development mode:
   ```bash
   npm run dev
   ```

![Backend Setup](docs/assets/backend-setup.png)

*Figure 2: Backend service initialization and live endpoint bindings.*

---

#### 2. ESP-32 Firmware Deployment
1. Open `esp32/esp_code/esp_code.ino` in the Arduino IDE.
2. Verify that `water_quality_model.h` resides in the same directory.
3. Update `WIFI_SSID`, `WIFI_PASSWORD`, `BACKEND_IP`, and `BACKEND_PORT` to match your local network configuration.
4. Select target board **ESP32 Dev Module**, connect via USB, and click **Upload**.
5. Open the Serial Monitor at **115200 baud** to view real-time DSP-filtered telemetry and TinyML inference outputs.

![ESP32 Setup](docs/assets/esp32-setup.png)

*Figure 3: Serial monitor output demonstrating telemetry acquisition and batch submission.*

---

#### 3. Frontend Web Console Setup
1. Navigate to the `frontend` directory:
   ```bash
   cd frontend
   npm install
   npm run dev
   ```
2. Open `http://localhost:3000` in your web browser.
3. Log in using your configured credentials.

![Frontend Setup](docs/assets/frontend-setup.png)

*Figure 4: Frontend service execution.*

![Landing Page](docs/assets/landing-page.png)

*Figure 5: Web Console Access Portal.*

---

### Dashboard Operations and Telemetry

#### Live Monitoring & Historical Trends
The primary console displays live cards for water temperature, pH, turbidity, and rainfall with dynamic status badges. Multi-point trend charts allow farmers to observe parameter drifts over time.

| Live Status Cards | Historical Parameter Trends |
|:---:|:---:|
| ![Updates menu](docs/assets/updates-page.png) | ![Trend charts](docs/assets/updates-page-2.png) |
| *Figure 6: Live Telemetry Cards* | *Figure 7: Parameter History Charts* |

#### Actuator Controls and Scheduling
Farmers can configure timed feeding routines or trigger manual overrides for the aeration pump and food dispenser.

| Actuator Controls | Automated Feeding Scheduler |
|:---:|:---:|
| ![Controls menu](docs/assets/controls-page.png) | ![Feeding schedule](docs/assets/feeding-schedule.png) |
| *Figure 8: Actuator Control Panel* | *Figure 9: Feeder Routine Configuration* |

#### Threshold Settings
Safe operating windows for temperature, pH, and precipitation triggers can be reconfigured dynamically through the settings panel.

![Settings Configuration](docs/assets/settings-page.png)

*Figure 10: Threshold and Alert Configuration Interface.*

![Account settings](docs/assets/account-page.png)

*Figure 11: Security and API Key Management.*

---

### Summary and Engineering Impact

This project demonstrates an accessible, robust smart aquaculture solution combining physical sensor interfacing, deterministic on-chip intelligence, and responsive web telemetry. By relocating critical water-stress classification and fail-safe actuation directly to the ESP-32 hardware edge, the system guarantees continuous pond preservation even under complete network failure.
