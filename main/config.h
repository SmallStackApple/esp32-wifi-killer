// 配置定义：目标SSID、LED引脚、操作模式按钮及协议常量
// Configuration definitions: target SSID, LED pin, operation mode buttons, and protocol constants
#define TARGET_SSID "chuer(16)"        // 待攻击的目标WiFi名称 | Target WiFi SSID to attack
#define LED_PIN 2                      // 状态指示灯引脚 | Status LED pin
#define SSID_MAX_LEN 32                // IEEE802.11协议最大SSID长度 | IEEE802.11 maximum SSID length
#define CHANNEL_MIN 1                  // 最小WiFi信道 | Minimum WiFi channel
#define CHANNEL_MAX 14                 // 最大WiFi信道 | Maximum WiFi channel
#define MAC_LEN 6                      // MAC地址字节长度 | MAC address byte length
