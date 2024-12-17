#include "main.h"

int wifi_connect_status = 0;
static const char *TAG_AP = "BeyBlades WiFi SoftAP";
static const char *TAG_STA = "BeyBlades WiFi Station";
int s_retry_num = 0;

#define WIFI_AP_SSID            CONFIG_WIFI_AP_SSID
#define WIFI_AP_PASSWD          CONFIG_WIFI_AP_PASSWORD
#define WIFI_AP_CHANNEL            CONFIG_WIFI_AP_CHANNEL
#define WIFI_MAX_AP_STA_CONN            CONFIG_MAX_STA_CONN_AP

#define WIFI_STA_SSID CONFIG_STA_WIFI_SSID
#define WIFI_STA_PASSWORD CONFIG_STA_WIFI_PASSWORD
#define WIFI_STA_MAXIMUM_RETRY CONFIG_STA_MAXIMUM_RETRY
// change this to use the method selected
#ifdef CONFIG_STA_WIFI_AUTH_WPA_WPA2_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WPA_WPA2_PSK
#endif
#ifdef CONFIG_STA_WIFI_AUTH_OPEN
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_OPEN
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WEP 
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WEP
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WPA_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WPA_PSK
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WPA2_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WPA2_PSK
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WPA3_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WPA3_PSK
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WPA2_WPA3_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WPA2_WPA3_PSK
#endif
#ifdef CONFIG_STA_WIFI_AUTH_WAPI_PSK
#define WIFI_AUTH_METHOD CONFIG_STA_WIFI_AUTH_WAPI_PSK
#endif

EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1


void startMdnsService()
{
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    mdns_hostname_set(CONFIG_MDNS_HOSTNAME);
    //set default instance
    mdns_instance_name_set("BeyBlades Camera");
}

static void wifiEventHandler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ip = event->ip_info.ip;
        gw = event->ip_info.gw;
        net = event->ip_info.netmask;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


esp_netif_t *wifiInitSoftAp(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWD,
            .max_connection = WIFI_MAX_AP_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
            WIFI_AP_SSID, WIFI_AP_PASSWD,WIFI_AP_CHANNEL);

    return esp_netif_ap;
}

/* Initialize wifi station */
esp_netif_t *wifiInitStation(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = WIFI_STA_SSID,
            .password = WIFI_STA_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = WIFI_STA_MAXIMUM_RETRY,
            .threshold.authmode = WIFI_AUTH_METHOD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}

void wifiConnect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

   // ESP_ERROR_CHECK(esp_event_loop_create_default());
    

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiEventHandler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifiEventHandler,
                                                        NULL,
                                                        NULL));
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifiInitSoftAp();
    ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifiInitStation();
   
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG_STA, "connected to ap SSID:%s password:%s",
                 WIFI_STA_SSID, WIFI_STA_PASSWORD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG_STA, "Failed to connect to SSID:%s, password:%s",
                 WIFI_STA_SSID, WIFI_STA_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG_STA, "UNEXPECTED EVENT");
    }
    
    /* Set sta as the default interface */
    esp_netif_set_default_netif(esp_netif_sta);

    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG_AP, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }

    vEventGroupDelete(s_wifi_event_group);
}