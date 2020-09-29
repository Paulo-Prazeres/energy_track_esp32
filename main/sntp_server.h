#ifndef SNTP_SERVER_H_
#define SNTP_SERVER_H_

#include <time.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_sntp.h"

#define SNTP_CONNECTED_BIT BIT0
#define SNTP_FAIL_BIT      BIT1

EventGroupHandle_t sntp_event_group;

//static void obtain_time(void);
//static void initialize_sntp(void);
void get_time_from_internet();


#endif