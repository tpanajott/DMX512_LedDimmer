#ifndef HAMQTT_H
#define HAMQTT_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

struct MQTTLight {
    String name;
    short brightness;
    bool state;
};

class HAMqtt {
    public:
        HAMqtt();
        void begin(void (*callback)(MQTTLight), String mqttBaseName, String deviceName);
        void mqttHandler(char* topic, byte* payload, unsigned int length);
        void sendLightUpdate(MQTTLight light, PubSubClient (*client));
        void registerLight(MQTTLight light, PubSubClient (*client));
        void sendOnlineStatusUpdate(PubSubClient (*client));
    private:
        void (*_callback)(MQTTLight light);
        String _mqttBaseName;
        String _deviceName;
};

#endif