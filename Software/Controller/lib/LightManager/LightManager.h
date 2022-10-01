#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include <Arduino.h>
#include <list>
#include <ESPDMX.h>
#include <PubSubClient.h>

struct _buttonEvent {
    bool pressed = false;
    unsigned long millis = 0;
};

struct DimmerButton {
    uint8_t id;
    uint8_t channel = 1;
    uint8 pin = 0;
    uint8 min = 0;
    uint8 max = 255;
    uint8 dimLevel = 255;
    uint8 dimLevelBeforeSmoothOff = 255;
    uint8 autoDimTarget = 255;
    uint8 autoDimmingSpeed = 1;
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
    uint16_t mqttSendWaitTime = 500; // Time to wait after last dim event before sending an MQTT update
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
    bool smoothOff = false; // Dim from current level to 0 smoothly.
    char* name;
    _buttonEvent _buttonEvents[3];
};

class LightManager {
    public:
        //DimmerButton dimmerButtons[4];
        std::list<DimmerButton> dimmerButtons;
        void init(DMXESPSerial *dmx, PubSubClient *pubSubClient);
        DimmerButton* initDimmerButton(uint8_t id, uint8_t pin, uint8_t min, uint8_t max, uint8_t channel, bool enabled, char *name);
        void handleDimmerButtonEvent();
        void loop();
        void setLightLevel(DimmerButton *btn, uint8_t dimLevel);
        void setAutoDimmingTarget(DimmerButton *btn, uint8_t dimLevel);
        void setOutputState(DimmerButton *btn, bool outputState);
        DimmerButton* getLightById(uint8_t id);
        // The following two items are for ISR handeling of button presses
        // Static pointer to instance of object
        static LightManager *instance;
        // Static forward function to forward ISR to instance
        static void IRAM_ATTR ISRForwarder();
    private:
        DMXESPSerial *_dmx;
        PubSubClient *_pubSubClient;
        void _updateDimmerButtonStatus(DimmerButton *btn);
        void _performDimmingOfLight(DimmerButton *btn);
        void _performAutoDimmingOfLight(DimmerButton *btn);
        void _performSmoothOff(DimmerButton *btn);
        unsigned long _getButtonStateTimeDelta(DimmerButton *btn, uint8_t firstEventIndex, uint8_t secondEventIndex);
        bool _getButtonState(DimmerButton *btn);
};

#endif