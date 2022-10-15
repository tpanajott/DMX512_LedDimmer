#ifndef LMAN_CONFIG_H
#define LMAN_CONFIG_H

#include <string>
#include <list>

class ChannelConfig
{
public:
    /// @brief The name of this channel (mostly used for home assistant)
    std::string name;
    /// @brief The minimum light level for this channel
    uint8_t min = 0;
    /// @brief The maximum light level for this channel
    uint8_t max = 255;
    /// @brief The DMX channel to send data on. A channel of 0 = no initialized/valid
    uint8_t channel = 0;
    /// @brief The speed in ms to wait between dimming events.
    uint8_t dimmingSpeed = 5;
    /// @brief The period to hold the light at min/max when reached before reversing dimming.
    uint16_t holdPeriod = 800;
    /// @brief The speed of dimming in ms when auto-dimming.
    uint8_t autoDimmingSpeed = 1;
    /// @brief Return the base topic where all other sub-topics for this channel exists
    /// @return MQTT Topic
    std::string getBaseTopic();
    /// @brief Return the topic where state updates of this channel should be sent
    /// @return MQTT Topic
    std::string getStateTopic();
    /// @brief Return the topic where commands for this channel are sent
    /// @return MQTT Topic
    std::string getCmdTopic();
    /// @brief Return the topic where configuration for this channel are sent
    /// @return MQTT Topic
    std::string getCfgTopic();
    /// @brief Return the topic where availability for this channel are sent
    /// @return MQTT Topic
    std::string getAvailabilityTopic();

private:
    std::string _baseTopic;
};

struct ButtonConfig
{
    bool enabled;
    uint8_t channel;
};

class LMANConfig
{
public:
    /// @brief Will try to initialize and load LittleFS
    /// @return True if successful
    bool init();
    /// @brief Load config file from LittleFS
    /// @return True if successful
    bool loadFromLittleFS();
    /// @brief Save current config file to LittleFS
    /// @return True if successful
    bool saveToLittleFS();
    /// @brief The instance of the config manager
    static LMANConfig *instance;

    uint8_t logging_level;

    /// @brief The hostname of this device
    std::string wifi_hostname;
    /// @brief The WiFi name to connect to
    std::string wifi_ssid;
    /// @brief The pre-shared key for the WiFi
    std::string wifi_psk;

    /// @brief How often to try to connect to WiFi
    uint16_t wifi_retry_timeout_ms;
    /// @brief Hwo long to wait after home assistant has sent "online"
    /// before registring entities again
    uint16_t home_assistant_wait_online_ms;
    /// @brief The base topic of home assistant, typically "homeassistant/"
    std::string home_assistant_base_topic;
    /// @brief Address to MQTT server
    std::string mqtt_server;
    /// @brief The port to connect to MQTT with
    uint16_t mqtt_port;
    /// @brief MQTT Username
    std::string mqtt_username;
    /// @brief MQTT Password
    std::string mqtt_password;

    /// @brief The minimum time a button must have to same state to register.
    uint8_t buttonPressMinTime;
    /// @brief The maximum time for a button press before it is considered a "hold" action
    uint16_t buttonPressMaxTime;

    /// @brief Configuration for all DMX channels
    ChannelConfig channelConfigs[4];
    /// @brief Configuration for all buttons
    ButtonConfig buttonConfigs[4];
};

#endif