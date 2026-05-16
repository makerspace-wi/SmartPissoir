#include <Arduino.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ElegantOTA.h>

// VL53L0X test via I2C (Pololu library)
// Wiring (ESP8266):
// TOF SDA -> D2 (GPIO4)
// TOF SCL -> D1 (GPIO5)
// GND    -> GND
// VCC    -> 3.3V or 5V (sensor dependent)


// Pin-Definitionen für ESP8266
#define SOLENOID_PIN 12     // D6: MOSFET-Gate für Magnetventil
#define SENSOR_INTERRUPT 14 // D5: Verbunden mit GPIO1 des VL53L0X
#define SENSOR_XSHUT 13     // D7: Zum Reset/Ausschalten des Sensors

VL53L0X sensor;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer otaServer(80);

float readDistance();
void runCycle();
void setupWifi();
void ensureMqttConnection();
void onMqttMessage(char* topic, byte* payload, unsigned int length);
bool parsePositiveInt(const byte* payload, unsigned int length, int& valueOut);
void onWifiManagerSaveConfig();
void loadRuntimeConfig();
void saveRuntimeConfig();
void loadMqttConfig();
void saveMqttConfig();
void processRemoteFlushRequest();
void performFlush();
void publishConfigState();
void setupOta();
void serviceOta();

// Parameter (anpassbar)
    const int LED_PIN = LED_BUILTIN;  // D4/GPIO2
    int activationThresh = 300; // x mm Aktivierungsabstand [2]
    int minPresenceTime = 5000; // 5 Sek. Mindestanwesenheit [1]
    int flushDuration = 10000;  // 10 Sek. Spüldauer [3]

  char mqttBroker[40] = "192.168.0.5";
  uint16_t mqttPort = 1883;
  const char MQTT_CLIENT_ID[] = "smartpissoir-esp8266";
  const char MQTT_TOPIC_ACTIVATION[] = "smartpissoir/config/activationThresh";
  const char MQTT_TOPIC_PRESENCE[] = "smartpissoir/config/minPresenceTime";
  const char MQTT_TOPIC_FLUSH[] = "smartpissoir/config/flushDuration";
  const char MQTT_TOPIC_STATE[] = "smartpissoir/config/state";
  const char MQTT_SET_TOPIC_ACTIVATION[] = "smartpissoir/config/set/activationThresh";
  const char MQTT_SET_TOPIC_PRESENCE[] = "smartpissoir/config/set/minPresenceTime";
  const char MQTT_SET_TOPIC_FLUSH[] = "smartpissoir/config/set/flushDuration";
  const char MQTT_COMMAND_TOPIC_FLUSH[] = "smartpissoir/command/flush";
  const char MQTT_TOPIC_LWT[] = "smartpissoir/status/online";

  unsigned long lastMqttReconnectAttempt = 0;
  bool shouldSaveMqttConfig = false;
  bool remoteFlushRequested = false;

    void setup()
    {
      Serial.begin(115200);
      pinMode(SOLENOID_PIN, OUTPUT);
      pinMode(LED_PIN, OUTPUT);
      digitalWrite(SOLENOID_PIN, LOW); // Ventil zu Beginn geschlossen
      digitalWrite(LED_PIN, HIGH); // LED zu Beginn ausgeschaltet
      Wire.begin();

      // Sensor initialisieren
      if (!sensor.init())
      {
        Serial.println(F("VL53L0X konnte nicht gestartet werden!"));
        while (1)
          ;
      }
      sensor.setTimeout(500);

      setupWifi();
      setupOta();
      mqttClient.setServer(mqttBroker, mqttPort);
      mqttClient.setCallback(onMqttMessage);
      ensureMqttConnection();
      publishConfigState();

      Serial.println(F("System aktiv. Warte auf Nutzer..."));
    }

void loop()
{
  if (!mqttClient.connected())
  {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > 5000)
    {
      lastMqttReconnectAttempt = now;
      ensureMqttConnection();
      if (mqttClient.connected())
      {
        publishConfigState();
      }
    }
  }
  else
  {
    mqttClient.loop();
  }

  serviceOta();
  processRemoteFlushRequest();
  runCycle();
  delay(100);
}

void setupOta()
{
  otaServer.on("/", HTTP_GET, []() {
    otaServer.send(200, "text/plain", "SmartPissoir OTA aktiv. Update unter /update");
  });

  ElegantOTA.begin(&otaServer);
  otaServer.begin();
  Serial.println(F("ElegantOTA bereit: http://<geraet-ip>/update"));
}

void serviceOta()
{
  otaServer.handleClient();
  ElegantOTA.loop();
}

void setupWifi()
{
  if (!LittleFS.begin())
  {
    Serial.println(F("LittleFS konnte nicht initialisiert werden."));
  }
  loadRuntimeConfig();
  loadMqttConfig();

  WiFiManager wm;
  char mqttPortStr[6];
  snprintf(mqttPortStr, sizeof(mqttPortStr), "%u", mqttPort);

  WiFiManagerParameter mqttServerParam("mqtt_server", "MQTT Server", mqttBroker, sizeof(mqttBroker));
  WiFiManagerParameter mqttPortParam("mqtt_port", "MQTT Port", mqttPortStr, sizeof(mqttPortStr));
  wm.addParameter(&mqttServerParam);
  wm.addParameter(&mqttPortParam);
  wm.setSaveConfigCallback(onWifiManagerSaveConfig);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("SmartPissoir-Setup"))
  {
    Serial.println(F("Kein WLAN konfiguriert, Neustart..."));
    ESP.restart();
  }

  strncpy(mqttBroker, mqttServerParam.getValue(), sizeof(mqttBroker) - 1);
  mqttBroker[sizeof(mqttBroker) - 1] = '\0';
  int parsedPort = atoi(mqttPortParam.getValue());
  if (parsedPort > 0 && parsedPort <= 65535)
  {
    mqttPort = static_cast<uint16_t>(parsedPort);
  }
  if (shouldSaveMqttConfig)
  {
    saveMqttConfig();
  }

  Serial.print(F("WLAN verbunden, IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("MQTT Ziel: "));
  Serial.print(mqttBroker);
  Serial.print(':');
  Serial.println(mqttPort);
}

void ensureMqttConnection()
{
  if (mqttClient.connected())
  {
    return;
  }

  Serial.print(F("MQTT verbindet zu "));
  Serial.print(mqttBroker);
  Serial.print(':');
  Serial.println(mqttPort);

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_TOPIC_LWT, 1, true, "offline"))
  {
    Serial.println(F("MQTT verbunden."));
    // Status als online publizieren
    mqttClient.publish(MQTT_TOPIC_LWT, "online", true);
    mqttClient.subscribe(MQTT_SET_TOPIC_ACTIVATION);
    mqttClient.subscribe(MQTT_SET_TOPIC_PRESENCE);
    mqttClient.subscribe(MQTT_SET_TOPIC_FLUSH);
    mqttClient.subscribe(MQTT_COMMAND_TOPIC_FLUSH);
  }
  else
  {
    Serial.print(F("MQTT Fehler, rc="));
    Serial.println(mqttClient.state());
  }
}

void onWifiManagerSaveConfig()
{
  shouldSaveMqttConfig = true;
}

void loadRuntimeConfig()
{
  if (!LittleFS.exists("/runtime.cfg"))
  {
    return;
  }

  File f = LittleFS.open("/runtime.cfg", "r");
  if (!f)
  {
    return;
  }

  String activation = f.readStringUntil('\n');
  activation.trim();
  String presence = f.readStringUntil('\n');
  presence.trim();
  String flush = f.readStringUntil('\n');
  flush.trim();
  f.close();

  int parsedActivation = activation.toInt();
  int parsedPresence = presence.toInt();
  int parsedFlush = flush.toInt();

  if (parsedActivation >= 50 && parsedActivation <= 2000)
  {
    activationThresh = parsedActivation;
  }
  if (parsedPresence >= 500 && parsedPresence <= 30000)
  {
    minPresenceTime = parsedPresence;
  }
  if (parsedFlush >= 500 && parsedFlush <= 30000)
  {
    flushDuration = parsedFlush;
  }
}

void saveRuntimeConfig()
{
  File f = LittleFS.open("/runtime.cfg", "w");
  if (!f)
  {
    Serial.println(F("Konnte Laufzeitparameter nicht speichern."));
    return;
  }

  f.println(activationThresh);
  f.println(minPresenceTime);
  f.println(flushDuration);
  f.close();
  Serial.println(F("Laufzeitparameter gespeichert."));
}

void loadMqttConfig()
{
  if (!LittleFS.exists("/mqtt.cfg"))
  {
    return;
  }

  File f = LittleFS.open("/mqtt.cfg", "r");
  if (!f)
  {
    return;
  }

  String server = f.readStringUntil('\n');
  server.trim();
  String port = f.readStringUntil('\n');
  port.trim();
  f.close();

  if (server.length() > 0)
  {
    server.toCharArray(mqttBroker, sizeof(mqttBroker));
  }
  int parsedPort = port.toInt();
  if (parsedPort > 0 && parsedPort <= 65535)
  {
    mqttPort = static_cast<uint16_t>(parsedPort);
  }
}

void saveMqttConfig()
{
  File f = LittleFS.open("/mqtt.cfg", "w");
  if (!f)
  {
    Serial.println(F("Konnte MQTT-Konfiguration nicht speichern."));
    return;
  }

  f.println(mqttBroker);
  f.println(mqttPort);
  f.close();
  shouldSaveMqttConfig = false;
  Serial.println(F("MQTT-Konfiguration gespeichert."));
}

void publishConfigState()
{
  if (!mqttClient.connected())
  {
    return;
  }

  char payload[256];
  String ipStr = WiFi.localIP().toString();
  snprintf(
    payload,
    sizeof(payload),
    "{\"activationThresh\":%d,\"minPresenceTime\":%d,\"flushDuration\":%d,\"mqttBroker\":\"%s\",\"mqttPort\":%u,\"ip\":\"%s\"}",
    activationThresh,
    minPresenceTime,
    flushDuration,
    mqttBroker,
    mqttPort,
    ipStr.c_str());

  mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
  Serial.println(F("MQTT Config State publiziert."));
}

void onMqttMessage(char* topic, byte* payload, unsigned int length)
{
  if (strcmp(topic, MQTT_COMMAND_TOPIC_FLUSH) == 0)
  {
    remoteFlushRequested = true;
    Serial.println(F("MQTT Fernspuelung angefordert."));
    return;
  }

  int newValue = 0;
  if (!parsePositiveInt(payload, length, newValue))
  {
    Serial.println(F("MQTT Payload ungueltig (erwartet positive Zahl)."));
    return;
  }

  if (strcmp(topic, MQTT_SET_TOPIC_ACTIVATION) == 0)
  {
    if (newValue >= 50 && newValue <= 2000)
    {
      activationThresh = newValue;
      Serial.print(F("activationThresh gesetzt auf: "));
      Serial.println(activationThresh);
      saveRuntimeConfig();
      publishConfigState();
    }
    return;
  }

  if (strcmp(topic, MQTT_SET_TOPIC_PRESENCE) == 0)
  {
    if (newValue >= 500 && newValue <= 30000)
    {
      minPresenceTime = newValue;
      Serial.print(F("minPresenceTime gesetzt auf: "));
      Serial.println(minPresenceTime);
      saveRuntimeConfig();
      publishConfigState();
    }
    return;
  }

  if (strcmp(topic, MQTT_SET_TOPIC_FLUSH) == 0)
  {
    if (newValue >= 500 && newValue <= 30000)
    {
      flushDuration = newValue;
      Serial.print(F("flushDuration gesetzt auf: "));
      Serial.println(flushDuration);
      saveRuntimeConfig();
      publishConfigState();
    }
    return;
  }
}

bool parsePositiveInt(const byte* payload, unsigned int length, int& valueOut)
{
  if (length == 0 || length >= 11)
  {
    return false;
  }

  char buf[11];
  for (unsigned int i = 0; i < length; i++)
  {
    if (!isDigit(payload[i]))
    {
      return false;
    }
    buf[i] = static_cast<char>(payload[i]);
  }
  buf[length] = '\0';

  long parsed = atol(buf);
  if (parsed <= 0 || parsed > 2147483647L)
  {
    return false;
  }

  valueOut = static_cast<int>(parsed);
  return true;
}

void runCycle()
{
  // PRUEFPHASE: Ist jemand da?
  unsigned long startTime = millis();
  bool userVerified = false;

  while (readDistance() < activationThresh)
  {
    mqttClient.loop();
    serviceOta();
    processRemoteFlushRequest();
    if (remoteFlushRequested)
    {
      break;
    }

    if (millis() - startTime >= static_cast<unsigned long>(minPresenceTime))
    {
      userVerified = true;
      Serial.println(F("Nutzer verifiziert (5s anwesend)."));
      break;
    }
    delay(100);
  }

  if (userVerified)
  {
    // WARTEPHASE: Warten bis der Nutzer geht
    while (readDistance() < activationThresh)
    {
      mqttClient.loop();
      serviceOta();
      processRemoteFlushRequest();
      if (remoteFlushRequested)
      {
        break;
      }

      delay(500); // Passives Warten waehrend der Nutzung
    }

    performFlush();
  }
  else if (remoteFlushRequested)
  {
    performFlush();
  }
}

void processRemoteFlushRequest()
{
}

void performFlush()
{
  remoteFlushRequested = false;

  // SPUELPHASE: Aktion ausloesen
  Serial.println(F("Spuelung startet..."));
  digitalWrite(SOLENOID_PIN, HIGH);
  digitalWrite(LED_PIN, LOW); // LED an waehrend Spuelung
  unsigned long flushStart = millis();
  while (millis() - flushStart < static_cast<unsigned long>(flushDuration))
  {
    mqttClient.loop();
    serviceOta();
    delay(20);
  }
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(LED_PIN, HIGH); // LED aus nach Spuelung
  Serial.println(F("Spuelung beendet."));
  Serial.println(F("Warte auf naechsten Nutzer..."));
}

float readDistance()
{
  uint16_t distance = sensor.readRangeSingleMillimeters();
  if (!sensor.timeoutOccurred())
  {
    return distance;
  }
  return 2000.0;
}