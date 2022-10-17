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
    this->logging_level = doc["log_level"] | 4; // Set logging to debug if no level was read from file.
    LOG_INFO("Setting logging level to ", LOG_BOLD, this->logging_level);
    ArduLog::getInstance()->SetLogLevel(static_cast<ArduLogLevel>(this->logging_level));

    this->home_assistant_base_topic = doc["home_assistant_base_topic"] | "homeassistant/";

    this->buttonPressMinTime = doc["buttonPressMinTime"] | 80;
    this->buttonPressMaxTime = doc["buttonPressMaxTime"] | 800;

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
        this->channelConfigs[i].autoDimmingSpeed = channelArray[i]["autoDimmingSpeed"].as<uint8_t>();
        this->channelConfigs[i].dimmingSpeed = channelArray[i]["dimmingSpeed"].as<uint8_t>();
        this->channelConfigs[i].enabled = (channelArray[i]["enabled"].as<uint8_t>() | 0) == 1;
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
    StaticJsonDocument<2048> config_json;

    config_json["wifi_hostname"] = this->wifi_hostname.c_str();
    config_json["wifi_ssid"] = this->wifi_ssid.c_str();
    config_json["wifi_psk"] = this->wifi_psk.c_str();
    config_json["home_assistant_base_topic"] = this->home_assistant_base_topic.c_str();
    config_json["mqtt_server"] = this->mqtt_server.c_str();
    config_json["mqtt_port"] = this->mqtt_port;
    config_json["mqtt_username"] = this->mqtt_username;
    config_json["mqtt_password"] = this->mqtt_password;
    config_json["buttonPressMinTime"] = this->buttonPressMinTime;
    config_json["buttonPressMaxTime"] = this->buttonPressMaxTime;
    config_json["log_level"] = this->logging_level;

    JsonArray channels = config_json.createNestedArray("channels");
    JsonObject channel1 = channels.createNestedObject();
    channel1["name"] = this->channelConfigs[0].name.c_str();
    channel1["channel"] = this->channelConfigs[0].channel;
    channel1["min"] = this->channelConfigs[0].min;
    channel1["max"] = this->channelConfigs[0].max;
    channel1["dimmingSpeed"] = this->channelConfigs[0].dimmingSpeed;
    channel1["holdPeriod"] = this->channelConfigs[0].holdPeriod;
    channel1["autoDimmingSpeed"] = this->channelConfigs[0].autoDimmingSpeed;
    channel1["enabled"] = this->channelConfigs[0].enabled ? 1 : 0;
    JsonObject channel2 = channels.createNestedObject();
    channel2["name"] = this->channelConfigs[1].name.c_str();
    channel2["channel"] = this->channelConfigs[1].channel;
    channel2["min"] = this->channelConfigs[1].min;
    channel2["max"] = this->channelConfigs[1].max;
    channel2["dimmingSpeed"] = this->channelConfigs[1].dimmingSpeed;
    channel2["holdPeriod"] = this->channelConfigs[1].holdPeriod;
    channel2["autoDimmingSpeed"] = this->channelConfigs[1].autoDimmingSpeed;
    channel2["enabled"] = this->channelConfigs[1].enabled ? 1 : 0;
    JsonObject channel3 = channels.createNestedObject();
    channel3["name"] = this->channelConfigs[2].name.c_str();
    channel3["channel"] = this->channelConfigs[2].channel;
    channel3["min"] = this->channelConfigs[2].min;
    channel3["max"] = this->channelConfigs[2].max;
    channel3["dimmingSpeed"] = this->channelConfigs[2].dimmingSpeed;
    channel3["holdPeriod"] = this->channelConfigs[2].holdPeriod;
    channel3["autoDimmingSpeed"] = this->channelConfigs[2].autoDimmingSpeed;
    channel3["enabled"] = this->channelConfigs[2].enabled ? 1 : 0;
    JsonObject channel4 = channels.createNestedObject();
    channel4["name"] = this->channelConfigs[3].name.c_str();
    channel4["channel"] = this->channelConfigs[3].channel;
    channel4["min"] = this->channelConfigs[3].min;
    channel4["max"] = this->channelConfigs[3].max;
    channel4["dimmingSpeed"] = this->channelConfigs[3].dimmingSpeed;
    channel4["holdPeriod"] = this->channelConfigs[3].holdPeriod;
    channel4["autoDimmingSpeed"] = this->channelConfigs[3].autoDimmingSpeed;
    channel4["enabled"] = this->channelConfigs[3].enabled ? 1 : 0;

    JsonArray buttons = config_json.createNestedArray("buttons");
    JsonObject button1 = buttons.createNestedObject();
    button1["enabled"] = this->buttonConfigs[0].enabled ? 1 : 0;
    button1["channel"] = this->buttonConfigs[0].channel;
    JsonObject button2 = buttons.createNestedObject();
    button2["enabled"] = this->buttonConfigs[1].enabled ? 1 : 0;
    button2["channel"] = this->buttonConfigs[1].channel;
    JsonObject button3 = buttons.createNestedObject();
    button3["enabled"] = this->buttonConfigs[2].enabled ? 1 : 0;
    button3["channel"] = this->buttonConfigs[2].channel;
    JsonObject button4 = buttons.createNestedObject();
    button4["enabled"] = this->buttonConfigs[3].enabled ? 1 : 0;
    button4["channel"] = this->buttonConfigs[3].channel;

    File config_file = LittleFS.open("/config.json", "w");
    if (!config_file)
    {
        LOG_ERROR("Failed to open 'config.json' for writing.");
    }
    else if (serializeJson(config_json, config_file) == 0)
    {
        LOG_ERROR("Failed to save config file.");
    }
    else
    {
        LOG_INFO("Saved config file.");
    }

    return true;
}

bool LMANConfig::factoryReset()
{
    this->wifi_hostname = "lman";
    this->wifi_ssid = "";
    this->wifi_psk = "";
    this->logging_level = 0;

    this->home_assistant_base_topic = "homeassistant/";

    this->mqtt_server = "";
    this->mqtt_port = 1883;
    this->mqtt_username = "";
    this->mqtt_password = "";

    this->buttonPressMinTime = 80;
    this->buttonPressMaxTime = 800;

    this->channelConfigs[0].name = "channel1";
    this->channelConfigs[0].channel = 1;
    this->channelConfigs[0].min = 1;
    this->channelConfigs[0].max = 255;
    this->channelConfigs[0].dimmingSpeed = 5;
    this->channelConfigs[0].autoDimmingSpeed = 1;
    this->channelConfigs[0].holdPeriod = 800;
    this->channelConfigs[0].enabled = false;
    this->channelConfigs[1].name = "channel2";
    this->channelConfigs[1].channel = 1;
    this->channelConfigs[1].min = 1;
    this->channelConfigs[1].max = 255;
    this->channelConfigs[1].dimmingSpeed = 5;
    this->channelConfigs[1].autoDimmingSpeed = 1;
    this->channelConfigs[1].holdPeriod = 800;
    this->channelConfigs[1].enabled = false;
    this->channelConfigs[2].name = "channel3";
    this->channelConfigs[2].channel = 1;
    this->channelConfigs[2].min = 1;
    this->channelConfigs[2].max = 255;
    this->channelConfigs[2].dimmingSpeed = 5;
    this->channelConfigs[2].autoDimmingSpeed = 1;
    this->channelConfigs[2].holdPeriod = 800;
    this->channelConfigs[2].enabled = false;
    this->channelConfigs[3].name = "channel4";
    this->channelConfigs[3].channel = 1;
    this->channelConfigs[3].min = 1;
    this->channelConfigs[3].max = 255;
    this->channelConfigs[3].dimmingSpeed = 5;
    this->channelConfigs[3].autoDimmingSpeed = 1;
    this->channelConfigs[3].holdPeriod = 800;
    this->channelConfigs[3].enabled = false;

    this->buttonConfigs[0].channel = 1;
    this->buttonConfigs[0].enabled = 0;
    this->buttonConfigs[1].channel = 1;
    this->buttonConfigs[1].enabled = 0;
    this->buttonConfigs[2].channel = 1;
    this->buttonConfigs[2].enabled = 0;
    this->buttonConfigs[3].channel = 1;
    this->buttonConfigs[3].enabled = 0;
    return this->saveToLittleFS();
}