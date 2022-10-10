#ifndef LMAN_CONFIG_H
#define LMAN_CONFIG_H

#include <string>
#include <list>

struct ChannelConfig
{
    /// @brief The minimum light level for this channel
    uint8_t min = 0;
    /// @brief The maximum light level for this channel
    uint8_t max = 255;
    /// @brief The DMX channel to send data on
    uint8_t channel;
    /// @brief The speed in ms to wait between dimming events.
    uint8_t dimmingSpeed = 5;
    /// @brief The period to hold the light at min/max when reached before reversing dimming.
    uint16_t holdPeriod = 800;
    /// @brief The speed of dimming in ms when auto-dimming.
    uint8_t autoDimmingSpeed = 1;
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
    /// @brief Address to MQTT server
    std::string mqtt_server;
    /// @brief The port to connect to MQTT with
    uint16_t mqtt_port;
    /// @brief MQTT Username
    std::string mqtt_username;
    /// @brief MQTT Password
    std::string mqtt_password;

    /// @brief Configuration for all DMX channels
    std::list<ChannelConfig> channelConfigs;
    /// @brief Configuration for all buttons
    ButtonConfig buttonConfigs[4];
};

#endif