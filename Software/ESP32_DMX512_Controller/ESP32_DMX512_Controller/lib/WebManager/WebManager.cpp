#include <WebManager.h>
#include <ArduLog.h>
#include <LittleFS.h>

// Make space for variables in memory
WebManager *WebManager::instance;

void WebManager::init()
{
    this->instance = this;
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

    LOG_INFO("Starting web server.");

    this->_server.serveStatic("/static", LittleFS, "/static");
    this->_server.begin();
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