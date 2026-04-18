#include "webui/webui_server.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "application/use_cases.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "infrastructure/network_manager.h"
#include "infrastructure/websocket_transport.h"

static const char *TAG = "webui";
static app_controller_t *s_controller = NULL;
static bool s_sta_switch_in_progress = false;

#define WEBUI_HTML_BUF_LEN 8192
#define WEBUI_EVENTS_MAX 12
#define WEBUI_EVENT_TEXT_MAX 120
#define WEBUI_LIST_PREVIEW_MAX 128

static char s_recent_events[WEBUI_EVENTS_MAX][WEBUI_EVENT_TEXT_MAX];
static size_t s_recent_events_count = 0;

static void add_event(const char *fmt, ...)
{
    char msg[WEBUI_EVENT_TEXT_MAX];
    va_list ap;
    size_t dst;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (s_recent_events_count < WEBUI_EVENTS_MAX) dst = s_recent_events_count++;
    else {
        memmove(&s_recent_events[0], &s_recent_events[1], (WEBUI_EVENTS_MAX - 1U) * WEBUI_EVENT_TEXT_MAX);
        dst = WEBUI_EVENTS_MAX - 1U;
    }
    strlcpy(s_recent_events[dst], msg, sizeof(s_recent_events[dst]));
}

static void sta_switch_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    if (s_controller && !s_controller->network.connect(&s_controller->config)) {
        ESP_LOGE(TAG, "Background STA switch failed for SSID=%s", s_controller->config.wifi_ssid);
    }
    s_sta_switch_in_progress = false;
    vTaskDelete(NULL);
}

static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else *dst++ = *src++;
    }
    *dst = '\0';
}

static bool form_get_value(char *body, const char *key, char *out, size_t out_len)
{
    char *saveptr = NULL;
    size_t key_len = strlen(key);
    for (char *tok = strtok_r(body, "&", &saveptr); tok; tok = strtok_r(NULL, "&", &saveptr)) {
        if (strncmp(tok, key, key_len) == 0 && tok[key_len] == '=') {
            strlcpy(out, tok + key_len + 1, out_len);
            url_decode(out);
            return true;
        }
    }
    return false;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    int read_len = httpd_req_recv(req, buf, len - 1);
    if (read_len <= 0) return ESP_FAIL;
    buf[read_len] = '\0';
    return ESP_OK;
}

static esp_err_t render_root_page(httpd_req_t *req, const char *message)
{
    char ip[20] = "";
    char events_html[2048] = {0};
    size_t events_off = 0;
    char *html = malloc(WEBUI_HTML_BUF_LEN);
    bool softap = network_manager_is_softap_mode();
    bool show_setup = softap || !s_controller->config.initialized;
    if (!html) return httpd_resp_send_500(req);

    for (size_t i = 0; i < s_recent_events_count; ++i) {
        int written = snprintf(events_html + events_off, sizeof(events_html) - events_off, "<li>%s</li>", s_recent_events[i]);
        if (written < 0 || (size_t)written >= (sizeof(events_html) - events_off)) break;
        events_off += (size_t)written;
    }

    if (softap) {
        if (!network_manager_get_softap_ip(ip, sizeof(ip))) strlcpy(ip, "192.168.4.1", sizeof(ip));
    } else {
        if (!network_manager_get_sta_ip(ip, sizeof(ip))) strlcpy(ip, "(pending DHCP)", sizeof(ip));
    }

    if (show_setup) {
        snprintf(html, WEBUI_HTML_BUF_LEN,
                 "<html><body><h1>Initial Setup</h1><p><b>AP IP:</b> http://%s</p><p style='color:#1565c0;'>%s</p>"
                 "<form method='POST' action='/setup/wifi'><label>WiFi SSID</label><br><input name='wifi_ssid' required maxlength='32'><br><br>"
                 "<label>WiFi Password</label><br><input type='password' name='wifi_password' maxlength='64'><br><br>"
                 "<button type='submit'>Save and Switch to WiFi Client</button></form></body></html>", ip, message ? message : "");
    } else {
        snprintf(html, WEBUI_HTML_BUF_LEN,
                 "<html><body><h1>Device Admin</h1><p><b>Current IP:</b> %s</p><p><b>Device:</b> %s</p><p style='color:#1565c0;'>%s</p>"
                 "<h2>WebSocket Config</h2><form method='POST' action='/config/websocket'>"
                 "<label>Server Host</label><br><input name='websocket_host' value='%s' maxlength='127'><br><br>"
                 "<label>Server Port</label><br><input name='websocket_port' value='%u' type='number'><br><br>"
                 "<label>Path</label><br><input name='websocket_path' value='%s' maxlength='95'><br><br>"
                 "<label>Auth Token</label><br><input name='websocket_auth_token' value='%s' maxlength='255'><br><br>"
                 "<label>TLS</label><br><select name='tls_enabled'><option value='0'>ws:// (local/test)</option><option value='1' %s>wss:// (production)</option></select><br><br>"
                 "<button type='submit'>Save WebSocket Config</button></form>"
                 "<h2>Demo</h2><form method='POST' action='/demo/register'><button type='submit'>Enroll</button></form><br>"
                 "<form method='POST' action='/demo/checkin'><button type='submit'>Identify</button></form><br>"
                 "<form method='POST' action='/demo/list'><button type='submit'>List IDs</button></form><br>"
                 "<h2>Recent Events</h2><ul>%s</ul></body></html>",
                 ip, s_controller->config.device_id, message ? message : "", s_controller->config.websocket_host,
                 s_controller->config.websocket_port, s_controller->config.websocket_path,
                 s_controller->config.websocket_auth_token, s_controller->config.tls_enabled ? "selected" : "", events_html);
    }

    esp_err_t send_ret = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return send_ret;
}

static esp_err_t handle_root(httpd_req_t *req) { return s_controller ? render_root_page(req, NULL) : httpd_resp_send_500(req); }

static esp_err_t handle_setup_wifi(httpd_req_t *req)
{
    char body[512] = {0}, copy[512] = {0}, ssid[33] = {0}, password[65] = {0};
    if (!s_controller || read_body(req, body, sizeof(body)) != ESP_OK) return httpd_resp_send_500(req);
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "wifi_ssid", ssid, sizeof(ssid));
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "wifi_password", password, sizeof(password));
    if (!ssid[0]) return render_root_page(req, "WiFi SSID is required.");

    strlcpy(s_controller->config.wifi_ssid, ssid, sizeof(s_controller->config.wifi_ssid));
    strlcpy(s_controller->config.wifi_password, password, sizeof(s_controller->config.wifi_password));
    s_controller->config.initialized = true;
    if (!s_controller->config.websocket_port) s_controller->config.websocket_port = 8080;
    if (!s_controller->config.websocket_path[0]) strlcpy(s_controller->config.websocket_path, "/device", sizeof(s_controller->config.websocket_path));
    if (!s_controller->config.device_id[0]) strlcpy(s_controller->config.device_id, "esp32-fingerprint-01", sizeof(s_controller->config.device_id));
    s_controller->uc.config_repo.save(&s_controller->config);

    if (!s_sta_switch_in_progress) {
        s_sta_switch_in_progress = true;
        if (xTaskCreate(sta_switch_task, "sta_switch", 4096, NULL, 5, NULL) != pdPASS) {
            s_sta_switch_in_progress = false;
            return render_root_page(req, "Could not schedule WiFi switch task.");
        }
    }
    return httpd_resp_send(req, "<html><body><h1>Applying WiFi settings</h1></body></html>", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_config_websocket(httpd_req_t *req)
{
    char body[1024] = {0}, copy[1024] = {0}, host[DOMAIN_ENDPOINT_HOST_MAX] = {0}, path[DOMAIN_ENDPOINT_PATH_MAX] = {0};
    char token[DOMAIN_AUTH_TOKEN_MAX] = {0}, port_str[16] = {0}, tls_str[4] = {0};
    if (!s_controller || read_body(req, body, sizeof(body)) != ESP_OK) return httpd_resp_send_500(req);

    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "websocket_host", host, sizeof(host));
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "websocket_port", port_str, sizeof(port_str));
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "websocket_path", path, sizeof(path));
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "websocket_auth_token", token, sizeof(token));
    strlcpy(copy, body, sizeof(copy)); form_get_value(copy, "tls_enabled", tls_str, sizeof(tls_str));

    if (host[0]) strlcpy(s_controller->config.websocket_host, host, sizeof(s_controller->config.websocket_host));
    if (path[0]) strlcpy(s_controller->config.websocket_path, path, sizeof(s_controller->config.websocket_path));
    if (token[0]) strlcpy(s_controller->config.websocket_auth_token, token, sizeof(s_controller->config.websocket_auth_token));
    if (port_str[0]) { int port = atoi(port_str); if (port > 0 && port <= 65535) s_controller->config.websocket_port = (uint16_t)port; }
    s_controller->config.tls_enabled = (tls_str[0] == '1');
    s_controller->uc.config_repo.save(&s_controller->config);

    if (!websocket_transport_start(&s_controller->config, &s_controller->uc)) {
        ESP_LOGW(TAG, "WebSocket reconnect attempt failed after settings update");
        return render_root_page(req, "WebSocket settings saved, but reconnect failed.");
    }
    return render_root_page(req, "WebSocket settings saved and reconnect requested.");
}

static esp_err_t handle_demo_register(httpd_req_t *req) { operation_result_t r={0}; bool ok=use_case_register_fingerprint(&s_controller->uc,"web-demo-register",true,&r); add_event(ok?"Enroll ok id=%lu":"Enroll failed",(unsigned long)r.fingerprint_id); return render_root_page(req, ok?"Enroll successful":"Enroll failed"); }
static esp_err_t handle_demo_checkin(httpd_req_t *req) { uint32_t id=0; bool ok=use_case_check_in_once_with_id(&s_controller->uc,&id); add_event(ok?"Identify ok id=%lu":"Identify failed",(unsigned long)id); return render_root_page(req, ok?"Identify successful":"Identify failed"); }
static esp_err_t handle_demo_list(httpd_req_t *req) { uint32_t ids[WEBUI_LIST_PREVIEW_MAX]; size_t found=0; bool ok=use_case_list_registered_fingerprints(&s_controller->uc,ids,WEBUI_LIST_PREVIEW_MAX,&found); add_event(ok?"List ok count=%u":"List failed",(unsigned)found); return render_root_page(req, ok?"List completed":"List failed"); }

void webui_server_start(app_controller_t *controller)
{
    s_controller = controller;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 16384; cfg.max_uri_handlers = 8;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) return;

    httpd_uri_t root = {.uri="/",.method=HTTP_GET,.handler=handle_root};
    httpd_uri_t setup_wifi = {.uri="/setup/wifi",.method=HTTP_POST,.handler=handle_setup_wifi};
    httpd_uri_t config_ws = {.uri="/config/websocket",.method=HTTP_POST,.handler=handle_config_websocket};
    httpd_uri_t demo_register = {.uri="/demo/register",.method=HTTP_POST,.handler=handle_demo_register};
    httpd_uri_t demo_checkin = {.uri="/demo/checkin",.method=HTTP_POST,.handler=handle_demo_checkin};
    httpd_uri_t demo_list = {.uri="/demo/list",.method=HTTP_POST,.handler=handle_demo_list};

    if (httpd_register_uri_handler(server, &root) != ESP_OK ||
        httpd_register_uri_handler(server, &setup_wifi) != ESP_OK ||
        httpd_register_uri_handler(server, &config_ws) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_register) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_checkin) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_list) != ESP_OK) {
        httpd_stop(server);
        return;
    }
}
