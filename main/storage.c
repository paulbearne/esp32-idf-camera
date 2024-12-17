/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example, SD card / SPIFFS mount functions.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIcamsensor, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "main.h"

static const char *TAG = "storage";
bool filesystem = false;

#ifdef CONFIG_USE_SD_CARD

filesystem_type_t fstype;
setting_t conf;



void setDefaults(void)
{
    conf.framesize = FRAMESIZE_QVGA;
    conf.quality = 12;
    conf.aec2 = 0;
    conf.aecvalue = 300;
    conf.aelevel = 0;
    conf.agcgain = 1;
    conf.awbgain = 1;
    conf.bpc  = 1;
    conf.brightness = 0;
    conf.colorbar = 0;
    conf.contrast = 0;
    conf.dcw = 1;
    conf.exposurectl = 0;
    conf.gainceiling = (gainceiling_t)0;
    conf.gainctl = 0;
    conf.hmirror = 0;
    conf.lenc = 1;
    conf.rawgma = 1;
    conf.saturation = 0;
    conf.specialeffect = 0;
    conf.vflip = 0;
    conf.wbmode = 0;
    conf.whitebalance = 0;
    conf.wpc = 1;
    conf.usews2812 = true;
    conf.autolamp = false;
    conf.xclk = XCLOCK_FREQ / 1000000;
    conf.lamppin = 48;
    conf.lampval = 0; // fully off
    conf.minFrameTime = 200;  //
    conf.checksum = sizeof(conf);
}

esp_err_t initSD(const char *base_path)
{
    ESP_LOGI(TAG, "Initializing SD card");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_FORMAT_IF_MOUNT_SDCARD_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // CONFIG_EXAMPLE_FORMAT_IF_MOUNT_SDCARD_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
    esp_err_t ret;
    sdmmc_card_t *card;

#ifdef CONFIG_SD_USE_SDMMC_HOST
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

#ifdef CONFIG_SD_BUS_WIDTH_4
    slot_config.width = 4;
#else
    slot_config.width=1;
#endif

#ifdef SOC_SDMMC_USE_GPIO_MATRIX
    // For chips which support GPIO Matrix for SDMMC peripheral, specify the pins.
    slot_config.clk = CONFIG_SD_PIN_CLK;
    slot_config.cmd = CONFIG_SD_PIN_CMD;
    slot_config.d0 = CONFIG_SD_PIN_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4

    slot_config.d1 = CONFIG_SD_PIN_D1;
    slot_config.d2 = CONFIG_SD_PIN_D2;
    slot_config.d3 = CONFIG_SD_PIN_D3;
#endif
#endif // SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);

#else // CONFIG_SD_USE_SDMMC_HOST

    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_EXAMPLE_PIN_MOSI,
        .miso_io_num = CONFIG_EXAMPLE_PIN_MISO,
        .sclk_io_num = CONFIG_EXAMPLE_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_EXAMPLE_PIN_CS;
    slot_config.host_id = host.slot;
    ret = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card);

#endif // !CONFIG_USE_SDMMC_HOST

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    filesystem = true;
    fstype = SD;
    return ESP_OK;
}

#else // CONFIG_MOUNT_SD_CARD

/* Function to initialize SPIFFS */
esp_err_t mountSpiifs(const char *base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = 5, // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used);
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

#endif // !CONFIG_USE_SD_CARD

void listFolder(const char *rootDir)
{
    DIR *dir = opendir(rootDir);
    if (dir == NULL)
    {
        ESP_LOGI(TAG, "Spiffs folder %s is empty", rootDir);
        return;
    }

    while (true)
    {
        struct dirent *de = readdir(dir);
        if (!de)
        {
            break;
        }
        ESP_LOGI(TAG, "Found file: %s", de->d_name);
    }
    closedir(dir);
}

esp_err_t mountSpiffs(const char *base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = 5, // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

esp_err_t initSpiffs(void)
{
    esp_err_t err = mountSpiffs("/spiffs");
    if (err == ESP_OK)
    {
        fstype = SPIFFS;
        // listFolder("/spiffs/");
        filesystem = true;
    }
    return err;
}

bool fexists(char *path, struct stat *fstat)
{
    return stat(path, fstat) == ESP_OK;
}

void loadConfig(filesystem_type_t fstype)
{
    FILE *fd = NULL;
    char fpath[FILE_PATH_LEN];
    struct stat file_stat;
    if (fstype == SPIFFS)
    {
        strcpy(fpath, "/spiffs/");
    }
    else
    {
        strcpy(fpath, "/sdcard/");
    }
    strcat(fpath, PREFERENCES_FILE);
    if (fexists(fpath, &file_stat))
    {
        // read file into a string
        // char *prefs;
        ESP_LOGI(TAG, "Loading preferences from file %s\r\n", PREFERENCES_FILE);
        fd = fopen(fpath, "r");
        if (!fd)
        {
            ESP_LOGI(TAG, "Failed to open preferences file for reading, maybe corrupt, removing");
            deleteConfig(SPIFFS);
            return;
        }
        int res = fread(&conf, 1, sizeof(setting_t), fd);
        if (res != sizeof(setting_t))
        {
            ESP_LOGI(TAG, "error reading settings");
            setDefaults();
            saveConfig(fstype);
        }
        
        if (sizeof(conf) != conf.checksum){
            ESP_LOGI(TAG, "config size mismatch resetting to defaults");
            deleteConfig(SPIFFS);
            setDefaults();
            saveConfig(fstype);
        }
        // close the file
        fclose(fd);
        printConfig();
    }
    else
    {
        ESP_LOGI(TAG, "config file %s not found; using system defaults.\r\n", PREFERENCES_FILE);
        setDefaults();
        saveConfig(fstype);
        printConfig();
    }
}

void saveConfig(filesystem_type_t fstype)
{
    FILE *fd = NULL;
    char fpath[FILE_PATH_LEN];
    struct stat file_stat;
    if (fstype == SPIFFS)
    {
        strcpy(fpath, "/spiffs/");
    }
    else
    {
        strcpy(fpath, "/sdcard/");
    }
    strcat(fpath, PREFERENCES_FILE);
    if (fexists(fpath, &file_stat))
    {
        deleteConfig(fstype);
    }
    fd = fopen(fpath, "w");
    int res = fwrite(&conf, 1, sizeof(conf), fd);
    if (res != sizeof(conf))
    {
        ESP_LOGE(TAG, "Error saving config file");
    }
    fclose(fd);
}

void deleteConfig(filesystem_type_t fstype)
{
    
    char fpath[FILE_PATH_LEN];
    struct stat file_stat;
    if (fstype == SPIFFS)
    {
        strcpy(fpath, "/spiffs/");
    }
    else
    {
        strcpy(fpath, "/sdcard/");
    }
    strcat(fpath, PREFERENCES_FILE);
    if (fexists(fpath, &file_stat))
    {
        remove(fpath);
    }
}

void printConfig(void){
    ESP_LOGI(TAG,"frame size : %d",conf.framesize);
    ESP_LOGI(TAG,"quality : %d",conf.quality);
    ESP_LOGI(TAG,"aec2 value : %d",conf.aec2);
    ESP_LOGI(TAG,"aec value : %d",conf.aecvalue);
    ESP_LOGI(TAG,"ae level : %d",conf.aelevel);
    ESP_LOGI(TAG,"agc gain %d",conf.agcgain);
    ESP_LOGI(TAG,"awb gain : %d",conf.awbgain);
    ESP_LOGI(TAG,"bpc : %d",conf.bpc);
    ESP_LOGI(TAG,"brightness : %d",conf.brightness);
    ESP_LOGI(TAG,"color bar on : %s",conf.colorbar ? "true" : "false");
    ESP_LOGI(TAG,"contrast : %d",conf.contrast);
    ESP_LOGI(TAG,"dcw : %d",conf.dcw);
    ESP_LOGI(TAG,"exposure : %d",conf.exposurectl);
    ESP_LOGI(TAG,"gain ceiling : %d",conf.gainceiling);
    ESP_LOGI(TAG,"gain control : %d",conf.gainctl);
    ESP_LOGI(TAG,"h mirror : %d",conf.hmirror);
    ESP_LOGI(TAG,"lenc : %d",conf.lenc);
    ESP_LOGI(TAG,"raw gma : %d",conf.rawgma);
    ESP_LOGI(TAG,"saturatiom : %d",conf.saturation);
    ESP_LOGI(TAG,"frame size : %d",conf.specialeffect);
    ESP_LOGI(TAG,"v flip: %d",conf.vflip);
    ESP_LOGI(TAG,"wb mode : %d",conf.wbmode);
    ESP_LOGI(TAG,"white balance : %d",conf.whitebalance);
    ESP_LOGI(TAG,"wpc : %d",conf.wpc);
    ESP_LOGI(TAG,"is ws2812 : %s",conf.usews2812 ? "true":"false");
    ESP_LOGI(TAG,"auto lamp : %s", conf.autolamp  ? "true":"false");
    ESP_LOGI(TAG,"lamp pin : %d",(int)conf.lamppin);
    ESP_LOGI(TAG,"lamp value : %d",conf.lampval); // fully on
    ESP_LOGI(TAG,"checksum : %u",(unsigned int)conf.checksum);
}
