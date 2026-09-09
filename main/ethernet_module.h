#ifndef ETHERNET_MODULE_H
#define ETHERNET_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ENC28J60 Ethernet.
 *
 * Initializes:
 *  - TCP/IP stack
 *  - Event loop
 *  - Ethernet netif
 *  - SPI bus
 *  - ENC28J60 MAC/PHY
 *  - Ethernet driver
 *  - DHCP
 *
 * @return ESP_OK on success
 */
esp_err_t ethernet_init(void);

/**
 * @brief Check whether Ethernet has obtained an IP address.
 *
 * @return true if Ethernet is connected and has an IP address.
 */
bool ethernet_is_connected(void);
bool ethernet_link_is_up(void);

/**
 * @brief Get Ethernet IP information.
 *
 * @param ip_info Pointer to structure where IP information will be stored.
 *
 * @return ESP_OK if IP information is available.
 */
esp_err_t ethernet_get_ip_info(esp_netif_ip_info_t *ip_info);

/**
 * @brief Perform HTTP GET request.
 *
 * @param url HTTP/HTTPS URL.
 *
 * @return ESP_OK if request was successfully performed.
 */
esp_err_t ethernet_http_get(const char *url);

/**
 * @brief Perform HTTP POST request.
 *
 * @param url HTTP/HTTPS URL.
 * @param post_data POST body.
 *
 * @return ESP_OK if request was successfully performed.
 */
esp_err_t ethernet_http_post(
    const char *url,
    const char *post_data
);

#ifdef __cplusplus
}
#endif

#endif /* ETHERNET_MODULE_H */