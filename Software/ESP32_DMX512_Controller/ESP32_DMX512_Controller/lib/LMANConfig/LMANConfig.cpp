#include <LMANConfig.h>
#include <LittleFS.h>
#include <ArduLog.h>
#include <ArduinoJson.h>
#include <FS.h>

std::string ChannelConfig::getBaseTopic()
{
    if (this->_baseTopic.empty())
    {
        LOG_DEBUG("_baseTopic not set yet. Building!");
        this->_baseTopic.append(LMANConfig::instance->home_assistant_base_topic);
        this->_baseTopic.append("light/");
        this->_baseTopic.append(LMANConfig::instance->wifi_hostname);
        this->_baseTopic.append("/");
        this->_baseTopic.append(this->name);
    }
    return this->_baseTopic;
}

std::string ChannelConfig::getCmdTopic()
{
    std::string cmdTopic = this->getBaseTopic();
    cmdTopic.append("/cmd");
    return cmdTopic;
}

std::string ChannelConfig::getStateTopic()
{
    std::string stateTopic = this->getBaseTopic();
    stateTopic.append("/state");
    return stateTopic;
}

std::string ChannelConfig::getCfgTopic()
{
    std::string cfgTopic = this->getBaseTopic();
    cfgTopic.append("/config");
    return cfgTopic;
}

std::string ChannelConfig::getAvailabilityTopic()
{
    std::string availabilityTopic = LMANConfig::instance->home_assistant_base_topic;
    availabilityTopic.append("light/");
    availabilityTopic.append(LMANConfig::instance->wifi_hostname);
    availabilityTopic.append("/aval");
    return availabilityTopic;
}

// LMANConfig
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

    StaticJsonDocument<3000> doc;
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

    this->home_assistant_wait_online_ms = doc["home_assistant_wait_online_ms"].as<uint16_t>() | 30000;
    this->home_assistant_base_topic = doc["home_assistant_base_topic"] | "homeassistant/";

    this->mqtt_server = doc["mqtt_server"] | "";
    this->mqtt_port = doc["mqtt_port"].as<uint8_t>() | 1883;
    this->mqtt_username = doc["mqtt_username"] | "";
    this->mqtt_password = doc["mqtt_password"] | "";

    JsonArray channelArray = doc["channels"].as<JsonArray>();
    for (int i = 0; i < channelArray.size(); i++)
    {
        uint8_t channel = channelArray[i]["channel"].as<uint8_t>();
        LOG_DEBUG("Loading channel ", LOG_BOLD, channel);
        this->channelConfigs[i].channel = channel;
        this->channelConfigs[i].name = channelArray[i]["name"] | "";
        this->channelConfigs[i].min = channelArray[i]["min"].as<uint8_t>();
        this->channelConfigs[i].max = channelArray[i]["max"].as<uint8_t>();
        this->channelConfigs[i].holdPeriod = channelArray[i]["holdPeriod"].as<uint16_t>();
        this->channelConfigs[i].dimmingSpeed = channelArray[i]["dimmingSpeed"].as<uint8_t>();
    }

    JsonArray btnConfigs = doc["buttons"].as<JsonArray>();
    for (int i = 0; i < btnConfigs.size(); i++)
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