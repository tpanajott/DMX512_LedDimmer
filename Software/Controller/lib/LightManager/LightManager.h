#ifndef LIGHTMANAGER_H
#define LIGHTMANAGER_H

#include <ArduLog.h>
#include <Arduino.h>
#include <ESPDMX.h>
#include <LMANConfig.h>
#include <freertos/semphr.h>

#include <list>
#include <string>

class DMXChannel
{
public:
  void init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx, ChannelConfig *config);
  ChannelConfig *config;
  /// @brief The current dim level
  uint8_t level;
  /// @brief The current dimming direction. True = getting brighter, false = fading
  bool dimmingDirection = true;
  /// @brief The last light level change in ms
  unsigned long lastLevelChange = 0;
  /// @brief The target for the auto-dimming function.
  uint8_t levelBeforeAutoDimming = 255;
  /// @brief The target for auto-dimming.
  uint8_t autoDimmingTarget = 0;
  /// @brief Wether or not this channel is auto-dimming.
  bool isAutoDimming = false;
  /// @brief Wether or not to turn off light when the auto-dimming target has been reached.
  bool turnOffWhenAutoDimComplete = false;
  /// @brief Current state. True = output on, false = output off.
  bool state;
  /// @brief Wether the state has changed since last MQTT update was sent.
  /// Set to true so that at first connection a state update is sent.
  bool mqttSendUpdate = true;
  bool webSendUpdate = false;
  /// @brief Set the output state and update DMX
  /// @param state The output state. true = on, false = off
  void setState(bool state);
  /// @brief Set the new dim level
  /// @param level The level to set
  void setLevel(uint8_t level);
  /// @brief Update DMX data and cause a send out straight away.
  /// @param sendUpdate Weather or not to send the update straight away or wait until next cycle.
  void updateDMXData(bool sendUpdate);
  /// @brief If auto-dimming is currently happening, stop it.
  /// @return True if stop was successful
  bool stopAutoDimming();

private:
  TaskHandle_t *_dmxSendTask;
  DMXESPSerial *_dmx;
  SemaphoreHandle_t _autoDimmingHandleMutex = NULL;
};

struct ButtonEvent
{
  bool state = false;
  bool handled = false;
  unsigned long millis = 0;
};

class Button
{
public:
  ButtonConfig *config;
  /// @brief The GPIO pin to read state for this button
  uint8_t pin;
  /// @brief The DMX channel used by this button
  DMXChannel *dmxChannel;
  /// @brief The last 3 button events
  ButtonEvent buttonEvents[3];
  /// @brief Add a new button event. Time of the event is set to current millis()
  /// @param state The new state.
  void addButtonEvent(bool state);
  /// @brief Check wether or not the current button read state has been determined or if more reads/check are neccesary
  /// @return True if button state has been determined and put into the buttonEvents list
  bool hasDeterminedState();
  /// @brief Read current state and if changed, update state list
  void updateState();
  /// @brief Get the time difference between two button events.
  /// @param firstEvent The index of the first event.
  /// @param secondEvent The index of the second event.
  /// @return The time difference between the two events in ms.
  unsigned long getTimeDelta(uint8_t firstEvent, uint8_t secondEvent);
  /// @brief Get the time difference between last event and now.
  /// @return The time in ms.
  unsigned long getTimeDeltaNowLastState();

private:
  /// @brief The data used to keep track of debouncing.
  ButtonEvent _currentButtonState;
};

class LightManager
{
public:
  /// @brief Start LightManager processing.
  /// @param dmxSendTask Task handle to the last that needs to be notified of changes in DMX data.
  /// @param dmx The DMX handler
  void init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx);
  /// @brief Initialize a button used for input and control over a DMX channel
  /// @param buttonPin The GPIO pin used to read button state
  /// @param buttonConfig The Button configuration from config manager
  Button *initButton(uint8_t buttonPin, ButtonConfig *buttonConfig);
  /// @brief Initiate a DMX channel
  /// @param config The config object for the channel
  /// @return The created DMX channel object
  DMXChannel *initDMXChannel(ChannelConfig *config);
  /// @brief The instance of the LightManager started with .init();
  static LightManager *instance;
  /// @brief Button interupt handler
  static void IRAM_ATTR ISRForwarder();
  static void taskPollButtonState();
  /// @brief When a button event has occured, perform the needed actions.
  static void taskProcessButtonEvents(void *param);
  /// @brief Start auto-dimming to specified target. Starting from already set value.
  /// @param dmxChannel The channel to auto-dim
  /// @param level The level to dim to.
  void autoDimTo(DMXChannel *dmxChannel, uint8_t level);
  /// @brief Turn on light by auto-dimming to the previous level.
  /// @param dmxChannel The DMX Channel to turn on.
  /// @param level The requested level.
  void autoDimOnToLevel(DMXChannel *dmxChannel, uint8_t level);
  /// @brief Turn on light by auto-dimming to the previous level.
  /// @param dmxChannel The DMX Channel to turn on.
  void autoDimOn(DMXChannel *dmxChannel);
  /// @brief Turn off light by auto-dimming to the previous level.
  /// @param dmxChannel The DMX Channel to turn on.
  void autoDimOff(DMXChannel *dmxChannel);
  static void taskAutoDimDMXChannel(void *param);
  /// @brief The list of DMX Channels in use
  std::list<DMXChannel> dmxChannels;
  /// @brief The list of active buttons
  std::list<Button> buttons;

private:
  TaskHandle_t _taskHandleReadButtonStates;
  static void _taskReadButtonStates(void *param);
  TaskHandle_t _taskHandleDimLights;
  static void _taskDimLights(void *param);
  TaskHandle_t _taskHandleAutoDimLights;
  static void _taskAutoDimLights(void *param);
  TaskHandle_t *_dmxSendTask;
  /// @brief The DMX handler
  DMXESPSerial *_dmx;
};

#endif