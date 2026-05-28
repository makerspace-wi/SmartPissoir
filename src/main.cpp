#include <Arduino.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ElegantOTA.h>

// Rev. 2024-06-01: Initiale Version
// VL53L0X test via I2C (Pololu library)
// Wiring (ESP8266):
// TOF SDA -> D2 (GPIO4)
// TOF SCL -> D1 (GPIO5)
// GND    -> GND
// VCC    -> 3.3V or 5V (sensor dependent)


// Pin-Definitionen für ESP8266
#define L9110_IN1 12     // D6: L9110S IN1
#define L9110_IN2 13     // D7: L9110S IN2

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
bool parseEnableValue(const byte* payload, unsigned int length, bool& enabledOut);
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
void sendValveOffSequenceOnBoot();
void valveOpen();
void valveClose();

// Parameter (anpassbar)
    int activationThresh = 300; // x mm Aktivierungsabstand [2]
    int minPresenceTime = 5000; // 5 Sek. Mindestanwesenheit [1]
    int flushDuration = 10000;  // 10 Sek. Spüldauer [3]

  char mqttBroker[40] = "192.168.0.5";
  uint16_t mqttPort = 1883;
  const char MQTT_CLIENT_ID[] = "smartpissoir-esp8266";
  const char MQTT_TOPIC_STATE[] = "smartpissoir/config/state";
  const char MQTT_SET_TOPIC_ACTIVATION[] = "smartpissoir/config/set/activationThresh";
  const char MQTT_SET_TOPIC_PRESENCE[] = "smartpissoir/config/set/minPresenceTime";
  const char MQTT_SET_TOPIC_FLUSH[] = "smartpissoir/config/set/flushDuration";
  const char MQTT_SET_TOPIC_ENABLED[] = "smartpissoir/config/set/enabled";
  const char MQTT_COMMAND_TOPIC_FLUSH[] = "smartpissoir/command/flush";
  const char MQTT_TOPIC_LWT[] = "smartpissoir/status/online";
  const char MQTT_TOPIC_ENABLED_STATUS[] = "smartpissoir/status/enabled";
  const char MQTT_TOPIC_FLUSHING[] = "smartpissoir/status/flushing";

  unsigned long lastMqttReconnectAttempt = 0;
  bool shouldSaveMqttConfig = false;
  bool remoteFlushRequested = false;
  bool systemEnabled = true;

    void setup()
    {
      Serial.begin(115200);
      pinMode(L9110_IN1, OUTPUT);
      pinMode(L9110_IN2, OUTPUT);
      digitalWrite(L9110_IN1, LOW);
      digitalWrite(L9110_IN2, LOW);
      sendValveOffSequenceOnBoot();
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
    mqttClient.subscribe(MQTT_SET_TOPIC_ENABLED);
    mqttClient.subscribe(MQTT_COMMAND_TOPIC_FLUSH);
    mqttClient.publish(MQTT_TOPIC_ENABLED_STATUS, systemEnabled ? "enabled" : "disabled", true);
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
  String enabled = f.readStringUntil('\n');
  enabled.trim();
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
  if (enabled.equalsIgnoreCase("enabled") || enabled == "1" || enabled.equalsIgnoreCase("true") || enabled.equalsIgnoreCase("on"))
  {
    systemEnabled = true;
  }
  else if (enabled.equalsIgnoreCase("disabled") || enabled == "0" || enabled.equalsIgnoreCase("false") || enabled.equalsIgnoreCase("off"))
  {
    systemEnabled = false;
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
  f.println(systemEnabled ? "enabled" : "disabled");
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
    "{\"activationThresh\":%d,\"minPresenceTime\":%d,\"flushDuration\":%d,\"enabled\":%s,\"mqttBroker\":\"%s\",\"mqttPort\":%u,\"ip\":\"%s\"}",
    activationThresh,
    minPresenceTime,
    flushDuration,
    systemEnabled ? "true" : "false",
    mqttBroker,
    mqttPort,
    ipStr.c_str());

  mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
  mqttClient.publish(MQTT_TOPIC_ENABLED_STATUS, systemEnabled ? "enabled" : "disabled", true);
  Serial.println(F("MQTT Config State publiziert."));
}

void onMqttMessage(char* topic, byte* payload, unsigned int length)
{
  if (strcmp(topic, MQTT_COMMAND_TOPIC_FLUSH) == 0)
  {
    if (!systemEnabled)
    {
      Serial.println(F("MQTT Fernspuelung ignoriert (System deaktiviert)."));
      return;
    }
    remoteFlushRequested = true;
    Serial.println(F("MQTT Fernspuelung angefordert."));
    return;
  }

  if (strcmp(topic, MQTT_SET_TOPIC_ENABLED) == 0)
  {
    bool newEnabled = true;
    if (!parseEnableValue(payload, length, newEnabled))
    {
      Serial.println(F("MQTT Payload ungueltig fuer enabled (nutze enabled/disabled, on/off, true/false, 1/0)."));
      return;
    }

    if (systemEnabled != newEnabled)
    {
      systemEnabled = newEnabled;
      if (!systemEnabled)
      {
        remoteFlushRequested = false;
      }
      Serial.print(F("Systemstatus gesetzt auf: "));
      Serial.println(systemEnabled ? F("enabled") : F("disabled"));
      saveRuntimeConfig();
    }
    publishConfigState();
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

bool parseEnableValue(const byte* payload, unsigned int length, bool& enabledOut)
{
  if (length == 0 || length > 16)
  {
    return false;
  }

  char buf[17];
  for (unsigned int i = 0; i < length; i++)
  {
    buf[i] = static_cast<char>(payload[i]);
  }
  buf[length] = '\0';

  String value = String(buf);
  value.trim();
  value.toLowerCase();

  if (value == "1" || value == "true" || value == "on" || value == "enable" || value == "enabled")
  {
    enabledOut = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "off" || value == "disable" || value == "disabled")
  {
    enabledOut = false;
    return true;
  }

  return false;
}

void runCycle()
{
  if (!systemEnabled)
  {
    remoteFlushRequested = false;
    return;
  }

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

void sendValveOffSequenceOnBoot()
{
  // Force a deterministic OFF state at boot.
  digitalWrite(L9110_IN1, LOW);
  digitalWrite(L9110_IN2, LOW);
  delay(50);

  valveClose();
  delay(80);
  valveClose();

  digitalWrite(L9110_IN1, LOW);
  digitalWrite(L9110_IN2, LOW);
  Serial.println(F("Boot OFF-Sequenz gesendet."));
}

void valveOpen() {
  // 100ms Puls: Ventil öffnen
  digitalWrite(L9110_IN1, HIGH);
  digitalWrite(L9110_IN2, LOW);
  delay(100);
  digitalWrite(L9110_IN1, LOW);
  digitalWrite(L9110_IN2, LOW);
}

void valveClose() {
  // 100ms Puls: Ventil schließen
  digitalWrite(L9110_IN1, LOW);
  digitalWrite(L9110_IN2, HIGH);
  delay(100);
  digitalWrite(L9110_IN1, LOW);
  digitalWrite(L9110_IN2, LOW);
}

void performFlush()
{
  if (!systemEnabled)
  {
    remoteFlushRequested = false;
    return;
  }

  remoteFlushRequested = false;

  // SPUELPHASE: Aktion ausloesen
  Serial.println(F("Spuelung startet..."));
  mqttClient.publish(MQTT_TOPIC_FLUSHING, "true", true);
  valveOpen();
  unsigned long flushStart = millis();
  while (millis() - flushStart < static_cast<unsigned long>(flushDuration))
  {
    mqttClient.loop();
    serviceOta();
    delay(20);
  }
  valveClose();
  mqttClient.publish(MQTT_TOPIC_FLUSHING, "false", true);
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