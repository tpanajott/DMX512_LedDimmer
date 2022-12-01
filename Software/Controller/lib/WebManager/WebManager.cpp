#include <WebManager.h>
#include <ArduLog.h>
#include <LittleFS.h>
#include <LMANConfig.h>
#include <LightManager.h>
#include <list>
#include <WiFi.h>
#include <Arduino.h>
#include <Update.h>

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

    this->_server.on("/raw_config", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(LittleFS, "/config.json"); });

    this->_server.on("/connection_test", HTTP_GET, [](AsyncWebServerRequest *request)
                     { request->send(200, "text/plain", "OK"); });

    this->_server.on("/do_reboot", HTTP_GET, WebManager::doRebootAt);
    this->_server.on("/save_config", HTTP_POST, WebManager::saveConfigFromWeb);
    this->_server.on("/available_wifi_networks", HTTP_GET, respondAvailableWiFiNetworks);
    this->_server.onFileUpload(performFirmwareUpdate);
    this->_server.on("/do_factory_reset", HTTP_GET, [](AsyncWebServerRequest *request)
                     {
        LMANConfig::instance->factoryReset();
        request->redirect("/reboot"); });

    this->_server.onNotFound([](AsyncWebServerRequest *request)
                             { request->send(404, "text/plain", "Path/File not found!"); });

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
    json["home_assistant_state_change_wait"] = LMANConfig::instance->home_assistant_state_change_wait;
    json["mqtt_status"] = WebManager::instance->_mqttClient->connected() ? "Connected" : "DISCONNECTED";
    json["log_level"] = LMANConfig::instance->logging_level;

    // General button data
    json["button_min_time"] = LMANConfig::instance->buttonPressMinTime;
    json["button_max_time"] = LMANConfig::instance->buttonPressMaxTime;

    JsonArray channelData = json.createNestedArray("channels");
    for (std::list<DMXChannel>::iterator it = LightManager::instance->dmxChannels.begin(); it != LightManager::instance->dmxChannels.end(); ++it)
    {
        JsonObject doc = channelData.createNestedObject();
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
    }

    JsonArray buttonData = json.createNestedArray("buttons");
    for (std::list<Button>::iterator it = LightManager::instance->buttons.begin(); it != LightManager::instance->buttons.end(); ++it)
    {
        JsonObject doc = buttonData.createNestedObject();
        doc["channel"] = it->config->channel;
        doc["enabled"] = it->config->enabled ? 1 : 0;
    }

    LOG_TRACE("Serializing indexData BaseData");
    char json_data[2048];
    serializeJson(json, json_data);
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
                            if (!it->config->enabled)
                            {
                                LOG_ERROR("Found matching disabled channel. Will break loop!");
                                break;
                            }

                            LOG_DEBUG("Found matching channel. Processing command!");
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

void WebManager::saveConfigFromWeb(AsyncWebServerRequest *request)
{
    StaticJsonDocument<2048> config_json;

    LMANConfig::instance->wifi_hostname = request->arg("wifi_hostname").c_str();
    LMANConfig::instance->wifi_ssid = request->arg("wifi_ssid").c_str();
    LMANConfig::instance->wifi_psk = request->arg("wifi_psk").c_str();

    LMANConfig::instance->logging_level = request->arg("log_level").toInt();

    LMANConfig::instance->mqtt_server = request->arg("mqtt_server").c_str();
    LMANConfig::instance->mqtt_port = request->arg("mqtt_port").toInt();
    LMANConfig::instance->mqtt_username = request->arg("mqtt_username").c_str();
    LMANConfig::instance->mqtt_password = request->arg("mqtt_password").c_str();
    LMANConfig::instance->home_assistant_base_topic = request->arg("mqtt_base_topic").c_str();
    LMANConfig::instance->home_assistant_state_change_wait = request->arg("home_assistant_state_change_wait").toInt();

    LMANConfig::instance->buttonPressMaxTime = request->arg("button_max_press").toInt();
    LMANConfig::instance->buttonPressMinTime = request->arg("button_min_press").toInt();

    LMANConfig::instance->channelConfigs[0].enabled = request->hasArg("channel1_enabled");
    LMANConfig::instance->channelConfigs[0].name = request->arg("channel1_name").c_str();
    LMANConfig::instance->channelConfigs[0].channel = request->arg("channel1_channel").toInt();
    LMANConfig::instance->channelConfigs[0].min = request->arg("channel1_min").toInt();
    LMANConfig::instance->channelConfigs[0].max = request->arg("channel1_max").toInt();
    LMANConfig::instance->channelConfigs[0].dimmingSpeed = request->arg("channel1_dimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[0].autoDimmingSpeed = request->arg("channel1_autoDimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[0].holdPeriod = request->arg("channel1_holdPeriod").toInt();

    LMANConfig::instance->channelConfigs[1].enabled = request->hasArg("channel2_enabled");
    LMANConfig::instance->channelConfigs[1].name = request->arg("channel2_name").c_str();
    LMANConfig::instance->channelConfigs[1].channel = request->arg("channel2_channel").toInt();
    LMANConfig::instance->channelConfigs[1].min = request->arg("channel2_min").toInt();
    LMANConfig::instance->channelConfigs[1].max = request->arg("channel2_max").toInt();
    LMANConfig::instance->channelConfigs[1].dimmingSpeed = request->arg("channel2_dimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[1].autoDimmingSpeed = request->arg("channel2_autoDimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[1].holdPeriod = request->arg("channel2_holdPeriod").toInt();

    LMANConfig::instance->channelConfigs[2].enabled = request->hasArg("channel3_enabled");
    LMANConfig::instance->channelConfigs[2].name = request->arg("channel3_name").c_str();
    LMANConfig::instance->channelConfigs[2].channel = request->arg("channel3_channel").toInt();
    LMANConfig::instance->channelConfigs[2].min = request->arg("channel3_min").toInt();
    LMANConfig::instance->channelConfigs[2].max = request->arg("channel3_max").toInt();
    LMANConfig::instance->channelConfigs[2].dimmingSpeed = request->arg("channel3_dimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[2].autoDimmingSpeed = request->arg("channel3_autoDimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[2].holdPeriod = request->arg("channel3_holdPeriod").toInt();

    LMANConfig::instance->channelConfigs[3].enabled = request->hasArg("channel4_enabled");
    LMANConfig::instance->channelConfigs[3].name = request->arg("channel4_name").c_str();
    LMANConfig::instance->channelConfigs[3].channel = request->arg("channel4_channel").toInt();
    LMANConfig::instance->channelConfigs[3].min = request->arg("channel4_min").toInt();
    LMANConfig::instance->channelConfigs[3].max = request->arg("channel4_max").toInt();
    LMANConfig::instance->channelConfigs[3].dimmingSpeed = request->arg("channel4_dimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[3].autoDimmingSpeed = request->arg("channel4_autoDimmingSpeed").toInt();
    LMANConfig::instance->channelConfigs[3].holdPeriod = request->arg("channel4_holdPeriod").toInt();

    LMANConfig::instance->buttonConfigs[0].enabled = request->hasArg("button1_enabled");
    LMANConfig::instance->buttonConfigs[0].channel = request->arg("button1_channel").toInt();

    LMANConfig::instance->buttonConfigs[1].enabled = request->hasArg("button2_enabled");
    LMANConfig::instance->buttonConfigs[1].channel = request->arg("button2_channel").toInt();

    LMANConfig::instance->buttonConfigs[2].enabled = request->hasArg("button3_enabled");
    LMANConfig::instance->buttonConfigs[2].channel = request->arg("button3_channel").toInt();

    LMANConfig::instance->buttonConfigs[3].enabled = request->hasArg("button4_enabled");
    LMANConfig::instance->buttonConfigs[3].channel = request->arg("button4_channel").toInt();

    if (!LMANConfig::instance->saveToLittleFS())
    {
        LOG_ERROR("Failed to save new configuration!");
    }

    request->redirect("/reboot");
}

void WebManager::respondAvailableWiFiNetworks(AsyncWebServerRequest *request)
{
    String json = "[";
    int n = WiFi.scanComplete();
    if (n == -2)
    {
        WiFi.scanNetworks(true);
    }
    else if (n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (!WiFi.SSID(i).isEmpty())
            {
                LOG_DEBUG("Found WiFi ", WiFi.SSID(i).c_str());
                if (i)
                    json += ",";
                json += "{";
                json += "\"rssi\":" + String(WiFi.RSSI(i));
                json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
                json += ",\"channel\":" + String(WiFi.channel(i));

                switch (WiFi.encryptionType(i))
                {
                case wifi_auth_mode_t::WIFI_AUTH_OPEN:
                    json += ",\"security\": \"OPEN\"";
                    break;
                case wifi_auth_mode_t::WIFI_AUTH_WPA2_PSK:
                    json += ",\"security\": \"WPA2 PSK\"";
                    break;
                case wifi_auth_mode_t::WIFI_AUTH_WPA3_PSK:
                    json += ",\"security\": \"WPA3 PSK\"";
                    break;
                case wifi_auth_mode_t::WIFI_AUTH_WPA_PSK:
                    json += ",\"security\": \"WPA PSK\"";
                    break;
                case wifi_auth_mode_t::WIFI_AUTH_WEP:
                    json += ",\"security\": \"WEP\"";
                    break;

                default:
                    json += ",\"security\": \"UNKNOWN\"";
                    break;
                }
                json += "}";
            }
        }
        WiFi.scanDelete();
        if (WiFi.scanComplete() == -2)
        {
            WiFi.scanNetworks(true);
        }
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
}

void WebManager::performFirmwareUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!index)
    {
        LOG_INFO("Starting flash of file: '", filename.c_str(), "'. Length:", request->contentLength());

        // Update.runAsync(true);
        // Detect update type depending on filename
        int uploadType;
        if (filename.startsWith("firmware") && filename.endsWith(".bin"))
        {
            uploadType = U_FLASH;
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace, uploadType))
            {
                LOG_ERROR("Update error! Could not start update, error:");
                Update.printError(Serial);
            }
        }
        else if (filename.startsWith("littlefs") && filename.endsWith(".bin"))
        {
            uploadType = U_SPIFFS;
            size_t fsSize = LittleFS.totalBytes();
            if (!Update.begin(fsSize, uploadType))
            {
                LOG_ERROR("Update error! Could not start update, error:");
                Update.printError(Serial);
            }
        }
        else
        {
            request->send(500, "Unknown file type!");
            return;
        }
    }

    // If no error has occrued, continue updating
    if (!Update.hasError())
    {
        if (Update.write(data, len) != len)
        {
            LOG_ERROR("Update error occured: ");
            Update.printError(Serial);
        }
        else
        {
            int size = index + len;
            LOG_INFO("Update written ", size);
        }
    }

    if (final)
    {
        Update.end(true);
        if (filename.startsWith("firmware") && filename.endsWith(".bin"))
        {
            WebManager::instance->_hasFirmwareUpdated = true;
        }
        else if (filename.startsWith("littlefs") && filename.endsWith(".bin"))
        {
            WebManager::instance->_hasLITTLEFSUpdated = true;
            LMANConfig::instance->saveToLittleFS(); // Save currently loaded values to LittleFS
        }
        LOG_INFO("Update of file: ", filename.c_str(), " ended");
        if (WebManager::instance->_hasFirmwareUpdated && WebManager::instance->_hasLITTLEFSUpdated)
        {
            request->send(200, "text/plain", "OK");
        }
    }
}