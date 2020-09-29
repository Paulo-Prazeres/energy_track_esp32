#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>

#include <stdio.h>
#include <string.h>

#include <stddef.h>
#include <u8g2.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "u8g2_esp32_hal.h"
#include "station_example.h"
//#include "icons.h"

#include "esp_system.h" //Wi-fi libs
#include "esp_wifi.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"

#include "sntp_server.h"

#include "nvs_blob_example_main.h"

#define SDA_GPIO 4
#define SCL_GPIO 15

#define SDA1_GPIO 21
#define SCL1_GPIO 22

#define SDA_PIN GPIO_NUM_4
#define SCL_PIN GPIO_NUM_15
#define RST_PIN GPIO_NUM_16

#define LM75A_ADDRESS 0X44

static char *TAG = "ssd1306";
static const char *TAG2 = "MQTTWS_EXAMPLE";
static const char *TAGI2CSHT85 = "SHT85";
static const char *TAGDataProcessing = "DataProcessing";

//static char *expected_data = NULL;
//static char *actual_data = NULL;
//static size_t expected_size = 0;
//static size_t expected_published = 0;
//static size_t actual_published = 0;
//static int qos_test = 0;
static char *MqttMensagemReceb = NULL;
static EventGroupHandle_t mqtt_event_group;//publish_test.c
const static int CONNECTED_BIT = BIT0;//publish_test.c

time_t current_time;
struct tm date_and_time;
char buff_date[10];
char buff_time[10];
u8g2_t u8g2;


static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
	
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG2, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);//publish_test.c
            msg_id = esp_mqtt_client_subscribe(client, "/topic/paulesp32qos0", 0);
            ESP_LOGI(TAG2, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/paulesp32qos1", 1);
            ESP_LOGI(TAG2, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "/topic/paulesp32qos1");
            ESP_LOGI(TAG2, "sent unsubscribe successful, msg_id=%d", msg_id);
            
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG2, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG2, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            //msg_id = esp_mqtt_client_publish(client, "/topic/paulesp32qos0", "data", 0, 0, 0);
            //ESP_LOGI(TAG2, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG2, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG2, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG2, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            int dataLenght =  event->data_len;
            ESP_LOGI(TAG2, "Received size=%d",event->data_len);
            free(MqttMensagemReceb);
            MqttMensagemReceb = (char *) malloc((dataLenght + 1) * sizeof(char));
            memset(MqttMensagemReceb, 0, sizeof MqttMensagemReceb);
            ESP_LOGI(TAG2, "Size of malloked char array=%d",sizeof MqttMensagemReceb);
            strncpy(MqttMensagemReceb, event->data, dataLenght);
            MqttMensagemReceb[dataLenght] = '\0';
            //ESP_LOGI(TAG2, "Received size=%s",MqttMensagemReceb);
            ESP_LOGI(TAG2, "Received size MAIR=%.*s",dataLenght, MqttMensagemReceb);
			xQueueOverwrite(xQueue_MQTT_Received_Data, (void *) &MqttMensagemReceb);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG2, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG2, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG2, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

/*static void get_string(char *line, size_t size)
{

    int count = 0;
    while (count < size) {
        int c = fgetc(stdin);
        if (c == '\n') {
            line[count] = '\0';
            break;
        } else if (c > 0 && c < 127) {
            line[count] = c;
            ++count;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}*/

void task_read_temp_and_hum()
{

    char *sht85Temp;
    char *sht85Humi;
    uint8_t raw[5];
    uint8_t raw1;
    uint8_t cmd1 = 0x24;
    uint8_t cmd2 = 0x00;

    float const calculationConst = 65535.00;

    float tempe_conv;
    float humid_conv;

    sht85Temp = (char *) malloc(5 * sizeof(char));
    sht85Humi = (char *) malloc(5 * sizeof(char));
    
    // i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    // i2c_master_start(cmd_handle);
    // i2c_master_write_byte(cmd_handle, (LM75A_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    // i2c_master_write(cmd_handle, &cmd1, 1, I2C_MASTER_ACK);
    // i2c_master_write(cmd_handle, &cmd2, 1, I2C_MASTER_ACK);    
    // i2c_master_stop(cmd_handle);

    // i2c_cmd_handle_t cmd_handle2 = i2c_cmd_link_create();
    // i2c_master_start(cmd_handle2);
    // i2c_master_write_byte(cmd_handle2, (LM75A_ADDRESS << 1) | I2C_MASTER_READ, true);
    // i2c_master_read(cmd_handle2, (uint8_t *)&raw, 5, I2C_MASTER_ACK);
    // i2c_master_read(cmd_handle2, (uint8_t *)&raw1, 1, I2C_MASTER_NACK);
    // i2c_master_stop(cmd_handle2);

    vTaskDelay(10000 / portTICK_RATE_MS);

    while(1)
    {
        // xSemaphoreTake(mutexBusI2C, portMAX_DELAY); //mutex I2C
        // gpio_set_level(RST_PIN, 0);
        // vTaskDelay(10/ portTICK_RATE_MS);
        i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
        i2c_master_start(cmd_handle);
        i2c_master_write_byte(cmd_handle, (LM75A_ADDRESS << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd_handle, &cmd1, 1, I2C_MASTER_ACK);
        i2c_master_write(cmd_handle, &cmd2, 1, I2C_MASTER_ACK);	        
        i2c_master_stop(cmd_handle);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_1, cmd_handle, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd_handle);

        if(ret == ESP_OK)
        {
            //ESP_LOGI(TAGI2CSHT85, "Write in SHT85 OK");
            vTaskDelay(50/ portTICK_RATE_MS);
            i2c_cmd_handle_t cmd_handle2 = i2c_cmd_link_create();
            i2c_master_start(cmd_handle2);
            i2c_master_write_byte(cmd_handle2, (LM75A_ADDRESS << 1) | I2C_MASTER_READ, true);
            i2c_master_read(cmd_handle2, (uint8_t *)&raw, 5, I2C_MASTER_ACK);
            i2c_master_read(cmd_handle2, (uint8_t *)&raw1, 1, I2C_MASTER_NACK);
            i2c_master_stop(cmd_handle2);
            esp_err_t ret2 = i2c_master_cmd_begin(I2C_NUM_1, cmd_handle2, 1000 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd_handle2);

            if(ret2 == ESP_OK)
            {
                // gpio_set_level(RST_PIN, 1);
                // vTaskDelay(10/ portTICK_RATE_MS);
                //ESP_LOGI(TAGI2CSHT85, "Read from SHT85 OK");
                uint16_t tempe = raw[0] << 8 | raw[1];
                uint16_t humid = raw[3] << 8 | raw[4];

                tempe_conv = tempe;
                humid_conv = humid;
                tempe_conv = 175*tempe_conv/calculationConst - 45;
                humid_conv = 100*humid_conv/calculationConst;

                //ESP_LOGI(TAGI2CSHT85, "Temperature converted: %f", tempe_conv);
                //ESP_LOGI(TAGI2CSHT85, "Humidity converted: %f", humid_conv);

                sprintf(sht85Temp, "%f", tempe_conv);
                sprintf(sht85Humi, "%f", humid_conv);
                sht85Temp[5] = '\n';
                sht85Humi[5] = '\n';
                //xQueueOverwrite(xQueue_SHT85_MQTT, (void *) &tempe_conv);
                xQueueOverwrite(xQueue_SHT85_Display_Temp, (void *) &sht85Temp);
                xQueueOverwrite(xQueue_SHT85_Display_Humi, (void *) &sht85Humi);
                xQueueSend(xQueue_SHT85_DataProcessing, &tempe_conv, 1000 / portTICK_PERIOD_MS);
                //xSemaphoreGive(mutexBusI2C);//mutex I2C
                vTaskDelay(1000 / portTICK_RATE_MS);
            }  
            else  
            {   
                //ESP_LOGI(TAGI2CSHT85, "Read from SHT85 FAIL");   
                // xSemaphoreGive(mutexBusI2C);//mutex I2C     
            }
        }
        else  
        {
            //ESP_LOGI(TAGI2CSHT85, "Write in SHT85 FAIL");
            // xSemaphoreGive(mutexBusI2C);//mutex I2C
        }
        //gpio_set_level(RST_PIN, 1);
        //u8g2_SetPowerSave(&u8g2, 0); // wake up display      
        //vTaskDelay(5000 / portTICK_RATE_MS);
    }
}

void tasku8g2_display()
{
    //task_read_temp_and_hum();
	char *ipEnD = "";
	char *MessInDisplayTemp = "";
    char *MessInDisplayHumi = "";
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda = SDA_PIN;
	u8g2_esp32_hal.scl = SCL_PIN;
	u8g2_esp32_hal.reset = RST_PIN;
	u8g2_esp32_hal_init(u8g2_esp32_hal);
    int firstSTR, secondSTR;	

	//u8g2_t u8g2; // a structure which will contain all the data for one display
	u8g2_Setup_ssd1306_i2c_128x64_noname_f(
		&u8g2,
		U8G2_R0,
		u8g2_esp32_i2c_byte_cb,
		u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure

	u8x8_SetI2CAddress(&u8g2.u8x8,0x78);    

	ESP_LOGI(TAG, "u8g2_InitDisplay");
	u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
	ESP_LOGI(TAG, "u8g2_ClearDisplay");
	u8g2_ClearDisplay(&u8g2);
	ESP_LOGI(TAG, "u8g2_SetPowerSave");
	u8g2_SetPowerSave(&u8g2, 0); // wake up display	
	ESP_LOGI(TAG, "u8g2_ClearBuffer");
	u8g2_ClearBuffer(&u8g2);

	// u8g2_DrawXBM(&u8g2, 0, 0, 128, 25, logo_Scients1);
	// u8g2_SendBuffer(&u8g2);
	// vTaskDelay(1000 / portTICK_RATE_MS);
	//u8g2_ClearBuffer(&u8g2);
	// u8g2_DrawXBM(&u8g2, 0, 26, 128, 25, logo_Scients2);
	// u8g2_SendBuffer(&u8g2);
	vTaskDelay(1000 / portTICK_RATE_MS);
	u8g2_ClearBuffer(&u8g2);

	u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
	u8g2_DrawStr(&u8g2, 0,17,"Connecting...");
	u8g2_SendBuffer(&u8g2);

	xQueueReceive(xQueue_LCD, (void *) &ipEnD, portMAX_DELAY);
	ESP_LOGI(TAG, "IP received in task Display:%s", ipEnD);
	u8g2_ClearBuffer(&u8g2);
	u8g2_DrawStr(&u8g2, 0,17,ipEnD);
	u8g2_SendBuffer(&u8g2);

    get_time_from_internet();
    time(&current_time);
    localtime_r(&current_time, &date_and_time);
    ESP_LOGI(TAG, "Current Time is: %ld", current_time);
    ESP_LOGI(TAG, "Day:%i", date_and_time.tm_mday);
    ESP_LOGI(TAG, "Month:%i", date_and_time.tm_mon);
    ESP_LOGI(TAG, "Year:%i", date_and_time.tm_year);
    ESP_LOGI(TAG, "Hour:%i", date_and_time.tm_hour);
    ESP_LOGI(TAG, "Minutes:%i", date_and_time.tm_min);
    ESP_LOGI(TAG, "Seconds:%i", date_and_time.tm_sec);
    snprintf(buff_date, 10, "%d/%d/%d\n", date_and_time.tm_mday,(date_and_time.tm_mon+1),(date_and_time.tm_year+1900)); 
    snprintf(buff_time, 10, "%d:%d:%d\n", date_and_time.tm_hour,date_and_time.tm_min,date_and_time.tm_sec); 
    ESP_LOGI(TAG, "Brazil Date:%s", buff_date);
    ESP_LOGI(TAG, "Brazil Time:%s", buff_time);
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, u8g2_DrawStr(&u8g2, 0,10,buff_date) + 5, 10,buff_time);
    u8g2_SendBuffer(&u8g2);
	while(true)
	{
		xQueueReceive(xQueue_SHT85_Display_Temp, (void *) &MessInDisplayTemp, portMAX_DELAY);
        xQueueReceive(xQueue_SHT85_Display_Humi, (void *) &MessInDisplayHumi, portMAX_DELAY);        
		u8g2_ClearBuffer(&u8g2);

        time(&current_time);
        localtime_r(&current_time, &date_and_time);
        snprintf(buff_date, 10, "%d/%d/%d\n", date_and_time.tm_mday,(date_and_time.tm_mon+1),(date_and_time.tm_year+1900)); 
        snprintf(buff_time, 10, "%d:%d:%d\n", date_and_time.tm_hour,date_and_time.tm_min,date_and_time.tm_sec); 
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(&u8g2, u8g2_DrawStr(&u8g2, 0,10,buff_date) + 5, 10,buff_time);
        
        u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
        firstSTR = u8g2_DrawStr(&u8g2, 0,34,"T = ");
        secondSTR = u8g2_DrawStr(&u8g2, 0,51,"H = ");
        u8g2_DrawStr(&u8g2, firstSTR,34,MessInDisplayTemp);
		// ESP_LOGI(TAG,"Temperature DraWStr Return: %d", firstSTR );
        // ESP_LOGI(TAG,"Humidty DraWStr Return: %d",secondSTR);
        // ESP_LOGI(TAG,"tempValue DraWStr Return: %d", tempValSTR);
        u8g2_DrawStr(&u8g2,secondSTR,51,MessInDisplayHumi);
		u8g2_SendBuffer(&u8g2);
		
		
	}
	
	
	/*
	ESP_LOGI(TAG, "u8g2_SetFont");
	u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
	ESP_LOGI(TAG, "u8g2_DrawBox");
	u8g2_DrawBox(&u8g2, 0, 26, 80,6);
	u8g2_DrawFrame(&u8g2, 0,26,100,6);
	
	ESP_LOGI(TAG, "u8g2_DrawStr");
    u8g2_DrawStr(&u8g2, 0,17,"Hi nkolban!");
	ESP_LOGI(TAG, "u8g2_SendBuffer");
	u8g2_SendBuffer(&u8g2);
	vTaskDelay(5000 / portTICK_RATE_MS);*/
	/*    
	uint8_t resultado = u8g2_UserInterfaceSelectionList(&u8g2, "Menu", sp, "Option1\nOption2\nOption3\nOption4");
	ESP_LOGI(TAG, "Out of u8g2_UserInterfaceSelectionList");
	ESP_LOGI(TAG, "Start I2C transfer to %d",resultado);*/

}

void mqttWSInitTry()
{


    //char line[256];
    // char pattern[32];
    // char transport[32];
    // int repeat = 0;
    float temp_sht85 = 0;

    char sht85ValueString[10];

    ESP_LOGI(TAG2, "[APP] Startup..");
    ESP_LOGI(TAG2, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG2, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_WS", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    //ESP_ERROR_CHECK(nvs_flash_init());
    //tcpip_adapter_init();
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());

    //mqtt_app_start();

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "ws://mqtt.eclipse.org:80/mqtt",
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    while(1)
    {

        xQueueReceive(xQueue_SHT85_MQTT, (void *) &temp_sht85, portMAX_DELAY);
        ESP_LOGI(TAG2, "Temp received in MQTT task: %f", temp_sht85);
        sprintf(sht85ValueString, "%f", temp_sht85);
        ESP_LOGI(TAG2, "Temp received converted to string: %s", sht85ValueString);

        /*get_string(line, sizeof(line));
        sscanf(line, "%s %d %d %d", pattern, &repeat, &expected_published, &qos_test);
        ESP_LOGI(TAG2, "PATTERN:%s REPEATED:%d PUBLISHED:%d\n", pattern, repeat, expected_published);
        int pattern_size = strlen(pattern);
        free(expected_data);
        free(actual_data);
        actual_published = 0;        
        expected_size = pattern_size * repeat;
        expected_data = malloc(expected_size);
        actual_data = malloc(expected_size);
        for (int i = 0; i < repeat; i++) {
            memcpy(expected_data + i * pattern_size, pattern, pattern_size);
        }
        printf("EXPECTED STRING %.*s, SIZE:%d\n", expected_size, expected_data, expected_size);*/

        // esp_mqtt_client_stop(client);

        // xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);//publish_test.c
        // esp_mqtt_client_start(client);
        ESP_LOGI(TAG2, "Note free memory: %d bytes", esp_get_free_heap_size());
        xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);//publish_test.c
        int msg_id = esp_mqtt_client_publish(client, "/topic/paulesp32qos0", sht85ValueString, strlen(sht85ValueString), 0, 0);
        ESP_LOGI(TAG2, "[%d] Publishing...", msg_id);
        

    }
}

float absoluto(float x)
{
    if (x >= 0.0) return x;
    else return -x;
}

void task_process_sensor_data()
{   
    float actualTempValue; //The current value of the temperature in the loop.
    float lastTempValue = 0; //The value of the temperature read in the iteration (i-1)
    float actualTempDiff; //The current absulut value of the difference between actualTempValue and lastTempValue in each iteration step.
    float avg = 0; //The current average of temperarute. It will updated in each interation of the loop
    float avgDif = 0;// The current average of the difference of temperature between the actual temperature value (actualTempValue) and the last temperature value (lastTempValue); Average of the variaton rate.
    float avgSum = 0;//The auxiliar cumulative variable to calculate the average (avg)
    float avgDifSum = 0;//The auxiliar cumulative variable to calculate the average of the variation rate.
    uint32_t i = 0;//Iteration counter
    uint16_t learningIterationNumber = 10;// Number of iteration needed by the algorithm to learn the normal behavior of the sensor
    while(1)
    {
        if(xQueueReceive(xQueue_SHT85_DataProcessing, &actualTempValue , 5000 / portTICK_PERIOD_MS)) //Value of temperature sent by the task_read_temp_and_hum();
        {
            i++;                        
            if(lastTempValue == 0) //Initial condition. Need to initialize.
            {
                lastTempValue = actualTempValue;
                avgDifSum = absoluto(actualTempValue - lastTempValue); //Initialize the avgDifSum variable with (actualTempValue - lastTempValue)
            } 
            actualTempDiff = absoluto(actualTempValue - lastTempValue);
            avgSum = avgSum + actualTempValue;
            avg = avgSum/i;
            avgDifSum = avgDifSum + actualTempDiff;
            avgDif = avgDifSum/i;              
            lastTempValue = actualTempValue; //Update the value of lastTempValue that is gonna be used in the next iteration.
            //ESP_LOGI(TAGDataProcessing, "Avg: %f",avg);
            //ESP_LOGI(TAGDataProcessing, "AvgDif: %f",avgDif);    //Debug steps
            //ESP_LOGI(TAGDataProcessing, "Instant Differece: %f", actualTempDiff);
            //if(actualTempDiff > 2*avgDif && i > learningIterationNumber)  ESP_LOGI(TAGDataProcessing, "WARNING! An anomaly in the increase rate of temperature was detected!"); //Alert the user if any anomaly variation rate was detected after 
        }
    }
}

void task_process_received_command()
{
    char *commandReceived = "";

    while (1)
    {           
        xQueueReceive(xQueue_MQTT_Received_Data, (void *) &commandReceived, portMAX_DELAY);
        printf("Received in the task: task_process_received command: %s\nAnd his data lenght: %d\n",commandReceived, strlen(commandReceived));
        process_command(commandReceived);        
              
    }
}

void app_main(void)	
{
	xQueue_LCD = xQueueCreate( 1, sizeof( char *) );
	xQueue_SHT85_Display_Temp = xQueueCreate( 1, sizeof( char *) );
    xQueue_SHT85_Display_Humi = xQueueCreate( 1, sizeof( char *) );
    xQueue_SHT85_MQTT = xQueueCreate( 1, sizeof(float));
    xQueue_SHT85_DataProcessing = xQueueCreate(10, sizeof(float));
    xQueue_MQTT_Received_Data = xQueueCreate( 1, sizeof( char *) );
    mutexBusI2C = xSemaphoreCreateMutex();
    mqtt_event_group = xEventGroupCreate();//publish_test.c
    sntp_event_group = xEventGroupCreate();

    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000};
    i2c_param_config(I2C_NUM_0, &i2c_config);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    i2c_config_t i2c_config1 = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA1_GPIO,
        .scl_io_num = SCL1_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000};
    i2c_param_config(I2C_NUM_1, &i2c_config1);
    i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0);

    esp_err_t err = nvs_flash_init_partition("MyNvs");
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase_partition("MyNvs"));
        err = nvs_flash_init_partition("MyNvs");
    }

	xTaskCreate(
    tasku8g2_display                     /* Funcao a qual esta implementado o que a tarefa deve fazer */
    ,  "display"   /* Nome (para fins de debug, se necessário) */
    ,  1024 * 4                          /* Tamanho da stack (em words) reservada para essa tarefa */
    ,  NULL                         /* Parametros passados (nesse caso, não há) */
    ,  3                            /* Prioridade */
    ,  NULL );                      /* Handle da tarefa, opcional (nesse caso, não há) */

    xTaskCreate(
    task_read_temp_and_hum                     /* Funcao a qual esta implementado o que a tarefa deve fazer */
    ,  "Read_Temp_Hum"   /* Nome (para fins de debug, se necessário) */
    ,  1024 * 2                          /* Tamanho da stack (em words) reservada para essa tarefa */
    ,  NULL                         /* Parametros passados (nesse caso, não há) */
    ,  3                            /* Prioridade */
    ,  NULL );                      /* Handle da tarefa, opcional (nesse caso, não há) */

    xTaskCreate(
    task_process_sensor_data                     /* Funcao a qual esta implementado o que a tarefa deve fazer */
    ,  "Process_Sensor_Data"   /* Nome (para fins de debug, se necessário) */
    ,  1024 * 2                          /* Tamanho da stack (em words) reservada para essa tarefa */
    ,  NULL                         /* Parametros passados (nesse caso, não há) */
    ,  3                            /* Prioridade */
    ,  NULL );                      /* Handle da tarefa, opcional (nesse caso, não há) */

    xTaskCreate(
    task_process_received_command                     /* Funcao a qual esta implementado o que a tarefa deve fazer */
    ,  "Process_MQTT_Received_Data"   /* Nome (para fins de debug, se necessário) */
    ,  1024 * 2 /*1024 * 1 is getting stack overflow ERROR! - Tamanho da stack (em words) reservada para essa tarefa */
    ,  NULL                         /* Parametros passados (nesse caso, não há) */
    ,  3                            /* Prioridade */
    ,  NULL );                      /* Handle da tarefa, opcional (nesse caso, não há) */

    xTaskCreate(
    execute_schedule_action                     /* Funcao a qual esta implementado o que a tarefa deve fazer */
    ,  "ExecutingScheduleAction"   /* Nome (para fins de debug, se necessário) */
    ,  1024 * 2                         /* Tamanho da stack (em words) reservada para essa tarefa */
    ,  NULL                         /* Parametros passados (nesse caso, não há) */
    ,  3                            /* Prioridade */
    ,  NULL );                      /* Handle da tarefa, opcional (nesse caso, não há) */

	
	vTaskDelay(1000 / portTICK_RATE_MS);
	//esp_err_t GotIPOK = wiFiInitialization();

    ESP_ERROR_CHECK( err );
	wiFiInitialization();
	mqttWSInitTry();
    //vEventGroupDelete(sntp_event_group);

    



	ESP_LOGI(TAG, "All done!");	

}


