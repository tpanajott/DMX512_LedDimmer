#include <ArduLog.h>
#include <Arduino.h>
#include <LightManager.h>
#include <pins.h>
#include <LMANConfig.h>
#include <ESPDMX.h>
#include <WiFi.h>
#include <PubSubClient.h>
// #include <AsyncTCP.h>
// #include <ESPAsyncWebServer.h>
#include <WebManager.h>

bool lastButton1State = false;
ArduLog logger;
LMANConfig config;
LightManager lMan;
DMXESPSerial dmx;
TaskHandle_t taskHandleErrorLedHandle = NULL;
TaskHandle_t taskHandleSendDMXData = NULL;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
// AsyncWebServer webServer(80);
WebManager webMan;

void taskHandleErrorLed(void *param)
{
  LOG_INFO("taskHandleErrorLed started!");
  for (;;)
  {
    if (!WiFi.isConnected())
    {
      digitalWrite(PIN_ERROR_LED, 1);
    }
    else if (!mqttClient.connected())
    {
      digitalWrite(PIN_ERROR_LED, (millis() / 500) % 2 == 0);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void taskWiFiMqttHandler(void *param)
{
  LOG_INFO("taskWiFiMqttHandler started!");
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
        vTaskDelay(5000);
        if (WiFi.isConnected())
        {
          LOG_INFO("Connected to WiFi ", LOG_BOLD, config.wifi_ssid.c_str());
          LOG_INFO("IP Address: ", LOG_BOLD, WiFi.localIP());
          LOG_INFO("Netmask:    ", LOG_BOLD, WiFi.subnetMask());
          LOG_INFO("Gateway:    ", LOG_BOLD, WiFi.gatewayIP());
          // Start web server
          // webMan.init(&webServer);
          webMan.init();
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
        LOG_INFO("Connecting to MQTT server ", LOG_BOLD, config.mqtt_server.c_str());
        mqttClient.connect(config.wifi_hostname.c_str(), config.mqtt_username.c_str(), config.mqtt_password.c_str());
        vTaskDelay(5000);
        if (mqttClient.connected())
        {
          LOG_INFO("Connected to MQTT server ", LOG_BOLD, config.mqtt_server.c_str());
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
  if (webMan.doReboot())
  {
    ESP.restart();
  }
  vTaskDelay(250 / portTICK_PERIOD_MS);
}

void setup()
{
  Serial.begin(115200);
  logger.init();
  logger.SetSerial(&Serial);
  logger.SetLogLevel(ArduLogLevel::Debug);
  logger.SetUseDecorations(true);
  delay(50);

  config.init();
  config.loadFromLittleFS();

  dmx.init(5);

  pinMode(PIN_ERROR_LED, OUTPUT);
  xTaskCreatePinnedToCore(taskHandleErrorLed, "taskErrorLed", 5000, NULL, 1, &taskHandleErrorLedHandle, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskSendDMXData, "taskSendDMXData", 5000, NULL, 2, &taskHandleSendDMXData, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskWiFiMqttHandler, "taskWiFiMqttHandler", 5000, NULL, 0, NULL, CONFIG_ARDUINO_RUNNING_CORE);

  lMan.init(&taskHandleSendDMXData, &dmx);
  lMan.buttonPressMinTime = 80;
  lMan.buttonPressMaxTime = 800;
  lMan.initButton(PIN_BUTTON_1, &LMANConfig::instance->buttonConfigs[0]);
  // lMan.initButton(PIN_BUTTON_2, &LMANConfig::instance->buttonConfigs[1]);
  // lMan.initButton(PIN_BUTTON_3, &LMANConfig::instance->buttonConfigs[2]);
  // lMan.initButton(PIN_BUTTON_4, &LMANConfig::instance->buttonConfigs[3]]);
}