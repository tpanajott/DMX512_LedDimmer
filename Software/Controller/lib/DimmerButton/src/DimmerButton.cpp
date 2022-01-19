#include "DimmerButton.h"
#include "Arduino.h"
#include "Button.h"

DimmerButton::DimmerButton(unsigned int pin) : _button(pin) {
    _pin = pin;
}

void DimmerButton::Begin(unsigned int min, unsigned int max, bool pullUp, unsigned int channel, bool enabled, char *name) {
    _button.begin();
    _min = min;
    _max = max;
    _level = _max;
    _lastOnLevel = _level;
    _dimmingSpeed = 15;
    _dimmingTargetSpeed = 2;
    _holdPeriod = 800;
    _direction = false;
    _holdPositionReaced = false;
    _buttonPressThreshold = 800;
    _buttonMinPressThreshold = 80;
    _outputState = false;
    _channel = channel;
    _enabled = enabled;
    _lastLoopState = _button.read();
    _name = name;
    pinMode(_pin, pullUp ? INPUT_PULLUP : INPUT);

    // String str_name = name;
    // ESP_LOGV("NAME", str_name);

    Serial.printf("[BUTTON] Button for pin %i initialized.\n", _pin);
    if(!_enabled) {
        Serial.printf("[BUTTON] Button for pin %i is disabled.\n", _pin);
    }
}

char* DimmerButton::getName() {
    return _name;
}

int DimmerButton::getLevel() {
    return _level;
}

int DimmerButton::getMinLevel() {
    return _min;
}

int DimmerButton::getMaxLevel() {
    return _max;
}

void DimmerButton::setDirection(bool direction) {
    _direction = direction;
}

void DimmerButton::setHoldPeriod(unsigned int holdPeriod) {
    _holdPeriod = holdPeriod;
}

void DimmerButton::setDimmingSpeed(unsigned int dimmingSpeed) {
    _dimmingSpeed = dimmingSpeed;
}

void DimmerButton::setAutoDimmingSpeed(unsigned int dimmingSpeed) {
    _dimmingTargetSpeed = dimmingSpeed;
}

void DimmerButton::setButtonPressMinMillis(unsigned int millis) {
    _buttonMinPressThreshold = millis;
}

void DimmerButton::setButtonPressMaxMillis(unsigned int millis) {
    _buttonPressThreshold = millis;
}

bool DimmerButton::hasChanged() {
    if(_hasChanged) {
        _hasChanged = false;
        return true;
    }
    return false;
}

bool DimmerButton::sendStatusUpdate() {
    if(_sendStatusUpdate) {
        _sendStatusUpdate = false;
        return true;
    }
    return false;
}

unsigned int DimmerButton::GetChannel() {
    return _channel;
}

void DimmerButton::setLevel(unsigned int level) {
    if(level < _min) {
        level = _min;
    } else if (level > _max) {
        level = _max;
    }
    _level = level;
    _hasChanged = true;
}

void DimmerButton::setDimmingTarget(unsigned int target) {
    if(target > _max) {
        target = _max;
    } else if(target < _min) {
        target = _min;
    }
    _dimmingTarget = target;
    _dimmingTargetReached = false;
}

void DimmerButton::setOutputState(bool outputState) {
    if(outputState == _outputState) return;
    if(outputState) {
        // If this is a turn ON event
        if(_dimmingTargetSpeed > 0) {
            _dimmingTarget = _lastOnLevel; // Set the target to the current level.
            _dimmingTargetReached = false; // Indicate that we should dim up from this point
            _level = 0; // Set the current level to 0
            _outputState = true;
        } else {
            _level = _lastOnLevel;
            _outputState = true;
        }
    } else {
        // This is a turn OFF event
        if(_dimmingTargetSpeed > 0) {
            _lastOnLevel = _level;
            _dimmingTarget = 0; // Set the target to 0. In the dimming action, when target 0 is reached it is turned off with _outputState = false;
            _dimmingTargetReached = false; // Indicate that we should dim down from this point
        } else {
            _level = 0;
            _outputState = false;
        }
    }
    _hasChanged = true;
    _sendStatusUpdate = true;
}

bool DimmerButton::getOutputState() {
    return _outputState;
}

bool DimmerButton::getEnabled() {
    return _enabled;
}

// Shifts button events down so that a new event can be recorded to the first postition.
void DimmerButton::_shiftButtonEventsDown() {
    _buttonEvents[2] = _buttonEvents[1];
    _buttonEvents[1] = _buttonEvents[0];
}

void DimmerButton::update() {
    // If the button is not enabled. Do not perform update functions.
    if(!_enabled) {
        return;
    }
    
    // Check if a new event has happened, if that is the case, shift all registered events down and register the
    // new event at the top of the "event stack"
    if((_button.read() == Button::PRESSED) != _buttonEvents[0].state) {
        _shiftButtonEventsDown();
        _buttonEvents[0].state = (_button.read() == Button::PRESSED);
        _buttonEvents[0].millis = millis();
        // Cancel any auto dimming if currently running
        _dimmingTargetReached = true;
        // DEBUG OUTPUT:
        // Serial.printf("[BUTTON] New event. Button is %s\n", _button.read() == Button::PRESSED ? "PRESSED" : "RELEASED");
        // Serial.printf("[BUTTON] 0: %s\n", _buttonEvents[0].state ? "PRESSED" : "RELEASED");
        // Serial.printf("[BUTTON] 1: %s\n", _buttonEvents[1].state? "PRESSED" : "RELEASED");
        // Serial.printf("[BUTTON] 2: %s\n", _buttonEvents[2].state? "PRESSED" : "RELEASED");
        // Serial.printf("[BUTTON] --------------\n");
        
        // If the current state of the button is released, check if the last state was pressed
        // and if the time difference between the events was less than the threshold for a short press
        // and longer than the minimum time to hold button and in that case, invert the output state
        if(!_buttonEvents[0].state && _buttonEvents[1].state && _buttonEvents[0].millis - _buttonEvents[1].millis > _buttonMinPressThreshold) {
            if(_buttonEvents[0].millis - _buttonEvents[1].millis <= _buttonPressThreshold) {
                // Delta time between events was within button press event threshold
                setOutputState(!_outputState);
                // _outputState = !_outputState;
                // _hasChanged = true;
                // Serial.printf("[BUTTON] State: %s\n", _outputState ? "ON" : "OFF");
            } else if (_buttonEvents[0].millis - _buttonEvents[1].millis > _buttonPressThreshold) {
                // Delta time between events was to long for a press and therefore is a dimming event
                // As this is the button release after a dimming event, invert direction and send the status update
                _direction = !_direction;
                _sendStatusUpdate = true;
                Serial.println("Dimming done. Send update!");
                // Serial.println("[BUTTON] Inverter direction.");
            }
        }
    } else if (_buttonEvents[0].state && _outputState) {
        // Button is being held down and has been held down for longer than a simple press
        // and output is enabled
        // Cancel any auto dimming if currently running
        _dimmingTargetReached = true;
        if(millis() - _buttonEvents[0].millis > _buttonPressThreshold) {
            if(millis() - _lastDimEvent > _dimmingSpeed) {
                // If direction is true/positive, increase brightness
                if(_direction && !_holdPositionReaced) {
                    if(_level < _max) {
                        _level += 1;
                        _hasChanged = true;
                        // Serial.printf("[BUTTON] Dimlevel: %i\n", _level);
                        
                        // Have we reached the max position, if so, flag to hold position before changing direction.
                        if(_level == _max && !_holdPositionReaced) {
                            _lastHoldPositionReached = millis();
                            _holdPositionReaced = true;
                        }
                    }
                    // Hold at max position for given time
                } else if (!_direction && !_holdPositionReaced) {
                    // If direction is false/decrease, increase brightness
                    if(_level > _min) {
                        _level -= 1;
                        _hasChanged = true;
                        // Serial.printf("[BUTTON] Dimlevel: %i\n", _level);

                        // Have we reached the min position, if so, flag to hold position before changing direction.
                        if(_level == _min && !_holdPositionReaced) {
                            _lastHoldPositionReached = millis();
                            _holdPositionReaced = true;
                        }
                    }
                } else if (_holdPositionReaced && millis() - _lastHoldPositionReached >= _holdPeriod) {
                    // Hold position has been reached and the hold time has expiered, reset hold position flag
                    // and invert direction
                    _holdPositionReaced = false;
                    _direction = !_direction;
                }
                _lastDimEvent = millis();
            }
        }
    } else if (!_dimmingTargetReached && _outputState) {
        // A dimming target has been set but the target has not been reached.
        // If it is time for a new dimming event, do it.
        if(millis() - _lastDimEvent > _dimmingTargetSpeed) {
            if(_dimmingTarget > _level) {
                _level++;
                _hasChanged = true;
                _lastDimEvent = millis();
            } else if(_dimmingTarget < _level) {
                _level--;
                _hasChanged = true;
                _lastDimEvent = millis();
            }

            // If the target has been reached, stop the auto dimming and send the status update.
            if(_dimmingTarget == _level) {
                _dimmingTargetReached = true;
                _sendStatusUpdate = true;

                // If the level is 0, turn off the light
                if(_level <= 0) {
                    _outputState = false;
                    _sendStatusUpdate = true;
                }
            }
        }
    }
}