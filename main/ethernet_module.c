#include "ethernet_module.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_enc28j60.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "wifi_manager.h"


static const char *TAG = "ETHERNET_MODULE";


/* ============================================================
 * ENC28J60 Configuration
 * ============================================================ */

#define ETH_SPI_HOST        SPI2_HOST

#define ETH_MOSI_GPIO       25
#define ETH_MISO_GPIO       32
#define ETH_SCLK_GPIO       21
#define ETH_CS_GPIO         19
#define ETH_INT_GPIO        34

#define ETH_SPI_CLOCK_HZ    (8 * 1000 * 1000)


/* ============================================================
 * Module State
 * ============================================================ */

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;

static volatile bool s_ethernet_connected = false;
static volatile bool s_ethernet_has_ip = false;




/* ============================================================
 * HTTP Event Handler
 * ============================================================ */

static esp_err_t http_event_handler(
    esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:

            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");

            break;

        case HTTP_EVENT_ON_CONNECTED:

            ESP_LOGI(TAG, "HTTP connected");

            break;

        case HTTP_EVENT_HEADER_SENT:

            ESP_LOGI(TAG, "HTTP header sent");

            break;

        case HTTP_EVENT_ON_HEADER:

            ESP_LOGI(
                TAG,
                "HTTP header: %.*s",
                evt->data_len,
                (char *)evt->data
            );

            break;

        case HTTP_EVENT_ON_DATA:

            if (evt->data != NULL && evt->data_len > 0)
            {
                ESP_LOGI(
                    TAG,
                    "HTTP response: %.*s",
                    evt->data_len,
                    (char *)evt->data
                );
            }

            break;

        case HTTP_EVENT_ON_FINISH:

            ESP_LOGI(TAG, "HTTP request finished");

            break;

        case HTTP_EVENT_DISCONNECTED:

            ESP_LOGI(TAG, "HTTP disconnected");

            break;

        default:

            break;
    }

    return ESP_OK;
}


/* ============================================================
 * Ethernet Event Handler
 * ============================================================ */

static void eth_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    switch (event_id)
    {
        case ETHERNET_EVENT_START:

            ESP_LOGI(
                TAG,
                "Ethernet Started"
            );

            break;


        case ETHERNET_EVENT_CONNECTED:

            ESP_LOGI(
                TAG,
                "Ethernet Link Up"
            );

            s_ethernet_connected = true;

            break;


        case ETHERNET_EVENT_DISCONNECTED:

            ESP_LOGI(TAG,"Ethernet Link Down");

            s_ethernet_connected = false;
            s_ethernet_has_ip = false;
            wifi_update_network_led();
            if (!wifi_is_sta_connected()) {
                wifi_manager_start_sta();
                // gpio_set_level(WIFI_LED_GPIO, 1);
            }

            break;


        case ETHERNET_EVENT_STOP:

            ESP_LOGI(
                TAG,
                "Ethernet Stopped"
            );

            s_ethernet_connected = false;
            s_ethernet_has_ip = false;

            break;


        default:

            break;
    }
}


/* ============================================================
 * IP Event Handler
 * ============================================================ */

static void got_ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_id == IP_EVENT_ETH_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        esp_netif_ip_info_t *ip_info =
            &event->ip_info;


        s_ethernet_has_ip = true;
        wifi_update_network_led();

        // gpio_set_level(WIFI_LED_GPIO, 1);   // BLUE LED OFF


        ESP_LOGI(TAG, "Ethernet Got IP" );
        ESP_LOGI(TAG, "IP      : " IPSTR, IP2STR(&ip_info->ip));
        ESP_LOGI(TAG, "Netmask : " IPSTR, IP2STR(&ip_info->netmask));
        ESP_LOGI(TAG, "Gateway : " IPSTR, IP2STR(&ip_info->gw));
    }
}


/* ============================================================
 * Ethernet Init
 * ============================================================ */

esp_err_t ethernet_init(void)
{
    esp_err_t ret;


    ESP_LOGI(
        TAG,
        "Initializing ENC28J60 Ethernet..."
    );


    /* --------------------------------------------------------
     * 1. Install GPIO ISR service
     * -------------------------------------------------------- */

    ret = gpio_install_isr_service(0);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "Failed to install GPIO ISR service: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 2. Initialize TCP/IP stack
     * -------------------------------------------------------- */

    ret = esp_netif_init();

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_netif_init failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 3. Create default event loop
     * -------------------------------------------------------- */

    ret = esp_event_loop_create_default();

    if (ret != ESP_OK &&
        ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "Event loop creation failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 4. Create Ethernet network interface
     * -------------------------------------------------------- */

    esp_netif_config_t netif_cfg =
        ESP_NETIF_DEFAULT_ETH();


    s_eth_netif =
        esp_netif_new(&netif_cfg);


    if (s_eth_netif == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create Ethernet netif"
        );

        return ESP_FAIL;
    }


    /* --------------------------------------------------------
     * 5. Register Ethernet event handler
     * -------------------------------------------------------- */

    ret = esp_event_handler_register(
        ETH_EVENT,
        ESP_EVENT_ANY_ID,
        eth_event_handler,
        NULL
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to register Ethernet event handler"
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 6. Register IP event handler
     * -------------------------------------------------------- */

    ret = esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_ETH_GOT_IP,
        got_ip_event_handler,
        NULL
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to register IP event handler"
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 7. Configure SPI bus
     * -------------------------------------------------------- */

    spi_bus_config_t buscfg = {

        .mosi_io_num = ETH_MOSI_GPIO,

        .miso_io_num = ETH_MISO_GPIO,

        .sclk_io_num = ETH_SCLK_GPIO,

        .quadwp_io_num = -1,

        .quadhd_io_num = -1,

        .max_transfer_sz = 1600
    };


    ret = spi_bus_initialize(
        ETH_SPI_HOST,
        &buscfg,
        SPI_DMA_CH_AUTO
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "SPI bus initialization failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 8. Configure SPI device
     * -------------------------------------------------------- */

    spi_device_interface_config_t spi_devcfg = {

        .mode = 0,

        .clock_speed_hz =
            ETH_SPI_CLOCK_HZ,

        .spics_io_num =
            ETH_CS_GPIO,

        .queue_size = 20
    };


    spi_devcfg.cs_ena_posttrans =
        enc28j60_cal_spi_cs_hold_time(8);


    /* --------------------------------------------------------
     * 9. Configure ENC28J60 MAC
     * -------------------------------------------------------- */

    eth_mac_config_t mac_config =
        ETH_MAC_DEFAULT_CONFIG();


    eth_enc28j60_config_t enc_config =
        ETH_ENC28J60_DEFAULT_CONFIG(
            ETH_SPI_HOST,
            &spi_devcfg
        );


    enc_config.int_gpio_num =
        ETH_INT_GPIO;


    /* --------------------------------------------------------
     * 10. Create ENC28J60 MAC
     * -------------------------------------------------------- */

    esp_eth_mac_t *mac =
        esp_eth_mac_new_enc28j60(
            &enc_config,
            &mac_config
        );


    if (mac == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create ENC28J60 MAC"
        );

        return ESP_FAIL;
    }


    /* --------------------------------------------------------
     * 11. Configure PHY
     * -------------------------------------------------------- */

    eth_phy_config_t phy_config =
        ETH_PHY_DEFAULT_CONFIG();


    phy_config.autonego_timeout_ms = 0;

    phy_config.reset_gpio_num = -1;


    /* --------------------------------------------------------
     * 12. Create PHY
     * -------------------------------------------------------- */

    esp_eth_phy_t *phy =
        esp_eth_phy_new_enc28j60(
            &phy_config
        );


    if (phy == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create ENC28J60 PHY"
        );

        return ESP_FAIL;
    }


    /* --------------------------------------------------------
     * 13. Ethernet driver configuration
     * -------------------------------------------------------- */

    esp_eth_config_t eth_config =
        ETH_DEFAULT_CONFIG(
            mac,
            phy
        );


    /* --------------------------------------------------------
     * 14. Install Ethernet driver
     * -------------------------------------------------------- */

    ret = esp_eth_driver_install(
        &eth_config,
        &s_eth_handle
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Ethernet driver installation failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 15. Set MAC address
     * -------------------------------------------------------- */

    uint8_t mac_addr[6] = {

        0x02,
        0x00,
        0x00,
        0x12,
        0x34,
        0x56
    };


    ret = esp_eth_ioctl(
        s_eth_handle,
        ETH_CMD_S_MAC_ADDR,
        mac_addr
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to set MAC address"
        );

        return ret;
    }


    ESP_LOGI(
        TAG,
        "MAC Address: "
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac_addr[0],
        mac_addr[1],
        mac_addr[2],
        mac_addr[3],
        mac_addr[4],
        mac_addr[5]
    );


    /* --------------------------------------------------------
     * 16. Attach Ethernet to TCP/IP stack
     * -------------------------------------------------------- */

    esp_eth_netif_glue_handle_t glue =
        esp_eth_new_netif_glue(
            s_eth_handle
        );


    if (glue == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create Ethernet netif glue"
        );

        return ESP_FAIL;
    }


    ret = esp_netif_attach(
        s_eth_netif,
        glue
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to attach Ethernet netif"
        );

        return ret;
    }


    /* --------------------------------------------------------
     * 17. Start Ethernet
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Starting Ethernet..."
    );


    ret = esp_eth_start(
        s_eth_handle
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to start Ethernet: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    ESP_LOGI(
        TAG,
        "Ethernet initialization completed"
    );


    ESP_LOGI(
        TAG,
        "Waiting for DHCP IP..."
    );


    return ESP_OK;
}


/* ============================================================
 * Check Ethernet Connection
 * ============================================================ */

bool ethernet_is_connected(void)
{
    return s_ethernet_has_ip;
}


/* ============================================================
 * Get IP Information
 * ============================================================ */

esp_err_t ethernet_get_ip_info(
    esp_netif_ip_info_t *ip_info)
{
    if (ip_info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (s_eth_netif == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }


    if (!s_ethernet_has_ip)
    {
        return ESP_ERR_INVALID_STATE;
    }


    return esp_netif_get_ip_info(
        s_eth_netif,
        ip_info
    );
}


/* ============================================================
 * HTTP GET
 * ============================================================ */

esp_err_t ethernet_http_get(
    const char *url)
{
    if (url == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (!ethernet_is_connected())
    {
        ESP_LOGW(
            TAG,
            "Ethernet is not connected"
        );

        return ESP_ERR_INVALID_STATE;
    }


    ESP_LOGI(
        TAG,
        "HTTP GET: %s",
        url
    );


    esp_http_client_config_t config = {

        .url = url,

        .event_handler =
            http_event_handler,

        .crt_bundle_attach =
            esp_crt_bundle_attach,

        .timeout_ms = 10000
    };


    esp_http_client_handle_t client =
        esp_http_client_init(&config);


    if (client == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize HTTP client"
        );

        return ESP_FAIL;
    }


    esp_err_t err =
        esp_http_client_perform(client);


    if (err == ESP_OK)
    {
        int status_code =
            esp_http_client_get_status_code(client);

        int content_length =
            esp_http_client_get_content_length(client);


        ESP_LOGI(
            TAG,
            "HTTP Status = %d, Content Length = %d",
            status_code,
            content_length
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "HTTP GET failed: %s",
            esp_err_to_name(err)
        );
    }


    esp_http_client_cleanup(client);


    return err;
}


/* ============================================================
 * HTTP POST
 * ============================================================ */

esp_err_t ethernet_http_post(
    const char *url,
    const char *post_data)
{
    if (url == NULL || post_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (!ethernet_is_connected())
    {
        ESP_LOGW(
            TAG,
            "Ethernet is not connected"
        );

        return ESP_ERR_INVALID_STATE;
    }


    ESP_LOGI(
        TAG,
        "HTTP POST: %s",
        url
    );


    esp_http_client_config_t config = {

        .url = url,

        .event_handler =
            http_event_handler,

        .crt_bundle_attach =
            esp_crt_bundle_attach,

        .timeout_ms = 10000
    };


    esp_http_client_handle_t client =
        esp_http_client_init(&config);


    if (client == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize HTTP client"
        );

        return ESP_FAIL;
    }


    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST
    );


    esp_http_client_set_header(
        client,
        "Content-Type",
        "application/json"
    );


    esp_http_client_set_post_field(
        client,
        post_data,
        strlen(post_data)
    );


    esp_err_t err =
        esp_http_client_perform(client);


    if (err == ESP_OK)
    {
        int status_code =
            esp_http_client_get_status_code(client);


        ESP_LOGI(
            TAG,
            "HTTP POST Status = %d",
            status_code
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "HTTP POST failed: %s",
            esp_err_to_name(err)
        );
    }


    esp_http_client_cleanup(client);


    return err;
}