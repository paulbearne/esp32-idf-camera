ESP32 mutli stream camera using the ESP-IDF V5.3.1
this is a port to the IDF of this project
https://github.com/easytarget/esp32-cam-webserver/tree/multi-stream

i use Visual studio code esp-idf extension for development once the project is loaded you will have
to setup the inviroment for setup paths to the idf etc also
you may need to dependencys for mdns and esp32-camera
open an idf terminal window in vscode then type the following 

idf.py add-dependency espressif/esp32-camera
idf.py add-dependency espressif/mdns

this is still a work in progress 
currently tested on a esp32-s3 camera board which has psram and use N16R8 esp32

application specific settings are in the sdconfig file which can be modified in the vscode extension
sdkconfig editior.

you also need to PSram to the type used on your board on in sdkconfig
also check 
Initialize SPI RAM during startup
Ignore PSRAM when not found
checkboxes

freertos
and check that freertos is only runnung on core 1


