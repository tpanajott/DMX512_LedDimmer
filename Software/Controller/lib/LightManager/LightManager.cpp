#include <LightManager.h>
#include <DebugLog.h>

LightManager *LightManager::instance; // Give somewhere in ram for instance to exist

void LightManager::init(DMXESPSerial *dmx, PubSubClient *pubSubClient) {
    LightManager::instance = this;
    this->_dmx = dmx;
    LOG_INFO("LightManager initialized.");
}

void LightManager::initDimmerButton(uint8_t id, uint8_t pin, uint8_t min, uint8_t max, uint8_t channel, bool enabled, char *name) {
    this->dimmerButtons[id].pin = pin;
    this->dimmerButtons[id].min = min;
    this->dimmerButtons[id].max = max;
    this->dimmerButtons[id].dimLevel = max;
    this->dimmerButtons[id].channel = channel;
    this->dimmerButtons[id].enabled = enabled;
    this->dimmerButtons[id].name = name;

    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), LightManager::ISRForwarder, CHANGE);
    LOG_INFO("Initialized dimmerButton[", id ,"]");
}

void LightManager::handleDimmerButtonEvent() {
    LOG_TRACE("Processing button events.");
    for(unsigned int i = 0; i < 4; i++) {
        if(this->dimmerButtons[i].enabled) {
            bool read_state = (digitalRead(this->dimmerButtons[i].pin) == 0);
            if(millis() - this->dimmerButtons[i]._buttonEvents[0].millis >= this->dimmerButtons[i].debounce_ms && read_state != this->dimmerButtons[i]._buttonEvents[0].pressed) {
                // Shift current button events in button down
                this->dimmerButtons[i]._buttonEvents[2] = this->dimmerButtons[i]._buttonEvents[1];
                this->dimmerButtons[i]._buttonEvents[1] = this->dimmerButtons[i]._buttonEvents[0];
                // Update buttonEvent[0] to current state
                this->dimmerButtons[i]._buttonEvents[0].pressed = read_state;
                this->dimmerButtons[i]._buttonEvents[0].millis = millis();
                // Make button due for status calculation
                this->dimmerButtons[i].hasButtonEvent = true;
                LOG_TRACE("DimmerButton[", i, "] changed state to ", read_state ? 1 : 0);
            }
        }
    }
}

// Loop through and do any needed updates
void LightManager::loop() {
    for(int i = 0; i < 4; i++) {
        // If button is enabled and has changed, update status.
        if(this->dimmerButtons[i].enabled) {
            if(this->dimmerButtons[i].hasButtonEvent) {
                this->_updateDimmerButtonStatus(i);
            } else if (this->dimmerButtons[i].isDimming) {
                this->_performDimmingOfLight(i);
            } else if (this->dimmerButtons[i].isAutoDimming) {
                this->_performAutoDimmingOfLight(i);
            }
        }
    }
}

// Check button states and perform appropriate actions depending on
// current state, previous state and timing.
void LightManager::_updateDimmerButtonStatus(uint8_t buttonIndex) {
    // Do initial filtering. Nothing should be processed if the button press was less than the min defined theshold.
    unsigned long timeDelta = this->_getButtonStateTimeDelta(buttonIndex, 0, 1);
    if(timeDelta >= this->dimmerButtons[buttonIndex].buttonMinPressThreshold) {
        if(!this->_getButtonState(buttonIndex) && timeDelta <= this->dimmerButtons[buttonIndex].buttonPressThreshold) {
            // The button was held for less than the max pressThreshold and should be considered a press
            // if the current state is "not pressed".
            this->setOutputState(buttonIndex, !this->dimmerButtons[buttonIndex].outputState); // Invert current output state.
            this->dimmerButtons[buttonIndex].hasChanged = true;
            this->dimmerButtons[buttonIndex].hasButtonEvent = false;
            LOG_DEBUG("DimmerButton[", buttonIndex, "] is currently ", this->dimmerButtons[buttonIndex].outputState ? "ON" : "OFF");
        } else if(this->dimmerButtons[buttonIndex].outputState) {
            // The output is on, check for start/stop of dimming events.
            if(this->_getButtonState(buttonIndex) && millis() - this->dimmerButtons[buttonIndex]._buttonEvents[0].millis >= this->dimmerButtons[buttonIndex].buttonPressThreshold) {
                // The button is still pressed and has been for longer than the max pressThreshold, therefore start dimming.
                // Mark the buttonEvent as handled but start dimming the light.
                this->dimmerButtons[buttonIndex].hasButtonEvent = false;
                this->dimmerButtons[buttonIndex].isDimming = true;
                LOG_DEBUG("DimmerButton[", buttonIndex, "] start dimming.");
            } else if(!this->_getButtonState(buttonIndex) && timeDelta > this->dimmerButtons[buttonIndex].buttonPressThreshold) {
                // The button is not pressed and the press was longer than the max pressThreshold, therfore stop dimming.
                // Mark the buttonEvent as handled and stop dimming the light.
                // Also change direction of dimming to next time.
                this->dimmerButtons[buttonIndex].hasButtonEvent = false;
                this->dimmerButtons[buttonIndex].isDimming = false;
                this->dimmerButtons[buttonIndex].direction = !this->dimmerButtons[buttonIndex].direction;
                LOG_DEBUG("DimmerButton[", buttonIndex, "] stop dimming.");
            }
        }
    }
}

// The light is set to currently dim. Loop through dimming
// until the flag for dimming is not set.
void LightManager::_performDimmingOfLight(uint8_t buttonIndex) {
    // Disable any autoDimming that is currently happening
    this->dimmerButtons[buttonIndex].isAutoDimming = false;
    if(millis() - this->dimmerButtons[buttonIndex].lastDimEvent >= this->dimmerButtons[buttonIndex].dimmingSpeed) {
        // Wait between each dimming event as set by the dimmingSpeed variable.
        if(this->dimmerButtons[buttonIndex].dimLevel > this->dimmerButtons[buttonIndex].min && this->dimmerButtons[buttonIndex].dimLevel < this->dimmerButtons[buttonIndex].max) {
            // Only change dimLevel if between positions. Handle edge cases in "else if" below as required by holdPeriod.
            if (this->dimmerButtons[buttonIndex].direction) {
                this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].dimLevel + 1);
            } else {
                this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].dimLevel - 1);
            }
            // Update when the last dimming event occured.
            this->dimmerButtons[buttonIndex].lastDimEvent = millis();
        } else if (this->dimmerButtons[buttonIndex].dimLevel <= this->dimmerButtons[buttonIndex].min || this->dimmerButtons[buttonIndex].dimLevel >= this->dimmerButtons[buttonIndex].max) {
            // Handle edge cases here.

            // Current level is equal to min or max, hold until holdPeriod is over.
            if(millis() - this->dimmerButtons[buttonIndex].lastDimEvent >= this->dimmerButtons[buttonIndex].holdPeriod) {
                // Hold period is over, change direction and resume regular dimming
                if(this->dimmerButtons[buttonIndex].dimLevel <= this->dimmerButtons[buttonIndex].min) {
                    // We are currently at min, invert direction and set current dimming to min + 1.
                    // The rest of the dimming is handled as usual above.
                    this->dimmerButtons[buttonIndex].direction = true;
                    this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].min + 1);
                } else {
                    this->dimmerButtons[buttonIndex].direction = false;
                    this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].max - 1);
                }
                // Update when the last dimming event occured.
                this->dimmerButtons[buttonIndex].lastDimEvent = millis();
                
                LOG_DEBUG("dimmerButton[", buttonIndex, "] hold period expiered.");
                LOG_DEBUG("dimmerButton[", buttonIndex, "] Direction", this->dimmerButtons[buttonIndex].direction ? "UP" : "DOWN");
                LOG_DEBUG("dimmerButton[", buttonIndex, "] Min", this->dimmerButtons[buttonIndex].min);
                LOG_DEBUG("dimmerButton[", buttonIndex, "] Max", this->dimmerButtons[buttonIndex].max);
                LOG_DEBUG("dimmerButton[", buttonIndex, "] Level", this->dimmerButtons[buttonIndex].dimLevel);
            }
        }
    }
}

void LightManager::_performAutoDimmingOfLight(uint8_t buttonIndex) {
    if(millis() - this->dimmerButtons[buttonIndex].lastDimEvent >= this->dimmerButtons[buttonIndex].dimmingSpeed) {
        if(this->dimmerButtons[buttonIndex].dimLevel < this->dimmerButtons[buttonIndex].autoDimTarget) {
            this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].dimLevel + 1);
        } else if(this->dimmerButtons[buttonIndex].dimLevel > this->dimmerButtons[buttonIndex].autoDimTarget) {
            this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].dimLevel - 1);
        } else {
            // We arrived at the level, stop autoDimming.
            this->dimmerButtons[buttonIndex].isAutoDimming = false;
        }

        this->dimmerButtons[buttonIndex].lastDimEvent = millis();
    }
}

// Return the current state of a button.
// True: button is pressed.
// False: button is not pressed.
bool LightManager::_getButtonState(uint8_t buttonIndex) {
    return this->dimmerButtons[buttonIndex]._buttonEvents[0].pressed;
}

// Get a time delta between two button events on a dimmerButton.
unsigned long LightManager::_getButtonStateTimeDelta(uint8_t buttonIndex, uint8_t firstEventIndex, uint8_t secondEventIndex) {
    return this->dimmerButtons[buttonIndex]._buttonEvents[firstEventIndex].millis - this->dimmerButtons[buttonIndex]._buttonEvents[secondEventIndex].millis;
}

// Set the light level of a dimmerButton. Will clamp to min/max levels.
void LightManager::setLightLevel(uint8_t buttonIndex, uint8_t dimLevel) {
    if(dimLevel < this->dimmerButtons[buttonIndex].min) {
        dimLevel = this->dimmerButtons[buttonIndex].min;
    } else if(dimLevel > this->dimmerButtons[buttonIndex].max) {
        dimLevel = this->dimmerButtons[buttonIndex].max;
    }
    this->dimmerButtons[buttonIndex].dimLevel = dimLevel;
    this->dimmerButtons[buttonIndex].hasChanged = true;

    // If the output is on, write the new lightLevel to DMX and mark due for MQTT status update.
    if(this->dimmerButtons[buttonIndex].outputState) {
        this->_dmx->write(this->dimmerButtons[buttonIndex].channel, dimLevel);
        this->dimmerButtons[buttonIndex].sendMqttUpdate = true;
    }
}

// Set the light level of a dimmerButton. Will clamp to min/max levels.
void LightManager::setAutoDimmingTarget(uint8_t buttonIndex, uint8_t dimLevel) {
    // Make direction the inverse of dimming direction.
    this->dimmerButtons[buttonIndex].direction = (this->dimmerButtons[buttonIndex].dimLevel <= dimLevel);
    
    if(this->dimmerButtons[buttonIndex].outputState) {
        // If the light is currently on, set the dimming target and auto-dim.
        if(dimLevel < this->dimmerButtons[buttonIndex].min) {
            dimLevel = this->dimmerButtons[buttonIndex].min;
        } else if(dimLevel > this->dimmerButtons[buttonIndex].max) {
            dimLevel = this->dimmerButtons[buttonIndex].max;
        }
        this->dimmerButtons[buttonIndex].autoDimTarget = dimLevel;
        this->dimmerButtons[buttonIndex].isAutoDimming = true;
        this->dimmerButtons[buttonIndex].sendMqttUpdate = true;
    } else {
        // Light if currently off, turn it on on its lowest setting
        // and then slowly dim up to target.
        this->setLightLevel(buttonIndex, this->dimmerButtons[buttonIndex].min);
        this->setOutputState(buttonIndex, true);
        this->setAutoDimmingTarget(buttonIndex, dimLevel);
    }
}

void LightManager::setOutputState(uint8_t buttonIndex, bool outputState) {
    // TODO: Make slowly turn on/off
    this->dimmerButtons[buttonIndex].outputState = outputState;
    this->dimmerButtons[buttonIndex].hasChanged = true;

    // Update DMX data.
    if(outputState) {
        this->_dmx->write(this->dimmerButtons[buttonIndex].channel, this->dimmerButtons[buttonIndex].dimLevel);
    } else {
        this->_dmx->write(this->dimmerButtons[buttonIndex].channel, 0);
    }
    // Mark light due for MQTT status update.
    this->dimmerButtons[buttonIndex].sendMqttUpdate = true;
}

// Static function to forward interrupts from dimmer buttons to instance
void IRAM_ATTR LightManager::ISRForwarder() {
    if(instance) {
        instance->handleDimmerButtonEvent();
    }
}