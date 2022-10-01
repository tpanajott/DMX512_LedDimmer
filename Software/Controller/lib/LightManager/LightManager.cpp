#include <LightManager.h>
#include <iterator>
#include <ArduLog.h>

LightManager *LightManager::instance; // Give somewhere in ram for instance to exist

void LightManager::init(DMXESPSerial *dmx, PubSubClient *pubSubClient) {
    LightManager::instance = this;
    this->_dmx = dmx;
    LOG_INFO("LightManager initialized.");
}

DimmerButton* LightManager::initDimmerButton(uint8_t id, uint8_t pin, uint8_t min, uint8_t max, uint8_t channel, bool enabled, char *name) {
    DimmerButton btn {
        .id = id,
        .channel = channel,
        .pin = pin,
        .min = min,
        .max = max,
        .enabled = enabled,
        .name = name
    };
    this->dimmerButtons.push_back(btn);

    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), LightManager::ISRForwarder, CHANGE);
    LOG_INFO("Initialized dimmerButton[", btn.id ,"]");

    return this->getLightById(id);
}

void LightManager::handleDimmerButtonEvent() {
    LOG_TRACE("Processing button events.");
    for (std::list<DimmerButton>::iterator it = this->dimmerButtons.begin(); it != this->dimmerButtons.end(); ++it){
        if(it->enabled) {
            bool read_state = (digitalRead(it->pin) == 0);
            if(millis() - it->_buttonEvents[0].millis >= it->debounce_ms && read_state != it->_buttonEvents[0].pressed) {
                // Shift current button events in button down
                it->_buttonEvents[2] = it->_buttonEvents[1];
                it->_buttonEvents[1] = it->_buttonEvents[0];
                // Update buttonEvent[0] to current state
                it->_buttonEvents[0].pressed = read_state;
                it->_buttonEvents[0].millis = millis();
                // Make button due for status calculation
                it->hasButtonEvent = true;
                LOG_TRACE("DimmerButton[", it->id, "] changed state to ", read_state ? 1 : 0);
            }
        }
    }
}

// Loop through and do any needed updates
void LightManager::loop() {
    for (std::list<DimmerButton>::iterator it = this->dimmerButtons.begin(); it != this->dimmerButtons.end(); ++it){
        // If button is enabled and has changed, update status.
        if(it->enabled) {
            if(it->hasButtonEvent) {
                this->_updateDimmerButtonStatus(&(*it));
            } else if (it->isDimming) {
                this->_performDimmingOfLight(&(*it));
            } else if (it->isAutoDimming) {
                this->_performAutoDimmingOfLight(&(*it));
            } else if (it->smoothOff) {
                this->_performSmoothOff(&(*it));
            }
        }
    }
}

// Check button states and perform appropriate actions depending on
// current state, previous state and timing.
void LightManager::_updateDimmerButtonStatus(DimmerButton *btn) {
    // Do initial filtering. Nothing should be processed if the button press was less than the min defined theshold.
    unsigned long timeDelta = this->_getButtonStateTimeDelta(btn, 0, 1);
    if(timeDelta >= btn->buttonMinPressThreshold) {
        if(!btn->_buttonEvents[0].pressed && timeDelta <= btn->buttonPressThreshold) {
            // The button was held for less than the max pressThreshold and should be considered a press
            // if the current state is "not pressed".
            this->setOutputState(btn, !btn->outputState); // Invert current output state.
            btn->hasChanged = true;
            btn->hasButtonEvent = false;
            LOG_DEBUG("DimmerButton[", btn->id, "] is currently ", btn->outputState ? "ON" : "OFF");
        } else if(btn->outputState) {
            // The output is on, check for start/stop of dimming events.
            if(btn->_buttonEvents[0].pressed && millis() - btn->_buttonEvents[0].millis >= btn->buttonPressThreshold) {
                // The button is still pressed and has been for longer than the max pressThreshold, therefore start dimming.
                // Mark the buttonEvent as handled but start dimming the light.
                btn->hasButtonEvent = false;
                btn->isDimming = true;
                LOG_DEBUG("DimmerButton[", btn->id, "] start dimming.");
            } else if(!btn->_buttonEvents[0].pressed && timeDelta > btn->buttonPressThreshold) {
                // The button is not pressed and the press was longer than the max pressThreshold, therfore stop dimming.
                // Mark the buttonEvent as handled and stop dimming the light.
                // Also change direction of dimming to next time.
                btn->hasButtonEvent = false;
                btn->isDimming = false;
                btn->direction = !btn->direction;
                LOG_DEBUG("DimmerButton[", btn->id, "] stop dimming.");
            }
        }
    }
}

// The light is set to currently dim. Loop through dimming
// until the flag for dimming is not set.
void LightManager::_performDimmingOfLight(DimmerButton *btn) {
    // Disable any autoDimming that is currently happening
    btn->isAutoDimming = false;
    if(millis() - btn->lastDimEvent >= btn->dimmingSpeed) {
        // Wait between each dimming event as set by the dimmingSpeed variable.
        if(btn->dimLevel > btn->min && btn->dimLevel < btn->max) {
            // Only change dimLevel if between positions. Handle edge cases in "else if" below as required by holdPeriod.
            if (btn->direction) {
                this->setLightLevel(btn, btn->dimLevel + 1);
            } else {
                this->setLightLevel(btn, btn->dimLevel - 1);
            }
            // Update when the last dimming event occured.
            btn->lastDimEvent = millis();
        } else if (btn->dimLevel <= btn->min || btn->dimLevel >= btn->max) {
            // Handle edge cases here.

            // Current level is equal to min or max, hold until holdPeriod is over.
            if(millis() - btn->lastDimEvent >= btn->holdPeriod) {
                // Hold period is over, change direction and resume regular dimming
                if(btn->dimLevel <= btn->min) {
                    // We are currently at min, invert direction and set current dimming to min + 1.
                    // The rest of the dimming is handled as usual above.
                    btn->direction = true;
                    this->setLightLevel(btn, btn->min + 1);
                } else {
                    btn->direction = false;
                    this->setLightLevel(btn, btn->max - 1);
                }
                // Update when the last dimming event occured.
                btn->lastDimEvent = millis();
                
                LOG_DEBUG("dimmerButton[", btn->id, "] hold period expiered.");
                LOG_DEBUG("dimmerButton[", btn->id, "] Direction ", btn->direction ? "UP" : "DOWN");
                LOG_DEBUG("dimmerButton[", btn->id, "] Min ", btn->min);
                LOG_DEBUG("dimmerButton[", btn->id, "] Max ", btn->max);
                LOG_DEBUG("dimmerButton[", btn->id, "] Level ", btn->dimLevel);
            }
        }
    }
}

void LightManager::_performAutoDimmingOfLight(DimmerButton *btn) {
    if(millis() - btn->lastDimEvent >= btn->autoDimmingSpeed) {
        if(btn->dimLevel < btn->autoDimTarget) {
            this->setLightLevel(btn, btn->dimLevel + 1);
        } else if(btn->dimLevel > btn->autoDimTarget) {
            this->setLightLevel(btn, btn->dimLevel - 1);
        } else {
            // We arrived at the level, stop autoDimming.
            btn->isAutoDimming = false;
        }

        btn->lastDimEvent = millis();
    }
}

// Get a time delta between two button events on a dimmerButton.
unsigned long LightManager::_getButtonStateTimeDelta(DimmerButton *btn, uint8_t firstEventIndex, uint8_t secondEventIndex) {
    return btn->_buttonEvents[firstEventIndex].millis - btn->_buttonEvents[secondEventIndex].millis;
}

// Set the light level of a dimmerButton. Will clamp to min/max levels.
void LightManager::setLightLevel(DimmerButton *btn, uint8_t dimLevel) {
    if(dimLevel < btn->min) {
        dimLevel = btn->min;
    } else if(dimLevel > btn->max) {
        dimLevel = btn->max;
    }
    btn->dimLevel = dimLevel;
    btn->outputState = true;
    btn->hasChanged = true;

    // If the output is on, write the new lightLevel to DMX and mark due for MQTT status update.
    if(btn->outputState) {
        btn->sendMqttUpdate = true;
        this->_dmx->write(btn->channel, dimLevel);
    }
}

// Set the light level of a dimmerButton. Will clamp to min/max levels.
void LightManager::setAutoDimmingTarget(DimmerButton *btn, uint8_t dimLevel) {
    // Make direction the inverse of dimming direction.
    btn->direction = (btn->dimLevel <= dimLevel);
    
    if(btn->outputState) {
        // If the light is currently on, set the dimming target and auto-dim.
        if(dimLevel < btn->min) {
            // Clam to min level if a lower value was given.
            dimLevel = btn->min;
        } else if(dimLevel > btn->max) {
            // Clam to max level if a higher value was given.
            dimLevel = btn->max;
        }
        btn->autoDimTarget = dimLevel;
        btn->isAutoDimming = true;
        btn->sendMqttUpdate = true;
    } else {
        // Light if currently off, turn it on on its lowest setting
        // and then slowly dim up to target.
        this->setLightLevel(btn, btn->min);
        this->setOutputState(btn, true);
        this->setAutoDimmingTarget(btn, dimLevel);
    }
}

void LightManager::setOutputState(DimmerButton *btn, bool outputState) {
    if(btn->outputState && !outputState) {
        LOG_DEBUG("Setting SmoothOff on DimmerButton");
        btn->dimLevelBeforeSmoothOff = btn->dimLevel;
        btn->smoothOff = true;
        btn->hasChanged = true;
    } else if (!btn->outputState && outputState && !btn->isAutoDimming) {
        // Get the current light level.
        uint8_t lightLevel = btn->dimLevel;
        // Turn the light on on it's min level
        this->setLightLevel(btn, btn->min);
        // Set to auto dim up to the previosly set level.
        this->setAutoDimmingTarget(btn, lightLevel);
    }

    // Mark light due for MQTT status update.
    btn->sendMqttUpdate = true;
}

// Smootly dim down to min level and then turn off output.
void LightManager::_performSmoothOff(DimmerButton *btn) {
    if(btn->dimLevel == btn->min) {
        LOG_DEBUG("DimmerButton reached min level and is smooth off. Will turn off output.");
        btn->outputState = false;
        this->_dmx->write(btn->channel, 0);
        btn->dimLevel = btn->dimLevelBeforeSmoothOff; // Restore dimLevel
        btn->smoothOff = false;
    } else {
        this->setAutoDimmingTarget(btn, btn->min);
    }
}

// Try to find a DimmerButton with a given ID.
// If none is found, NULL is returned.
DimmerButton* LightManager::getLightById(uint8_t id) {
    LOG_TRACE("Trying to find a DimmerButton with ID ", id);
    // Loop through all elements via iterator
    for (std::list<DimmerButton>::iterator it = this->dimmerButtons.begin(); it != this->dimmerButtons.end(); ++it){
        LOG_TRACE("Foud DimmerButton with ID ", (*it).id);
        if((*it).id == id) {
            LOG_TRACE("Foud matching DimmerButton with ID ", id);
            return &(*it);
        }
    }

    LOG_ERROR("Did not find a matching DimmerButton with ID ", id);
    return NULL;
}

// Static function to forward interrupts from dimmer buttons to instance
void IRAM_ATTR LightManager::ISRForwarder() {
    if(instance) {
        instance->handleDimmerButtonEvent();
    }
}