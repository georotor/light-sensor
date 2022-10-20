#include <EEPROM.h>
#include <BH1750.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

struct Config {
  char mqtt_server[40] = "mqtt.lan";
  char mqtt_port[6] = "1883";
} config;

bool shouldSaveConfig = false;
unsigned long lastPub = 0;

BH1750 lightMeter;
WiFiClient wifiClient;
PubSubClient pubSubClient(wifiClient);

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

  WiFiManager wifiManager;
  // reset settings - for testing
  // wifiManager.resetSettings();
  // wifiManager.setConnectTimeout(180);
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

void initBH1750FVI() {
  Wire.begin();
  lightMeter.begin();
  Serial.println("BH1750 Test begin");
}

void bh1750LightLevel() {
  unsigned long pub = millis();
  if ((pub - lastPub) < 1000*60 and pub > lastPub and lastPub != 0)
    return;

  float lux = lightMeter.readLightLevel();
  Serial.printf("Light level: %f\n", lux);

  char lux_str[20];
  sprintf(lux_str, "%f", lux);
  pubSubClient.publish("bh1750fvi/lightlevel", lux_str);
  lastPub = pub;
}

void setup() {
  Serial.begin(9600);

  initEEPROM();
  runWiFi();
  if (MDNS.begin("bh1750fvi")) Serial.println("MDNS responder started");
  initMQTT();
  initBH1750FVI();
}

void loop() {
  while (!pubSubClient.connected())
    connectMQTT();

  bh1750LightLevel();

  pubSubClient.loop();
  MDNS.update();
}
