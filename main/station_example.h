#ifndef STATION_EXAMPLE_H_
#define STATION_EXAMPLE_H_

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

QueueHandle_t xQueue_LCD;
QueueHandle_t xQueue_SHT85_Display_Temp;
QueueHandle_t xQueue_SHT85_Display_Humi;
QueueHandle_t xQueue_SHT85_MQTT;
QueueHandle_t xQueue_SHT85_DataProcessing;
QueueHandle_t xQueue_MQTT_Received_Data;


//static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
void wifi_init_sta();
void wiFiInitialization();

#endif