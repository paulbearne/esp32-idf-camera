
#include "main.h"
TaskHandle_t hMjpeg; // handles client connections to the webserver
TaskHandle_t hCam;   // handles getting picture frames from the camera and storing them locally
TaskHandle_t hStream;
static const char *TAG = "BeyBlades Main";
unsigned long streamsServed = 0; // Total completed streams
unsigned long imagesServed = 0;  // Total image requests
char httpURL[64] = {"Undefined"};
char streamURL[64] = {"Undefined"};
esp_ip4_addr_t ip;
esp_ip4_addr_t net;
esp_ip4_addr_t gw;
SemaphoreHandle_t frameSync = NULL;
int streamPort = 81;
int httpPort = 80;

int constrain(int x, int a, int b)
{
    if (x < a)
    {
        return a;
    }
    else if (b < x)
    {
        return b;
    }
    else
        return x;
}

void calcURLs()
{
// Set the URL's
#if defined(URL_HOSTNAME)
    if (httpPort != 80)
    {
        sprintf(httpURL, "http://%s:%d/", URL_HOSTNAME, httpPort);
    }
    else
    {
        sprintf(httpURL, "http://%s/", URL_HOSTNAME);
    }
    sprintf(streamURL, "http://%s:%d/", URL_HOSTNAME, streamPort);
#else
    ESP_LOGI(TAG, "Setting httpURL");
    if (httpPort != 80)
    {
        sprintf(httpURL, "http://" IPSTR ":%d/", IP2STR(&ip), httpPort);
    }
    else
    {
        sprintf(httpURL, "http://" IPSTR "/", IP2STR(&ip));
    }
    sprintf(streamURL, "http://" IPSTR ":%d/", IP2STR(&ip), streamPort);
#endif
}

void app_main(void)
{
    // static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    initLed();
    initRgbLed();
    setRgbLedLevel(128);
    flashLED(1000);
    wifiConnect();
    startMdnsService();

    if (initSpiffs() != ESP_OK)
    {
        ESP_LOGI(TAG, "failed to init spiffs");
    }

    flashLED(1000);
    setRgbLedLevel(0);
    loadConfig(SPIFFS);
    startCamera();

    startCameraServer(httpPort, streamPort, "/spiffs");
    calcURLs();
    ESP_LOGI(TAG, "Web url %s", httpURL);
    ESP_LOGI(TAG, "Stream url %s", streamURL);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
