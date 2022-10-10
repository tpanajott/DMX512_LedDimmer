#include <ArduLog.h>
#include <Arduino.h>
#include <LightManager.h>
#include <pins.h>
#include <LMANConfig.h>
#include <ESPDMX.h>

bool lastButton1State = false;
ArduLog logger;
LMANConfig config;
LightManager lMan;

DMXESPSerial dmx;

TaskHandle_t taskHandleErrorLedHandle = NULL;
TaskHandle_t taskHandleSendDMXData = NULL;
void taskHandleErrorLed(void *param)
{
  for (;;)
  {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
    {
      digitalWrite(PIN_ERROR_LED, !digitalRead(PIN_ERROR_LED));
      LOG_DEBUG("Changing LED state!");
    }
  }
}

void taskSendDMXData(void *param)
{
  LOG_INFO("Starting taskSendDMXData");
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, 1000); // Wait 1000ms or until notified to send DMX data.
    dmx.update();
  }
}

void loop()
{
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
  xTaskCreatePinnedToCore(taskHandleErrorLed, "taskErrorLed", 1000, NULL, tskIDLE_PRIORITY, &taskHandleErrorLedHandle, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskSendDMXData, "taskSendDMXData", 5000, NULL, 2, &taskHandleSendDMXData, CONFIG_ARDUINO_RUNNING_CORE);

  lMan.init(&taskHandleSendDMXData, &dmx);
  lMan.buttonPressMinTime = 80;
  lMan.buttonPressMaxTime = 800;
  lMan.initButton(PIN_BUTTON_1, &LMANConfig::instance->buttonConfigs[0]);
  // lMan.initButton(PIN_BUTTON_2, 1);
  // lMan.initButton(PIN_BUTTON_3, 3);
  // lMan.initButton(PIN_BUTTON_4, 4);
}