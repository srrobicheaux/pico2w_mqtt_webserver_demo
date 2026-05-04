# Tesla Sync Node 🚗⚡

An intelligent BLE tracking node built for the **Raspberry Pi Pico 2 W**. This project monitors the RSSI (Signal Strength) of a Tesla vehicle to determine if it is approaching or leaving a garage, allowing for smart home automation triggers.

## Key Features
- **High-Frequency BLE Scanning:** Real-time tracking of Tesla BLE advertisements.
- **Gaussian Weighted Trend Analysis:** Uses a Gaussian-weighted linear regression to calculate signal slope. This filters out RSSI "jitter" while remaining highly responsive to movement.
- **Web Dashboard:** A live, browser-based dashboard served directly from the Pico using Server-Sent Events (SSE).
- **Event Logging:** Automatic timestamped logs of state changes (Stationary -> Approaching -> Connected).

Device Discovery & API Integration
This node is designed to minimize expensive API calls to the Tesla Developer Portal by using local BLE intelligence.

1. Smart Target Acquisition
The system allows for flexible provisioning via the local web interface. You can configure a target in two ways:

BLE Name: The node scans for the specific Tesla broadcast name (e.g., SblenameofteslaC).

MAC Address: You can provide a direct hardware MAC address for faster locking.

The Discovery Flow: If a Name is provided, the node will actively scan all nearby BLE advertisements. Once a name match is found, the system automatically resolves the hardware MAC Address, stores it in Flash memory, and switches the tracking target to that MAC for increased performance and reliability.

2. Tesla Developer API Brokering
While this node handles the local "heavy lifting" of distance tracking, it is designed to communicate with a Middleware Web Server.

Reduced Overhead: Instead of constant polling, the Pico only triggers the middleware when the Gaussian Trend confirms a high-confidence "Arrival" or "Departure" event.

JSON Response: The node can be configured to pull target updates from a remote endpoint. It expects a JSON payload format: {"BLE": "YourTeslaNameOrMAC"}.

3. Automated Triggers (Roadmap)
Future updates will utilize the validated trend data to automate vehicle commands (e.g., pre-conditioning, garage door opening, or charging start) by sending secure handshake requests to the Tesla API broker only when the vehicle crosses specific RSSI slope thresholds.

## Hardware Requirements
- Raspberry Pi Pico 2 W (or Pico W)
- A Tesla Vehicle with Phone Key / BLE enabled

## Software Setup

### 1. Secrets Management
Create a `secrets.h` file in the root directory (this file is ignored by Git for security):
```cpp
#ifndef SECRETS_H
#define SECRETS_H

//I have a web server brokering calls to Tesla Developer API.
//This is not required, but you must fill in a BLE MAC or BLE Name on the provisioniong website.
//Eventually this code will trigger calls to Tesla to confirm that the car is arriving or departing. This code helps reduce those calls to a bare minimum.
//It responds with a JSON packet that contains {"BLE":"SblenameofteslaC"}.
//The name is then stored to flash as a target. WHen the BLE MAC is discovered the target is changed to the MAC String. You can configure the provisionoing website with a BLE Name or MAC String. If blank it will try the website configured.
//As such you might find it useful to have this code pull from a website the BLE Name.


#define AZURE_HOST "YOURSERVERHERE.azurewebsites.net"
#define PHP_PATH "/YOURPHPHERE.php"

//actually these are option. You can use the provisioning service to setup WiFi.
//The captive portal wont work with apple devices.
//Just go to website 192.168.4.1 once you have joing the pico's Access Point
#define WIFI_SSID "Your_WiFi_Name"
#define WIFI_PASSWORD "Your_WiFi_Password"

#endif