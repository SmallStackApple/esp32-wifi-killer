#include <esp_wifi_types_generic.h>
#ifndef WIFI_DEAUTHER_H
#define WIFI_DEAUTHER_H
/**
 * @brief Start deauth task
 * @param ap_info AP info
 */
void start_deauth_task(wifi_ap_record_t *ap_info);
#endif