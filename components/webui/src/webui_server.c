#include "webui/webui_server.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "webui";

/*
 * NOTE: Server-rendered pages are intentionally simple HTML strings in v1 scaffold.
 * Next step should move templates to static assets and add CSRF/auth hardening.
 */

static esp_err_t handle_root(httpd_req_t *req)
{
    const char *html =
        "<html><body><h1>ESP32 Fingerprint Terminal</h1>"
        "<p>Scaffold UI active.</p>"
        "<ul>"
        "<li><a href='/setup'>Initial setup</a></li>"
        "<li><a href='/admin'>Admin panel</a></li>"
        "</ul></body></html>";
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

void webui_server_start(app_controller_t *controller)
{
    (void)controller;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
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
    httpd_register_uri_handler(server, &root);
    ESP_LOGI(TAG, "Web UI started");
}
