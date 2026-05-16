#ifndef TWINS_H
#define TWINS_H

#include <esp_wifi_types_generic.h>

void start_twins_ap(wifi_ap_record_t *ap_info);
void stop_twins_ap(void);

#endif
