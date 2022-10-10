#include <LMANConfig.h>
#include <LittleFS.h>
#include <ArduLog.h>
#include <ArduinoJson.h>
#include <FS.h>

// Give somewhere in memory for instance to exist
LMANConfig *LMANConfig::instance;

bool LMANConfig::init()
{
    LMANConfig::instance = this;
    if (LittleFS.begin(false))
    {
        LOG_INFO("LittleFS mounted.");
        return true;
    }
    else
    {
        LOG_ERROR("Failed to mount LittleFS");
        return false;
    }
}

bool LMANConfig::loadFromLittleFS()
{
    LOG_INFO("Loading config from LittleFS");
    File configFile = LittleFS.open("/config.json");
    if (!configFile)
    {
        LOG_ERROR("Failed to load config.json!");
        return false;
    }

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    if (error)
    {
        LOG_ERROR("Failed to deserialize config.json");
        return false;
    }

    // Load config data
    this->wifi_hostname = doc["wifi_hostname"] | "LightManager";
    this->wifi_ssid = doc["wifi_ssid"] | "";
    this->wifi_psk = doc["wifi_psk"] | "";

    this->wifi_retry_timeout_ms = doc["wifi_retry_timeout_ms"].as<uint16_t>() | 30000;
    this->home_assistant_wait_online_ms = doc["home_assistant_wait_online_ms"].as<uint16_t>() | 30000;

    this->mqtt_server = doc["mqtt_server"] | "";
    this->mqtt_port = doc["mqtt_port"].as<uint8_t>() | 1883;
    this->mqtt_username = doc["mqtt_username"] | "";
    this->mqtt_password = doc["mqtt_password"] | "";

    for (JsonVariant v : doc["channels"].as<JsonArray>())
    {
        uint8_t channel = v["channel"].as<uint8_t>();
        LOG_DEBUG("Loading channel ", LOG_BOLD, channel);
        ChannelConfig cfg;
        cfg.channel = channel;
        cfg.min = v["min"].as<uint8_t>();
        cfg.max = v["max"].as<uint8_t>();
        cfg.holdPeriod = v["holdPeriod"].as<uint16_t>();
        cfg.dimmingSpeed = v["dimmingSpeed"].as<uint8_t>();
        cfg.autoDimmingSpeed = v["autoDimmingSpeed"].as<uint8_t>();
        this->channelConfigs.push_back(cfg);
    }

    JsonArray btnConfigs = doc["buttons"].as<JsonArray>();
    for (int i = 0; i < 4; i++)
    {
        LOG_INFO("Loading button ", LOG_BOLD, i);
        this->buttonConfigs[i].channel = btnConfigs[i]["channel"].as<uint8_t>() | 1;
        this->buttonConfigs[i].enabled = (btnConfigs[i]["enabled"].as<uint8_t>() | 0) == 1;
    }

    LOG_INFO("Config data loaded.");
    return true;
}

bool LMANConfig::saveToLittleFS()
{
    LOG_INFO("Saving config to LittleFS");
    return true;
}