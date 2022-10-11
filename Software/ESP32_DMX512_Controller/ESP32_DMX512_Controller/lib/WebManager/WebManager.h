#ifndef LMAN_WEB_MANAGER
#define LMAN_WEB_MANAGER

#include <LMANConfig.h>
// #include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

class WebManager
{
public:
    // void init(AsyncWebServer *server);
    void init();
    static void doRebootAt(AsyncWebServerRequest *request);
    /// @brief Indicate wether a reboot should be done or not
    /// @return True = time to reboot
    bool doReboot();
    static WebManager *instance;

private:
    AsyncWebServer _server = AsyncWebServer(80);
    unsigned long _doRebootAt;
};

#endif