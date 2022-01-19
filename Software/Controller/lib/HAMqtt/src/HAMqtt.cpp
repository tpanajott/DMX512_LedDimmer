#include "HAMqtt.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

HAMqtt::HAMqtt() {}

void HAMqtt::begin(void (*callback)(MQTTLight), String mqttBaseName, String deviceName) {
    _callback = callback;
    _mqttBaseName = mqttBaseName;
    _deviceName = deviceName;
}

void HAMqtt::mqttHandler(char* topic, byte* payload, unsigned int length) {
    // This function will, if the decoding was successful, call the defined
    // callback function with the apropriate MQTTLight struct.
    MQTTLight light;
    light.brightness = -1; // The brightness to a negative value to indicate that it has not been set.
    light.name = "";      // An empty name indicates that the light has not been decoded correctly.
    light.state = true;    // Assume the light is on if a message was received.

    // Handle the raw data received by MQTT
    char data[length];
    for (unsigned int i = 0; i < length; i++) {
        data[i] = (char)payload[i];
    }
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, data);
    if(error) {
        Serial.print("[ERROR] Failed to parse JSON data sent to topic: "); Serial.println(topic);
    } else { 
        if(doc.containsKey("brightness")) {
            light.brightness = doc["brightness"].as<int>();
        }
        if (doc.containsKey("state")) {
            light.state = (doc["state"].as<String>() == String("ON"));
        }
    }

    // //Try to get the name for the light.
    String topic_str = String(topic);
    //                                                          "/light" + "-"
    int removeLength = _mqttBaseName.length() + _deviceName.length() + 6 + 1;
    topic_str.remove(0, removeLength);
    topic_str.remove(topic_str.indexOf("/")); // Remove all trailing /...
    light.name = topic_str;

    _callback(light);
}

void HAMqtt::sendLightUpdate(MQTTLight light, PubSubClient (*client)) {
    if(client->connected()) {
        String state_topic = String(_mqttBaseName);
        state_topic.concat("light/");
        state_topic.concat(_deviceName);
        state_topic.concat("-");
        state_topic.concat(light.name);
        state_topic.concat("/state");

        StaticJsonDocument<200> doc;
        doc["state"] = light.state ? "ON" : "OFF";
        doc["brightness"] = light.brightness >= 0 ? light.brightness : 0;

        String message;
        serializeJson(doc, message);
        client->publish(state_topic.c_str(), message.c_str());
    }
}

void HAMqtt::registerLight(MQTTLight light, PubSubClient (*client)) {
    if(client->connected()) {
        // Will register and at the same time subscribe to relevant topics for MQTT light
        // in Home Assistant
        DynamicJsonDocument doc(1024);
        // Put together all the pieces to create the base
        String base = String(_mqttBaseName);
        base.concat("light/");
        base.concat(_deviceName);
        base.concat("-");
        base.concat(light.name);

        String configBase = String(base);
        configBase.concat("/config");

        String availability_topic = String(_mqttBaseName);
        availability_topic.concat("light/");
        availability_topic.concat(_deviceName);
        availability_topic.concat("/availability");

        // Put together all the pieces to create the name
        String name = String(_deviceName);
        name.concat("-");
        name.concat(light.name);

        doc["~"] = base.c_str();
        doc["name"] = light.name.c_str();
        doc["uniq_id"] = name.c_str();
        doc["cmd_t"] = "~/command";
        doc["stat_t"] = "~/state";
        doc["schema"] = "json";
        doc["brightness"] = true;
        doc["avty_t"] = availability_topic.c_str();

        // Device information
        String device_configuration_url = "http://";
        device_configuration_url.concat(WiFi.localIP().toString());
        
        JsonObject device = doc.createNestedObject("device");
        device["configuration_url"] = device_configuration_url.c_str();
        device["name"] = _deviceName;
        JsonArray connections = device.createNestedArray("connections");
        JsonArray mac_address =  connections.createNestedArray();
        mac_address.add("mac");
        mac_address.add(WiFi.macAddress());
        JsonArray ip_address = connections.createNestedArray();
        ip_address.add("ip");
        ip_address.add(WiFi.localIP().toString());
        
        String message;
        serializeJson(doc, message);
        client->publish(configBase.c_str(), message.c_str());

        client->subscribe(String(base + "/command").c_str());
        client->subscribe(String(base + "/brightness").c_str());
    }
}

void HAMqtt::sendOnlineStatusUpdate(PubSubClient (*client)) {
    // Send an online status update to the last will topic (availability topic)
    String availability_topic = String(_mqttBaseName);
    availability_topic.concat("light/");
    availability_topic.concat(_deviceName);
    availability_topic.concat("/availability");

    // Send "online" status update
    client->publish(availability_topic.c_str(), "online");
}