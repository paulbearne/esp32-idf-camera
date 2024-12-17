#include "main.h"

#ifdef CONFIG_FRAMESIZE_96X96
#define CAMERA_FRAMESIZE FRAMESIZE_96X96
#endif
#ifdef CONFIG_FRAMESIZE_QQVGA
#define CAMERA_FRAMESIZE FRAMESIZE_QQVGA
#endif
#ifdef CONFIG_FRAMESIZE_128X128
#define CAMERA_FRAMESIZE FRAMESIZE_128X128
#endif
#ifdef CONFIG_FRAMESIZE_QCIF
#define CAMERA_FRAMESIZE FRAMESIZE_96X96
#endif
#ifdef CONFIG_FRAMESIZE_HQVGA
#define CAMERA_FRAMESIZE FRAMESIZE_HQVGA
#endif
#ifdef CONFIG_FRAMESIZE_240X240
#define CAMERA_FRAMESIZE FRAMESIZE_240X240
#endif
#ifdef CONFIG_FRAMESIZE_QVGA
#define CAMERA_FRAMESIZE FRAMESIZE_QVGA
#endif
#ifdef CONFIG_FRAMESIZE_320X320
#define CAMERA_FRAMESIZE FRAMESIZE_320X320
#endif
#ifdef CONFIG_FRAMESIZE_CIF
#define CAMERA_FRAMESIZE FRAMESIZE_CIF
#endif
#ifdef CONFIG_FRAMESIZE_HVGA
#define CAMERA_FRAMESIZE FRAMESIZE_HVGA
#endif
#ifdef CONFIG_FRAMESIZE_VGA
#define CAMERA_FRAMESIZE FRAMESIZE_VGA
#endif
#ifdef CONFIG_FRAMESIZE_SVGA
#define CAMERA_FRAMESIZE FRAMESIZE_SVGA
#endif
#ifdef CONFIG_FRAMESIZE_XGA
#define CAMERA_FRAMESIZE FRAMESIZE_XGA
#endif
#ifdef CONFIG_FRAMESIZE_HD
#define CAMERA_FRAMESIZE FRAMESIZE_HD
#endif
#ifdef CONFIG_FRAMESIZE_SXGA
#define CAMERA_FRAMESIZE FRAMESIZE_SXGA
#endif
#ifdef CONFIG_FRAMESIZE_UXGA
#define CAMERA_FRAMESIZE FRAMESIZE_UXGA
#endif


static const char *TAG = "BeyBlades CameraSTask";
char critERR[255] = "";
bool debugData;
int sensorPID;

#if !defined(XCLOCK_FREQ)
unsigned long xclk = 8;
#else
unsigned long xclk = XCLOCK_FREQ;
#endif

int minFrameTime = CONFIG_CAMERA_MIN_FRAME_TIME;
int camRotation = CONFIG_CAMERA_ROTATION;
/*
#if defined(LAMP_DISABLE)
int lampVal = -1; // lamp is disabled in config
#elif defined(LAMP_PIN)
#if defined(LAMP_DEFAULT)
int lampVal = constrain(LAMP_DEFAULT, 0, 100); // initial lamp value, range 0-100
#else
int lampVal = 0; // default to off
#endif
#else
int lampVal = -1; // no lamp pin assigned
#endif
*/
camera_config_t config;

void startCamera()
{
    // Populate camera config structure with hardware and other defaults
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.fb_location = CAMERA_FB_IN_PSRAM,
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // need a delay here or the next serial o/p gets missed
        ESP_LOGI(TAG, "\r\n\r\nCRITICAL FAILURE: Camera sensor failed to initialise.\r\n\r\n");
        ESP_LOGI(TAG, "A full (hard, power off/on) reboot will probably be needed to recover from this.\r\n");
        ESP_LOGI(TAG, "Meanwhile; this unit will reboot in 1 minute since these errors sometime clear automatically\r\n");
        strcpy(critERR, (char *)"<h1>Error!</h1><hr><p>Camera module failed to initialise!</p><p>Please reset (power off/on) the camera.</p>");
        strcpy(critERR, (char *)"<p>We will continue to reboot once per minute since this error sometimes clears automatically.</p>");
 
    }
    else
    {
        ESP_LOGI(TAG, "Camera init succeeded");

        // Get a reference to the sensor
        sensor_t *camsensor = esp_camera_sensor_get();

        // Dump camera module, warn for unsupported modules.
        sensorPID = camsensor->id.PID;
        switch (sensorPID)
        {
        case OV9650_PID:
            ESP_LOGI(TAG, "WARNING: OV9650 camera module is not properly supported, will fallback to OV2640 operation");
            break;
        case OV7725_PID:
            ESP_LOGI(TAG, "WARNING: OV7725 camera module is not properly supported, will fallback to OV2640 operation");
            break;
        case OV2640_PID:
            ESP_LOGI(TAG, "OV2640 camera module detected");
            break;
        case OV3660_PID:
            ESP_LOGI(TAG, "OV3660 camera module detected");
            break;
        default:
            ESP_LOGI(TAG, "WARNING: Camera module is unknown and not properly supported, will fallback to OV2640 operation");
        }

        // OV3660 initial sensors are flipped vertically and colors are a bit saturated
        if (sensorPID == OV3660_PID)
        {
            camsensor->set_vflip(camsensor, 1);       // flip it back
            camsensor->set_brightness(camsensor, 1);  // up the blightness just a bit
            camsensor->set_saturation(camsensor, -2); // lower the saturation
        }

        // Config can override mirror and flip
        if (conf.hmirror)
        {
            camsensor->set_hmirror(camsensor, conf.hmirror);
        }
        if (conf.vflip)
        {
            camsensor->set_vflip(camsensor, conf.vflip);
        }
        // set initial frame rate // get sensor reference
      //  sensor_t *camsensor = esp_camera_sensor_get();

        // process local settings
        camRotation = conf.camRotation;

        // process camera settings
        camsensor->set_framesize(camsensor, conf.framesize);
        camsensor->set_quality(camsensor, conf.quality);
      //  camsensor->set_xclk(camsensor, LEDC_TIMER_0, conf.xclk);
        camsensor->set_brightness(camsensor, conf.brightness);
        camsensor->set_contrast(camsensor, conf.contrast);
        camsensor->set_saturation(camsensor, conf.saturation);
        camsensor->set_special_effect(camsensor, conf.specialeffect);
        camsensor->set_wb_mode(camsensor, conf.wbmode);
        camsensor->set_whitebal(camsensor, conf.awb);
        camsensor->set_awb_gain(camsensor, conf.awbgain);
        camsensor->set_exposure_ctrl(camsensor, conf.aec);
        camsensor->set_aec2(camsensor, conf.aec2);
        camsensor->set_ae_level(camsensor, conf.aelevel);
        camsensor->set_aec_value(camsensor, conf.aecvalue);
        camsensor->set_gain_ctrl(camsensor, conf.agc);
        camsensor->set_agc_gain(camsensor, conf.agcgain);
        camsensor->set_gainceiling(camsensor, conf.gainceiling);
        camsensor->set_bpc(camsensor, conf.bpc);
        camsensor->set_wpc(camsensor, conf.wpc);
        camsensor->set_raw_gma(camsensor, conf.rawgma);
        camsensor->set_lenc(camsensor, conf.lenc);
        camsensor->set_vflip(camsensor, conf.vflip);
        camsensor->set_hmirror(camsensor, conf.hmirror);
        camsensor->set_dcw(camsensor, conf.dcw);
        camsensor->set_colorbar(camsensor, conf.colorbar);
        camsensor->set_framesize(camsensor, conf.framesize);

    }
}