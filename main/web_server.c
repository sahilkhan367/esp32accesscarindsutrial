#include "esp_http_server.h"
#include "wifi_manager.h"
#include <ctype.h>
#include "esp_log.h"


static httpd_handle_t server = NULL;



static const char *wifi_rssi_quality(int rssi)
{
    if (rssi >= -50) return "Excellent";
    if (rssi >= -60) return "Good";
    if (rssi >= -70) return "Fair";
    if (rssi >= -80) return "Weak";
    return "Very Weak";
}



static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {

            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';

            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';

            *dst++ = 16 * a + b;
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




static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[len] = 0;

    char ssid[32] = {0};
    char pass[64] = {0};

    char raw_ssid[64] = {0};
    char raw_pass[128] = {0};

    sscanf(buf, "ssid=%63[^&]&pass=%127s", raw_ssid, raw_pass);

    url_decode(ssid, raw_ssid);
    url_decode(pass, raw_pass);

    ESP_LOGI("WEB", "SSID: %s", ssid);
    ESP_LOGI("WEB", "PASS: %s", pass);

    wifi_connect_sta(ssid, pass);

    httpd_resp_send(req,
        "WiFi credentials saved. Rebooting device...",
        HTTPD_RESP_USE_STRLEN
    );

    // 🔥 IMPORTANT: delay before reboot
    vTaskDelay(pdMS_TO_TICKS(1500));

    esp_restart();   // ✅ CLEAN REBOOT

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char html[1200];

    bool connected = wifi_is_sta_connected();
    int rssi = wifi_get_rssi();

    char ssid[33] = "N/A";
    if (connected) {
        wifi_get_connected_ssid(ssid, sizeof(ssid));
    }

    const char *quality = connected ? wifi_rssi_quality(rssi) : "N/A";
    const char *status  = connected ? "Connected" : "Disconnected";

    snprintf(html, sizeof(html),
        "<html>"
        "<head>"
        "<title>ESP32 WiFi Setup</title>"
        "</head>"
        "<body>"
        "<h2>WiFi Status</h2>"
        "<p>Status: <b>%s</b></p>"
        "<p>Connected SSID: <b>%s</b></p>"
        "<p>Signal Strength: <b>%d dBm</b></p>"
        "<p>Quality: <b>%s</b></p>"
        "<hr>"
        "<h3>Configure WiFi</h3>"
        "<form method='POST'>"
        "SSID:<br><input name='ssid'><br>"
        "Password:<br><input name='pass' type='password'><br><br>"
        "<input type='submit' value='Save & Reboot'>"
        "</form>"
        "</body>"
        "</html>",
        status,
        ssid,
        connected ? rssi : 0,
        quality
    );

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &config);

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler
    };

    httpd_uri_t post = {
        .uri = "/",
        .method = HTTP_POST,
        .handler = wifi_post_handler
    };

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &post);
}


