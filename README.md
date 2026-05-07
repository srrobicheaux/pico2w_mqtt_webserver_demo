BatMon | Pico 2W Battery & IO Monitor
BatMon is a specialized firmware for the Raspberry Pi Pico 2W (RP2350) designed to monitor marine or vehicle battery systems. It provides a real-time web interface, MQTT telemetry, and persistent configuration storage.

🚀 Key Features
Real-Time Dashboard: High-frequency updates via Server-Sent Events (SSE).

Dynamic Web Config: Full management of WiFi, MQTT, and IO mapping via /config.

Persistent Flash Storage: Settings are stored in the RP2350’s non-volatile memory (Flash) as a JSON string.

Scalable Monitoring:

3 Analog Channels: Optimized for voltage monitoring with configurable ratios.

12 Digital PIO Pins: Real-time state tracking for switches or bilge pumps.

MQTT Integration: Native support for IoT brokers like Home Assistant.

🛠 Hardware Configuration
Component	Description
MCU	Raspberry Pi Pico 2W (RP2350)
Analog Inputs	ADC0, ADC1, ADC2 (GPIO 26-28)
Digital IO	12 Pins via PIO (Configurable)
Voltage Dividers	External 10kΩ/47kΩ resistors suggested for 12V/24V systems
📂 Project Structure
Plaintext
├── webserver.c    # lwIP TCP server, POST/GET handlers, SSE logic
├── flash.c        # RP2350 Flash erase/program logic for settings
├── system_info.c  # CPU temperature and telemetry data
├── html/          # UI source files (Embedded as Raw HTML strings)
└── CMakeLists.txt # Pico SDK build configuration
⚙️ Installation & Build
Clone the Repository:

Bash
git clone https://github.com/srrobicheaux/pico2w_mqtt_webserver_demo.git
cd pico2w_mqtt_webserver_demo
Build:

Bash
mkdir build && cd build
cmake ..
make


3.  **Flash**:
    Deploy the `BatMon.uf2` file to your Pico 2W using **BOOTSEL** mode.

## 🔧 Setup & Usage

### 1. Network Connection
On first boot, the device will attempt to connect using the default credentials. If unsuccessful, it will initialize in **Provisioning Mode**.

### 2. Configuration
Navigate to `http://<pico-ip>/config` to:
*   Set **WiFi SSID** and **MQTT Broker** details.
*   Name your Battery Banks and assign **Ratios** for calibration.
*   Enable/Disable **Digital Pins** and set their direction (Input/Output).

### 3. Home Assistant Integration
The device publishes state changes to your MQTT broker. You can add sensors to your `configuration.yaml` using the JSON attributes provided by the `/settings` endpoint.

## 📝 License
This project is licensed under the MIT License.
