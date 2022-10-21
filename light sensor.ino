#include <EEPROM.h>
#include <BH1750.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

struct Config {
  char mqtt_server[40] = "mqtt.lan";
  char mqtt_port[6] = "1883";
} config;

bool shouldSaveConfig = false;
unsigned long lastReadTime = 0;
unsigned long lastPubTime = 0;
float lux;

BH1750 lightMeter;
WiFiClient wifiClient;
WiFiManager wifiManager;
PubSubClient pubSubClient(wifiClient);
ESP8266WebServer httpServer(80);

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void initEEPROM() {
  EEPROM.begin(128);
  if (EEPROM.read(0) != 1) {  // first boot
    EEPROM.write(0, 1);
    
    EEPROM.put(1, config);
    EEPROM.commit();
  }
  EEPROM.get(1, config);
}

void runWiFi() {
  WiFiManagerParameter custom_mqtt_server("server", "Server mqtt", config.mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Port mqtt", config.mqtt_port, 6);

  // reset settings - for testing
  // wifiManager.resetSettings();
  wifiManager.setConnectTimeout(180);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  std::vector<const char *> menu = {"wifi","param","sep","restart","exit"};
  wifiManager.setMenu(menu);

  if (!wifiManager.autoConnect("BH1750FVI")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  if (shouldSaveConfig) {
    strcpy(config.mqtt_server, custom_mqtt_server.getValue());
    strcpy(config.mqtt_port, custom_mqtt_port.getValue());
    EEPROM.put(1, config);
    EEPROM.commit();
    Serial.println("Save config");
  }
}

void initMQTT() {
  pubSubClient.setServer(config.mqtt_server, atoi(config.mqtt_port));
  
  while (!pubSubClient.connected())
    connectMQTT();
}

void connectMQTT() {
  Serial.println("Connecting to MQTT...");
  if (pubSubClient.connect("BH1750FVI")) {
    Serial.println("connected");
  } else {
    Serial.printf("%s: failed with state %d\n", config.mqtt_server, pubSubClient.state());
    delay(2000);
  }
}

void initHttpServer() {
  httpServer.on("/", []() {
    httpServer.send(200, "text/plain", String(lux, 2));
  });

  httpServer.on("/json", []() {
    char buff[50];
    sprintf(buff, "{\"lightLevel\": %0.2f}\n", lux);
    httpServer.send(200, "text/plain", buff);
  });

  httpServer.on("/esp/reboot", []() {
    if (!httpServer.authenticate(wifiManager.getWiFiSSID().c_str(), wifiManager.getWiFiPass().c_str()))
      return httpServer.requestAuthentication();

    httpServer.send(200, "text/plain", "Reboot...");
    delay(1000);
    ESP.restart();
  });

  httpServer.on("/esp/reset", []() {
    if (!httpServer.authenticate(wifiManager.getWiFiSSID().c_str(), wifiManager.getWiFiPass().c_str()))
      return httpServer.requestAuthentication();

    httpServer.send(200, "text/plain", "Reset settings and reboot...");
    delay(1000);
    wifiManager.resetSettings();    
    ESP.restart();
  });

  httpServer.begin();
}

void initBH1750FVI() {
  Wire.begin();
  lightMeter.begin();
  Serial.println("BH1750 Start begin");
}

void bh1750LightLevelGet() {
  unsigned long readTime = millis();
  if ((readTime - lastReadTime) < 2000 and readTime > lastReadTime and lastReadTime != 0)
    return;

  lux = lightMeter.readLightLevel();
  lastReadTime = lastReadTime;
  
  bh1750LightLevelSend();
}

void bh1750LightLevelSend() {
  unsigned long pubTime = millis();
  if ((pubTime - lastPubTime) < 1000*60 and pubTime > lastPubTime and lastPubTime != 0)
    return;

  char buff[20];
  sprintf(buff, "%.2f", lux);
  Serial.printf("Light level: %s\n", buff);
  
  pubSubClient.publish("bh1750fvi/lightlevel", buff);
  lastPubTime = pubTime;
}

void setup() {
  Serial.begin(9600);

  initEEPROM();
  runWiFi();
  if (MDNS.begin("bh1750fvi")) Serial.println("MDNS responder started");
  initHttpServer();
  initMQTT();
  initBH1750FVI();
}

void loop() {
  while (!pubSubClient.connected())
    connectMQTT();

  bh1750LightLevelGet();

  pubSubClient.loop();
  MDNS.update();
  httpServer.handleClient();
}
