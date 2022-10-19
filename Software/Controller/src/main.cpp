#include <ArduLog.h>
#include <Arduino.h>
#include <string>
#include <LightManager.h>
#include <pins.h>
#include <LMANConfig.h>
#include <ESPDMX.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebManager.h>

ArduLog logger;
LMANConfig config;
LightManager lMan;
DMXESPSerial dmx;
TaskHandle_t taskHandleErrorLedHandle = NULL;
TaskHandle_t taskHandleSendDMXData = NULL;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebManager webMan;
bool lastResetButtonState = false;
unsigned long lastResetButtonStateChange = 0;

void taskHandleErrorLed(void *param)
{
  LOG_INFO("taskHandleErrorLed started!");
  for (;;)
  {
    if (lastResetButtonState && millis() - lastResetButtonStateChange > 1000)
    {
      digitalWrite(PIN_ERROR_LED, (millis() / 100) % 2 == 0);
    }
    else if (LMANConfig::instance->wifi_ssid.empty())
    {
      digitalWrite(PIN_ERROR_LED, (millis() / 250) % 2 == 0);
    }
    else if (!WiFi.isConnected())
    {
      digitalWrite(PIN_ERROR_LED, 1);
    }
    else if (!mqttClient.connected())
    {
      digitalWrite(PIN_ERROR_LED, (millis() / 500) % 2 == 0);
    } else {
      // Turn off light if no error exists
      digitalWrite(PIN_ERROR_LED, 0);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  std::string tpc = topic;
  std::string pld;
  pld.assign((char *)payload, length);
  LOG_TRACE("Got message on ", LOG_BOLD, tpc.c_str());
  LOG_TRACE(pld.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err)
  {
    LOG_ERROR("Failed to deserialize JSON from message.");
    return;
  }
  // Deserialization was successfull
  for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
  {
    if (it->config->channel != 0)
    {
      if (it->config->getCmdTopic().compare(tpc) == 0)
      {
        try
        {
          LOG_INFO("Got MQTT command for ", LOG_BOLD, it->config->name.c_str());
          if (doc.containsKey("brightness"))
          {
            uint8_t brightess = 128;
            try
            {
              brightess = doc["brightness"].as<uint8_t>();
            }
            catch (const std::exception &e)
            {
              LOG_ERROR("Failed to cast brightness to uint8_t");
              break; // Error converting. Do not do anything.
            }

            if (it->state)
            {
              // Light is already on, just dim to requested level.
              LightManager::instance->autoDimTo(&(*it), brightess);
            }
            else if (!it->state)
            {
              // Light is off but a level was requested, turn on and dim to target.
              LOG_INFO("Slow turn on requested by MQTT for ", LOG_BOLD, it->config->name.c_str(), LOG_RESET_DECORATIONS, " to level ", LOG_BOLD, brightess);
              lMan.autoDimOnToLevel(&(*it), brightess);
            }
            else
            {
              LOG_ERROR("Unknown brightness!");
            }
          }
          else if (doc.containsKey("state"))
          {
            std::string state = doc["state"] | "";
            if (!it->state && state.compare("ON") == 0)
            {
              // Light us currently off and it was requsted on without brightess. Turn on to level from before.
              LOG_INFO("Slow turn on requested by MQTT for ", LOG_BOLD, it->config->name.c_str());
              lMan.autoDimOn(&(*it));
            }
            else if (it->state && state.compare("OFF") == 0)
            {
              // Light is on and a turn off was requested
              lMan.autoDimOff(&(*it));
            }
            else
            {
              LOG_ERROR("Unknown state!");
            }
          }
        }
        catch (const std::exception &e)
        {
          LOG_ERROR("Exception when handling MQTT message: ", LOG_BOLD, e.what());
        }
        break;
      }
    }
  }
}

/// @brief Send, if needed, a status update to MQTT
void sendMqttStatusUpdate()
{
  if (!mqttClient.connected())
  {
    return;
  }

  for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
  {
    if (it->config->channel != 0 && millis() - it->lastLevelChange > 100 && it->mqttSendUpdate)
    {
      StaticJsonDocument<200> doc;
      doc["state"] = it->state ? "ON" : "OFF";
      doc["brightness"] = it->level;

      char buffer[256];
      size_t length = serializeJson(doc, buffer);
      if (mqttClient.publish(it->config->getStateTopic().c_str(), buffer, length))
      {
        it->mqttSendUpdate = false;
      }
      else
      {
        LOG_ERROR("Failed to send state update for ", LOG_BOLD, it->config->name.c_str());
      }
    }
  }
}

/// @brief Register device and channels to MQTT
void registerToMqtt()
{
  for (int i = 0; i < sizeof(LMANConfig::instance->channelConfigs) / sizeof(ChannelConfig); i++)
  {
    if (LMANConfig::instance->channelConfigs[i].channel != 0)
    {
      // Subscribe to command topic
      std::string cmdTopic = LMANConfig::instance->channelConfigs[i].getCmdTopic();
      LOG_DEBUG("Subscribing to ", LOG_BOLD, cmdTopic.c_str());
      mqttClient.subscribe(cmdTopic.c_str());

      // Register light to home assistant
      DynamicJsonDocument doc(1024);
      ChannelConfig *config = &LMANConfig::instance->channelConfigs[i];
      std::string unique_name = LMANConfig::instance->wifi_hostname;
      unique_name.append("-");
      unique_name.append(config->name);
      doc["~"] = config->getBaseTopic();
      doc["name"] = config->name.c_str();
      doc["cmd_t"] = "~/cmd";
      doc["stat_t"] = "~/state";
      doc["schema"] = "json";
      doc["brightness"] = true;
      doc["avty_t"] = config->getAvailabilityTopic();

      // Device information
      std::string device_configuration_url = "http://";
      device_configuration_url.append(WiFi.localIP().toString().c_str());

      JsonObject device = doc.createNestedObject("device");
      device["cu"] = device_configuration_url.c_str();
      device["name"] = LMANConfig::instance->wifi_hostname;
      device["mf"] = "Tim P";
      JsonArray connections = device.createNestedArray("cns");
      JsonArray mac_address = connections.createNestedArray();
      mac_address.add("mac");
      mac_address.add(WiFi.macAddress());
      JsonArray ip_address = connections.createNestedArray();
      ip_address.add("ip");
      ip_address.add(WiFi.localIP().toString());

      char buffer[1024];
      size_t length = serializeJson(doc, buffer);
      if (!mqttClient.publish(config->getCfgTopic().c_str(), (uint8_t *)buffer, length, true))
      {
        LOG_ERROR("Failed to register light ", LOG_BOLD, config->name.c_str());
      }
      else
      {
        LOG_INFO("Registered light to ", LOG_BOLD, config->getCfgTopic().c_str());
      }
    }
  }
}

void taskWiFiMqttHandler(void *param)
{
  LOG_INFO("taskWiFiMqttHandler started!");
  if (!LMANConfig::instance->wifi_ssid.empty())
  {
    for (;;)
    {
      if (!WiFi.isConnected() && !config.wifi_ssid.empty())
      {
        LOG_ERROR("WiFi not connected!");
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(config.wifi_hostname.c_str());
        while (!WiFi.isConnected())
        {
          WiFi.begin(config.wifi_ssid.c_str(), config.wifi_psk.c_str());
          LOG_INFO("Connecting to WiFi ", LOG_BOLD, config.wifi_ssid.c_str());
          vTaskDelay(1000);
          if (WiFi.isConnected())
          {
            LOG_INFO("Connected to WiFi ", LOG_BOLD, config.wifi_ssid.c_str());
            LOG_INFO("IP Address: ", LOG_BOLD, WiFi.localIP());
            LOG_INFO("Netmask:    ", LOG_BOLD, WiFi.subnetMask());
            LOG_INFO("Gateway:    ", LOG_BOLD, WiFi.gatewayIP());
            // Start web server
            // webMan.init(&webServer);
            webMan.init(&mqttClient);
          }
          else
          {
            LOG_ERROR("Failed to connect to WiFi. Will try again in 10 seconds");
          }
        }
      }
      else if (config.wifi_ssid.empty())
      {
        LOG_ERROR("No WiFi SSID configured!");
      }

      if (WiFi.isConnected() && !mqttClient.connected() && !config.mqtt_server.empty())
      {
        LOG_ERROR("MQTT not connected!");
        while (WiFi.isConnected() && !mqttClient.connected())
        {
          mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
          mqttClient.setCallback(mqttCallback);
          mqttClient.setBufferSize(2048);
          LOG_INFO("Connecting to MQTT server ", LOG_BOLD, config.mqtt_server.c_str());
          // mqttClient.connect(config.wifi_hostname.c_str(), config.mqtt_username.c_str(), config.mqtt_password.c_str());
          mqttClient.connect(config.wifi_hostname.c_str(), config.mqtt_username.c_str(), config.mqtt_password.c_str(), LMANConfig::instance->channelConfigs[0].getAvailabilityTopic().c_str(), 1, 1, "offline");
          vTaskDelay(1000);
          if (mqttClient.connected())
          {
            LOG_INFO("Connected to MQTT server ", LOG_BOLD, config.mqtt_server.c_str());
            mqttClient.subscribe(LMANConfig::instance->home_assistant_base_topic.c_str());
            mqttClient.publish(LMANConfig::instance->channelConfigs[0].getAvailabilityTopic().c_str(), "online", true);
            registerToMqtt();
          }
          else
          {
            LOG_ERROR("Failed to connect to MQTT. Will try again in 10 seconds");
          }
        }
      }
      else if (config.mqtt_server.empty())
      {
        LOG_ERROR("No MQTT server configured!");
      }
      vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
  }
  else
  {
    LOG_ERROR("No WiFi configuration exists. Starting AP!");
    IPAddress local_ip(192, 168, 1, 1);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);

    if (WiFi.softAPConfig(local_ip, gateway, subnet))
    {
      LOG_INFO("Soft-AP configuration applied.");
      if (WiFi.softAP("Light Controller", "password"))
      {
        LOG_INFO("Soft-AP started.");

        LOG_INFO("WiFi SSID: Light Controller");
        LOG_INFO("WiFi PSK : password");
        LOG_INFO("WiFi IP Address: ", WiFi.softAPIP().toString().c_str());
        webMan.init(&mqttClient);
        vTaskDelete(NULL); // This task is complete. Stop processing.
      }
      else
      {
        LOG_ERROR("Failed to start Soft-AP!");
      }
    }
    else
    {
      LOG_ERROR("Failed to apply Soft-AP configuration!");
    }
  }
}

void taskSendDMXData(void *param)
{
  LOG_INFO("Starting taskSendDMXData");
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS); // Wait 1000ms or until notified to send DMX data.
    dmx.update();
  }
}

void loop()
{
  mqttClient.loop();
  sendMqttStatusUpdate();
  if (webMan.doReboot())
  {
    ESP.restart();
  }

  bool currentResetButtonState = (digitalRead(PIN_FACTORY_RESET) == LOW);
  if (currentResetButtonState != lastResetButtonState)
  {
    lastResetButtonStateChange = millis();
  }
  else if (currentResetButtonState && millis() - lastResetButtonStateChange > 10000)
  {
    LOG_WARNING("Performing factory reset at user request!");
    LMANConfig::instance->factoryReset();
    ESP.restart();
  }

  lastResetButtonState = currentResetButtonState;
  vTaskDelay(100 / portTICK_PERIOD_MS);
}

void setup()
{
  Serial.begin(115200);
  logger.init();
  logger.SetSerial(&Serial);
  logger.SetLogLevel(ArduLogLevel::Debug);
  logger.SetUseDecorations(true);
  delay(50);

  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);

  config.init();
  config.loadFromLittleFS();

  // Find highest DMX-channel in use.
  uint16_t higestDMXChannel = 0;
  for (int i = 0; i < sizeof(LMANConfig::instance->channelConfigs) / sizeof(ChannelConfig); i++)
  {
    if (LMANConfig::instance->channelConfigs[i].enabled && LMANConfig::instance->channelConfigs[i].channel > higestDMXChannel)
    {
      higestDMXChannel = LMANConfig::instance->channelConfigs[i].channel + 1;
    }
  }
  LOG_INFO("Initializing DMX-library with ", LOG_BOLD, higestDMXChannel, LOG_RESET_DECORATIONS " channels");
  // Init with highest DMX-channel+1
  dmx.init(&Serial2, higestDMXChannel, 0);

  pinMode(PIN_ERROR_LED, OUTPUT);
  xTaskCreatePinnedToCore(taskHandleErrorLed, "taskErrorLed", 5000, NULL, 1, &taskHandleErrorLedHandle, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskSendDMXData, "taskSendDMXData", 5000, NULL, 2, &taskHandleSendDMXData, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskWiFiMqttHandler, "taskWiFiMqttHandler", 5000, NULL, 0, NULL, CONFIG_ARDUINO_RUNNING_CORE);

  lMan.init(&taskHandleSendDMXData, &dmx);

  lMan.initDMXChannel(&LMANConfig::instance->channelConfigs[0]);
  lMan.initDMXChannel(&LMANConfig::instance->channelConfigs[1]);
  lMan.initDMXChannel(&LMANConfig::instance->channelConfigs[2]);
  lMan.initDMXChannel(&LMANConfig::instance->channelConfigs[3]);

  lMan.initButton(PIN_BUTTON_1, &LMANConfig::instance->buttonConfigs[0]);
  lMan.initButton(PIN_BUTTON_2, &LMANConfig::instance->buttonConfigs[1]);
  lMan.initButton(PIN_BUTTON_3, &LMANConfig::instance->buttonConfigs[2]);
  lMan.initButton(PIN_BUTTON_4, &LMANConfig::instance->buttonConfigs[3]);
}