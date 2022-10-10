#ifndef LIGHTMANAGER_H
#define LIGHTMANAGER_H

#include <ArduLog.h>
#include <Arduino.h>
#include <ESPDMX.h>
#include <LMANConfig.h>

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
  /// @brief Wether or not to turn off light when the auto-dimming target has been reached.
  bool turnOffWhenAutoDimComplete = false;
  /// @brief The task that is handeling auto-dimming. NULL if no task is active
  TaskHandle_t taskHandleAutoDimming = NULL;
  /// @brief Current state. True = output on, false = output off.
  bool state;
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
  void stopAutoDimming();

private:
  TaskHandle_t *_dmxSendTask;
  DMXESPSerial *_dmx;
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
  /// @brief Wether the button is enabled or not.
  bool enabled = true;
  /// @brief The GPIO pin to read state for this button
  uint8_t pin;
  /// @brief The DMX channel used by this button
  DMXChannel *dmxChannel;
  /// @brief The last 3 button events
  ButtonEvent buttonEvents[3];
  /// @brief Add a new button event. Time of the event is set to current millis()
  /// @param state The new state.
  void addButtonEvent(bool state);
  /// @brief Read current state and if changed, update state list
  void updateState();
  /// @brief Get the time difference between two button events.
  /// @param firstEvent The index of the first event.
  /// @param secondEvent The index of the second event.
  /// @return The time difference between the two events in ms.
  unsigned long getTimeDelta(uint8_t firstEvent, uint8_t secondEvent);
  /// @brief Get the time difference between lats event and now.
  /// @return The time in ms.
  unsigned long getTimeDeltaNowLastState();
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
  /// @brief The time in ms a button must have the same state for it to be considered valid
  uint8_t buttonPressMinTime;
  /// @brief The time max time in ms for a button to be high to be considered a "press".
  /// After this time it will start dimming.
  uint16_t buttonPressMaxTime;
  /// @brief The instance of the LightManager started with .init();
  static LightManager *instance;
  /// @brief Button interupt handler
  static void IRAM_ATTR ISRForwarder();
  TaskHandle_t taskHandleProcessButtonEvents;
  static void taskProcessButtonEvents(void *param);
  /// @brief Start auto-dimming to specified target. Starting from already set value.
  /// @param dmxChannel The channel to auto-dim
  /// @param level The level to dim to.
  void autoDimTo(DMXChannel *dmxChannel, uint8_t level);
  static void taskAutoDimDMXChannel(void *param);

private:
  TaskHandle_t _taskHandleReadButtonStates;
  static void _taskReadButtonStates(void *param);
  TaskHandle_t _taskHandleDimLights;
  static void _taskDimLights(void *param);
  /// @brief The list of active buttons
  std::list<Button> _buttons;
  /// @brief The list of DMX Channels in use
  std::list<DMXChannel> _dmxChannels;
  TaskHandle_t *_dmxSendTask;
  /// @brief The DMX handler
  DMXESPSerial *_dmx;
};

#endif