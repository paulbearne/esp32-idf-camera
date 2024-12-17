idf_component_register(SRCS 
    "storage.c" 
    "wificonnect.c" 
    "psramutils.c"
    "camstream.c"
    "main.c"
    "cameratask.c"
    "lamp.c"
    "ws2812.c"
    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)