
#include "main.h"
// use rmt in peripherals
static const char *TAG = "BeyBlades Lamp";

#if defined(USE_WS2812)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM LED_2_PIN
#define EXAMPLE_LED_NUMBERS 1
static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];
rmt_channel_handle_t led_chan;
rmt_encoder_handle_t led_encoder;
#endif

esp_err_t initRgbLed(void)
{
    #if defined(USE_WS2812)
    esp_err_t err;
    // WS2812 RGB high intensity led
    ESP_LOGI(TAG, "Create RMT TX channel");
    led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };

    err = rmt_new_tx_channel(&tx_chan_config, &led_chan);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "Error creating new rmt tx channel");
        return err;
    }

    ESP_LOGI(TAG, "Install led strip encoder");
    led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    err = rmt_new_led_strip_encoder(&encoder_config, &led_encoder);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "Error creating new encoder");
        return err;
    }
    ESP_LOGI(TAG, "Enable RMT TX channel");
    err = rmt_enable(led_chan);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "Error enabling rmt");
        return err;
    }
    ESP_LOGI(TAG, "Startint led");
    #endif
    return ESP_OK;
    
}

esp_err_t setRgbLedLevel(uint8_t level)
{
    #if defined(USE_WS2812)
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    for (int i = 0; i < 3; i++)
    {
        for (int j = i; j < EXAMPLE_LED_NUMBERS; j += 3)
        {
            led_strip_pixels[j * 3 + 0] = level;
            led_strip_pixels[j * 3 + 1] = level;
            led_strip_pixels[j * 3 + 2] = level;
        }
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
    }
    #endif
    return ESP_OK;
}



void initLed(void)
{
    #if defined(LED_1_PIN)  
    esp_rom_gpio_pad_select_gpio(LED_1_PIN);
    gpio_set_direction(LED_1_PIN, GPIO_MODE_OUTPUT);
    #endif
}

// Notification LED
void flashLED(int flashtime)
{
#if defined(LED_1_PIN)                            // If we have it; flash it.
    gpio_set_level(LED_1_PIN, 1);                 // On at full power.
    vTaskDelay(pdMS_TO_TICKS(flashtime)); // delay
    gpio_set_level(LED_1_PIN, 0);                 // turn Off
#else
    //return; // No notifcation LED, do nothing, no delay
#endif
}

