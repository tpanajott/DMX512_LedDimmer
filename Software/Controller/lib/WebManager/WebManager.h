#ifndef LMAN_WEB_MANAGER
#define LMAN_WEB_MANAGER

#include <LMANConfig.h>
// #include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

class WebManager
{
public:
    // void init(AsyncWebServer *server);
    void init(PubSubClient *mqttClient);
    static void doRebootAt(AsyncWebServerRequest *request);
    /// @brief Indicate wether a reboot should be done or not
    /// @return True = time to reboot
    bool doReboot();
    static WebManager *instance;
    static void handleIndexDataEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    static void sendBaseData(AsyncWebSocketClient *client);
    static void taskSendStatusUpdates(void *param);
    static void saveConfigFromWeb(AsyncWebServerRequest *request);
    static void respondAvailableWiFiNetworks(AsyncWebServerRequest *request);
    static void performFirmwareUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

private:
    AsyncWebServer _server = AsyncWebServer(80);
    AsyncWebSocket _indexDataSocket = AsyncWebSocket("/index_data");
    PubSubClient *_mqttClient;
    unsigned long _doRebootAt;
    bool _hasFirmwareUpdated = false;
    bool _hasLITTLEFSUpdated = false;
};

#endif