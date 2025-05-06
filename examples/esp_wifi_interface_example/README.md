# esp_wifi_interface

## Requirements
- ESP‑IDF (v4.x or newer) with **NVS Flash** and **HTTP Server** components enabled  
- ESP32 board supported by your ESP‑IDF version  

## Behavior
- On boot, reads `SSID` from NVS  
  - If `SSID != "empty"`, enters **STA mode** and tries to connect  
  - If connection fails **esp_max_retry** times, or if `SSID == "empty"`, falls back to **AP mode** at **192.168.4.1**  

## Web Configuration
1. Connect your PC/phone to the Wi‑Fi network  
    SSID: COIIOTE  
    Password: coiiote123  
2. Open in your browser:  
    http://192.168.4.1/getssid  
3. Enter your target **SSID** and **Password**, then submit  
4. ESP32 saves credentials to NVS and **restarts**  

## Usage
Build, flash and monitor:  
- **First boot** (NVS empty): runs as AP for setup  
- **Subsequent boots**: runs as STA until it either connects or retries exhaust, then re-enters AP mode for reconfiguration  

# trouble shooting
Component Config -> HTTP Server -> Max HTTP Request Header Length: 1024