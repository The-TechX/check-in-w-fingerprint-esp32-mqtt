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
#include "infrastructure/mqtt_adapter.h"

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

    if (s_recent_events_count < WEBUI_EVENTS_MAX) {
        dst = s_recent_events_count++;
    } else {
        memmove(&s_recent_events[0], &s_recent_events[1], (WEBUI_EVENTS_MAX - 1U) * WEBUI_EVENT_TEXT_MAX);
        dst = WEBUI_EVENTS_MAX - 1U;
    }

    strlcpy(s_recent_events[dst], msg, sizeof(s_recent_events[dst]));
}

static void sta_switch_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(600));

    if (s_controller != NULL) {
        ESP_LOGI(TAG, "Applying WiFi STA credentials in background task");
        if (!s_controller->network.connect(&s_controller->config)) {
            ESP_LOGE(TAG, "Background STA switch failed for SSID=%s", s_controller->config.wifi_ssid);
        } else {
            ESP_LOGI(TAG, "Background STA switch started for SSID=%s", s_controller->config.wifi_ssid);
        }
    }

    s_sta_switch_in_progress = false;
    vTaskDelete(NULL);
}

/*
 * NOTE: Server-rendered pages are intentionally simple HTML strings in v1 scaffold.
 * Next step should move templates to static assets and add CSRF/auth hardening.
 */

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
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool form_get_value(char *body, const char *key, char *out, size_t out_len)
{
    char *saveptr = NULL;
    size_t key_len = strlen(key);
    char *tok = strtok_r(body, "&", &saveptr);

    while (tok != NULL) {
        if (strncmp(tok, key, key_len) == 0 && tok[key_len] == '=') {
            strlcpy(out, tok + key_len + 1, out_len);
            url_decode(out);
            return true;
        }
        tok = strtok_r(NULL, "&", &saveptr);
    }

    return false;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    int read_len = httpd_req_recv(req, buf, len - 1);
    if (read_len <= 0) {
        return ESP_FAIL;
    }
    buf[read_len] = '\0';
    return ESP_OK;
}

static esp_err_t render_root_page(httpd_req_t *req, const char *message)
{
    char ip[20] = "";
    char events_html[2048] = {0};
    size_t events_off = 0;
    char *html = malloc(WEBUI_HTML_BUF_LEN);
    bool initialized = s_controller->config.initialized;
    bool softap = network_manager_is_softap_mode();
    bool show_setup = softap || !initialized;
    esp_err_t send_ret;

    if (html == NULL) {
        return httpd_resp_send_500(req);
    }

    for (size_t i = 0; i < s_recent_events_count; ++i) {
        int written = snprintf(events_html + events_off, sizeof(events_html) - events_off, "<li>%s</li>", s_recent_events[i]);
        if (written < 0 || (size_t)written >= (sizeof(events_html) - events_off)) {
            break;
        }
        events_off += (size_t)written;
    }

    if (softap) {
        if (!network_manager_get_softap_ip(ip, sizeof(ip))) {
            strlcpy(ip, "192.168.4.1", sizeof(ip));
        }
    } else {
        if (!network_manager_get_sta_ip(ip, sizeof(ip))) {
            strlcpy(ip, "(pending DHCP)", sizeof(ip));
        }
    }

    if (show_setup) {
        snprintf(html, WEBUI_HTML_BUF_LEN,
            "<html><body><h1>Initial Setup</h1>"
            "<p><b>Mode:</b> SoftAP onboarding</p>"
            "<p><b>Connect to SSID:</b> FP-Terminal-Setup</p>"
            "<p><b>Password:</b> setup1234</p>"
            "<p><b>AP IP:</b> http://%s</p>"
            "<p>If this device had old credentials, re-enter WiFi SSID/password to switch again.</p>"
            "<p style='color:#1565c0;'>%s</p>"
            "<form method='POST' action='/setup/wifi'>"
            "<label>WiFi SSID</label><br><input name='wifi_ssid' required maxlength='32'><br><br>"
            "<label>WiFi Password</label><br><input type='password' name='wifi_password' maxlength='64'><br><br>"
            "<button type='submit'>Save and Switch to WiFi Client</button>"
            "</form>"
            "</body></html>",
            ip,
            message ? message : "");
    } else {
        snprintf(html, WEBUI_HTML_BUF_LEN,
            "<html><body><h1>Device Admin</h1>"
            "<p><b>Mode:</b> %s</p>"
            "<p><b>Current IP:</b> %s</p>"
            "<p><b>Device:</b> %s</p>"
            "<p style='color:#1565c0;'>%s</p>"
            "<h2>MQTT Config</h2>"
            "<form method='POST' action='/config/mqtt'>"
            "<label>Broker Host</label><br><input name='mqtt_host' value='%s' maxlength='127'><br><br>"
            "<label>Broker Port</label><br><input name='mqtt_port' value='%u' type='number'><br><br>"
            "<label>Topic Prefix</label><br><input name='topic_prefix' value='%s' maxlength='63'><br><br>"
            "<label>Auth Token</label><br><input name='auth_token' value='%s' maxlength='255'><br><br>"
            "<button type='submit'>Save MQTT Config</button>"
            "</form>"
            "<h2>Demo / Use Cases</h2>"
            "<form method='POST' action='/demo/register'><button type='submit'>Enroll Fingerprint (Demo)</button></form><br>"
            "<form method='POST' action='/demo/checkin'><button type='submit'>Run Check-in Once</button></form><br>"
            "<form method='POST' action='/demo/list'><button type='submit'>Show Registered IDs</button></form><br>"
            "<form method='POST' action='/demo/delete'>"
            "<label>Fingerprint ID to delete</label><br><input name='fingerprint_id' type='number' min='1' required><br><br>"
            "<button type='submit'>Delete Fingerprint (Demo)</button>"
            "</form><br>"
            "<form method='POST' action='/demo/wipe' onsubmit=\"return confirm('Delete ALL fingerprints?')\">"
            "<button type='submit'>Wipe All Fingerprints (Demo only)</button>"
            "</form>"
            "<h2>Recent Events</h2>"
            "<div id='events' style='border:1px solid #ddd; padding:10px; max-width:720px; background:#fafafa;'>"
            "<ul>%s</ul>"
            "</div>"
            "</body></html>",
            softap ? "SoftAP" : "WiFi Client (STA)",
            ip,
            s_controller->config.device_id,
            message ? message : "",
            s_controller->config.mqtt_broker_host,
            s_controller->config.mqtt_port,
            s_controller->config.mqtt_topic_prefix,
            s_controller->config.mqtt_auth_token,
            events_html);
    }

    send_ret = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return send_ret;
}

static esp_err_t handle_root(httpd_req_t *req)
{
    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }
    return render_root_page(req, NULL);
}

static esp_err_t handle_setup_wifi(httpd_req_t *req)
{
    char *body = NULL;
    char *copy = NULL;
    char ssid[33] = {0};
    char password[65] = {0};

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    body = malloc(512);
    copy = malloc(512);
    if (body == NULL || copy == NULL) {
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    if (read_body(req, body, 512) != ESP_OK) {
        ESP_LOGE(TAG, "setup/wifi POST failed to read request body");
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    strlcpy(copy, body, 512);
    form_get_value(copy, "wifi_ssid", ssid, sizeof(ssid));
    strlcpy(copy, body, 512);
    form_get_value(copy, "wifi_password", password, sizeof(password));

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "setup/wifi POST without SSID");
        free(body);
        free(copy);
        return render_root_page(req, "WiFi SSID is required.");
    }

    ESP_LOGI(TAG, "setup/wifi POST received SSID=%s", ssid);

    strlcpy(s_controller->config.wifi_ssid, ssid, sizeof(s_controller->config.wifi_ssid));
    strlcpy(s_controller->config.wifi_password, password, sizeof(s_controller->config.wifi_password));
    s_controller->config.initialized = true;
    if (s_controller->config.mqtt_port == 0) {
        s_controller->config.mqtt_port = 1883;
    }
    if (s_controller->config.device_id[0] == '\0') {
        strlcpy(s_controller->config.device_id, "esp32-fingerprint-01", sizeof(s_controller->config.device_id));
    }
    if (s_controller->config.mqtt_topic_prefix[0] == '\0') {
        strlcpy(s_controller->config.mqtt_topic_prefix, "fingerprint", sizeof(s_controller->config.mqtt_topic_prefix));
    }

    s_controller->uc.config_repo.save(&s_controller->config);

    if (!s_sta_switch_in_progress) {
        s_sta_switch_in_progress = true;
        if (xTaskCreate(sta_switch_task, "sta_switch", 4096, NULL, 5, NULL) != pdPASS) {
            s_sta_switch_in_progress = false;
            ESP_LOGE(TAG, "Failed to create STA switch task");
            free(body);
            free(copy);
            return render_root_page(req, "Could not schedule WiFi switch task.");
        }
    } else {
        ESP_LOGW(TAG, "STA switch already in progress; ignoring duplicate setup request");
    }

    ESP_LOGI(TAG, "setup/wifi accepted SSID=%s and scheduled STA switch", ssid);
    free(body);
    free(copy);
    return httpd_resp_send(req,
                          "<html><body><h1>Applying WiFi settings</h1><p>Credentials saved. Device will switch from SoftAP to WiFi client in a moment.</p><p>Reconnect your phone/laptop to your normal WiFi and open the device IP from your router DHCP list.</p></body></html>",
                          HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_setup_wifi_get(httpd_req_t *req)
{
    return render_root_page(req, NULL);
}

static esp_err_t handle_config_mqtt(httpd_req_t *req)
{
    char *body = NULL;
    char *copy = NULL;
    char host[DOMAIN_BROKER_HOST_MAX] = {0};
    char topic[DOMAIN_TOPIC_PREFIX_MAX] = {0};
    char token[DOMAIN_AUTH_TOKEN_MAX] = {0};
    char port_str[16] = {0};

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    body = malloc(1024);
    copy = malloc(1024);
    if (body == NULL || copy == NULL) {
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    if (read_body(req, body, 1024) != ESP_OK) {
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    strlcpy(copy, body, 1024);
    form_get_value(copy, "mqtt_host", host, sizeof(host));
    strlcpy(copy, body, 1024);
    form_get_value(copy, "mqtt_port", port_str, sizeof(port_str));
    strlcpy(copy, body, 1024);
    form_get_value(copy, "topic_prefix", topic, sizeof(topic));
    strlcpy(copy, body, 1024);
    form_get_value(copy, "auth_token", token, sizeof(token));

    if (host[0] != '\0') {
        strlcpy(s_controller->config.mqtt_broker_host, host, sizeof(s_controller->config.mqtt_broker_host));
    }
    if (port_str[0] != '\0') {
        int port = atoi(port_str);
        if (port > 0 && port <= 65535) {
            s_controller->config.mqtt_port = (uint16_t)port;
        }
    }
    if (topic[0] != '\0') {
        strlcpy(s_controller->config.mqtt_topic_prefix, topic, sizeof(s_controller->config.mqtt_topic_prefix));
    }
    if (token[0] != '\0') {
        strlcpy(s_controller->config.mqtt_auth_token, token, sizeof(s_controller->config.mqtt_auth_token));
    }

    s_controller->uc.config_repo.save(&s_controller->config);

    if (s_controller->config.mqtt_port == 0) {
        s_controller->config.mqtt_port = 1883;
    }
    if (s_controller->config.device_id[0] == ' ') {
        strlcpy(s_controller->config.device_id, "esp32-fingerprint-01", sizeof(s_controller->config.device_id));
    }

    if (!mqtt_adapter_start(&s_controller->config, &s_controller->uc)) {
        ESP_LOGW(TAG, "MQTT reconnect attempt failed after settings update");
        free(body);
        free(copy);
        return render_root_page(req, "MQTT settings saved, but reconnect failed. Check host/deviceId/topic/network.");
    }

    free(body);
    free(copy);
    return render_root_page(req, "MQTT settings saved and reconnect requested.");
}

static esp_err_t handle_demo_register(httpd_req_t *req)
{
    operation_result_t result = {0};
    bool ok;
    char msg[96];

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    ok = use_case_register_fingerprint(&s_controller->uc, "web-demo-register", true, &result);
    if (ok) {
        snprintf(msg, sizeof(msg), "Enroll successful. ID=%lu", (unsigned long)result.fingerprint_id);
        add_event("Enroll successful with ID=%lu", (unsigned long)result.fingerprint_id);
        return render_root_page(req, msg);
    }
    add_event("Enroll failed");
    return render_root_page(req, "Demo enroll failed.");
}

static esp_err_t handle_demo_checkin(httpd_req_t *req)
{
    bool ok;
    uint32_t fingerprint_id = 0;
    char msg[96];

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    ESP_LOGI(TAG, "demo/checkin requested");
    ok = use_case_check_in_once_with_id(&s_controller->uc, &fingerprint_id);
    if (ok) {
        ESP_LOGI(TAG, "demo/checkin finished OK");
        snprintf(msg, sizeof(msg), "Check-in successful. ID=%lu", (unsigned long)fingerprint_id);
        add_event("Check-in successful with ID=%lu", (unsigned long)fingerprint_id);
        return render_root_page(req, msg);
    }
    ESP_LOGW(TAG, "demo/checkin finished with no match/failure");
    add_event("Check-in failed (no match)");
    return render_root_page(req, "Check-in failed (no fingerprint identified yet).");
}

static esp_err_t handle_demo_delete(httpd_req_t *req)
{
    char *body = NULL;
    char *copy = NULL;
    char id_str[16] = {0};
    operation_result_t result = {0};
    uint32_t fingerprint_id;
    bool ok;

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    body = malloc(256);
    copy = malloc(256);
    if (body == NULL || copy == NULL) {
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    if (read_body(req, body, 256) != ESP_OK) {
        free(body);
        free(copy);
        return httpd_resp_send_500(req);
    }

    strlcpy(copy, body, 256);
    form_get_value(copy, "fingerprint_id", id_str, sizeof(id_str));
    free(body);
    free(copy);

    if (id_str[0] == '\0') {
        return render_root_page(req, "Fingerprint ID is required.");
    }

    fingerprint_id = (uint32_t)strtoul(id_str, NULL, 10);
    if (fingerprint_id == 0) {
        return render_root_page(req, "Fingerprint ID must be > 0.");
    }

    ok = use_case_delete_fingerprint(&s_controller->uc, fingerprint_id, "web-demo-delete", &result);
    if (ok) {
        ESP_LOGI(TAG, "demo/delete success fingerprint_id=%lu", (unsigned long)fingerprint_id);
        add_event("Delete successful for ID=%lu", (unsigned long)fingerprint_id);
        return render_root_page(req, "Delete successful.");
    }

    ESP_LOGW(TAG, "demo/delete failed fingerprint_id=%lu", (unsigned long)fingerprint_id);
    add_event("Delete failed for ID=%lu", (unsigned long)fingerprint_id);
    return render_root_page(req, "Delete fingerprint failed.");
}

static esp_err_t handle_demo_wipe(httpd_req_t *req)
{
    operation_result_t result = {0};
    bool ok;

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    ok = use_case_wipe_all_fingerprints(&s_controller->uc, "web-demo-wipe", &result);
    if (ok) {
        ESP_LOGI(TAG, "demo/wipe success");
        add_event("Wipe all successful");
        return render_root_page(req, "All fingerprints removed.");
    }

    ESP_LOGW(TAG, "demo/wipe failed");
    add_event("Wipe all failed");
    return render_root_page(req, "Wipe all failed.");
}

static esp_err_t handle_demo_list(httpd_req_t *req)
{
    uint32_t ids[WEBUI_LIST_PREVIEW_MAX];
    size_t found = 0;
    bool ok;
    char msg[256];
    char line[128];
    size_t off = 0;

    if (s_controller == NULL) {
        return httpd_resp_send_500(req);
    }

    ok = use_case_list_registered_fingerprints(&s_controller->uc, ids, WEBUI_LIST_PREVIEW_MAX, &found);
    if (!ok) {
        add_event("List IDs failed");
        return render_root_page(req, "Could not read IDs from module.");
    }

    off += (size_t)snprintf(msg + off, sizeof(msg) - off, "Module templates: %u", (unsigned)found);
    if (found > 0U) {
        off += (size_t)snprintf(msg + off, sizeof(msg) - off, ". IDs: ");
        size_t show = found < WEBUI_LIST_PREVIEW_MAX ? found : WEBUI_LIST_PREVIEW_MAX;
        for (size_t i = 0; i < show; ++i) {
            int n = snprintf(line, sizeof(line), "%s%lu", (i == 0U) ? "" : ", ", (unsigned long)ids[i]);
            if (n <= 0 || (off + (size_t)n) >= sizeof(msg)) {
                break;
            }
            memcpy(msg + off, line, (size_t)n);
            off += (size_t)n;
            msg[off] = '\0';
        }
        if (found > WEBUI_LIST_PREVIEW_MAX && off < sizeof(msg) - 4U) {
            strlcpy(msg + off, " ...", sizeof(msg) - off);
        }
    }

    add_event("Listed %u template IDs from module", (unsigned)found);
    return render_root_page(req, msg);
}

void webui_server_start(app_controller_t *controller)
{
    s_controller = controller;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 16384;
    cfg.max_uri_handlers = 10;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
    };
    httpd_uri_t setup_wifi = {
        .uri = "/setup/wifi",
        .method = HTTP_POST,
        .handler = handle_setup_wifi,
    };
    httpd_uri_t setup_wifi_get = {
        .uri = "/setup/wifi",
        .method = HTTP_GET,
        .handler = handle_setup_wifi_get,
    };
    httpd_uri_t config_mqtt = {
        .uri = "/config/mqtt",
        .method = HTTP_POST,
        .handler = handle_config_mqtt,
    };
    httpd_uri_t demo_register = {
        .uri = "/demo/register",
        .method = HTTP_POST,
        .handler = handle_demo_register,
    };
    httpd_uri_t demo_checkin = {
        .uri = "/demo/checkin",
        .method = HTTP_POST,
        .handler = handle_demo_checkin,
    };
    httpd_uri_t demo_delete = {
        .uri = "/demo/delete",
        .method = HTTP_POST,
        .handler = handle_demo_delete,
    };
    httpd_uri_t demo_wipe = {
        .uri = "/demo/wipe",
        .method = HTTP_POST,
        .handler = handle_demo_wipe,
    };
    httpd_uri_t demo_list = {
        .uri = "/demo/list",
        .method = HTTP_POST,
        .handler = handle_demo_list,
    };

    if (httpd_register_uri_handler(server, &root) != ESP_OK ||
        httpd_register_uri_handler(server, &setup_wifi_get) != ESP_OK ||
        httpd_register_uri_handler(server, &setup_wifi) != ESP_OK ||
        httpd_register_uri_handler(server, &config_mqtt) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_register) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_checkin) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_delete) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_wipe) != ESP_OK ||
        httpd_register_uri_handler(server, &demo_list) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register one or more HTTP handlers");
        httpd_stop(server);
        return;
    }

    ESP_LOGI(TAG, "Web UI started");
}
