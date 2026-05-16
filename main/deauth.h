#ifndef DEAUTH_H
#define DEAUTH_H

#include <esp_wifi_types_generic.h>

void start_deauth_task(const wifi_ap_record_t *ap_info);
void stop_deauth_task(void);

void start_deauth_all_task(void);
void stop_deauth_all_task(void);

#endif
