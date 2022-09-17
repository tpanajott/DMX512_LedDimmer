#include <pins.h>
#include <Arduino.h>
#include <DebugLog.h>
#include <ArduinoOTA.h>
#include <ESPDMX.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
// #include <DimmerButton.h>
#include <LightManager.h>
#include <HAMqtt.h>

struct structConfig {
  char wifi_hostname[64];
  char wifi_ssid[64];
  char wifi_psk[64];
  unsigned int wifi_retry_timeout_ms;
  unsigned int home_assistant_online_wait_period_ms;
  char mqtt_server[64];
  int mqtt_port;
  char mqtt_base_topic[64];
  bool mqtt_auth;
  char mqtt_username[64];
  char mqtt_psk[64];

  char button1_name[64];
  char button2_name[64];
  char button3_name[64];
  char button4_name[64];
  short button1_min;
  short button2_min;
  short button3_min;
  short button4_min;
  short button1_max;
  short button2_max;
  short button3_max;
  short button4_max;
  short button1_dimmingSpeed;
  short button2_dimmingSpeed;
  short button3_dimmingSpeed;
  short button4_dimmingSpeed;
  short button1_autoDimmingSpeed;
  short button2_autoDimmingSpeed;
  short button3_autoDimmingSpeed;
  short button4_autoDimmingSpeed;
  short button1_holdPeriod;
  short button2_holdPeriod;
  short button3_holdPeriod;
  short button4_holdPeriod;
  short button1_minPressMillis;
  short button2_minPressMillis;
  short button3_minPressMillis;
  short button4_minPressMillis;
  short button1_maxPressMillis;
  short button2_maxPressMillis;
  short button3_maxPressMillis;
  short button4_maxPressMillis;
  short button1_channel;
  short button2_channel;
  short button3_channel;
  short button4_channel;
  bool button1_enabled;
  bool button2_enabled;
  bool button3_enabled;
  bool button4_enabled;
};
// Configuration object to easily access all configuration values
structConfig config;
// Wifi variables
unsigned long lastWiFiCheckMillis = 0;
bool lastWiFiCheckStatus = false;
// MQTT variables
WiFiClient espClient;
PubSubClient pubSubClient(espClient);
HAMqtt hamqtt;
unsigned long lastMqttReconnectTry = 0;
unsigned long _homeAssistantOnlineUpdate = 0;
bool _triggerHomeAssistantOnlineUpdate = false;
bool _homeAssistantAvailableOnMQTT = true; // Home assistant do not persist status so we have to
                                           // assume it is online when we connect.
// Web server variables
AsyncWebServer server(80);
AsyncWebSocket indexDataSocket("/index_data");
LightManager lMan;
// Misc variables
bool doReboot = false;
unsigned long doRebootAt = 0;
DMXESPSerial dmx;
// Firmware Update variables
bool hasFirmwareUpdated = false;
bool hasLITTLEFSUpdated = false;
String config_file_data = ""; // Used to store config file data while updating LittleFS

void setupWiFi() {
  // If a configuration exists, continue to connect to configured WiFi
  if(!String(config.wifi_ssid).isEmpty()) {
    Serial.printf("[WIFI] Configuration exists. Trying to connect to WiFi %s\n", config.wifi_ssid);
    WiFi.hostname(config.wifi_hostname);
    WiFi.begin(config.wifi_ssid, config.wifi_psk);
    WiFi.setAutoConnect(true);
    WiFi.softAPdisconnect(true);
  } else {
    Serial.println("[WIFI] No configuration exists! Starting configuration AP.");
    IPAddress local_ip(192,168,1,1);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);

    if(WiFi.softAPConfig(local_ip, gateway, subnet)) {
      Serial.println("[WIFI] Soft-AP configuration applied.");
      if(WiFi.softAP("Light Controller", "password")) {
        Serial.println("[WIFI] Soft-AP started.");

        Serial.println("[WIFI] SSID: Light Controller");
        Serial.println("[WIFI] PSK : password");
        Serial.printf("[WIFI] IP Address: %s\n", WiFi.softAPIP().toString().c_str());
      } else {
        Serial.println("[WIFI] Failed to start Soft-AP!");
      }
    } else {
      Serial.println("[WIFI] Failed to apply Soft-AP configuration!");
    }
  }
}

// Task to check and report WiFi status
void taskWiFiManager() {
  // Only check WiFi state according to paramaters
  if(millis() - lastWiFiCheckMillis >= config.wifi_retry_timeout_ms) {
    if(strcmp(config.wifi_ssid, "") != 0) { // Only check WiFi if an SSID is configured
      if(WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WIFI] Trying to connect to WiFi %s.\n", config.wifi_ssid);
      } else if(WiFi.status() == WL_CONNECTED && !lastWiFiCheckStatus) {
        Serial.printf("[WIFI] Connected to %s as %s.\n", config.wifi_ssid, config.wifi_hostname);
        Serial.printf("[WIFI] IP Address : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("[WIFI] MAC Address: %s\n", WiFi.macAddress().c_str());
        Serial.printf("[WIFI] Gateway    : %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("[WIFI] DNS        : %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("[WIFI] RSSI       : %i\n", WiFi.RSSI());
        Serial.printf("[WIFI] SSID       : %s\n", WiFi.SSID().c_str());
      }
      lastWiFiCheckMillis = millis();
      lastWiFiCheckStatus = (WiFi.status() == WL_CONNECTED);
    }
  }
}

bool initLittleFS() {
  Serial.println("[CONFIG] Initializing LittleFS.");
  if(!LittleFS.begin()){
    Serial.println("[CONFIG] LITTLEFS Mount Failed. Device needs to be re-flashed.");
    return false;
  }
  Serial.println("[CONFIG] LittleFS initialized without error.");
  return true;
}

// A factory default is simply done by removing the config file. Next time default values will be loaded.
bool doFactoryReset() {
  if(LittleFS.exists("/config.json")) {
    Serial.println("[INFO] Device reset requested.");
    return LittleFS.remove("/config.json"); // Remove the config file in order to reset the device to defaults
  } else {
    Serial.println("[INFO] Tried to do a factory reset when no file exists. Returning success.");
    return true;
  }
}

bool loadConfig() {
  Serial.println("[CONFIG] Loading configuration.");
  // Read config file into JSON object
  File config_file = LittleFS.open("/config.json", "r");
  StaticJsonDocument<2048> doc;

  if(config_file) {
    // Read file into JSON object
    DeserializationError error = deserializeJson(doc, config_file);
    if (error) {
      Serial.println("[CONFIG] Failed to deserialize config file! Loading default values.");
      config_file.close();
    }
    config_file.close();
  } else {
    Serial.println("[CONFIG] Failed to open 'config.json' for reading! Loading default values.");
  }

  // WiFi values
  strlcpy(config.wifi_hostname, doc["wifi_hostname"] | "", sizeof(config.wifi_hostname));
  strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
  strlcpy(config.wifi_psk, doc["wifi_psk"] | "", sizeof(config.wifi_psk));
  config.wifi_retry_timeout_ms = doc["wifi_retry_timeout_ms"] | 15000;

  // MQTT Values
  strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
  strlcpy(config.mqtt_base_topic, doc["mqtt_base_topic"] | "homeassistant/", sizeof(config.mqtt_base_topic));
  strlcpy(config.mqtt_username, doc["mqtt_username"] | "", sizeof(config.mqtt_username));
  strlcpy(config.mqtt_psk, doc["mqtt_psk"] | "", sizeof(config.mqtt_psk));
  config.mqtt_auth = doc["mqtt_auth"] | false;
  config.mqtt_port = doc["mqtt_port"] | 1883;

  // Button values
  strlcpy(config.button1_name, doc["button1_name"] | "button1", sizeof(config.button1_name));
  strlcpy(config.button2_name, doc["button2_name"] | "button2", sizeof(config.button2_name));
  strlcpy(config.button3_name, doc["button3_name"] | "button3", sizeof(config.button3_name));
  strlcpy(config.button4_name, doc["button4_name"] | "button4", sizeof(config.button4_name));
  config.button1_min = doc["button1_min"] | 1;
  config.button2_min = doc["button2_min"] | 1;
  config.button3_min = doc["button3_min"] | 1;
  config.button4_min = doc["button4_min"] | 1;
  config.button1_max = doc["button1_max"] | 255;
  config.button2_max = doc["button2_max"] | 255;
  config.button3_max = doc["button3_max"] | 255;
  config.button4_max = doc["button4_max"] | 255;
  config.button1_dimmingSpeed = doc["button1_dimmingSpeed"] | 15;
  config.button2_dimmingSpeed = doc["button2_dimmingSpeed"] | 15;
  config.button3_dimmingSpeed = doc["button3_dimmingSpeed"] | 15;
  config.button4_dimmingSpeed = doc["button4_dimmingSpeed"] | 15;
  config.button1_autoDimmingSpeed = doc["button1_autoDimmingSpeed"] | 1;
  config.button2_autoDimmingSpeed = doc["button2_autoDimmingSpeed"] | 1;
  config.button3_autoDimmingSpeed = doc["button3_autoDimmingSpeed"] | 1;
  config.button4_autoDimmingSpeed = doc["button4_autoDimmingSpeed"] | 1;
  config.button1_holdPeriod = doc["button1_holdPeriod"] | 800;
  config.button2_holdPeriod = doc["button2_holdPeriod"] | 800;
  config.button3_holdPeriod = doc["button3_holdPeriod"] | 800;
  config.button4_holdPeriod = doc["button4_holdPeriod"] | 800;
  config.button1_minPressMillis = doc["button1_minPressMillis"] | 80;
  config.button2_minPressMillis = doc["button2_minPressMillis"] | 80;
  config.button3_minPressMillis = doc["button3_minPressMillis"] | 80;
  config.button4_minPressMillis = doc["button4_minPressMillis"] | 80;
  config.button1_maxPressMillis = doc["button1_maxPressMillis"] | 800;
  config.button2_maxPressMillis = doc["button2_maxPressMillis"] | 800;
  config.button3_maxPressMillis = doc["button3_maxPressMillis"] | 800;
  config.button4_maxPressMillis = doc["button4_maxPressMillis"] | 800;
  config.button1_channel = doc["button1_channel"] | 1;
  config.button2_channel = doc["button2_channel"] | 2;
  config.button3_channel = doc["button3_channel"] | 3;
  config.button4_channel = doc["button4_channel"] | 4;
  config.button1_enabled = doc["button1_enabled"] | false;
  config.button2_enabled = doc["button2_enabled"] | false;
  config.button3_enabled = doc["button3_enabled"] | false;
  config.button4_enabled = doc["button4_enabled"] | false;

  // Misc values
  config.home_assistant_online_wait_period_ms = doc["home_assistant_online_wait_period_ms"] | 30000;

  Serial.println("[CONFIG] Configuration loaded.");
  return true;
}

void saveConfigFromWeb(AsyncWebServerRequest *request) {
  File config_file = LittleFS.open("/config.json", "w");
  if(!config_file) {
    Serial.println("[CONFIG] Failed to open 'config.json' for writing.");
  }

  StaticJsonDocument<2048> json;
  json["home_assistant_online_wait_period_ms"] = request->arg("home_assistant_online_wait_period_ms").toInt();
  // WiFi values
  json["wifi_hostname"] = request->arg("wifi_hostname");
  json["wifi_ssid"] = request->arg("wifi_ssid");
  json["wifi_psk"] = request->arg("wifi_psk");
  json["wifi_retry_timeout_ms"] = request->arg("wifi_retry_timeout_ms").toInt();
  // MQTT Values
  json["mqtt_server"] = request->arg("mqtt_server");
  json["mqtt_port"] = request->arg("mqtt_port").toInt();
  json["mqtt_auth"] = request->hasArg("mqtt_auth");
  json["mqtt_username"] = request->arg("mqtt_username");
  json["mqtt_psk"] = request->arg("mqtt_psk");
  
  if(!request->arg("mqtt_base_topic").endsWith("/")) {
    String mqtt_base_topic = request->arg("mqtt_base_topic");
    mqtt_base_topic.concat("/");
    json["mqtt_base_topic"] = mqtt_base_topic;
  } else {
    json["mqtt_base_topic"] = request->arg("mqtt_base_topic");
  }
  // Button states
  json["button1_name"] = request->arg("button1_name").c_str();
  json["button2_name"] = request->arg("button2_name").c_str();
  json["button3_name"] = request->arg("button3_name").c_str();
  json["button4_name"] = request->arg("button4_name").c_str();
  json["button1_min"] = request->arg("button1_min").toInt();
  json["button2_min"] = request->arg("button2_min").toInt();
  json["button3_min"] = request->arg("button3_min").toInt();
  json["button4_min"] = request->arg("button4_min").toInt();
  json["button1_max"] = request->arg("button1_max").toInt();
  json["button2_max"] = request->arg("button2_max").toInt();
  json["button3_max"] = request->arg("button3_max").toInt();
  json["button4_max"] = request->arg("button4_max").toInt();
  json["button1_dimmingSpeed"] = request->arg("button1_dimmingSpeed").toInt();
  json["button2_dimmingSpeed"] = request->arg("button2_dimmingSpeed").toInt();
  json["button3_dimmingSpeed"] = request->arg("button3_dimmingSpeed").toInt();
  json["button4_dimmingSpeed"] = request->arg("button4_dimmingSpeed").toInt();
  json["button1_autoDimmingSpeed"] = request->arg("button1_autoDimmingSpeed").toInt();
  json["button2_autoDimmingSpeed"] = request->arg("button2_autoDimmingSpeed").toInt();
  json["button3_autoDimmingSpeed"] = request->arg("button3_autoDimmingSpeed").toInt();
  json["button4_autoDimmingSpeed"] = request->arg("button4_autoDimmingSpeed").toInt();
  json["button1_holdPeriod"] = request->arg("button1_holdPeriod").toInt();
  json["button2_holdPeriod"] = request->arg("button2_holdPeriod").toInt();
  json["button3_holdPeriod"] = request->arg("button3_holdPeriod").toInt();
  json["button4_holdPeriod"] = request->arg("button4_holdPeriod").toInt();
  json["button1_minPressMillis"] = request->arg("button1_minPressMillis").toInt();
  json["button2_minPressMillis"] = request->arg("button2_minPressMillis").toInt();
  json["button3_minPressMillis"] = request->arg("button3_minPressMillis").toInt();
  json["button4_minPressMillis"] = request->arg("button4_minPressMillis").toInt();
  json["button1_maxPressMillis"] = request->arg("button1_maxPressMillis").toInt();
  json["button2_maxPressMillis"] = request->arg("button2_maxPressMillis").toInt();
  json["button3_maxPressMillis"] = request->arg("button3_maxPressMillis").toInt();
  json["button4_maxPressMillis"] = request->arg("button4_maxPressMillis").toInt();
  json["button1_channel"] = request->arg("button1_channel").toInt();
  json["button2_channel"] = request->arg("button2_channel").toInt();
  json["button3_channel"] = request->arg("button3_channel").toInt();
  json["button4_channel"] = request->arg("button4_channel").toInt();
  json["button1_enabled"] = request->hasArg("button1_enabled");
  json["button2_enabled"] = request->hasArg("button2_enabled");
  json["button3_enabled"] = request->hasArg("button3_enabled");
  json["button4_enabled"] = request->hasArg("button4_enabled");

  if(serializeJson(json, config_file) == 0) {
    Serial.println("[CONFIG] Failed to save config file.");
  } else {
    Serial.println("[CONFIG] Saved config file.");
  }
  config_file.close();
  request->redirect("/reboot");
}

void sendBaseData(AsyncWebSocketClient * client) {
  // A client just connected. Curate all data into a single JSON response
  StaticJsonDocument<1024> json;
  json["home_assistant_online_wait_period_ms"] = config.home_assistant_online_wait_period_ms;
  json["home_assistant_status"] = _homeAssistantAvailableOnMQTT ? "Connected" : "DISCONNECTED";
  // WiFi values
  json["wifi_hostname"] = config.wifi_hostname;
  json["wifi_ssid"] = config.wifi_ssid;
  json["wifi_psk"] = config.wifi_psk;
  json["wifi_retry_timeout_ms"] = config.wifi_retry_timeout_ms;
  // MQTT Values
  json["mqtt_server"] = config.mqtt_server;
  json["mqtt_port"] = config.mqtt_port;
  json["mqtt_auth"] = config.mqtt_auth;
  json["mqtt_username"] = config.mqtt_username;
  json["mqtt_psk"] = config.mqtt_psk;
  json["mqtt_base_topic"] = config.mqtt_base_topic;
  json["mqtt_status"] = pubSubClient.connected() ? "Connected" : "DISCONNECTED";

  char json_data[1024];
  serializeJsonPretty(json, json_data);
  client->printf(json_data);
}

void sendButtonData(AsyncWebSocketClient * client) {
  // A client just connected. Curate all data into a single JSON response
  StaticJsonDocument<1024> json;
  json["button1_output"] = lMan.dimmerButtons[0].outputState ? String(lMan.dimmerButtons[0].dimLevel) : (lMan.dimmerButtons[0].enabled ? "Off" : "DISABLED");
  json["button2_output"] = lMan.dimmerButtons[1].outputState ? String(lMan.dimmerButtons[1].dimLevel) : (lMan.dimmerButtons[1].enabled ? "Off" : "DISABLED");
  json["button3_output"] = lMan.dimmerButtons[2].outputState ? String(lMan.dimmerButtons[2].dimLevel) : (lMan.dimmerButtons[2].enabled ? "Off" : "DISABLED");
  json["button4_output"] = lMan.dimmerButtons[3].outputState ? String(lMan.dimmerButtons[3].dimLevel) : (lMan.dimmerButtons[3].enabled ? "Off" : "DISABLED");
  json["button1_name"] = config.button1_name;
  json["button2_name"] = config.button2_name;
  json["button3_name"] = config.button3_name;
  json["button4_name"] = config.button4_name;
  json["button1_min"] = config.button1_min;
  json["button2_min"] = config.button2_min;
  json["button3_min"] = config.button3_min;
  json["button4_min"] = config.button4_min;
  json["button1_max"] = config.button1_max;
  json["button2_max"] = config.button2_max;
  json["button3_max"] = config.button3_max;
  json["button4_max"] = config.button4_max;
  json["button1_channel"] = config.button1_channel;
  json["button2_channel"] = config.button2_channel;
  json["button3_channel"] = config.button3_channel;
  json["button4_channel"] = config.button4_channel;
  json["button1_enabled"] = config.button1_enabled;
  json["button2_enabled"] = config.button2_enabled;
  json["button3_enabled"] = config.button3_enabled;
  json["button4_enabled"] = config.button4_enabled;

  // Send first part
  char json_data[1024];
  serializeJsonPretty(json, json_data);
  client->printf(json_data);

  json.clear();
  json["button1_dimmingSpeed"] = config.button1_dimmingSpeed;
  json["button2_dimmingSpeed"] = config.button2_dimmingSpeed;
  json["button3_dimmingSpeed"] = config.button3_dimmingSpeed;
  json["button4_dimmingSpeed"] = config.button4_dimmingSpeed;
  json["button1_autoDimmingSpeed"] = config.button1_autoDimmingSpeed;
  json["button2_autoDimmingSpeed"] = config.button2_autoDimmingSpeed;
  json["button3_autoDimmingSpeed"] = config.button3_autoDimmingSpeed;
  json["button4_autoDimmingSpeed"] = config.button4_autoDimmingSpeed;
  json["button1_holdPeriod"] = config.button1_holdPeriod;
  json["button2_holdPeriod"] = config.button2_holdPeriod;
  json["button3_holdPeriod"] = config.button3_holdPeriod;
  json["button4_holdPeriod"] = config.button4_holdPeriod;
  json["button1_minPressMillis"] = config.button1_minPressMillis;
  json["button2_minPressMillis"] = config.button2_minPressMillis;
  json["button3_minPressMillis"] = config.button3_minPressMillis;
  json["button4_minPressMillis"] = config.button4_minPressMillis;
  json["button1_maxPressMillis"] = config.button1_maxPressMillis;
  json["button2_maxPressMillis"] = config.button2_maxPressMillis;
  json["button3_maxPressMillis"] = config.button3_maxPressMillis;
  json["button4_maxPressMillis"] = config.button4_maxPressMillis;

  serializeJsonPretty(json, json_data);
  client->printf(json_data);
}

// Handle data to and from index page via websockets
void indexDataEventHandler(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    sendBaseData(client);
    sendButtonData(client);
  } else if(type == WS_EVT_ERROR){
    //error was received from the other end
    Serial.printf("[INDEX_DATA_SOCKET] ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_DATA){
    //data packet
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      // Serial.printf("[INDEX_DATA_SOCKET] ws[%s][%u] %s-message[%llu]\n", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
      if(info->opcode == WS_TEXT){
        data[len] = 0;
        // String message = String((char*)data);
        // Serial.printf("%s\n", message.c_str());
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, (char*)data);
        if(error) {
          Serial.print(F("[INDEX_DATA_SOCKET] deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
        String buttonId = doc["id"];
        int dimmingTarget = doc["value"];

        if(buttonId.startsWith("button1")) {
          if(dimmingTarget > 0) {
            lMan.setOutputState(0, true);
            lMan.setAutoDimmingTarget(0, dimmingTarget);
          } else {
            lMan.setOutputState(0, false);
          }
        } else if(buttonId.startsWith("button2")) {
          if(dimmingTarget > 0) {
            lMan.setOutputState(1, true);
            lMan.setAutoDimmingTarget(1, dimmingTarget);
          } else {
            lMan.setOutputState(1, false);
          }
        } else if(buttonId.startsWith("button3")) {
          if(dimmingTarget > 0) {
            lMan.setOutputState(2, true);
            lMan.setAutoDimmingTarget(2, dimmingTarget);
          } else {
            lMan.setOutputState(2, false);
          }
        } else if(buttonId.startsWith("button4")) {
          if(dimmingTarget > 0) {
            lMan.setOutputState(3, true);
            lMan.setAutoDimmingTarget(3, dimmingTarget);
          } else {
            lMan.setOutputState(3, false);
          }
        }

        // if(buttonId.startsWith("button1")) {
        //   if(value > 0) {
        //     dimmerButtons[0].setOutputState(true);
        //     dimmerButtons[0].setDimmingTarget(value);
        //   } else {
        //     dimmerButtons[0].setOutputState(false);
        //   }
        // } else if(buttonId.startsWith("button2")) {
        //   if(value > 0) {
        //     dimmerButtons[1].setOutputState(true);
        //     dimmerButtons[1].setDimmingTarget(value);
        //   } else {
        //     dimmerButtons[1].setOutputState(false);
        //   }
        // } else if(buttonId.startsWith("button3")) {
        //   if(value > 0) {
        //     dimmerButtons[2].setOutputState(true);
        //     dimmerButtons[2].setDimmingTarget(value);
        //   } else {
        //     dimmerButtons[2].setOutputState(false);
        //   }
        // } else if(buttonId.startsWith("button4")) {
        //   if(value > 0) {
        //     dimmerButtons[3].setOutputState(true);
        //     dimmerButtons[3].setDimmingTarget(value);
        //   } else {
        //     dimmerButtons[3].setOutputState(false);
        //   }
        // }
      }
    }
  }
}

void respondAvailableWiFiNetworks(AsyncWebServerRequest *request) {
  String json = "[";
  int n = WiFi.scanComplete();
  if(n == -2){
    WiFi.scanNetworks(true);
  } else if(n){
    for (int i = 0; i < n; ++i){
      if(!WiFi.SSID(i).isEmpty()) {
        Serial.printf("[WIFI] Found WiFi %s\n", WiFi.SSID(i).c_str());
        if(i) json += ",";
        json += "{";
        json += "\"rssi\":"+String(WiFi.RSSI(i));
        json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
        json += ",\"channel\":"+String(WiFi.channel(i));
        
        switch (WiFi.encryptionType(i))
        {
        case wl_enc_type::ENC_TYPE_AUTO:
          json += ",\"security\": \"AUTO\"";
          break;
        case wl_enc_type::ENC_TYPE_CCMP:
          json += ",\"security\": \"CCMP\"";
          break;
        case wl_enc_type::ENC_TYPE_NONE:
          json += ",\"security\": \"NONE\"";
          break;
        case wl_enc_type::ENC_TYPE_TKIP:
          json += ",\"security\": \"TKIP\"";
          break;
        case wl_enc_type::ENC_TYPE_WEP:
          json += ",\"security\": \"WEP\"";
          break;
        
        default:
          json += ",\"security\": \"UNKNOWN\"";
          break;
        }
        json += "}";
      }
    }
    WiFi.scanDelete();
    if(WiFi.scanComplete() == -2){
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  request->send(200, "application/json", json);
  json = String();
}

void performFirmwareUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if(!index){
    Serial.printf("Starting flash of file: '%s'. Length: %i\n", filename.c_str(), request->contentLength());

    Update.runAsync(true);
    // Detect update type depending on filename
    int uploadType;
    if(filename.startsWith("firmware") && filename.endsWith(".bin")) {
      uploadType = U_FLASH;
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if(!Update.begin(maxSketchSpace, uploadType)) {
        Serial.println("[UPDATE] ERROR! Could not start update, error:");
        Update.printError(Serial);
      }
    } else if (filename.startsWith("littlefs") && filename.endsWith(".bin")) {
      // An upload of LittleFS has been requested. Save the current config in a variable to later
      // write it to the new LittleFS.
      
      if(LittleFS.exists("/config.json")) {
        Serial.println("[UPDATE] Will restore config file after update.");
        File config_file = LittleFS.open("/config.json", "r");
        if(config_file) {
          while(config_file.available()) {
            config_file_data += char(config_file.read());
          }
          config_file.close();
        } else {
          Serial.println("[UPDATE] Failed to store config file in temporary variable while updating.");
        }
      }
      
      uploadType = U_FS;
      size_t fsSize = ((size_t) &_FS_end - (size_t) &_FS_start);
      if(!Update.begin(fsSize, uploadType)) {
        Serial.println("[UPDATE] ERROR! Could not start update, error:");
        Update.printError(Serial);
      }
    } else {
      request->send(500, "Unknown file type!");
      return;
    }
  }

  // If no error has occrued, continue updating
  if(!Update.hasError()){
    if(Update.write(data, len) != len){
      Serial.println("[UPDATE] Error occured: ");
      Update.printError(Serial);
    } else {
      int size = index + len;
      Serial.printf("[UPDATE] Written %i\n", size);
    }
  }

  if(final){
    Update.end(true);
    if(filename.startsWith("firmware") && filename.endsWith(".bin")) {
      hasFirmwareUpdated = true;
    } else if (filename.startsWith("littlefs") && filename.endsWith(".bin")) {
      hasLITTLEFSUpdated = true;

      // Write stored config file to LittleFS
      if(config_file_data.length() > 0) {
        File config_file = LittleFS.open("/config.json", "w");
        if(config_file) {
          Serial.println("[UPDATE] Restoring config file after LittleFS update!");
          config_file.write(config_file_data.c_str());
          config_file.close();
        } else {
          Serial.println("[UPDATE] ERROR! Could not restore config file after update.");
        }
      }
    }
    Serial.printf("[UPDATE] Update of file: %s ended\n", filename.c_str());
    if(hasFirmwareUpdated && hasLITTLEFSUpdated) {
      request->send(200, "text/plain", "OK");
    }
  }
}

void setupWebServer() {
  // All static files start with url /static and is served from the /static directory on LittleFS
  server.serveStatic("/static", LittleFS, "/static");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/reboot.html");
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/update.html");
  });

  server.on("/save_config", HTTP_POST, saveConfigFromWeb);
  server.on("/available_wifi_networks", HTTP_GET, respondAvailableWiFiNetworks);

  server.on("/connection_test", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  });

  server.on("/do_reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
    doReboot = true;
    doRebootAt = millis() + 2000; // Reboot in 2 seconds
  });

  server.on("/do_factory_reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(doFactoryReset()) {
      request->send(200, "text/plain", "OK");
    } else {
      request->send(500, "text/plain", "Failed to perform factory reset");
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "File not found!");
  });

  server.onFileUpload(performFirmwareUpdate);
  indexDataSocket.onEvent(indexDataEventHandler);
  server.addHandler(&indexDataSocket);
  // ESP Async Web Server will start its own thread to run in.
  server.begin();
}

// Send light status to MQTT for spcific light connected to buttonId
void sendMqttLightUpdate(int buttonId) {
  // Send MQTT Light Status update if light is enabled
  if(lMan.dimmerButtons[buttonId].enabled && millis() - lMan.dimmerButtons[buttonId].lastMqttUpdate >= lMan.dimmerButtons[buttonId].mqttUpdateInterval) {
    LOG_TRACE("Sending MQTT status update for dimmerButton[", buttonId, "]");
    MQTTLight sendLight;
    sendLight.name = lMan.dimmerButtons[buttonId].name;
    sendLight.state = lMan.dimmerButtons[buttonId].outputState;
    sendLight.brightness = lMan.dimmerButtons[buttonId].dimLevel;
    hamqtt.sendLightUpdate(sendLight, &pubSubClient);
    // Update when the last MQTT update was sent and remove mark for sending a new update.
    lMan.dimmerButtons[buttonId].lastMqttUpdate = millis();
    lMan.dimmerButtons[buttonId].sendMqttUpdate = false;
  }
}

// Register all enabled lights to MQTT
void registerMqttLights() {
  // // Once connected, publish an announcement...
  // // Register all enabled lights and subscribe to relevant topics
  for(int i = 0; i < 4; i++) {
    if(lMan.dimmerButtons[i].enabled) {
      Serial.printf("[MQTT] Registring light with name: %s\n", lMan.dimmerButtons[i].name);
      MQTTLight light;
      light.name = lMan.dimmerButtons[i].name;
      hamqtt.registerLight(light, &pubSubClient);
      // Mark lights due for an MQTT status update.
      lMan.dimmerButtons[i].sendMqttUpdate = true;
    }
  }
}

// Handle incomming MQTT Messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Only handle Home Assistant reconnect here. All other MQTT i handled by HAMQTT
  String status_topic = config.mqtt_base_topic;
  status_topic.concat("status");
  if(String(topic) == status_topic) {
    char data[length];
    for (unsigned int i = 0; i < length; i++) {
        data[i] = (char)payload[i];
    }
    String status_string = String(data);

    // String data_string = String(data);
    // data_string.trim();
    // Serial.printf("[MQTT] Home Assistant status update: %s\n", data_string.c_str());
    if(status_string.startsWith("online")) {
      Serial.println("[MQTT] Home Assistant reconneced to MQTT. Starting wait period before registring lights via MQTT.");
      // Signal that home assistant has come online, wait for defined time before sending update
      _homeAssistantOnlineUpdate = millis();
      _triggerHomeAssistantOnlineUpdate = true;
      _homeAssistantAvailableOnMQTT = true;
      // Tell all connected clients about the resolved error
      char json_data[64];
      sprintf(json_data, "{\"home_assistant_status\": \"Connected\"}");
      indexDataSocket.textAll(json_data);
    } else if (status_string.startsWith("offline")) {
      Serial.println("[MQTT] Home Assistant unavailable on MQTT.");
      _homeAssistantAvailableOnMQTT = false;
      // Tell all connected clients about the error
      char json_data[64];
      sprintf(json_data, "{\"home_assistant_status\": \"DISCONNECTED\"}");
      indexDataSocket.textAll(json_data);
    } else {
      Serial.printf("[MQTT] Unknown Home Assistant status: %s\n", status_string.c_str());
    }
  } else {
    hamqtt.mqttHandler(topic, payload, length);
  }
}

void mqtt_callback(MQTTLight light) {
  short buttonIndex = -1;
  if(light.name == String(config.button1_name)) {
    buttonIndex = 0;
  } else if(light.name == config.button2_name) {
    buttonIndex = 1;
  } else if(light.name == config.button1_name) {
    buttonIndex = 2;
  } else if(light.name == config.button1_name) {
    buttonIndex = 3;
  }
  if(buttonIndex >= 0) {
    if(light.brightness > 0) {
      lMan.setAutoDimmingTarget(buttonIndex, light.brightness);
      lMan.setOutputState(buttonIndex, light.state);
    } else {
      lMan.setOutputState(buttonIndex, light.state);
    }
  } else {
    LOG_ERROR("Unknown button", light.name.c_str());
  }
}

void setupMQTT() {
  if(strcmp(config.mqtt_server, "") != 0) {
    Serial.printf("[MQTT] Configuration exists. Trying to connect to MQTT server %s\n", config.mqtt_server);
    // Configure MQTT
    pubSubClient.setServer(config.mqtt_server, config.mqtt_port);
    pubSubClient.setBufferSize(2048); // Increase MQTT buffer size
    pubSubClient.setCallback(mqttCallback);
    hamqtt.begin(&mqtt_callback, config.mqtt_base_topic, config.wifi_hostname);
  } else {
    Serial.println("[MQTT] No configuration for MQTT exists.");
  }
}

// Task to handle all things MQTT
void taskMQTTManager() {
  if(!pubSubClient.connected() && WiFi.status() == WL_CONNECTED) {
    if(millis() - lastMqttReconnectTry >= 5000) {
      char json_data[64];
      sprintf(json_data, "{\"mqtt_status\": \"DISCONNECTED\"}");
      indexDataSocket.textAll(json_data);
      
      Serial.println("[MQTT] Trying to connect to MQTT...");
      // Last will and testament configuration for availability detection in Home Assistant
      String availability_topic = String(config.mqtt_base_topic);
      availability_topic.concat("light/");
      availability_topic.concat(config.wifi_hostname);
      availability_topic.concat("/availability");
      bool connectResult = false;
      if(config.mqtt_auth) {
        connectResult = pubSubClient.connect(config.wifi_hostname, config.mqtt_username, config.mqtt_psk, availability_topic.c_str(), 0, false, "offline");
      } else {
        connectResult = pubSubClient.connect(config.wifi_hostname, availability_topic.c_str(), 0, false, "offline");
      }

      if(connectResult) {
        char json_data[64];
        sprintf(json_data, "{\"mqtt_status\": \"Connected\"}");
        indexDataSocket.textAll(json_data);
        Serial.println("[MQTT] Connected");
        registerMqttLights();
        // Subscribe to home assistant status topic
        String status_topic = config.mqtt_base_topic;
        status_topic.concat("status");
        Serial.printf("[MQTT] Subscribing to home assistant status topic: %s\n", status_topic.c_str());
        pubSubClient.subscribe(status_topic.c_str());
        hamqtt.sendOnlineStatusUpdate(&pubSubClient);
      } else {
        Serial.printf("[MQTT] Failed to connect to MQTT, rc=%i\n", pubSubClient.state());
        Serial.println("[MQTT] Trying again in 5 seconds");
      }
      lastMqttReconnectTry = millis();
    }
  }
    
  // Gather all MQTT messages
  pubSubClient.loop();
}

void checkIfFactoryReset() {
  delay(25);

  if(digitalRead(FACTORY_RESET_PIN) == LOW) {
    unsigned long buttonPressedTime = millis();
    while(digitalRead(FACTORY_RESET_PIN) == LOW) {
      // If button was held for 10 seconds. Perform factory reset.
      digitalWrite(STATUS_LED_PIN, (millis() / 50) % 2 == 0);
      if(buttonPressedTime + 10000 <= millis()) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        Serial.println("[INFO] Factory reset button held for 10 seconds. Performing reset.");
        
        if(doFactoryReset()) { // Remove the config file in order to reset the device to defaults
          Serial.println("[INFO] Config file removed.");
        } else {
          Serial.println("[LittleFS] Failed to delete config file.");
        }
        delay(500);
        while(digitalRead(FACTORY_RESET_PIN) == LOW) {
          Serial.println("[INFO] Erase still LOW. Waiting for button release.");
          delay(500);
        }
        ESP.restart();
      }
      delay(25);
    }
  } else {
    Serial.println("[LittleFS] No flash erease requested.");
  }
}

void setupDimmerButtons() {
  // Start buttons
  lMan.initDimmerButton(0, BUTTON_1_PIN, config.button1_min, config.button1_max, config.button1_channel, config.button1_enabled, config.button1_name);
  lMan.dimmerButtons[0].dimmingSpeed            = config.button1_dimmingSpeed;
  lMan.dimmerButtons[0].holdPeriod              = config.button1_holdPeriod;
  lMan.dimmerButtons[0].buttonMinPressThreshold = config.button1_minPressMillis;
  lMan.dimmerButtons[0].buttonPressThreshold    = config.button1_maxPressMillis;

  lMan.initDimmerButton(1, BUTTON_2_PIN, config.button2_min, config.button2_max, config.button2_channel, config.button2_enabled, config.button2_name);
  lMan.dimmerButtons[1].dimmingSpeed            = config.button2_dimmingSpeed;
  lMan.dimmerButtons[1].holdPeriod              = config.button2_holdPeriod;
  lMan.dimmerButtons[1].buttonMinPressThreshold = config.button2_minPressMillis;
  lMan.dimmerButtons[1].buttonPressThreshold    = config.button2_maxPressMillis;

  lMan.initDimmerButton(2, BUTTON_3_PIN, config.button3_min, config.button3_max, config.button3_channel, config.button3_enabled, config.button3_name);
  lMan.dimmerButtons[2].dimmingSpeed            = config.button3_dimmingSpeed;
  lMan.dimmerButtons[2].holdPeriod              = config.button3_holdPeriod;
  lMan.dimmerButtons[2].buttonMinPressThreshold = config.button3_minPressMillis;
  lMan.dimmerButtons[2].buttonPressThreshold    = config.button3_maxPressMillis;

  lMan.initDimmerButton(3, BUTTON_4_PIN, config.button4_min, config.button4_max, config.button4_channel, config.button4_enabled, config.button4_name);
  lMan.dimmerButtons[3].dimmingSpeed            = config.button4_dimmingSpeed;
  lMan.dimmerButtons[3].holdPeriod              = config.button4_holdPeriod;
  lMan.dimmerButtons[3].buttonMinPressThreshold = config.button4_minPressMillis;
  lMan.dimmerButtons[3].buttonPressThreshold    = config.button4_maxPressMillis;
}

// Go through the buttons that are enabled and check for the highest channel.
int getHighestDMXChannelInUse() {
  int ret = 1; // Set default to 1

  if(config.button1_enabled && config.button1_channel > ret) {
    ret = config.button1_channel;
  }
  if(config.button2_enabled && config.button2_channel > ret) {
    ret = config.button2_channel;
  }
  if(config.button3_enabled && config.button3_channel > ret) {
    ret = config.button3_channel;
  }
  if(config.button4_enabled && config.button4_channel > ret) {
    ret = config.button4_channel;
  }

  return ret;
}

// Setup has to be on bottom to be able to reference stuff from above
void setup() {
  pinMode(FACTORY_RESET_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  Serial.begin(115200);
  LOG_SET_LEVEL(DebugLogLevel::LVL_DEBUG);

  if(initLittleFS()) {
    checkIfFactoryReset(); // Check if button for factory reset is being held

    loadConfig();
    delay(100);  // Wait for 100ms for config to settle in
    setupWiFi();
    setupMQTT();
    setupWebServer();
  } else {
    // LittleFS has failed. Hang until device is rebooted
    for(;;) {
      Serial.println("[ERROR] Failed to mount LittleFS.");
      digitalWrite(STATUS_LED_PIN, HIGH);
      delay(1000);
    }
  }

  // Get number of channels in use with DMX and initialize DMX library.
  int number_of_channels = getHighestDMXChannelInUse();
  Serial.printf("[DMX] Initializing DMX with %i channels.\n", number_of_channels);
  dmx.init(number_of_channels);
  // Start LightManager which will handle all buttonEvents.
  lMan.init(&dmx, &pubSubClient);

  setupDimmerButtons();

  hamqtt.begin(&mqtt_callback, config.mqtt_base_topic, config.wifi_hostname);
  Serial.println("[INFO] Setup done. Start loop.");

  // TODO: Remove before final release.
  pinMode(D1, OUTPUT);
}

void loop() {
  taskWiFiManager();
  taskMQTTManager();
  lMan.loop();

  // Update state of all dimmer buttons
  for(int i = 0; i < 4; i++) {
    // Send MQTT and web socket status update if marked due and on interval.
    if(lMan.dimmerButtons[i].sendMqttUpdate && millis() - lMan.dimmerButtons[i].lastMqttUpdate >= lMan.dimmerButtons[i].mqttUpdateInterval) {
      sendMqttLightUpdate(i);
      char json_data[64];
      if(lMan.dimmerButtons[i].outputState) {
        sprintf(json_data, "{\"button%i_output\": %i}", i+1, lMan.dimmerButtons[i].dimLevel);
      } else {
        sprintf(json_data, "{\"button%i_output\": \"Off\"}", i+1);
      }
      indexDataSocket.textAll(json_data);
    }
  }

  // Check if home assistant has come online and we are waiting to send an update
  // Also check if we have waited enough time to send a register update
  if(_triggerHomeAssistantOnlineUpdate && millis() - _homeAssistantOnlineUpdate >= config.home_assistant_online_wait_period_ms) {
    Serial.println("[MQTT] Wait period for Home Assistant registration over, registrating lights.");
    registerMqttLights();
    // Wait for registration to complete and then send status update
    delay(1000);
    // Mark lights due for update.
    for(int i = 0; i < 4; i++) {
      lMan.dimmerButtons[i].sendMqttUpdate = true;
    }
    hamqtt.sendOnlineStatusUpdate(&pubSubClient);
    _triggerHomeAssistantOnlineUpdate = false;
  }

  if(doReboot && millis() >= doRebootAt) {
    Serial.println("[REBOOT] Rebooting device");
    ESP.restart();
  } else if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    ESP.restart();
  }

  // Manage Status LED
  if(!WiFi.isConnected()) {
    digitalWrite(STATUS_LED_PIN, (millis() / 1000) % 2 == 0); // Blink every second if no WiFi connection is active.
  } else if (!pubSubClient.connected()) {
    digitalWrite(STATUS_LED_PIN, (millis() / 100) % 2 == 0); // Rapid blink if no MQTT connection is available.
  } else {
    digitalWrite(STATUS_LED_PIN, LOW); // Reset status LED.
  }

  // Send out DMX data update on the bus.
  dmx.update();

  // TODO: Remove before final release.
  analogWrite(D1, dmx.read(1));
}