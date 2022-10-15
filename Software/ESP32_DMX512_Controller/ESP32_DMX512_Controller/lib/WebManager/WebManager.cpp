#include <WebManager.h>
#include <ArduLog.h>
#include <LittleFS.h>
#include <LMANConfig.h>
#include <LightManager.h>
#include <list>

// Make space for variables in memory
WebManager *WebManager::instance;

void WebManager::init(PubSubClient *mqttClient)
{
    this->instance = this;
    this->_mqttClient = mqttClient;
    this->_doRebootAt = 0;

    LOG_DEBUG("Setting up web server routes");
    // this->_server = server;
    this->_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(LittleFS, "/index.html"); });

    this->_server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(LittleFS, "/reboot.html"); });

    this->_server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(LittleFS, "/update.html"); });

    this->_server.on("/connection_test", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(200, "text/plain", "OK"); });

    this->_server.on("/do_reboot", HTTP_GET, WebManager::doRebootAt);

    this->_server.onNotFound([](AsyncWebServerRequest *request)
                             { request->send(404, "text/plain", "File not found!"); });

    this->_indexDataSocket.onEvent(this->handleIndexDataEvent);

    LOG_INFO("Starting web server.");

    this->_server.serveStatic("/static", LittleFS, "/static");
    this->_server.addHandler(&this->_indexDataSocket);
    this->_server.begin();

    xTaskCreatePinnedToCore(WebManager::taskSendStatusUpdates, "taskWebStatusUpdates", 5000, NULL, 0, NULL, CONFIG_ARDUINO_RUNNING_CORE);
}

void WebManager::sendBaseData(AsyncWebSocketClient *client)
{
    LOG_TRACE("Constructing indexData BaseData");
    // A client just connected. Curate all data into a single JSON response
    StaticJsonDocument<2048> json;
    json["home_assistant_online_wait_period_ms"] = LMANConfig::instance->home_assistant_wait_online_ms;

    // WiFi values
    json["wifi_hostname"] = LMANConfig::instance->wifi_hostname.c_str();
    json["wifi_ssid"] = LMANConfig::instance->wifi_ssid.c_str();
    json["wifi_psk"] = LMANConfig::instance->wifi_psk.c_str();
    // MQTT Values
    json["mqtt_server"] = LMANConfig::instance->mqtt_server.c_str();
    json["mqtt_port"] = LMANConfig::instance->mqtt_port;
    json["mqtt_username"] = LMANConfig::instance->mqtt_username.c_str();
    json["mqtt_psk"] = LMANConfig::instance->mqtt_password.c_str();
    json["mqtt_base_topic"] = LMANConfig::instance->home_assistant_base_topic.c_str();
    json["mqtt_status"] = WebManager::instance->_mqttClient->connected() ? "Connected" : "DISCONNECTED";
    json["log_level"] = LMANConfig::instance->logging_level;

    // General button data
    json["button_min_time"] = LMANConfig::instance->buttonPressMinTime;
    json["button_max_time"] = LMANConfig::instance->buttonPressMaxTime;

    JsonArray channelData = json.createNestedArray("channels");
    for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
    {
        StaticJsonDocument<256> doc;
        doc["name"] = it->config->name;
        doc["enabled"] = it->config->enabled ? 1 : 0;
        doc["channel"] = it->config->channel;
        doc["min"] = it->config->min;
        doc["max"] = it->config->max;
        doc["dimmingSpeed"] = it->config->dimmingSpeed;
        doc["autoDimmingSpeed"] = it->config->autoDimmingSpeed;
        doc["holdPeriod"] = it->config->holdPeriod;
        doc["level"] = it->level;
        doc["state"] = it->state ? 1 : 0;
        channelData.add(doc);
    }

    JsonArray buttonData = json.createNestedArray("buttons");
    for (std::list<Button>::iterator it = LightManager::instance->buttons.begin(); it != LightManager::instance->buttons.end(); ++it)
    {
        StaticJsonDocument<256> doc;

        doc["channel"] = it->config->channel;
        doc["enabled"] = it->config->enabled ? 1 : 0;
        buttonData.add(doc);
    }

    LOG_TRACE("Serializing indexData BaseData");
    char json_data[2048];
    serializeJsonPretty(json, json_data);
    LOG_TRACE("Sending indexData BaseData");
    client->printf(json_data);
}

void WebManager::handleIndexDataEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        LOG_INFO("New connection on /index_data");
        WebManager::sendBaseData(client);
        // sendButtonData(client);
        LOG_INFO("New connection to /index_data handled");
    }
    else if (type == WS_EVT_ERROR)
    {
        // error was received from the other end
        LOG_ERROR("INDEX_DATA_SOCKET error: ws[", server->url(), "][", client->id(), "] error(", *((uint16_t *)arg), "):", (char *)data);
    }
    else if (type == WS_EVT_DATA)
    {
        // data packet
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len)
        {
            // the whole message is in a single frame and we got all of it's data
            if (info->opcode == WS_TEXT)
            {
                data[len] = 0;
                StaticJsonDocument<256> doc;
                DeserializationError error = deserializeJson(doc, (char *)data);
                if (error)
                {
                    LOG_ERROR("[INDEX_DATA_SOCKET] deserializeJson() failed: ");
                    Serial.println(error.f_str());
                    return;
                }
                uint16_t channel = doc["channel"] | 0;
                uint8_t dimmingTarget = doc["value"] | 0;

                if (channel != 0)
                {
                    for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
                    {
                        if (it->config->channel == channel)
                        {
                            LOG_DEBUG("Found match!");
                            if (it->state && dimmingTarget == 0)
                            {
                                LightManager::instance->autoDimOff(&(*it));
                            }
                            else if (!it->state && dimmingTarget != 0)
                            {
                                LightManager::instance->autoDimOnToLevel(&(*it), dimmingTarget);
                            }
                            else if (it->state)
                            {
                                LightManager::instance->autoDimTo(&(*it), dimmingTarget);
                            }
                            else
                            {
                                LOG_ERROR("Unknown combination of command data!");
                                LOG_ERROR("State : ", LOG_BOLD, it->state ? "ON" : "OFF");
                                LOG_ERROR("Target: ", LOG_BOLD, dimmingTarget);
                            }
                            break;
                        }
                    }
                }
                else
                {
                    LOG_ERROR("Failed to get channel!");
                }
            }
        }
    }
}

void WebManager::taskSendStatusUpdates(void *param)
{
    LOG_INFO("Started taskSendStatusUpdates.");
    for (;;)
    {
        if (WebManager::instance->_mqttClient->connected())
        {
            bool sendStatusUpdate = false;
            for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
            {

                if (it->webSendUpdate && millis() - it->lastLevelChange > 100)
                {
                    it->webSendUpdate = false;
                    sendStatusUpdate = true;
                    break;
                }
            }

            if (sendStatusUpdate)
            {
                // Build and send status updates for all channels
                StaticJsonDocument<256> doc;
                JsonArray channelData = doc.createNestedArray("channels");
                for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
                {
                    StaticJsonDocument<128> data;
                    data["state"] = it->state ? 1 : 0;
                    data["level"] = it->level;
                    channelData.add(data);
                }
                char buffer[256];
                size_t length = serializeJson(doc, buffer);
                if (length)
                {
                    WebManager::instance->_indexDataSocket.textAll(buffer, length);
                }
            }
        }
        vTaskDelay(100);
    }
}

void WebManager::doRebootAt(AsyncWebServerRequest *request)
{
    WebManager::instance->_doRebootAt = millis() + 2000;
    request->send(200, "text/plain", "OK");
}

bool WebManager::doReboot()
{
    return this->_doRebootAt > 0 && this->_doRebootAt <= millis();
}