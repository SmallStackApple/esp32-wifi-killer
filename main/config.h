// 配置定义：目标SSID、LED引脚、操作模式按钮及协议常量
// Configuration definitions: target SSID, LED pin, operation mode buttons, and protocol constants
#define TARGET_SSID "chuer(16)"        // 待攻击的目标WiFi名称 | Target WiFi SSID to attack
#define LED_PIN 2                      // 状态指示灯引脚 | Status LED pin
#define SSID_MAX_LEN 32                // IEEE802.11协议最大SSID长度 | IEEE802.11 maximum SSID length
#define CHANNEL_2G_MIN 1               // 2.4G最小信道 | Minimum 2.4GHz channel
#define CHANNEL_2G_MAX 14              // 2.4G最大信道 | Maximum 2.4GHz channel
#define CHANNEL_MIN CHANNEL_2G_MIN     // 兼容旧代码 | Backward compat
#define CHANNEL_MAX CHANNEL_2G_MAX     // 兼容旧代码 | Backward compat
#define MAC_LEN 6                      // MAC地址字节长度 | MAC address byte length