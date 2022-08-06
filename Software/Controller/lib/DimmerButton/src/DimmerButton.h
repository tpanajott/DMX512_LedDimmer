#ifndef DIMMER_BUTTON_H
#define DIMMER_BUTTON_H
#include <Arduino.h>
#include "Button.h"

struct ButtonEvent {
    bool state = false; // Default value represent released button
    unsigned long millis = 0;
};

class DimmerButton {
    public:
        DimmerButton(unsigned int pin);
        void Begin(unsigned int min, unsigned int max, bool pullUp, unsigned int channel, bool enabled, char *name);
        int getLevel();
        int getMinLevel();
        int getMaxLevel();
        void setHoldPeriod(unsigned int holdPeriod);
        void setDimmingSpeed(unsigned int updateDelay);
        void setAutoDimmingSpeed(unsigned int updateDelay);
        void setDimmingTarget(unsigned int target);
        void setButtonPressMinMillis(unsigned int millis);
        void setButtonPressMaxMillis(unsigned int millis);
        void setDirection(bool direction);
        void setLevel(unsigned int level);
        unsigned int GetChannel();
        bool hasChanged();
        bool sendStatusUpdate();
        bool getOutputState();
        bool getEnabled();
        char* getName();
        void setOutputState(bool outputState);
        void update();
    private:
        ButtonEvent _buttonEvents[3];
        char *_name;
        void _shiftButtonEventsDown();
        unsigned long _lastDimEvent;
        unsigned long _lastHoldPositionReached;
        unsigned int _dimmingSpeed;
        unsigned int _dimmingTargetSpeed;
        unsigned int _min;
        unsigned int _level;
        unsigned int _lastOnLevel;
        unsigned int _max;
        unsigned int _pin;
        unsigned int _directionChangeThreshold;
        unsigned int _buttonMinPressThreshold;
        unsigned int _buttonPressThreshold;
        unsigned int _holdPeriod;
        unsigned int _channel;
        unsigned int _dimmingTarget;
        bool _dimmingTargetReached;
        bool _holdPositionReaced;
        bool _direction;
        bool _hasChanged;
        bool _sendStatusUpdate;
        bool _lastLoopState;
        bool _outputState;
        bool _enabled;
        Button _button;
};

#endif