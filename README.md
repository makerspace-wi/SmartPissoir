# SmartPissoir

Automatische Spuelsteuerung auf ESP8266 mit VL53L0X-Abstandssensor.
Das System erkennt Anwesenheit, wartet auf das Verlassen des Bereichs und startet danach eine zeitgesteuerte Spuelung.

## Features

- Abstandsmessung mit VL53L0X (I2C)
- Mindest-Anwesenheitszeit zur Verifikation
- Spuelung ueber Ausgangspin (z. B. MOSFET/Relais)
- Status-LED waehrend der Spuelung
- WLAN-Setup ueber WiFiManager (Captive Portal)
- MQTT-Anbindung mit Reconnect
- Konfigurationswerte werden per MQTT publiziert und sind per MQTT zur Laufzeit aenderbar
- Elegant OTA Web-Update ueber Browser

## Projektstruktur

- platformio.ini: Board-, Build- und Library-Konfiguration
- src/main.cpp: Hauptlogik (Sensor, Ablauf, WLAN, MQTT)
- include/, lib/, test/: Standard-Ordner von PlatformIO

## Hardware

Aktuelle Pinbelegung in src/main.cpp:

- SOLENOID_PIN = 2
- LED_PIN = LED_BUILTIN
- SENSOR_INTERRUPT = 14 (derzeit nicht aktiv genutzt)
- SENSOR_XSHUT = 13 (derzeit nicht aktiv genutzt)

Hinweis:
Auf ESP8266-Boards kann LED_BUILTIN invertiert sein (LOW = an, HIGH = aus).

## Sensor-Verdrahtung (VL53L0X)

- SDA -> D2 (GPIO4)
- SCL -> D1 (GPIO5)
- GND -> GND
- VCC -> 3.3V oder 5V (abh. vom Modul)

## Build, Upload, Monitor

Im Projektordner ausfuehren:

- Build: ~/.platformio/penv/bin/platformio run
- Upload: ~/.platformio/penv/bin/platformio run --target upload
- Serial Monitor: ~/.platformio/penv/bin/platformio device monitor --baud 115200

## WLAN-Einrichtung

Beim ersten Start oeffnet WiFiManager ein Konfigurationsportal mit SSID:

- SmartPissoir-Setup

Dort WLAN auswaehlen und Zugangsdaten speichern.

Zusatzfelder im Portal:

- MQTT Server
- MQTT Port

Diese Werte werden nach dem Speichern persistent im Dateisystem (LittleFS) abgelegt und beim naechsten Start automatisch geladen.

## MQTT-Konfiguration

Standardwerte in src/main.cpp:

- mqttBroker = 192.168.0.5
- mqttPort = 1883
- MQTT_CLIENT_ID = smartpissoir-esp8266

Empfohlen:
MQTT Server und MQTT Port im WiFiManager-Portal setzen. Ein Neu-Flashen ist dafuer nicht noetig.

## OTA-Update (ElegantOTA)

Nach WLAN-Verbindung ist das OTA-Webinterface unter der Geraete-IP erreichbar:

- http://<geraet-ip>/update

Basis-Statusseite:

- http://<geraet-ip>/

Damit kannst du neue Firmware direkt im Browser hochladen.

## MQTT Topics

Status/State (retained):

- smartpissoir/config/activationThresh
- smartpissoir/config/minPresenceTime
- smartpissoir/config/flushDuration
- smartpissoir/config/state
- smartpissoir/status/online (LWT: "online" bei Verbindung, "offline" bei Trennung)

Set-Topics (zur Laufzeit veraendern):

- smartpissoir/config/set/activationThresh
- smartpissoir/config/set/minPresenceTime
- smartpissoir/config/set/flushDuration

Command-Topics:

- smartpissoir/command/flush

Payload-Format fuer Set-Topics:

- Positive Ganzzahl als Text, z. B. 300

Aktuell implementierte Wertebereiche:

- activationThresh: 50 bis 2000 (mm)
- minPresenceTime: 500 bis 30000 (ms)
- flushDuration: 500 bis 30000 (ms)

Diese drei Werte werden nach MQTT-Aenderungen auf LittleFS gespeichert und nach einem Neustart wieder geladen.

Beispiel:

Wenn auf smartpissoir/config/set/flushDuration der Wert 7000 publiziert wird, setzt das Geraet die Spueldauer auf 7000 ms und publiziert den neuen Wert wieder auf smartpissoir/config/flushDuration.

Wenn auf smartpissoir/command/flush eine Nachricht gesendet wird, startet sofort eine Spuelung.

Das Topic smartpissoir/config/state enthaelt den kompletten aktuellen Zustand als JSON, zum Beispiel:

{ "activationThresh": 300, "minPresenceTime": 5000, "flushDuration": 10000, "mqttBroker": "192.168.0.5", "mqttPort": 1883 }

## Ablauf

1. Nutzer unterschreitet activationThresh.
2. Anwesenheit muss mindestens minPresenceTime anhalten.
3. System wartet, bis der Bereich wieder frei ist.
4. Ventil wird fuer flushDuration aktiviert.
5. System geht direkt zurueck in den Wartezustand.

## Abhaengigkeiten

In platformio.ini:

- pololu/VL53L0X
- tzapu/WiFiManager
- knolleary/PubSubClient
- ayushsharma82/ElegantOTA

## Troubleshooting

- Keine WLAN-Verbindung:
  Neustart, dann erneut mit SmartPissoir-Setup verbinden.
- Keine MQTT-Verbindung:
  Broker-IP/Port pruefen und sicherstellen, dass Broker im gleichen Netz erreichbar ist.
- Keine Distanzwerte:
  I2C-Verdrahtung und Sensorversorgung pruefen.
- Unerwartetes Schaltverhalten:
  LED-Logik auf invertierte LED_BUILTIN-Prinzipien des Boards pruefen.
