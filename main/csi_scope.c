#include "csi_scope.h"
#include "bsp_battery.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "csi_scope";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static csi_view_t s_view = {.battery = -1};
static uint32_t s_last_frame_ms;
static uint8_t s_bssid[6];
static bool s_capture;
static QueueHandle_t s_frames, s_commands;
static esp_netif_t *s_sta, *s_ap;
static httpd_handle_t s_http;
static esp_ping_handle_t s_ping;
static bool s_link_up, s_setup, s_trying, s_pending_save;
static unsigned s_retries;
static uint32_t s_attempt_ms, s_ap_close_ms, s_last_battery_ms, s_last_stats_ms;
static wifi_config_t s_credentials;

typedef struct {
    uint32_t time_ms, epoch;
    int8_t rssi;
    int8_t iq[128];
} sample_t;

typedef enum { CMD_CONNECT, CMD_GOT_IP, CMD_DISCONNECT, CMD_SETUP, CMD_CALIBRATE, CMD_CLEAR } command_kind_t;
typedef struct {
    command_kind_t kind;
    uint16_t reason;
    char ssid[33], password[65];
} command_t;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void set_status(const char *text)
{
    portENTER_CRITICAL(&s_mux);
    snprintf(s_view.status, sizeof(s_view.status), "%s", text);
    portEXIT_CRITICAL(&s_mux);
}

static void fail_status(const char *what, esp_err_t error)
{
    char text[72];
    snprintf(text, sizeof(text), "%s: %s", what, esp_err_to_name(error));
    set_status(text);
    ESP_LOGE(TAG, "%s", text);
}

void csi_scope_view(csi_view_t *out)
{
    uint32_t now = now_ms();
    portENTER_CRITICAL(&s_mux);
    *out = s_view;
    out->fresh = s_capture && s_view.frames > 0 && now - s_last_frame_ms < CSI_STALE_MS;
    portEXIT_CRITICAL(&s_mux);
}

static void reset_capture(bool enabled)
{
    portENTER_CRITICAL(&s_mux);
    s_capture = enabled;
    ++s_view.epoch;
    s_view.frames = s_view.dropped = s_view.rejected = s_view.rate = 0;
    s_view.calibrated = false;
    s_view.calibration_percent = 0;
    s_view.amplitude = s_view.score = 0;
    s_last_frame_ms = 0;
    memset(s_view.spectrum, 0, sizeof(s_view.spectrum));
    portEXIT_CRITICAL(&s_mux);
}

static void csi_received(void *context, wifi_csi_info_t *info)
{
    (void)context;
    if (!info || !info->buf) return;
    sample_t sample;
    portENTER_CRITICAL(&s_mux);
    bool accept = s_capture && memcmp(info->mac, s_bssid, 6) == 0;
    sample.epoch = s_view.epoch;
    portEXIT_CRITICAL(&s_mux);
    if (!accept) return;
    // 20 MHz OFDM/HT share the 64-bin LLTF layout; reject HT40 and other layouts.
    if (info->rx_ctrl.sig_mode > 1 || info->rx_ctrl.cwb || info->len != 128 || info->rx_ctrl.rx_state) {
        portENTER_CRITICAL(&s_mux);
        ++s_view.rejected;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    sample.time_ms = now_ms();
    sample.rssi = info->rx_ctrl.rssi;
    memcpy(sample.iq, info->buf, sizeof(sample.iq));
    // Driver callback owns info->buf. Copy immediately; never retain its pointer.
    if (xQueueSend(s_frames, &sample, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_mux);
        ++s_view.dropped;
        portEXIT_CRITICAL(&s_mux);
    }
}

static void process_samples(void *arg)
{
    (void)arg;
    csi_model_t model = {0};
    sample_t sample;
    csi_features_t features;
    uint32_t epoch = UINT32_MAX, rate_started = 0, rate_count = 0, count = 0, rate = 0;
    for (;;) {
        if (xQueueReceive(s_frames, &sample, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        portENTER_CRITICAL(&s_mux);
        bool current = s_capture && sample.epoch == s_view.epoch;
        portEXIT_CRITICAL(&s_mux);
        if (!current) continue;
        if (epoch != sample.epoch) {
            epoch = sample.epoch;
            csi_model_reset(&model);
            rate_started = sample.time_ms;
            rate_count = count = rate = 0;
        }
        if (!csi_extract(sample.iq, sizeof(sample.iq), &features)) {
            portENTER_CRITICAL(&s_mux);
            ++s_view.rejected;
            portEXIT_CRITICAL(&s_mux);
            continue;
        }
        csi_model_feed(&model, &features, sample.time_ms);
        ++rate_count;
        ++count;
        uint32_t elapsed = sample.time_ms - rate_started;
        if (elapsed >= 1000) {
            rate = rate_count * 1000u / elapsed;
            rate_count = 0;
            rate_started = sample.time_ms;
        }
        portENTER_CRITICAL(&s_mux);
        if (sample.epoch == s_view.epoch && s_capture) {
            s_view.frames = count;
            s_view.rate = rate;
            s_view.rssi = sample.rssi;
            s_view.amplitude = features.mean_amplitude;
            s_view.score = model.score;
            s_view.calibrated = model.calibrated;
            s_view.calibration_percent = csi_model_progress(&model, sample.time_ms);
            memcpy(s_view.spectrum, features.amplitude, sizeof(s_view.spectrum));
            s_last_frame_ms = sample.time_ms;
        }
        portEXIT_CRITICAL(&s_mux);
    }
}

void csi_scope_key(bsp_btn_t button, bsp_btn_ev_t event, void *arg)
{
    (void)arg;
    if (!s_commands) return;
    command_t cmd = {0};
    if (button == BSP_BTN_DOWN && event == BSP_BTN_LONG) cmd.kind = CMD_CLEAR;
    else if (button == BSP_BTN_OK && event == BSP_BTN_LONG) cmd.kind = CMD_SETUP;
    else if (button == BSP_BTN_OK && event == BSP_BTN_CLICK) cmd.kind = CMD_CALIBRATE;
    else return;
    (void)xQueueSend(s_commands, &cmd, 0);
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    command_t cmd = {0};
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) cmd.kind = CMD_GOT_IP;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        cmd.kind = CMD_DISCONNECT;
        cmd.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else return;
    (void)xQueueSend(s_commands, &cmd, 0);
}

extern const uint8_t provision_html_start[] asm("_binary_provision_html_start");
extern const uint8_t provision_html_end[] asm("_binary_provision_html_end");

static esp_err_t portal_get(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'");
    return httpd_resp_send(request, (const char *)provision_html_start,
                           provision_html_end - provision_html_start - 1);
}

static esp_err_t portal_post(httpd_req_t *request)
{
    char body[512];
    char content_type[48];
    command_t cmd = {.kind = CMD_CONNECT};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "请求格式错误");
    if (request->content_len <= 0 || request->content_len >= sizeof(body))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "请求大小错误");
    size_t used = 0;
    while (used < request->content_len) {
        int n = httpd_req_recv(request, body + used, request->content_len - used);
        if (n <= 0) {
            memset(body, 0, sizeof(body));
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "请求不完整，请重试");
        }
        used += n;
    }
    body[used] = '\0';
    cJSON *json = cJSON_ParseWithLength(body, used);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    bool valid = cJSON_IsString(ssid) && cJSON_IsString(password) &&
                 csi_credentials_valid(ssid->valuestring, password->valuestring);
    if (valid) {
        snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid->valuestring);
        snprintf(cmd.password, sizeof(cmd.password), "%s", password->valuestring);
    }
    if (cJSON_IsString(password)) memset(password->valuestring, 0, strlen(password->valuestring));
    cJSON_Delete(json);
    memset(body, 0, sizeof(body));
    if (!valid) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "请检查名称和密码长度：密码为 8-63 字节，开放网络可留空");
    bool sent = xQueueSend(s_commands, &cmd, 0) == pdTRUE;
    memset(&cmd, 0, sizeof(cmd));
    if (!sent) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "设备忙，请稍后重试");
    }
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "OK");
}

static void stop_sampling(void)
{
    reset_capture(false);
    (void)esp_wifi_set_csi(false);
    if (s_ping) {
        (void)esp_ping_stop(s_ping);
        (void)esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

static esp_err_t start_sampling(void)
{
    wifi_ap_record_t ap;
    esp_netif_ip_info_t ip;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err != ESP_OK) return err;
    err = esp_netif_get_ip_info(s_sta, &ip);
    if (err != ESP_OK || ip.gw.addr == 0) return ESP_ERR_INVALID_STATE;
    stop_sampling();
    portENTER_CRITICAL(&s_mux);
    memcpy(s_bssid, ap.bssid, sizeof(s_bssid));
    s_view.channel = ap.primary;
    s_view.connected = true;
    portEXIT_CRITICAL(&s_mux);
    wifi_csi_config_t csi = {
        .lltf_en = true, .htltf_en = false, .stbc_htltf2_en = false,
        .ltf_merge_en = false, .channel_filter_en = false,
        .manu_scale = false, .shift = 0,
    };
    err = esp_wifi_set_csi_config(&csi);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_csi_rx_cb(csi_received, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) return err;
    reset_capture(true);
    esp_ping_config_t ping = ESP_PING_DEFAULT_CONFIG();
    ping.count = ESP_PING_COUNT_INFINITE;
    ping.interval_ms = 10;
    ping.timeout_ms = 100;
    ping.data_size = 64;
    ping.task_stack_size = 3072;
    ping.interface = esp_netif_get_netif_impl_index(s_sta);
    ip_addr_set_ip4_u32(&ping.target_addr, ip.gw.addr);
    esp_ping_callbacks_t callbacks = {0};
    err = esp_ping_new_session(&ping, &callbacks, &s_ping);
    if (err == ESP_OK) err = esp_ping_start(s_ping);
    if (err != ESP_OK) stop_sampling();
    else set_status("保持环境静止，开始校准");
    return err;
}

static esp_err_t start_portal(void)
{
    if (s_setup) return ESP_OK;
    stop_sampling();
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "CSI-Passport-%02X%02X", mac[4], mac[5]);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%08lx", (unsigned long)esp_random());
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) return err;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 6144;
    http.max_open_sockets = 3;
    http.lru_purge_enable = true;
    http.recv_wait_timeout = 3;
    http.send_wait_timeout = 3;
    err = httpd_start(&s_http, &http);
    if (err != ESP_OK) { (void)esp_wifi_set_mode(WIFI_MODE_STA); return err; }
    const httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = portal_get};
    const httpd_uri_t post = {.uri = "/configure", .method = HTTP_POST, .handler = portal_post};
    err = httpd_register_uri_handler(s_http, &get);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_http, &post);
    if (err != ESP_OK) {
        httpd_stop(s_http); s_http = NULL;
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        return err;
    }
    s_setup = true;
    s_ap_close_ms = 0;
    portENTER_CRITICAL(&s_mux);
    s_view.provisioning = true;
    snprintf(s_view.ap_name, sizeof(s_view.ap_name), "%.23s", ap.ap.ssid);
    snprintf(s_view.ap_password, sizeof(s_view.ap_password), "%.11s", ap.ap.password);
    portEXIT_CRITICAL(&s_mux);
    set_status("请用手机连接设备热点");
    return ESP_OK;
}

static bool load_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open("csi_scope", NVS_READONLY, &nvs) != ESP_OK) return false;
    // Single blob: the SSID/password pair is replaced atomically after connection.
    size_t size = sizeof(s_credentials);
    esp_err_t err = nvs_get_blob(nvs, "wifi_v1", &s_credentials, &size);
    nvs_close(nvs);
    return err == ESP_OK && size == sizeof(s_credentials) && s_credentials.sta.ssid[0] != 0;
}

static esp_err_t save_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("csi_scope", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "wifi_v1", &s_credentials, sizeof(s_credentials));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t connect_router(void)
{
    stop_sampling();
    s_link_up = false;
    portENTER_CRITICAL(&s_mux);
    s_view.connected = false;
    portEXIT_CRITICAL(&s_mux);
    // Disconnect is serialized on this worker; the matching event is ignored
    // by checking the current link state before acting on disconnect events.
    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_credentials);
    if (err == ESP_OK) err = esp_wifi_connect();
    s_trying = err == ESP_OK;
    s_attempt_ms = now_ms();
    if (err == ESP_OK) set_status("正在连接 2.4 GHz 路由器...");
    return err;
}

static esp_err_t network_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err; // Never erase existing NVS as a recovery shortcut.
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    esp_netif_config_t sta = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_config_t ap = ESP_NETIF_DEFAULT_WIFI_AP();
    s_sta = esp_netif_new(&sta);
    s_ap = esp_netif_new(&ap);
    if (!s_sta || !s_ap) return ESP_ERR_NO_MEM;
    err = esp_netif_attach_wifi_station(s_sta);
    if (err == ESP_OK) err = esp_wifi_set_default_wifi_sta_handlers();
    if (err == ESP_OK) err = esp_netif_attach_wifi_ap(s_ap);
    if (err == ESP_OK) err = esp_wifi_set_default_wifi_ap_handlers();
    if (err != ESP_OK) return err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_set_ps(WIFI_PS_NONE);
    return err;
}

void csi_scope_run(void)
{
    s_frames = xQueueCreate(32, sizeof(sample_t));
    s_commands = xQueueCreate(12, sizeof(command_t));
    if (!s_frames || !s_commands) { set_status("内存不足，队列创建失败"); return; }
    if (xTaskCreate(process_samples, "csi_process", 4096, NULL, 4, NULL) != pdPASS) {
        set_status("内存不足，采样任务失败"); return;
    }
    esp_err_t err = network_init();
    if (err != ESP_OK) { fail_status("无线网络初始化", err); return; }
    ESP_LOGI(TAG, "CSI Scope 0.1; heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (load_credentials()) err = connect_router();
    else err = start_portal();
    if (err != ESP_OK) fail_status("启动失败", err);
    for (;;) {
        command_t cmd = {0};
        if (xQueueReceive(s_commands, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (cmd.kind == CMD_CONNECT) {
                memset(&s_credentials, 0, sizeof(s_credentials));
                memcpy(s_credentials.sta.ssid, cmd.ssid, strlen(cmd.ssid));
                memcpy(s_credentials.sta.password, cmd.password, strlen(cmd.password));
                s_credentials.sta.pmf_cfg.capable = true;
                s_pending_save = true;
                s_retries = 0;
                s_ap_close_ms = 0;
                err = connect_router();
                if (err != ESP_OK) fail_status("连接失败", err);
            } else if (cmd.kind == CMD_GOT_IP) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                    memcmp(ap.ssid, s_credentials.sta.ssid, sizeof(s_credentials.sta.ssid)) == 0) {
                    s_trying = false;
                    s_link_up = true;
                    s_retries = 0;
                    bool saved = true;
                    if (s_pending_save) {
                        err = save_credentials();
                        saved = err == ESP_OK;
                        if (!saved) fail_status("保存配网失败", err);
                        s_pending_save = false;
                    }
                    if (s_setup) {
                        if (saved) set_status("已连接，正在结束配网...");
                        s_ap_close_ms = now_ms() + 2500;
                    } else {
                        err = start_sampling();
                        if (err != ESP_OK) fail_status("采样启动失败", err);
                    }
                }
            } else if (cmd.kind == CMD_DISCONNECT) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                    stop_sampling();
                    s_link_up = false;
                    s_ap_close_ms = 0;
                    portENTER_CRITICAL(&s_mux);
                    s_view.connected = false;
                    portEXIT_CRITICAL(&s_mux);
                    // A deliberate disconnect while replacing credentials can
                    // race with connect(); let the current attempt reach timeout.
                    if (cmd.reason != WIFI_REASON_ASSOC_LEAVE && ++s_retries <= 4) {
                        s_trying = true;
                        s_attempt_ms = now_ms();
                        err = esp_wifi_connect();
                        if (err != ESP_OK) fail_status("重连失败", err);
                        else set_status("路由器断开，正在重连...");
                    } else if (cmd.reason != WIFI_REASON_ASSOC_LEAVE) {
                        s_trying = false;
                        (void)start_portal();
                        set_status("连接失败，请检查名称和密码");
                    }
                }
            } else if (cmd.kind == CMD_SETUP) {
                s_trying = false;
                s_ap_close_ms = 0;
                err = start_portal();
                if (err != ESP_OK) fail_status("配网失败", err);
            } else if (cmd.kind == CMD_CLEAR && s_setup) {
                s_trying = s_pending_save = s_link_up = false;
                s_ap_close_ms = 0;
                (void)esp_wifi_disconnect();
                memset(&s_credentials, 0, sizeof(s_credentials));
                nvs_handle_t nvs;
                err = nvs_open("csi_scope", NVS_READWRITE, &nvs);
                if (err == ESP_OK) {
                    err = nvs_erase_key(nvs, "wifi_v1");
                    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
                    if (err == ESP_OK) err = nvs_commit(nvs);
                    nvs_close(nvs);
                }
                if (err == ESP_OK) set_status("已清除网络，请重新配网");
                else fail_status("清除配网失败", err);
            } else if (cmd.kind == CMD_CALIBRATE && s_link_up) {
                if (s_setup) s_ap_close_ms = now_ms() + 100;
                else {
                    reset_capture(true);
                    set_status("保持环境静止，开始校准");
                }
            }
            memset(&cmd, 0, sizeof(cmd));
        }
        uint32_t now = now_ms();
        if (now - s_last_stats_ms >= 1000) {
            s_last_stats_ms = now;
            csi_view_t view;
            csi_scope_view(&view);
            // Bounded numeric diagnostics, with no SSID, password or MAC address.
            ESP_LOGI(TAG, "CSI_STAT fresh=%u cal=%u rate=%lu amp=%.2f score=%.1f rx=%lu drop=%lu skip=%lu heap=%lu",
                     view.fresh, view.calibration_percent, (unsigned long)view.rate,
                     (double)view.amplitude, (double)view.score, (unsigned long)view.frames,
                     (unsigned long)view.dropped, (unsigned long)view.rejected,
                     (unsigned long)esp_get_free_heap_size());
        }
        if (s_ap_close_ms && (int32_t)(now - s_ap_close_ms) >= 0) {
            s_ap_close_ms = 0;
            if (s_http) { httpd_stop(s_http); s_http = NULL; }
            err = esp_wifi_set_mode(WIFI_MODE_STA);
            s_setup = false;
            portENTER_CRITICAL(&s_mux);
            s_view.provisioning = false;
            memset(s_view.ap_password, 0, sizeof(s_view.ap_password));
            portEXIT_CRITICAL(&s_mux);
            if (err == ESP_OK) err = start_sampling();
            if (err != ESP_OK) fail_status("采样启动失败", err);
        }
        if (s_trying && now - s_attempt_ms > 25000) {
            s_trying = false;
            (void)esp_wifi_disconnect();
            (void)start_portal();
            set_status("连接超时，请重新配网");
        }
        if (now - s_last_battery_ms > 10000) {
            s_last_battery_ms = now;
            int soc = bsp_battery_soc();
            portENTER_CRITICAL(&s_mux);
            s_view.battery = soc;
            portEXIT_CRITICAL(&s_mux);
        }
    }
}
