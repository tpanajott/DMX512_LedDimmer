#include <LightManager.h>
#include <ESPDMX.h>

// DMX Channel functions
void DMXChannel::init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx, ChannelConfig *config)
{
  this->config = config;
  this->level = this->config->max;
  this->_dmxSendTask = dmxSendTask;
  this->_dmx = dmx;
}

void DMXChannel::setState(bool state)
{
  this->state = state;
  this->updateDMXData(true);
}

void DMXChannel::setLevel(uint8_t level)
{
  this->level = level;
  this->lastLevelChange = millis();
  // If the light is on, send the update straight away
  this->updateDMXData(this->state);
}

void DMXChannel::updateDMXData(bool sendUpdate)
{
  uint8_t newLevel = this->state ? this->level : 0;
  LOG_TRACE("Updating DMX channel ", LOG_BOLD, this->config->channel, LOG_RESET_DECORATIONS, " to value ", LOG_BOLD, newLevel);
  this->_dmx->write(this->config->channel, newLevel);
  if (sendUpdate)
  {
    xTaskNotifyGive((*this->_dmxSendTask));
  }
}

void DMXChannel::stopAutoDimming()
{
  if (this->taskHandleAutoDimming)
  {
    this->taskHandleAutoDimming = NULL;
    vTaskDelete(this->taskHandleAutoDimming);
  }
}

// Button functions
void Button::addButtonEvent(bool state)
{
  LOG_TRACE("Adding new button event, new state: ", LOG_BOLD, state ? "ON" : "OFF");
  // Shift current events down the list
  this->buttonEvents[2] = this->buttonEvents[1];
  this->buttonEvents[1] = this->buttonEvents[0];
  // Add new event as the "first" in the list.
  ButtonEvent btnEvent;
  btnEvent.state = state;
  btnEvent.millis = millis();
  btnEvent.handled = false;
  this->buttonEvents[0] = btnEvent;
}

void Button::updateState()
{
  if (LightManager::instance)
  {
    // Invert read state as there is a PULLUP on the line.
    bool currentButtonState = !digitalRead(this->pin);
    if (currentButtonState != this->buttonEvents[0].state)
    {
      this->addButtonEvent(currentButtonState);
      xTaskNotifyGive(LightManager::instance->taskHandleProcessButtonEvents);
    }
  }
  else
  {
    LOG_ERROR("Trying to update button states. No need as no LightManager::instance exists!");
  }
}

unsigned long Button::getTimeDelta(uint8_t firstEvent, uint8_t secondEvent)
{
  return this->buttonEvents[firstEvent].millis - this->buttonEvents[secondEvent].millis;
}

unsigned long Button::getTimeDeltaNowLastState()
{
  return millis() - this->buttonEvents[0].millis;
}

// Light Manager functions
// Give somewhere in ram for instances to exist
LightManager *LightManager::instance;

Button *LightManager::initButton(uint8_t buttonPin, ButtonConfig *buttonConfig)
{
  // Find if the dmx channel to be used has already been created.
  DMXChannel *channel = nullptr;
  for (std::list<DMXChannel>::iterator it = this->_dmxChannels.begin(); it != this->_dmxChannels.end(); ++it)
  {
    if (it->config->channel == buttonConfig->channel)
    {
      LOG_INFO("DMX Channel ", LOG_BOLD, buttonConfig->channel, LOG_RESET_DECORATIONS, " already in use and found.");
      channel = &(*it);
      break;
    }
  }
  // If the dmx channel to be used was not found in the list, create it.
  if (channel == nullptr)
  {
    LOG_WARNING("DMX Channel ", LOG_BOLD, buttonConfig->channel, LOG_RESET_DECORATIONS, " was not found. Trying to init from config.");
    for (std::list<ChannelConfig>::iterator it = LMANConfig::instance->channelConfigs.begin(); it != LMANConfig::instance->channelConfigs.end(); ++it)
    {
      if (it->channel == buttonConfig->channel)
      {
        LOG_INFO("Found matching config for channel ", LOG_BOLD, it->channel, LOG_RESET_DECORATIONS, " building new channel object.");
        DMXChannel newChannel;
        newChannel.state = false;
        newChannel.init(this->_dmxSendTask, this->_dmx, &(*it));
        this->_dmxChannels.push_back(newChannel);
        channel = &this->_dmxChannels.back();
        break;
      }
    }
  }

  LOG_DEBUG("Initializing button on PIN ", LOG_BOLD, buttonPin);
  Button newBtn;
  newBtn.pin = buttonPin;
  newBtn.dmxChannel = channel;
  // Mark all events handled from the start.
  newBtn.buttonEvents[0].handled = true;
  newBtn.buttonEvents[1].handled = true;
  newBtn.buttonEvents[2].handled = true;
  attachInterrupt(buttonPin, LightManager::ISRForwarder, CHANGE);
  this->_buttons.push_back(newBtn);
  return &this->_buttons.back();
}

void LightManager::init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx)
{
  LightManager::instance = this;
  this->_dmxSendTask = dmxSendTask;
  this->_dmx = dmx;
  xTaskCreatePinnedToCore(_taskReadButtonStates, "taskReadButtonStates", 5000, NULL, 1, &this->_taskHandleReadButtonStates, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskProcessButtonEvents, "taskProcessButtonEvents", 5000, NULL, 1, &this->taskHandleProcessButtonEvents, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(_taskDimLights, "taskDimLights", 5000, NULL, 1, &this->_taskHandleDimLights, CONFIG_ARDUINO_RUNNING_CORE);
}

void LightManager::_taskReadButtonStates(void *param)
{
  LOG_INFO("Started _taskReadButtonStates");
  for (;;)
  {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
    {
      if (LightManager::instance)
      {
        for (std::list<Button>::iterator it = LightManager::instance->_buttons.begin(); it != LightManager::instance->_buttons.end(); ++it)
        {
          it->updateState();
        }
      }
      else
      {
        LOG_ERROR(
            "Trying to read button states event though no LightManager::instance is set!");
      }
    }
  }
}

void IRAM_ATTR LightManager::ISRForwarder()
{
  if (LightManager::instance && LightManager::instance->_taskHandleReadButtonStates)
  {
    vTaskNotifyGiveFromISR(LightManager::instance->_taskHandleReadButtonStates, NULL);
  }
}

void LightManager::taskProcessButtonEvents(void *param)
{
  LOG_INFO("Started taskProcessButtonEvents");
  for (;;)
  {
    if (LightManager::instance)
    {
      bool unhandledButtonEvents = true;
      for (std::list<Button>::iterator it = LightManager::instance->_buttons.begin(); it != LightManager::instance->_buttons.end(); ++it)
      {
        // Do initial filtering
        if (!it->buttonEvents[0].handled && it->getTimeDelta(0, 1) >= LightManager::instance->buttonPressMinTime)
        {
          if (it->buttonEvents[0].state && it->getTimeDeltaNowLastState() >= LightManager::instance->buttonPressMaxTime && it->dmxChannel->state)
          {
            // State is high and has been for the max threshold time. Consider it a dimming event.
            // If a dimming task has been created, notify it and mark event as handled.
            if (LightManager::instance->_taskHandleDimLights)
            {
              // Reverse direction so that a release of the button and then press again reverses it.
              it->dmxChannel->dimmingDirection = !it->dmxChannel->dimmingDirection;
              it->buttonEvents[0].handled = true;
              xTaskNotifyGive(LightManager::instance->_taskHandleDimLights);
            }
            else
            {
              LOG_ERROR("Dimming event occuring but no task started!");
            }
          }
          else if (!it->buttonEvents[0].state && it->buttonEvents[1].state && it->getTimeDelta(0, 1) <= LightManager::instance->buttonPressMaxTime)
          {
            // State is low, previous state was high and the time between states
            // was lower than max and higher than min. This was a toggle press.
            if (it->dmxChannel->state)
            {
              LOG_DEBUG("Slow turn off triggered for channel ", LOG_BOLD, it->dmxChannel->config->channel);
              it->dmxChannel->turnOffWhenAutoDimComplete = true;                              // Turn off the light when dimming is complete
              it->dmxChannel->levelBeforeAutoDimming = it->dmxChannel->level;                 // Save the current level
              LightManager::instance->autoDimTo(it->dmxChannel, it->dmxChannel->config->min); // Dim to targetUpdate DMX state.Update DMX state.Update DMX state.Update DMX state.Update DMX state.Update DMX state.Update DMX state.
            }
            else
            {
              LOG_DEBUG("Slow turn on triggered for channel ", LOG_BOLD, it->dmxChannel->config->channel);
              it->dmxChannel->turnOffWhenAutoDimComplete = false;                                        // Do not turn off when auto-dim done.
              it->dmxChannel->levelBeforeAutoDimming = it->dmxChannel->level;                            // What is the target level
              it->dmxChannel->setLevel(it->dmxChannel->config->min);                                     // Set the current level to min
              it->dmxChannel->setState(true);                                                            // Turn on the light
              LightManager::instance->autoDimTo(it->dmxChannel, it->dmxChannel->levelBeforeAutoDimming); // Dim to the target
            }
            it->buttonEvents[0].handled = true;
          }

          if (!unhandledButtonEvents && !it->buttonEvents[0].handled)
          {
            unhandledButtonEvents = true;
            LOG_DEBUG(unhandledButtonEvents);
          }
        }
      }
      if (!unhandledButtonEvents)
      {
        // No unhandled button events exist, wait untill notified
        LOG_INFO("All button events handled. Waiting for notification.");
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      }
      else
      {
        vTaskDelay(5);
      }
    }
    else
    {
      LOG_ERROR("No LightManager::instance available!");
      vTaskDelay(5);
    }
  }
}

void LightManager::_taskDimLights(void *param)
{
  LOG_INFO("Started _taskDimLights");
  for (;;)
  {
    bool hasDimmingJob = false;
    for (std::list<Button>::iterator it = LightManager::instance->_buttons.begin(); it != LightManager::instance->_buttons.end(); ++it)
    {
      // Latest state is high, dim the light
      if (it->buttonEvents[0].state)
      {
        // Indicate that there is still a dimming job to do.
        if (!hasDimmingJob)
        {
          hasDimmingJob = true;
        }
        // Update dim level.
        DMXChannel *channel = it->dmxChannel;
        channel->stopAutoDimming(); // Stop auto-dimming if it is currently happening
        if (channel->level != channel->config->min && channel->level != channel->config->max && millis() - channel->lastLevelChange >= channel->config->dimmingSpeed)
        {
          // We are not at min/max and have reached time for a new dim event.
          if (channel->dimmingDirection)
          {
            channel->setLevel(channel->level + 1);
          }
          else
          {
            channel->setLevel(channel->level - 1);
          }
        }
        else if (channel->level == channel->config->min && millis() - channel->lastLevelChange >= channel->config->holdPeriod)
        {
          LOG_DEBUG("Hold period over at min light, reversing!");
          channel->setLevel(channel->config->min + 1);
          channel->dimmingDirection = true;
        }
        else if (channel->level == channel->config->max && millis() - channel->lastLevelChange >= channel->config->holdPeriod)
        {
          LOG_DEBUG("Hold period over at max light, reversing!");
          channel->setLevel(channel->config->max - 1);
          channel->dimmingDirection = false;
        }
      }
    }
    if (hasDimmingJob)
    {
      vTaskDelay(1);
    }
    else
    {
      LOG_INFO("All dimming events done. Waiting for notification.");
      if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
      {
        LOG_DEBUG("_taskDimLights got notification!");
      }
    }
  }
}

void LightManager::autoDimTo(DMXChannel *dmxChannel, uint8_t level)
{
  dmxChannel->stopAutoDimming(); // Stop any auto-dimming that is currently happening.
  dmxChannel->autoDimmingTarget = level;
  xTaskCreatePinnedToCore(taskAutoDimDMXChannel, "autoDimChannel", 1500, (void *)dmxChannel, 1, &dmxChannel->taskHandleAutoDimming, CONFIG_ARDUINO_RUNNING_CORE);
}

void LightManager::taskAutoDimDMXChannel(void *param)
{
  DMXChannel *channel = (DMXChannel *)param;
  LOG_INFO("Started auto-dimming of DMX channel ", LOG_BOLD, channel->config->channel, LOG_RESET_DECORATIONS, " target ", LOG_BOLD, channel->autoDimmingTarget);
  while (channel->level != channel->autoDimmingTarget)
  {
    if (channel->level < channel->autoDimmingTarget)
    {
      channel->setLevel(channel->level + 1);
    }
    else if (channel->level > channel->autoDimmingTarget)
    {
      channel->setLevel(channel->level - 1);
    }
    else
    {
      LOG_ERROR("UNKNOWN STATE! Stopping.");
      channel->stopAutoDimming();
    }
    vTaskDelay(channel->config->autoDimmingSpeed);
  }
  // If turn off was requested, turn off the light when dimming is complete.
  if (channel->turnOffWhenAutoDimComplete)
  {
    channel->setState(false);
    channel->turnOffWhenAutoDimComplete = false;
    // Reset level to what it was before auto-dimming started.
    channel->setLevel(channel->levelBeforeAutoDimming);
  }

  // Dimming is done. Delete task handle on channel and stop the current task.
  channel->stopAutoDimming();
}