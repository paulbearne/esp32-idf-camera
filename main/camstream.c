#include "main.h"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE (200 * 1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE 8192

static const char *TAG = "BeyBlades CameraStream";

bool autoLamp = false;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
bool captivePortal = false;

#define CAM_STREAMER_MAX_CLIENTS 10
cam_streamer_t *cam_streamer;

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#if defined(DEFAULT_INDEX_FULL)
char default_index[] = "full";
#else
char default_index[] = "simple";
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
#define _STREAM_HEADERS "HTTP/1.1 200 OK\r\n"                \
                        "Access-Control-Allow-Origin: *\r\n" \
                        "Connection: Keep-Alive\r\n"         \
                        "Keep-Alive: timeout=15\r\n"         \
                        "Content-Type: multipart/x-mixed-replace;boundary=" PART_BOUNDARY "\r\n"

#define _TEXT_HEADERS "HTTP/1.1 200 OK\r\n"                \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Connection: Close\r\n"              \
                      "Content-Type: text/plain\r\n\r\n"
struct file_server_data
{
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];
    int port;

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

extern bool debugData;
//extern int lampVal;
extern bool autoLamp;
// extern void setLamp(int);
cam_streamer_t *cam_streamer;
// static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
// static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static uint8_t is_send_error(int r)
{
    switch (r)
    {
    case HTTPD_SOCK_ERR_INVALID:
        ESP_LOGI(TAG, "[cam_streamer] invalid argument occured!\n");
        return 1;
    case HTTPD_SOCK_ERR_TIMEOUT:
        ESP_LOGI(TAG, "[cam_streamer] timeout/interrupt occured!\n");
        return 1;
    case HTTPD_SOCK_ERR_FAIL:
        ESP_LOGI(TAG, "[cam_streamer] unrecoverable error while send()!\n");
        return 1;
    case ESP_ERR_INVALID_ARG:
        ESP_LOGI(TAG, "[text-streamer] session closed!\n");
        return 1;
    default:
        // ESP_LOGI(TAG, "sent %d bytes!\n", r);
        return 0;
    }
}

void cam_streamer_init(cam_streamer_t *s, httpd_handle_t server, uint16_t frame_delay)
{
    // ESP_LOGI(TAG, "streamer init");
    memset(s, 0, sizeof(cam_streamer_t));
    s->frame_delay = 1000 * frame_delay;
    s->clients = xQueueCreate(CAM_STREAMER_MAX_CLIENTS * 2, sizeof(int));
    s->server = server;
}

// frame_delay must be in ms (not us)
void cam_streamer_set_frame_delay(cam_streamer_t *s, uint16_t frame_delay)
{
    // ESP_LOGI(TAG, "streamer set frame delay");
    s->frame_delay = 1000 * frame_delay;
}

static void cam_streamer_update_frame(cam_streamer_t *s)
{
    uint8_t l = 0;
    while (!__atomic_compare_exchange_n(&s->buf_lock, &l, 1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
        l = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (s->buf)
        esp_camera_fb_return(s->buf);

    s->buf = esp_camera_fb_get();

    s->last_updated = esp_timer_get_time();
    s->part_len = snprintf(s->part_buf, 64, _STREAM_PART, s->buf->len);
    __atomic_store_n(&s->buf_lock, 0, __ATOMIC_RELAXED);
    // ESP_LOGI(TAG, "[cam_streamer] fetched new frame\n");
}

static void cam_streamer_decrement_num_clients(cam_streamer_t *s)
{
    size_t num_clients = s->num_clients;
    while (num_clients > 0 && !__atomic_compare_exchange_n(&s->num_clients, &num_clients, num_clients - 1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        ;
    // ESP_LOGI(TAG, "[cam_streamer] num_clients decremented\n");
}

void cam_streamer_task(void *p)
{
    cam_streamer_t *s = (cam_streamer_t *)p;
    ESP_LOGI(TAG, "started streamer task");
    uint64_t last_time = 0, current_time;
    int fd;
    unsigned int n_entries;
    for (;;)
    {
        while (!(n_entries = uxQueueMessagesWaiting(s->clients)))
        {
            if (autoLamp && conf.lampval != -1)
                setRgbLedLevel(0);
            vTaskSuspend(NULL);
            if (autoLamp && conf.lampval != -1)
                setRgbLedLevel(conf.lampval);
        }

        current_time = esp_timer_get_time();
        if ((current_time - last_time) < s->frame_delay)
            vTaskDelay((s->frame_delay - (current_time - last_time)) / (pdMS_TO_TICKS(1000)));

        cam_streamer_update_frame(s);

        //    ESP_LOGI(TAG, "[cam_streamer] frame_size: %luB %lums\n", (long unsigned int)s->buf->len, (long unsigned int)((current_time - last_time) / 1000));
        last_time = current_time;

        for (unsigned int i = 0; i < n_entries; ++i)
        {
            if (xQueueReceive(s->clients, &fd, pdMS_TO_TICKS(10)) == pdFALSE)
            {
                ESP_LOGI(TAG, "[cam_streamer] failed to dequeue fd!\n");
                continue;
            }

            //     ESP_LOGI(TAG, "[cam_streamer] fd %d dequeued\n", fd);

            if (is_send_error(httpd_socket_send(s->server, fd, s->part_buf, s->part_len, 0)))
            {
                cam_streamer_decrement_num_clients(s);
                continue;
            }

            if (is_send_error(httpd_socket_send(s->server, fd, (const char *)s->buf->buf, s->buf->len, 0)))
            {
                cam_streamer_decrement_num_clients(s);
                continue;
            }

            if (is_send_error(httpd_socket_send(s->server, fd, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY), 0)))
            {
                cam_streamer_decrement_num_clients(s);
                continue;
            }

            xQueueSend(s->clients, (void *)&fd, 10 / portTICK_PERIOD_MS);
            //  ESP_LOGI(TAG, "[cam_streamer] fd %d requeued\n", fd);
        }
    }
}

void cam_streamer_start(cam_streamer_t *s)
{
    ESP_LOGI(TAG, "streamer start");
    BaseType_t r = xTaskCreate(cam_streamer_task, "cam_streamer", 10 * 1024, (void *)s, tskIDLE_PRIORITY + 3, &s->task);

    if (r != pdPASS)
    {
        ESP_LOGI(TAG, "[cam_streamer] failed to create task!\n");
    }
    else
    {
        ESP_LOGI(TAG, "streamer task started");
    }
}

void cam_streamer_stop(cam_streamer_t *s)
{
    vTaskDelete(s->task);
}

size_t cam_streamer_get_num_clients(cam_streamer_t *s)
{
    return s->num_clients;
}

void cam_streamer_dequeue_all_clients(cam_streamer_t *s)
{
    xQueueReset(s->clients);
    __atomic_exchange_n(&s->num_clients, 0, __ATOMIC_RELAXED);
}

bool cam_streamer_enqueue_client(cam_streamer_t *s, int fd)
{
    if (s->num_clients >= CAM_STREAMER_MAX_CLIENTS)
    {
        if (httpd_socket_send(s->server, fd, _TEXT_HEADERS, strlen(_TEXT_HEADERS), 0))
        {
            ESP_LOGI(TAG, "failed sending text headers!\n");
            return false;
        }

#define EMSG "too many clients"
        if (httpd_socket_send(s->server, fd, EMSG, strlen(EMSG), 0))
        {
            ESP_LOGI(TAG, "failed sending message\n");
            return false;
        }
#undef EMSG
        close(fd);
        return false;
    }

    // ESP_LOGI(TAG, "sending stream headers:\n%s\nLength: %d\n", _STREAM_HEADERS, strlen(_STREAM_HEADERS));
    if (is_send_error(httpd_socket_send(s->server, fd, _STREAM_HEADERS, strlen(_STREAM_HEADERS), 0)))
    {
        ESP_LOGI(TAG, "failed sending headers!\n");
        return false;
    }

    if (is_send_error(httpd_socket_send(s->server, fd, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY), 0)))
    {
        ESP_LOGI(TAG, "failed sending boundary!\n");
        return false;
    }

    const BaseType_t r = xQueueSend(s->clients, (void *)&fd, 10 * portTICK_PERIOD_MS);
    if (r != pdTRUE)
    {
        ESP_LOGI(TAG, "[cam_streamer] failed to enqueue fd %d\n", fd);
#define EMSG "failed to enqueue"
        httpd_socket_send(s->server, fd, EMSG, strlen(EMSG), 0);
#undef EMSG
    }
    else
    {
        //      ESP_LOGI(TAG, "[cam_streamer] socket %d enqueued\n", fd);
        __atomic_fetch_add(&s->num_clients, 1, __ATOMIC_RELAXED);
        vTaskResume(s->task);
    }

    return r == pdTRUE ? true : false;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;

    ESP_LOGI(TAG, "Capture Requested");
    if (autoLamp && (conf.lampval != -1))
    {
        setRgbLedLevel(conf.lampval);
        vTaskDelay(pdMS_TO_TICKS(75)); // coupled with the status led flash this gives ~150ms for lamp to settle.
    }
    flashLED(75); // little flash of status LED

    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGI(TAG, "CAPTURE: failed to acquire frame");
        httpd_resp_send_500(req);
        if (autoLamp && (conf.lampval != -1))
            setRgbLedLevel(0);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t fb_len = 0;
    if (fb->format == PIXFORMAT_JPEG)
    {
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    }
    else
    {
        res = ESP_FAIL;
        ESP_LOGI(TAG, "Capture Error: Non-JPEG image returned by camera module");
    }
    esp_camera_fb_return(fb);
    fb = NULL;

    int64_t fr_end = esp_timer_get_time();
    if (debugData)
    {
        ESP_LOGI(TAG, "JPG: %uB %ums\r\n", (unsigned int)(fb_len), (unsigned int)((fr_end - fr_start) / 1000));
    }
    imagesServed++;
    if (autoLamp && (conf.lampval != -1))
    {
        setRgbLedLevel(0);
    }
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    //   ESP_LOGI(TAG, "in stream handler");
    if (fd == -1)
    {
        ESP_LOGI(TAG, "[stream_handler] could not get socket fd!\n");
        return ESP_FAIL;
    }
    //   ESP_LOGI(TAG, "socket queued");
    if (cam_streamer_enqueue_client(cam_streamer, fd))
        ++streamsServed;

    return ESP_OK;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[32] = {
        0,
    };
    char value[32] = {
        0,
    };

    flashLED(75);

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (strlen(critERR) > 0)
    {
        return httpd_resp_send_500(req);
    }
    int val = atoi(value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;
    if (strcmp(variable, "framesize") == 0) // frame size
    {
        if (s->pixformat == PIXFORMAT_JPEG)
        {
            res = s->set_framesize(s, (framesize_t)val);
            conf.framesize = (framesize_t)val;
        }
    }
    else if (strcmp(variable, "quality") == 0) // quality
    {
        res = s->set_quality(s, val);
        conf.quality = val;
    }
    else if (strcmp(variable, "xclk") == 0) // seems to screw up if set
    {
       // xclk = val;
        conf.xclk = val * 1000000;
        //res = s->set_xclk(s, LEDC_TIMER_0, conf.xclk);
        res = 0;
    }
    else if (strcmp(variable, "contrast") == 0) // contrast
    {
        res = s->set_contrast(s, val);
        conf.contrast = val;
    }
    else if (strcmp(variable, "brightness") == 0)  // brightness
    {
        res = s->set_brightness(s, val);
        conf.brightness = val;
    }
    else if (strcmp(variable, "saturation") == 0)  
    {
        res = s->set_saturation(s, val);
        conf.saturation = val;
    }
    else if (strcmp(variable, "gainceiling") == 0)
    {
        res = s->set_gainceiling(s, (gainceiling_t)val);
        conf.gainceiling =(gainceiling_t)val;
    }
    else if (strcmp(variable, "colorbar") == 0)
    {
        res = s->set_colorbar(s, val);
        conf.colorbar = val;
    }
    else if (strcmp(variable, "awb") == 0)
    {
        res = s->set_whitebal(s, val);
        conf.whitebalance = val;
    }
    else if (strcmp(variable, "agc") == 0)
    {
        res = s->set_gain_ctrl(s, val);
        conf.agcgain = val;
    }
    else if (strcmp(variable, "aec") == 0)
    {
        res = s->set_exposure_ctrl(s, val);
        conf.exposurectl = val;
    }
    else if (strcmp(variable, "hmirror") == 0)
    {
        res = s->set_hmirror(s, val);
        conf.hmirror = val;
    }
    else if (strcmp(variable, "vflip") == 0)
    {
        res = s->set_vflip(s, val);
        conf.vflip = val;
    }
    else if (strcmp(variable, "awb_gain") == 0)
    {
        res = s->set_awb_gain(s, val);
        conf.awbgain = val;
    }
    else if (strcmp(variable, "agc_gain") == 0)
    {
        res = s->set_agc_gain(s, val);
        conf.agcgain = val;
    }
    else if (strcmp(variable, "aec_value") == 0)
    {
        res = s->set_aec_value(s, val);
        conf.aecvalue = val;
    }
    else if (strcmp(variable, "aec2") == 0)
    {
        res = s->set_aec2(s, val);
        conf.aec2 = val;
    }
    else if (strcmp(variable, "dcw") == 0)
    {
        res = s->set_dcw(s, val);
        conf.dcw = val;
    }
    else if (strcmp(variable, "bpc") == 0)
    {
        res = s->set_bpc(s, val);
        conf.bpc = val;
    }
    else if (strcmp(variable, "wpc") == 0)
    {
        res = s->set_wpc(s, val);
        conf.wpc = val;
    }
    else if (strcmp(variable, "raw_gma") == 0)
    {
        res = s->set_raw_gma(s, val);
        conf.rawgma = val;
    }
    else if (strcmp(variable, "lenc") == 0)
    {
        res = s->set_lenc(s, val);
        conf.lenc = val;
    }
    else if (strcmp(variable, "special_effect") == 0)
    {
        res = s->set_special_effect(s, val);
        conf.specialeffect = val;
    }
    else if (strcmp(variable, "wb_mode") == 0)
    {
        res = s->set_wb_mode(s, val);
        conf.wbmode = val;
    }
    else if (strcmp(variable, "ae_level") == 0)
    {
        res = s->set_ae_level(s, val);
        conf.aelevel = val;
    }
    else if (strcmp(variable, "rotate") == 0)
    {
        camRotation = val;
        conf.camRotation = val;
    }
    else if (strcmp(variable, "min_frame_time") == 0)
    {
        cam_streamer_set_frame_delay(cam_streamer, val);
        conf.minFrameTime = val;
    }
    else if(strcmp(variable,"lampws2812")==0){
        conf.usews2812 = val == 1;
    }
    else if ((strcmp(variable, "autolamp") == 0) && (conf.lampval != -1))
    {
        autoLamp = val;
        conf.autolamp = val;
        if (autoLamp)
        {
            if (cam_streamer_get_num_clients(cam_streamer) > 0)
                setRgbLedLevel(conf.lampval);
            else
                setRgbLedLevel(0);
        }
        else
        {
            setRgbLedLevel(conf.lampval);
        }
    }
    else if ((strcmp(variable, "lamp") == 0) && (conf.lampval != -1))
    {
        conf.lampval = constrain(val,0,255);
        //lampVal = constrain(val, 0, 100);
        if (autoLamp)
        {
            if (cam_streamer_get_num_clients(cam_streamer) > 0)
                setRgbLedLevel(conf.lampval);
            else
                setRgbLedLevel(0);
        }
        else
        {
            setRgbLedLevel(conf.lampval);
        }
    } 
    else if (strcmp(variable, "save_prefs") == 0)
    {
        if (filesystem)
        {
            flashLED(1000);
            // read current sensor settings
            saveConfig(SPIFFS);
        }
    }
    else if (strcmp(variable, "clear_prefs") == 0)
    {
        if (filesystem)
        {
            flashLED(1000);
            deleteConfig(SPIFFS);
        }
    }
    else if (strcmp(variable, "reboot") == 0)
    {
        if (conf.lampval != -1)
        {
            setRgbLedLevel(0);
        }
        ESP_LOGI(TAG, "REBOOT requested");
        while (true)
        {
            flashLED(50);
            vTaskDelay(pdMS_TO_TICKS(150));
            esp_restart();
        }
    }
    else
    {
        res = -1;
    }
    if (res)
    {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];
    char *p = json_response;
    ESP_LOGI(TAG, "sending status");
    *p++ = '{';
    // Do not get attempt to get sensor when in error; causes a panic..
    if (strlen(critERR) == 0)
    {
        sensor_t *s = esp_camera_sensor_get();
        p += sprintf(p, "\"lamp\":%d,", conf.lampval);
        p += sprintf(p, "\"autolamp\":%d,", conf.autolamp);
        p += sprintf(p,"\"lampws2812\":%d,",conf.usews2812 ? 1:0);
        p += sprintf(p, "\"min_frame_time\":%d,",(int)conf.minFrameTime);
        p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
        p += sprintf(p, "\"quality\":%u,", s->status.quality);
        p += sprintf(p, "\"xclk\":%lu,", conf.xclk);
        p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
        p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
        p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
        p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
        p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
        p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
        p += sprintf(p, "\"awb\":%u,", s->status.awb);
        p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
        p += sprintf(p, "\"aec\":%u,", s->status.aec);
        p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
        p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
        p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
        p += sprintf(p, "\"agc\":%u,", s->status.agc);
        p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
        p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
        p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
        p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
        p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
        p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
        p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
        p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
        p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
        p += sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
        p += sprintf(p, "\"cam_name\":\"%s\",", CONFIG_APP_NAME);
        p += sprintf(p, "\"code_ver\":\"%s\",", CONFIG_APP_VER);
        p += sprintf(p, "\"rotate\":\"%d\",", CONFIG_CAMERA_ROTATION);
        p += sprintf(p, "\"stream_url\":\"%s\"", streamURL);
    }
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t info_handler(httpd_req_t *req)
{
    static char json_response[256];
    char *p = json_response;
    ESP_LOGI(TAG, "sending info");
    *p++ = '{';
    p += sprintf(p, "\"cam_name\":\"%s\",", CONFIG_APP_NAME);
    p += sprintf(p, "\"rotate\":\"%s\",", CONFIG_APP_VER);
    p += sprintf(p, "\"stream_url\":\"%s\"", streamURL);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

#define IS_FILE_EXT(filename, ext) (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf"))
    {
        return httpd_resp_set_type(req, "application/pdf");
    }
    else if (IS_FILE_EXT(filename, ".html"))
    {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg"))
    {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".ico"))
    {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".svg"))
    {
        return httpd_resp_set_type(req, "image/svg+xml");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

static const char *getPathFromUri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest)
    {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash)
    {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

static esp_err_t sendHtmlfile(httpd_req_t *req, char *filepath, const char *filename)
{
    FILE *fd = NULL;
    struct stat file_stat;
    ESP_LOGI(TAG, "filepath %s", filepath);
    if (stat(filepath, &file_stat) == -1)
    {
        // else if (strcmp(filename, "/favicon.ico") == 0)
        //{
        //     return favicon_get_handler(req);
        // }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }
    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        // Respond with 500 Internal Server Error
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filename);

    // Retrieve the pointer to scratch buffer for temporary storage
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do
    {
        // Read file in chunks into the scratch buffer
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            // Send the buffer contents as HTTP response chunk
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                // Abort sending file
                httpd_resp_sendstr_chunk(req, NULL);
                // Respond with 500 Internal Server Error
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        // Keep looping till the whole file is sent
    } while (chunksize != 0);

    // Close file after sending complete
    fclose(fd);

    // Respond with an empty chunk to signal HTTP response completion
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t getFilehandler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

  //  FILE *fd = NULL;
    struct stat file_stat;

    ESP_LOGI(TAG, "filehandler uri %s", req->uri);

    const char *filename = getPathFromUri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                          req->uri, sizeof(filepath));
    ESP_LOGI(TAG, "filename %s", filename);
    if (!filename)
    {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "filepath %s", filepath);
    if (stat(filepath, &file_stat) == -1)
    {

        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);

        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    return sendHtmlfile(req, filepath, filename);
}

char *strreplace(char *s, const char *s1, const char *s2)
{
    char *p = strstr(s, s1);
    if (p != NULL)
    {
        size_t len1 = strlen(s1);
        size_t len2 = strlen(s2);
        if (len1 != len2)
            memmove(p + len2, p + len1, strlen(p + len1) + 1);
        memcpy(p, s2, len2);
    }
    return s;
}

static esp_err_t error_handler(httpd_req_t *req)
{
    FILE *fd = NULL;
  //  struct stat file_stat;
    flashLED(75);
    ESP_LOGI(TAG, "Sending error page");
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    fd = fopen("/spiffs/error.html", "r");
    do
    {
        // Read file in chunks into the scratch buffer replace the data
        // read 1024 bytes at a time so we can
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            strreplace(chunk, "<APPURL>", httpURL);
            strreplace(chunk, "<STREAMURL>", streamURL);
            strreplace(chunk, "<CAMNAME>", CONFIG_APP_NAME);
            // Send the buffer contents as HTTP response chunk
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                // Abort sending file
                httpd_resp_sendstr_chunk(req, NULL);
                // Respond with 500 Internal Server Error
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        // Keep looping till the whole file is sent
    } while (chunksize != 0);

    // Close file after sending complete
    fclose(fd);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char view[32] = {
        0,
    };
    FILE *fd = NULL;
    struct stat file_stat;

    // See if we have a specific target (full/simple/portal) and serve as appropriate
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "view", view, sizeof(view)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        // no target specified; default.
        strcpy(view, default_index);
        // If captive portal is active send that instead
        if (captivePortal)
        {
            strcpy(view, "portal");
        }
    }

    if (strncmp(view, "simple", sizeof(view)) == 0)
    {
        ESP_LOGI(TAG, "Simple index page requested");
        if (strlen(critERR) > 0)
        {
            return error_handler(req);
        }
        //  httpd_resp_set_type(req, "text/html");
        // httpd_resp_set_hdr(req, "Content-Encoding", "identity");
        // return httpd_resp_send(req, (const char *)index_simple_html, index_simple_html_len);
        return sendHtmlfile(req, "/spiffs/simpleidx.html", "simpleidx.html");
    }
    else if (strncmp(view, "full", sizeof(view)) == 0)
    {
        ESP_LOGI(TAG, "Full index page requested");
        if (strlen(critERR) > 0)
            return error_handler(req);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Content-Encoding", "identity");
        if (sensorPID == OV2640_PID)
        {
            return sendHtmlfile(req, "/spiffs/ov2640idx.html", "ov2640idx.html");
            // httpd_resp_send(req, (const char *)index_ov3660_html, index_ov3660_html_len);
        }
        return sendHtmlfile(req, "/spiffs/ov3660idx.html", "ov3660idx.html");
        // return httpd_resp_send(req, (const char *)index_ov2640_html, index_ov2640_html_len);
    }
    else if (strncmp(view, "portal", sizeof(view)) == 0)
    {
        // Prototype captive portal landing page.
        ESP_LOGI(TAG, "Portal page requested");
        // std::string s(portal_html);

        if (stat("/spiffs/portal.html", &file_stat) == -1)
        {
            ESP_LOGE(TAG, "Failed to stat file : /spiffs/portal.html");
            /* Respond with 404 Not Found */
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
            return ESP_FAIL;
        }
        // set chunk to 1024 for fund replace
        char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
        size_t chunksize;
        fd = fopen("/spiffs/portal.html", "r");
        do
        {
            // Read file in chunks into the scratch buffer replace the data
            // read 1024 bytes at a time so we can
            chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

            if (chunksize > 0)
            {
                strreplace(chunk, "<APPURL>", httpURL);
                strreplace(chunk, "<STREAMURL>", streamURL);
                strreplace(chunk, "<CAMNAME>", CONFIG_APP_NAME);
                // Send the buffer contents as HTTP response chunk
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
                {
                    fclose(fd);
                    ESP_LOGE(TAG, "File sending failed!");
                    // Abort sending file
                    httpd_resp_sendstr_chunk(req, NULL);
                    // Respond with 500 Internal Server Error
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                    return ESP_FAIL;
                }
            }

            // Keep looping till the whole file is sent
        } while (chunksize != 0);

        // Close file after sending complete
        fclose(fd);
        return ESP_OK;
    }
    else
    {
        ESP_LOGI(TAG, "Unknown page requested: ");
        ESP_LOGI(TAG, "%s", view);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}

static esp_err_t dump_handler(httpd_req_t *req)
{
  /*  flashLED(75);
     ESP_LOGI(TAG,"\r\nDump requested via Web");
     serialDump();
     static char dumpOut[2000] = "";
     char * d = dumpOut;
     // Header
     d+= sprintf(d,"<html><head><meta charset=\"utf-8\">\n");
     d+= sprintf(d,"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n");
     d+= sprintf(d,"<title>%s - Status</title>\n", CONFIG_APP_NAME);
     d+= sprintf(d,"<link rel=\"icon\" type=\"image/png\" sizes=\"32x32\" href=\"/favicon-32x32.png\">\n");
     d+= sprintf(d,"<link rel=\"icon\" type=\"image/png\" sizes=\"16x16\" href=\"/favicon-16x16.png\">\n");
     d+= sprintf(d,"<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">\n");
     d+= sprintf(d,"</head>\n");
     d+= sprintf(d,"<body>\n");
     d+= sprintf(d,"<img src=\"/logo.svg\" style=\"position: relative; float: right;\">\n");
     if (strlen(critERR) > 0) {
         d+= sprintf(d,"%s<hr>\n", critERR);
     }
     d+= sprintf(d,"<h1>ESP32 Cam Webserver</h1>\n");
     // Module
     d+= sprintf(d,"Name: %s<br>\n", CONFIG_APP_NAME);
     d+= sprintf(d,"Firmware: %s (base: %s)<br>\n", CONFIG_APP_VER, baseVersion);
    // float sketchPct = 100 * sketchSize / sketchSpace;
   //  d+= sprintf(d,"Sketch Size: %i (total: %i, %.1f%% used)<br>\n", sketchSize, sketchSpace, sketchPct);
     //d+= sprintf(d,"MD5: %s<br>\n", sketchMD5.c_str());
     d+= sprintf(d,"ESP sdk: %s<br>\n",getSdkVersion());
     // Network
     d+= sprintf(d,"<h2>WiFi</h2>\n");
     if (CONFIG_WIFI_AP_SSID) {
         if (captivePortal) {
             d+= sprintf(d,"Mode: AccessPoint with captive portal<br>\n");
         } else {
             d+= sprintf(d,"Mode: AccessPoint<br>\n");
         }
         d+= sprintf(d,"SSID: %s<br>\n", CONFIG_WIFI_AP_SSID);
     } else {
         d+= sprintf(d,"Mode: Client<br>\n");
        // String ssidName = WiFi.SSID();
       //  d+= sprintf(d,"SSID: %s<br>\n", ssidName.c_str());
       //  d+= sprintf(d,"Rssi: %i<br>\n", WiFi.RSSI());
       //  String bssid = WiFi.BSSIDstr();
      //   d+= sprintf(d,"BSSID: %s<br>\n", bssid.c_str());
     }

    // d+= sprintf(d,"IP address: %d.%d.%d.%d<br>\n", ip[0], ip[1], ip[2], ip[3]);
   //  if (!CONFIG_WIFI_AP_SSID) {
    //     d+= sprintf(d,"Netmask: %d.%d.%d.%d<br>\n", net[0], net[1], net[2], net[3]);
    //     d+= sprintf(d,"Gateway: %d.%d.%d.%d<br>\n", gw[0], gw[1], gw[2], gw[3]);
  //   }
    // d+= sprintf(d,"Http port: %i, Stream port: %i<br>\n", httpPort, streamPort);
    // byte mac[6];
    // WiFi.macAddress(mac);
    // d+= sprintf(d,"MAC: %02X:%02X:%02X:%02X:%02X:%02X<br>\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

     // System
    // d+= sprintf(d,"<h2>System</h2>\n");
    // if (haveTime) {
    //     struct tm timeinfo;
    //     if(getLocalTime(&timeinfo)){
    //         char timeStringBuff[50]; //50 chars should be enough
     //        strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S, %A, %B %d %Y", &timeinfo);
    //         //print like "const char*"
     //        d+= sprintf(d,"Time: %s<br>\n", timeStringBuff);
     //    }
     }
     int64_t sec = esp_timer_get_time() / 1000000;
     int64_t upDays = int64_t(floor(sec/86400));
     int upHours = int64_t(floor(sec/3600)) % 24;
     int upMin = int64_t(floor(sec/60)) % 60;
     int upSec = sec % 60;
  //   int McuTc = (temprature_sens_read() - 32) / 1.8; // celsius
  //   int McuTf = temprature_sens_read(); // fahrenheit

     d+= sprintf(d,"Up: %" PRId64 ":%02i:%02i:%02i (d:h:m:s)<br>\n", upDays, upHours, upMin, upSec);
     d+= sprintf(d,"Active streams: %i, Previous streams: %lu, Images captured: %lu<br>\n", cam_streamer_get_num_clients(cam_streamer), streamsServed, imagesServed);
     d+= sprintf(d,"CPU Freq: %i MHz, Xclk Freq: %i MHz<br>\n", ESP.getCpuFreqMHz(), xclk);
     d+= sprintf(d,"<span title=\"NOTE: Internal temperature sensor readings can be innacurate on the ESP32-c1 chipset, and may vary significantly between devices!\">");
   //  d+= sprintf(d,"MCU temperature : %i &deg;C, %i &deg;F</span>\n<br>", McuTc, McuTf);
     d+= sprintf(d,"Heap: %i, free: %i, min free: %i, max block: %i<br>\n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
     if (psramFound()) {
         d+= sprintf(d,"Psram: %i, free: %i, min free: %i, max block: %i<br>\n", ESP.getPsramSize(), ESP.getFreePsram(), ESP.getMinFreePsram(), ESP.getMaxAllocPsram());
     } else {
         d+= sprintf(d,"Psram: <span style=\"color:red;\">Not found</span>, please check your board configuration.<br>\n");
         d+= sprintf(d,"- High resolution/quality images & streams will show incomplete frames due to low memory.<br>\n");
     }
     if (filesystem && (SPIFFS.totalBytes() > 0)) {
         d+= sprintf(d,"Spiffs: %i, used: %i<br>\n", SPIFFS.totalBytes(), SPIFFS.usedBytes());
     } else {
         d+= sprintf(d,"Spiffs: <span style=\"color:red;\">No filesystem found</span>, please check your board configuration.<br>\n");
         d+= sprintf(d,"- saving and restoring camera settings will not function without this.<br>\n");
     }

     // Footer
     d+= sprintf(d,"<br><div class=\"input-group\">\n");
     d+= sprintf(d,"<button title=\"Instant Refresh; the page reloads every minute anyway\" onclick=\"location.replace(document.URL)\">Refresh</button>\n");
     d+= sprintf(d,"<button title=\"Force-stop all active streams on the camera module\" ");
     d+= sprintf(d,"onclick=\"let throwaway = fetch('stop');setTimeout(function(){\nlocation.replace(document.URL);\n}, 200);\">Kill Stream</button>\n");
     d+= sprintf(d,"<button title=\"Close this page\" onclick=\"javascript:window.close()\">Close</button>\n");
     d+= sprintf(d,"</div>\n</body>\n");
     // A javascript timer to refresh the page every minute.
     d+= sprintf(d,"<script>\nsetTimeout(function(){\nlocation.replace(document.URL);\n}, 60000);\n");
     d+= sprintf(d,"</script>\n</html>\n");
     *d++ = 0;
     httpd_resp_set_type(req, "text/html");
     httpd_resp_set_hdr(req, "Content-Encoding", "identity");
     return httpd_resp_send(req, dumpOut, strlen(dumpOut));
     */
    return ESP_OK;
}

static esp_err_t streamviewer_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    char filename[FILE_NAME_LEN];
    strcpy(filepath, "/spiffs/stream.html");
    strcpy(filename, "stream.html");
    //  const char *filename = getPathFromUri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
    //    req->uri, sizeof(filepath));
    ESP_LOGI(TAG, "filename %s", filename);

    flashLED(75);
    ESP_LOGI(TAG, "Stream viewer requested");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    return sendHtmlfile(req, filepath, filename);
    // return httpd_resp_send(req, (const char *)streamviewer_html, streamviewer_html_len);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    flashLED(75);
    ESP_LOGI(TAG, "\r\nStream stop requested via Web");
    cam_streamer_dequeue_all_clients(cam_streamer);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t startCameraServer(int hPort, int sPort, const char *base_path)
{
    static struct file_server_data *server_data = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16; // we use more than the default 8 (on port 80)
    if (server_data)
    {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        // return ESP_ERR_NO_MEM;
    }
    config.server_port = hPort;
    config.ctrl_port = hPort;
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = server_data};
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = server_data};
    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = server_data};
    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = server_data};
    httpd_uri_t style_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = getFilehandler,
        .user_ctx = server_data};
    httpd_uri_t favicon_16x16_uri = {
        .uri = "/favicon-16x16.png",
        .method = HTTP_GET,
        .handler = getFilehandler,
        .user_ctx = server_data};
    httpd_uri_t favicon_32x32_uri = {
        .uri = "/favicon-32x32.png",
        .method = HTTP_GET,
        .handler = getFilehandler,
        .user_ctx = server_data};
    httpd_uri_t favicon_ico_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = getFilehandler,
        .user_ctx = server_data};
    httpd_uri_t logo_svg_uri = {
        .uri = "/logo.svg",
        .method = HTTP_GET,
        .handler = getFilehandler,
        .user_ctx = server_data};
    httpd_uri_t dump_uri = {
         .uri       = "/dump",
         .method    = HTTP_GET,
         .handler   = dump_handler,
         .user_ctx  = NULL
     };
    httpd_uri_t stop_uri = {
        .uri = "/stop",
        .method = HTTP_GET,
        .handler = stop_handler,
        .user_ctx = server_data};
    httpd_uri_t stream_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = server_data};
    httpd_uri_t streamviewer_uri = {
        .uri = "/view",
        .method = HTTP_GET,
        .handler = streamviewer_handler,
        .user_ctx = server_data};
    httpd_uri_t info_uri = {
        .uri = "/info",
        .method = HTTP_GET,
        .handler = info_handler,
        .user_ctx = server_data};
    httpd_uri_t error_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = error_handler,
        .user_ctx = server_data};
    httpd_uri_t viewerror_uri = {
        .uri = "/view",
        .method = HTTP_GET,
        .handler = error_handler,
        .user_ctx = server_data};

    // Request Handlers; config.max_uri_handlers (above) must be >= the number of handlers

    ESP_LOGI(TAG, "Starting web server on port: '%d'\r\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        if (strlen(critERR) > 0)
        {
            httpd_register_uri_handler(camera_httpd, &error_uri);
        }
        else
        {
            httpd_register_uri_handler(camera_httpd, &index_uri);
            httpd_register_uri_handler(camera_httpd, &cmd_uri);
            httpd_register_uri_handler(camera_httpd, &status_uri);
            httpd_register_uri_handler(camera_httpd, &capture_uri);
        }
        httpd_register_uri_handler(camera_httpd, &style_uri);
        httpd_register_uri_handler(camera_httpd, &favicon_16x16_uri);
        httpd_register_uri_handler(camera_httpd, &favicon_32x32_uri);
        httpd_register_uri_handler(camera_httpd, &favicon_ico_uri);
        httpd_register_uri_handler(camera_httpd, &logo_svg_uri);
        httpd_register_uri_handler(camera_httpd, &dump_uri);
        httpd_register_uri_handler(camera_httpd, &stop_uri);
    }

    config.server_port = sPort;
    config.ctrl_port = sPort;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'\r\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        if (strlen(critERR) > 0)
        {
            httpd_register_uri_handler(camera_httpd, &error_uri);
            httpd_register_uri_handler(camera_httpd, &viewerror_uri);
        }
        else
        {
            httpd_register_uri_handler(stream_httpd, &stream_uri);
            httpd_register_uri_handler(stream_httpd, &info_uri);
            httpd_register_uri_handler(stream_httpd, &streamviewer_uri);
            cam_streamer = (cam_streamer_t *)malloc(sizeof(cam_streamer_t));
            cam_streamer_init(cam_streamer, stream_httpd, minFrameTime);
            cam_streamer_start(cam_streamer);
        }
        httpd_register_uri_handler(stream_httpd, &favicon_16x16_uri);
        httpd_register_uri_handler(stream_httpd, &favicon_32x32_uri);
        httpd_register_uri_handler(stream_httpd, &favicon_ico_uri);
    }
    return ESP_OK;
}
