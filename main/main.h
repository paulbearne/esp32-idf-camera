/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example, common declarations

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once
#include <stdio.h>
#include <string.h>
#include "esp_psram.h"
#include "portmacro.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
#include "sdkconfig.h"
#include "dirent.h"
#include "cJSON.h"
#include "soc/soc_caps.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif
#include "sdmmc_cmd.h"
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "camera_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "mdns.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"
#include "driver/rmt_encoder.h"
#include "esp_check.h"
#include "esp_mac.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include <esp_netif_ip_addr.h>

#ifdef __cplusplus
extern "C"
{
#endif

// s3 has 2 leds normal on pin 2 and wa2812 on pin48
#define LED_1_PIN GPIO_NUM_2
#define USE_WS2812 // Use WS2812 rgb led


#ifdef USE_WS2812
#define LED_2_PIN GPIO_NUM_48
#endif

//#define LAMP_PIN LED_1_PIN
#define LAMP_PIN LED_2_PIN

#define APP_CPU 1
#define PRO_CPU 0
#define FAIL_IF_OOM true
#define OK_IF_OOM false
#define PSRAM_ONLY true
#define ANY_MEMORY false
#define FILE_NAME_LEN 64
#define FILE_PATH_LEN 255
#define PREFERENCES_MAX_SIZE 500
#define PREFERENCES_FILE "beyblades.cfg"

//#define LAMP_PIN 48

#ifdef CONFIG_XCLOCK_10000000
#define XCLOCK_FREQ 10000000
#endif
#ifdef CONFIG_XCLOCK_20000000
#define XCLOCK_FREQ 20000000
#endif

    typedef struct
    {
        QueueHandle_t clients;
        TaskHandle_t task;
        uint64_t last_updated;
        int64_t frame_delay;
        uint8_t buf_lock;
        camera_fb_t *buf;
        char part_buf[64];
        size_t part_len;
        httpd_handle_t server;
        size_t num_clients;
    } cam_streamer_t;

    typedef struct
    {
        bool autolamp;             //"\"autolamp\" : autolamp
        int lampval;               //"\"lamp\" : lampval
        bool usews2812;            //"\"lampws2812\" : usews2812 true uses ws2812 driver
        uint32_t lamppin;          // pin used for led lamp
        framesize_t framesize;     //"\"framesize\" : camsensor->status.framesize
        uint8_t quality;           //"\"quality\" : camsensor->status.quality
        uint32_t xclk;
        int8_t brightness;         //"\"brightness\" : camsensor->status.brightness
        int8_t contrast;           //"\"contrast\" : camsensor->status.contrast);
        int8_t saturation;         //"\"saturation\"  : camsensor->status.saturation
        uint8_t specialeffect;     //"\"special_effect  : camsensor->status.special_effect 0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia
        int8_t sharpness;          //"\"sharpness\" : camsensor->status.sharpness
        uint8_t wbmode;            //"\"wb_mode\" : camsensor->status.wb_mode
        uint8_t awb;               //"\"awb\" : camsensor->status.awb
        uint8_t awbgain;           //"\"awb_gain\" : camsensor->status.awb_gain
        uint8_t aec;               //"\"aec\" : camsensor->status.aec 
        uint8_t aec2;              //"\"aec2\" : camsensor->status.aec2
        int8_t aelevel;            //"\"ae_level\" : camsensor->status.ae_level
        uint16_t aecvalue;         //"\"aec_value\" : camsensor->status.aec_value
        uint8_t agc;               //"\"agc\" : camsensor->status.agc
        uint8_t agcgain;           //"\"agc_gain\" : camsensor->status.agc_gain  
        gainceiling_t gainceiling; //"\"gainceiling\" : camsensor->status.gainceiling
        uint8_t bpc;               //"\"bpc\" : camsensor->status.bpc
        uint8_t wpc;               //"\"wpc\" : camsensor->status.wpc
        uint8_t rawgma;            //"\"raw_gma\" : camsensor->status.raw_gma
        uint8_t lenc;              //"\"lenc\" : camsensor->status.lenc
        uint8_t vflip;             //"\"vflip\" : camsensor->status.vflip
        uint8_t hmirror;           //"\"hmirror\" : camsensor->status.hmirror
        uint8_t dcw;               //"\"dcw\" : camsensor->status.dcw
        uint8_t colorbar;          //"\"colorbar\" : camsensor->status.colorbar
        uint8_t camRotation;       //"\"rotate\" : CONFIG_CAMERA_ROTATION
        uint32_t minFrameTime;     // setd the minimum frame delay 
        uint32_t checksum;         // size of this structure used to set defaults if this struct changed

    } setting_t;

#define baseVersion "1.0"

    extern int wifi_connect_status;
    extern TaskHandle_t hMjpeg; // handles client connections to the webserver
    extern TaskHandle_t hCam;   // handles getting picture frames from the camera and storing them locally
    extern TaskHandle_t hStream;
    extern SemaphoreHandle_t frameSync;
    extern const int hdrLen;
    extern const int bdrLen;
    extern const int cntLen;
    extern bool autoLamp;
   // extern int lampVal;
    extern unsigned long streamsServed;
    extern unsigned long imagesServed;
    extern bool filesystem;
    extern char httpURL[64];
    extern char critERR[255];
    extern char streamURL[64];
    extern unsigned long xclk;
    extern int camRotation;
    extern int minFrameTime;
    extern esp_ip4_addr_t ip;
    extern esp_ip4_addr_t net;
    extern esp_ip4_addr_t gw;
    extern int sensorPID;
    extern setting_t conf;

    struct async_resp_arg
    {
        httpd_handle_t hd;
        int fd;
    };

    typedef struct
    {
        uint32_t resolution; /*!< Encoder resolution, in Hz */
    } led_strip_encoder_config_t;

    typedef enum
    {
        SPIFFS,
        SD
    } filesystem_type_t;

    esp_err_t initSD(const char *base_path);
    esp_err_t initSpiffs(void);
    void listFolder(const char *rootDir);
    int constrain(int x, int a, int b);
    // void startStreamServer(void);
    void startCamera();
    void wifiConnect(void);
    void startMdnsService(void);
    esp_err_t startCameraServer(int hPort, int sPort, const char *base_path);
    char *allocateMemory(char *aPtr, size_t aSize, bool fail, bool psramOnly);
    void initPsram(void);
    bool psramFound(void);
    void initLed(void);
    void flashLED(int flashtime);
    void setLamp(int newVal);
    void saveConfig(filesystem_type_t fstype);
    void loadConfig(filesystem_type_t fstype);
    void deleteConfig(filesystem_type_t fstype);
    void printConfig(void);

    esp_err_t initRgbLed(void);
    esp_err_t setRgbLedLevel(uint8_t level);
    esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif
