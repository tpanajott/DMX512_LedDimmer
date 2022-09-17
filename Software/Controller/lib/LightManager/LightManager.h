#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include <Arduino.h>
#include <ESPDMX.h>
#include <PubSubClient.h>

struct _buttonEvent {
    bool pressed = false;
    unsigned long millis = 0;
};

struct DimmerButton {
    uint8_t channel = 1;
    uint8 pin = 0;
    uint8 min = 0;
    uint8 max = 255;
    uint8 dimLevel = 255;
    uint8 autoDimTarget = 255;
    uint8 debounce_ms = 50;
    unsigned long lastDimEvent = 0;
    uint8_t dimmingSpeed = 5;
    uint16_t holdPeriod = 800;
    // Every button press that is longer than the minPressThreshold and shorter than the buttonPressThreshold is considered
    // a press. If the button is held longer than the buttonPressThreshold is it considered to be held and the light
    // should be dimmed if turned on.
    uint16_t buttonPressThreshold = 800;
    uint16_t buttonMinPressThreshold = 100;
    // MQTT 
    bool sendMqttUpdate = false;
    uint16_t mqttUpdateInterval = 250; // Send MQTT updates with maximum this interval in ms
    unsigned long lastMqttUpdate;
    // False: Dimming down.
    // True: Dimming up.
    bool direction = true;
    bool outputState = false;
    // PCB support 4 buttons. Should this button be enabled, ie. is it connected to anything.
    // If not, setting enabled to false will stop processing of this button.
    bool enabled = false;
    bool hasChanged = false;
    // Indicate that a new button event has occured.
    bool hasButtonEvent = false;
    bool isDimming = false; // Is this button set to dimming.
    bool isAutoDimming = false; // Is autoDimming from MQTT/web command.
    char* name;
    _buttonEvent _buttonEvents[3];
};

class LightManager {
    public:
        DimmerButton dimmerButtons[4];
        void init(DMXESPSerial *dmx, PubSubClient *pubSubClient);
        void initDimmerButton(uint8_t id, uint8_t pin, uint8_t min, uint8_t max, uint8_t channel, bool enabled, char *name);
        void handleDimmerButtonEvent();
        void loop();
        void setLightLevel(uint8_t buttonIndex, uint8_t dimLevel);
        void setAutoDimmingTarget(uint8_t buttonIndex, uint8_t dimLevel);
        void setOutputState(uint8_t buttonIndex, bool outputState);
        // The following two items are for ISR handeling of button presses
        // Static pointer to instance of object
        static LightManager *instance;
        // Static forward function to forward ISR to instance
        static void IRAM_ATTR ISRForwarder();
    private:
        DMXESPSerial *_dmx;
        PubSubClient *_pubSubClient;
        void _updateDimmerButtonStatus(uint8_t buttonIndex);
        void _performDimmingOfLight(uint8_t buttonIndex);
        void _performAutoDimmingOfLight(uint8_t buttonIndex);
        unsigned long _getButtonStateTimeDelta(uint8_t buttonIndex, uint8_t firstEventIndex, uint8_t secondEventIndex);
        bool _getButtonState(uint8_t buttonIndex);
};

#endif