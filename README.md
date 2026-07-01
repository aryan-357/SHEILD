PROJECT SHIELD Admin: Secure Hardware Interface // Attendance Tracking & Absence Monitoring

SHIELD Admin is an innovative IoT system that combines both hardware and software to monitor attendance, detect skipping behaviors, and pinpoint security issues instantly. By using an ESP32 microcontroller along with various sensors, it effectively keeps track of people entering and leaving through access points while providing a user-friendly web dashboard straight from the device.

-----------------------------------------------------------------------------------
Key Features

* **Smart Sensor Integration:** Combines RFID checks (`MFRC522`) with infrared (`IR`) beam interruptions and magnetic Hall Effect sensors to accurately log entries and exits.
* **Instant Security Notifications:** Quickly identifies breaches in security and keeps a record of safety status:
* *Tailgating Detected:* Multiple entries recorded with one valid card scan.
* *Unauthorized Entry:* Sensor activity noticed without a corresponding RFID tag scan.
* *Door Left Ajar:* Alerts triggered when a door isn't fully closed.
* **Integrated Web Dashboard:** The ESP32 runs a control interface that includes light and dark modes, on-the-spot data filtering, and a real-time leaderboard for monitoring absentees.
* **Printable Reports:** Features a modular style sheet (`report.html`) for neat print layouts, ideal for keeping formal records or generating PDFs.

---------------------------------------------------------------------------------------

 Pin Connection Guide

| Component | Function | ESP32 Pin |

| **MFRC522 RFID** | SDA (Slave Select) | GPIO 5 |
| **MFRC522 RFID** | SCK / MOSI / MISO / RST | GPIO 18 / 23 / 19 / 22 |
| **IR Sensor** | Digital Input for Beam Break | GPIO 27 |
| **Hall Sensor** | Digital Input for Door Magnet | GPIO 34 |
---------------------------------------------------------------------------------------
Getting Started & Setup

Requirements
* Ensure the Arduino IDE or PlatformIO is installed.
* Necessary hardware library: `MFRC522` (developed by Miguel Balboa).
---------------------------------------------------------------------------------------
Steps for Setup

1. **Connect Hardware:** Set up the components on your prototyping board or custom PCB in accordance with the pin connection chart.
2. **Network Configuration:** Access the main firmware setup and input your Wi-Fi details:
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
