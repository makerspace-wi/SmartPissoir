<img width="400" alt="IMG_6802" src="https://github.com/user-attachments/assets/0aaeb4b7-9a7e-42e6-9aa3-eeab565c8217" />

# SmartPissoir

Automatische Spülsteuerung auf ESP8266 mit VL53L0X-Abstandssensor.
Das System erkennt Anwesenheit, wartet auf das Verlassen des Bereichs und startet danach eine zeitgesteuerte Spülung.

## Features

- Abstandsmessung mit VL53L0X (I2C)
- Mindest-Anwesenheitszeit zur Verifikation
- Spülung über Ausgangspin (MOSFET)
- Status-LED während der Spülung
- WLAN-Setup über WiFiManager (Captive Portal)
- MQTT-Anbindung mit Reconnect
- Konfigurationszustand wird als MQTT State publiziert und ist per MQTT zur Laufzeit änderbar
- Elegant OTA Web-Update über Browser

## Projektstruktur

- platformio.ini: Board-, Build- und Library-Konfiguration
- src/main.cpp: Hauptlogik (Sensor, Ablauf, WLAN, MQTT)
- include/, lib/, test/: Standard-Ordner von PlatformIO

## Hardware

Aktuelle Pinbelegung in src/main.cpp:

- SOLENOID_PIN = GPIO12
- LED_PIN = LED_BUILTIN (GPIO2)

Hinweis:
Auf ESP8266-Boards kann LED_BUILTIN invertiert sein (LOW = an, HIGH = aus).

## Sensor-Verdrahtung (VL53L0X)

- SDA -> D2 (GPIO4)
- SCL -> D1 (GPIO5)
- GND -> GND
- VCC -> 3.3V

## Build, Upload, Monitor

Im Projektordner ausführen:

- Build: ~/.platformio/penv/bin/platformio run
- Upload: ~/.platformio/penv/bin/platformio run --target upload
- Serial Monitor: ~/.platformio/penv/bin/platformio device monitor --baud 115200

## WLAN-Einrichtung

Beim ersten Start öffnet WiFiManager ein Konfigurationsportal mit SSID:

- SmartPissoir-Setup

Dort WLAN auswählen und Zugangsdaten speichern.

Zusatzfelder im Portal:

- MQTT Server
- MQTT Port

Diese Werte werden nach dem Speichern persistent im Dateisystem (LittleFS) abgelegt und beim nächsten Start automatisch geladen.

## MQTT-Konfiguration

Standardwerte in src/main.cpp:

- mqttBroker = 192.168.0.5
- mqttPort = 1883
- MQTT_CLIENT_ID = smartpissoir-esp8266

Empfohlen:
MQTT Server und MQTT Port im WiFiManager-Portal setzen. Ein Neu-Flashen ist dafür nicht nötig.

## OTA-Update (ElegantOTA)

Nach WLAN-Verbindung ist das OTA-Webinterface unter der Geräte-IP erreichbar:

- http://<gerät-ip>/update

Basis-Statusseite:

- http://<gerät-ip>/

Damit kannst du neue Firmware direkt im Browser hochladen.

## MQTT Topics

Status/State (retained):

- smartpissoir/config/state
- smartpissoir/status/online (LWT: "online" bei Verbindung, "offline" bei Trennung)

Set-Topics (zur Laufzeit verändern):

- smartpissoir/config/set/activationThresh
- smartpissoir/config/set/minPresenceTime
- smartpissoir/config/set/flushDuration

Command-Topics:

- smartpissoir/command/flush

Payload-Format für Set-Topics:

- Positive Ganzzahl als Text, z. B. 300

Aktuell implementierte Wertebereiche:

- activationThresh: 50 bis 2000 (mm)
- minPresenceTime: 500 bis 30000 (ms)
- flushDuration: 500 bis 30000 (ms)

Diese drei Werte werden nach MQTT-Änderungen auf LittleFS gespeichert und nach einem Neustart wieder geladen.

Beispiel:

Wenn auf smartpissoir/config/set/flushDuration der Wert 7000 publiziert wird, setzt das Gerät die Spüldauer auf 7000 ms und publiziert den neuen Zustand auf smartpissoir/config/state.

Wenn auf smartpissoir/command/flush eine Nachricht gesendet wird, startet sofort eine Spülung.

Das Topic smartpissoir/config/state enthält den kompletten aktuellen Zustand als JSON, zum Beispiel:

{ "activationThresh": 300, "minPresenceTime": 5000, "flushDuration": 10000, "mqttBroker": "192.168.0.5", "mqttPort": 1883, "ip": "192.168.0.42" }

## Ablauf

1. Nutzer unterschreitet activationThresh.
2. Anwesenheit muss mindestens minPresenceTime anhalten.
3. System wartet, bis der Bereich wieder frei ist.
4. Ventil wird für flushDuration aktiviert.
5. System geht direkt zurück in den Wartezustand.

## Abhängigkeiten

In platformio.ini:

- pololu/VL53L0X
- tzapu/WiFiManager
- knolleary/PubSubClient
- ayushsharma82/ElegantOTA

## Troubleshooting

- Keine WLAN-Verbindung:
  Neustart, dann erneut mit SmartPissoir-Setup verbinden.
- Keine MQTT-Verbindung:
  Broker-IP/Port prüfen und sicherstellen, dass Broker im gleichen Netz erreichbar ist.
- Keine Distanzwerte:
  I2C-Verdrahtung und Sensorversorgung prüfen.
- Unerwartetes Schaltverhalten:
  LED-Logik auf invertierte LED_BUILTIN-Prinzipien des Boards prüfen.
  
<img width="400" alt="IMG_6799 2" src="https://github.com/user-attachments/assets/e53c3068-c3d8-4a6e-9d7a-cf4aae7b5b8f" />
