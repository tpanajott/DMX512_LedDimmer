#include <LightManager.h>
#include <ESPDMX.h>

// Reserve memory for variables
// std::mutex DMXChannel::_autoDimmingHandleMutex;

// DMX Channel functions
void DMXChannel::init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx, ChannelConfig *config)
{
  this->config = config;
  this->level = this->config->max;
  this->_dmxSendTask = dmxSendTask;
  this->_dmx = dmx;
  this->_autoDimmingHandleMutex = xSemaphoreCreateMutex();
}

void DMXChannel::setState(bool state)
{
  // Do nothing if this channel is disabled.
  if (!this->config->enabled)
  {
    return;
  }
  this->state = state;
  this->mqttSendUpdate = true;
  this->webSendUpdate = true;
  this->updateDMXData(true);
}

void DMXChannel::setLevel(uint8_t level)
{
  // Do nothing if this channel is disabled.
  if (!this->config->enabled)
  {
    return;
  }
  LOG_TRACE("Setting channel ", LOG_BOLD, this->config->channel, LOG_RESET_DECORATIONS, " to level ", LOG_BOLD, level);
  this->level = level;
  this->lastLevelChange = millis();
  this->mqttSendUpdate = true;
  this->webSendUpdate = true;
  // If the light is on, send the update straight away
  this->updateDMXData(this->state);
}

void DMXChannel::updateDMXData(bool sendUpdate)
{
  // Do nothing if this channel is disabled.
  if (!this->config->enabled)
  {
    return;
  }
  uint8_t newLevel = this->state ? this->level : 0;
  LOG_TRACE("Updating DMX channel ", LOG_BOLD, this->config->channel, LOG_RESET_DECORATIONS, " to value ", LOG_BOLD, newLevel);
  this->_dmx->write(this->config->channel, newLevel);
  if (sendUpdate)
  {
    try
    {
      xTaskNotifyGive((*this->_dmxSendTask));
    }
    catch (const std::exception &e)
    {
      LOG_ERROR("Failed to notify _dmxSendTask. Error: ", LOG_BOLD, e.what());
    }
  }
}

bool DMXChannel::stopAutoDimming()
{
  // Do nothing if this channel is disabled.
  if (!this->config->enabled)
  {
    return true;
  }
  try
  {
    LOG_TRACE("Stop auto-dimming requested!");
    this->isAutoDimming = false;
    return true;
  }
  catch (const std::exception &e)
  {
    LOG_ERROR("Exception while stopping auto-dimming. Error: ", LOG_BOLD, e.what());
    return false;
  }
}

// Button functions
void Button::addButtonEvent(bool state)
{
  // Do nothing if this channel is disabled.
  if (!this->config->enabled)
  {
    return;
  }
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
  // Do nothing if this button is disabled.
  if (!this->config->enabled)
  {
    return;
  }

  if (LightManager::instance)
  {
    // Invert read state as there is a PULLUP on the line.
    bool currentButtonState = !digitalRead(this->pin);
    if (!currentButtonState && currentButtonState != this->buttonEvents[0].state)
    {
      // The state went to LOW when it was previously HIGH, the button was released.
      LOG_DEBUG("(", millis(), ") ", "New state DEPRESSED from confirmed PRESSED. Skipping debounce checking for channel ", LOG_BOLD, this->config->channel);
      this->_currentButtonState.millis = millis();
      this->_currentButtonState.state = currentButtonState;
      this->_currentButtonState.handled = true;
      this->addButtonEvent(currentButtonState);
    }
    else if (currentButtonState && currentButtonState != this->_currentButtonState.state)
    {
      // Check that the current state is not the same as already confirmed
      // AND that the state is not the same as the last read state.
      LOG_DEBUG("(", millis(), ") ", "New state ", LOG_BOLD, currentButtonState ? "PRESSED" : "DEPRESSED", LOG_RESET_DECORATIONS, " for channel ", LOG_BOLD, this->config->channel);
      this->_currentButtonState.millis = millis();
      this->_currentButtonState.state = currentButtonState;
      this->_currentButtonState.handled = false;
    }
    else if (!this->_currentButtonState.handled && currentButtonState == this->_currentButtonState.state && millis() - this->_currentButtonState.millis >= LMANConfig::instance->buttonPressMinTime)
    {
      // Current state is not handled and it is confirmed as a read state and has been the same for button min press time
      LOG_DEBUG("(", millis(), ") ", "New state ", LOG_BOLD, currentButtonState ? "PRESSED" : "DEPRESSED", LOG_RESET_DECORATIONS, " confirmed for channel ", LOG_BOLD, this->config->channel);
      this->addButtonEvent(this->_currentButtonState.state);
      this->_currentButtonState.handled = true;
    }
  }
  else
  {
    LOG_ERROR("Trying to update button states. No need as no LightManager::instance exists!");
  }
}

bool Button::hasDeterminedState()
{
  if (this->config->enabled)
  {
    return this->_currentButtonState.handled;
  }
  else
  {
    return true; // If the button is disabled, the state is always determined as it will never change
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
  DMXChannel *dmxChannel = nullptr;
  for (DMXChannel &channel : LightManager::instance->dmxChannels)
  {
    if (channel.config->channel == buttonConfig->channel)
    {
      LOG_INFO("DMX Channel ", LOG_BOLD, buttonConfig->channel, LOG_RESET_DECORATIONS, " already in use and found.");
      dmxChannel = &channel;
      break;
    }
  }
  // If the dmx channel to be used was not found in the list, log error.
  if (dmxChannel == nullptr)
  {
    LOG_ERROR("Failed to find matching DMX channel. Will not initiate button!");
    return nullptr;
  }

  if (buttonConfig->enabled)
  {
    attachInterrupt(buttonPin, LightManager::ISRForwarder, CHANGE);
  }
  else
  {
    LOG_WARNING("Button on pin ", LOG_BOLD, buttonPin, LOG_RESET_DECORATIONS, " not enabled. Will not enabled interrupt!");
  }

  LOG_DEBUG("Initializing button on PIN ", LOG_BOLD, buttonPin);
  Button newBtn;
  newBtn.pin = buttonPin;
  newBtn.dmxChannel = dmxChannel;
  newBtn.config = buttonConfig;
  // Mark all events handled from the start.
  newBtn.buttonEvents[0].handled = true;
  newBtn.buttonEvents[1].handled = true;
  newBtn.buttonEvents[2].handled = true;
  pinMode(buttonPin, INPUT_PULLUP);
  this->buttons.push_back(newBtn);
  return &this->buttons.back();
}

DMXChannel *LightManager::initDMXChannel(ChannelConfig *config)
{
  LOG_INFO("Initiating DMX Channel ", LOG_BOLD, config->channel);
  DMXChannel newChannel;
  newChannel.state = false;
  newChannel.init(this->_dmxSendTask, this->_dmx, config);
  this->dmxChannels.push_back(newChannel);
  return &(*this->dmxChannels.end());
}

void LightManager::init(TaskHandle_t *dmxSendTask, DMXESPSerial *dmx)
{
  LightManager::instance = this;
  this->_dmxSendTask = dmxSendTask;
  this->_dmx = dmx;
  xTaskCreatePinnedToCore(_taskReadButtonStates, "taskReadButtonStates", 5000, NULL, 1, &this->_taskHandleReadButtonStates, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(taskProcessButtonEvents, "taskProcessButtonEvents", 5000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(_taskDimLights, "taskDimLights", 5000, NULL, 1, &this->_taskHandleDimLights, CONFIG_ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(_taskAutoDimLights, "taskAutoDimLights", 5000, NULL, 1, &this->_taskHandleAutoDimLights, CONFIG_ARDUINO_RUNNING_CORE);
}

void IRAM_ATTR LightManager::ISRForwarder()
{
  if (LightManager::instance && LightManager::instance->_taskHandleReadButtonStates)
  {
    vTaskNotifyGiveFromISR(LightManager::instance->_taskHandleReadButtonStates, NULL);
  }
}

void LightManager::_taskReadButtonStates(void *param)
{
  LOG_INFO("Started _taskReadButtonStates");

  uint8_t loopDelay = LMANConfig::instance->buttonPressMinTime;
  // Set the loop delay so that at least 3 readings will be done to determine
  // a state, if the value is to low set reading interval to 5 ms
  if (loopDelay / 3 >= 5)
  {
    loopDelay = loopDelay / 3;
  }
  else
  {
    loopDelay = 5;
  }
  LOG_DEBUG("Will poll button states every ", LOG_BOLD, loopDelay, LOG_RESET_DECORATIONS, " ms until determined state is set");

  for (;;)
  {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
    {
      LOG_TRACE("_taskReadButtonStates got notification.");
      if (LightManager::instance)
      {
        bool allButtonsHasDetminedState = false; // Keep looping over buttons and checking states until a determined state has been reached.
        while (!allButtonsHasDetminedState)
        {
          // Wait for next time to check
          vTaskDelay(loopDelay / portTICK_PERIOD_MS);
          allButtonsHasDetminedState = true; // Reset check. Assume all buttons have a determined state and only set to false
                                             // if a button is found to have an undetermined state.
          for (Button &btn : LightManager::instance->buttons)
          {
            btn.updateState();
            if (!btn.hasDeterminedState())
            {
              LOG_DEBUG("Button on PIN ", btn.pin, " has undetermined state, will check again in ", loopDelay, " ms");
              allButtonsHasDetminedState = false;
            }
          }
        }
        LOG_INFO("All buttons have reached a determined state, will wait for notification.");
      }
    }
    else
    {
      LOG_ERROR("Trying to read button states event though no LightManager::instance is set!");
    }
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
      for (Button &btn : LightManager::instance->buttons)
      {
        // Do initial filtering
        if (!btn.buttonEvents[0].handled && btn.getTimeDelta(0, 1) >= LMANConfig::instance->buttonPressMinTime)
        {
          if (btn.buttonEvents[0].state && btn.getTimeDeltaNowLastState() >= LMANConfig::instance->buttonPressMaxTime && btn.dmxChannel->state)
          {
            // State is high and has been for the max threshold time. Consider it a dimming event.
            // If a dimming task has been created, notify it and mark event as handled.
            if (LightManager::instance->_taskHandleDimLights)
            {
              // Reverse direction so that a release of the button and then press again reverses it.
              btn.dmxChannel->dimmingDirection = !btn.dmxChannel->dimmingDirection;
              btn.buttonEvents[0].handled = true;
              xTaskNotifyGive(LightManager::instance->_taskHandleDimLights);
            }
            else
            {
              LOG_ERROR("Dimming event occuring but no task started!");
            }
          }
          else if (!btn.buttonEvents[0].state && btn.buttonEvents[1].state && btn.getTimeDelta(0, 1) <= LMANConfig::instance->buttonPressMaxTime)
          {
            // State is low, previous state was high and the time between states
            // was lower than max and higher than min. This was a toggle press.
            if (btn.dmxChannel->state)
            {
              LOG_DEBUG("Slow turn off triggered for channel ", LOG_BOLD, btn.dmxChannel->config->channel);
              LightManager::instance->autoDimOff(&(*btn.dmxChannel));
            }
            else
            {
              LOG_DEBUG("Slow turn on triggered for channel ", LOG_BOLD, btn.dmxChannel->config->channel);
              LightManager::instance->autoDimOn(&(*btn.dmxChannel));
            }
            btn.buttonEvents[0].handled = true;
          }

          if (!unhandledButtonEvents && !btn.buttonEvents[0].handled)
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
        vTaskDelay(1 / portTICK_PERIOD_MS);
      }
    }
    else
    {
      LOG_ERROR("No LightManager::instance available!");
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

void LightManager::_taskDimLights(void *param)
{
  LOG_INFO("Started _taskDimLights");

  uint8_t fastestDimmingSpeed = 255;
  for (DMXChannel &channel : LightManager::instance->dmxChannels)
  {
    if (channel.config->enabled)
    {
      if (channel.config->dimmingSpeed < fastestDimmingSpeed && channel.config->dimmingSpeed > 0)
      {
        fastestDimmingSpeed = channel.config->dimmingSpeed;
      }
    }
  }

  for (;;)
  {
    bool hasDimmingJob = false;
    for (Button &btn : LightManager::instance->buttons)
    {
      // Latest state is high, dim the light
      if (btn.buttonEvents[0].state)
      {
        // Indicate that there is still a dimming job to do.
        if (!hasDimmingJob)
        {
          hasDimmingJob = true;
        }
        // Update dim level.
        DMXChannel *channel = btn.dmxChannel;
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
      vTaskDelay(fastestDimmingSpeed / portTICK_PERIOD_MS);
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
  if (dmxChannel->stopAutoDimming())
  {
    if (level > dmxChannel->config->max)
    {
      level = dmxChannel->config->max;
    }
    else if (level < dmxChannel->config->min)
    {
      level = dmxChannel->config->min;
    }

    LOG_DEBUG("Starting auto-dim to level: ", level);
    dmxChannel->autoDimmingTarget = level;
    dmxChannel->isAutoDimming = true;
    // Resume auto-dimming task
    xTaskNotifyGive(this->_taskHandleAutoDimLights);
  }
  else
  {
    LOG_ERROR("Failed to aquire stop current auto-dimming. Will not start auto-dimming!");
  }
}

void LightManager::autoDimOn(DMXChannel *dmxChannel)
{
  this->autoDimOnToLevel(dmxChannel, dmxChannel->level);
}

void LightManager::autoDimOnToLevel(DMXChannel *dmxChannel, uint8_t level)
{
  dmxChannel->turnOffWhenAutoDimComplete = false; // Do not turn off when auto-dim done.
  dmxChannel->setLevel(dmxChannel->config->min);  // Set the current level to min
  dmxChannel->setState(true);                     // Turn on the light
  LightManager::autoDimTo(dmxChannel, level);
}

void LightManager::autoDimOff(DMXChannel *dmxChannel)
{
  dmxChannel->turnOffWhenAutoDimComplete = true;                          // Turn off the light when dimming is complete
  dmxChannel->levelBeforeAutoDimming = dmxChannel->level;                 // Save the current level
  LightManager::instance->autoDimTo(dmxChannel, dmxChannel->config->min); // Dim to target
}

void LightManager::_taskAutoDimLights(void *param)
{
  LOG_INFO("Started _taskAutoDimLights");
  uint8_t fastestDimmingSpeed = 255;
  for (DMXChannel &channel : LightManager::instance->dmxChannels)
  {
    if (channel.config->enabled)
    {
      if (channel.config->autoDimmingSpeed < fastestDimmingSpeed && channel.config->autoDimmingSpeed > 0)
      {
        fastestDimmingSpeed = channel.config->autoDimmingSpeed;
      }
    }
  }

  for (;;)
  {
    bool hasAutoDimmingJob = false;
    for (DMXChannel &channel : LightManager::instance->dmxChannels)
    {
      if (channel.isAutoDimming)
      {
        if (channel.level > channel.autoDimmingTarget)
        {
          channel.setLevel(channel.level - 1);
        }
        else if (channel.level < channel.autoDimmingTarget)
        {
          channel.setLevel(channel.level + 1);
        }

        if (channel.turnOffWhenAutoDimComplete && channel.autoDimmingTarget == channel.level)
        {
          channel.setState(false);
          channel.turnOffWhenAutoDimComplete = false;
          // Reset level to what it was before auto-dimming started.
          LOG_DEBUG("Restoring previous level for DMX Channel: ", channel.levelBeforeAutoDimming);
          channel.setLevel(channel.levelBeforeAutoDimming);
        }
        else if (!hasAutoDimmingJob && channel.autoDimmingTarget != channel.level)
        {
          // Make sure we continue if more work is to be done.
          hasAutoDimmingJob = true;
        }
      }
    }
    if (hasAutoDimmingJob)
    {
      vTaskDelay(fastestDimmingSpeed / portTICK_PERIOD_MS);
    }
    else
    {
      LOG_INFO("All auto-dimming events done. Waiting for notification.");
      if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
      {
        LOG_DEBUG("_taskAutoDimLights got notification!");
      }
    }
  }
}