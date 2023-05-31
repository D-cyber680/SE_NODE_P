#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/i2c.h"
#include "esp_spiffs.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

#include "string.h"
#include "stdlib.h"
#include <math.h>
#include <limits.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <string.h>
#include "packagedata.h"
#include "crc32.h"

#include "ssd1306.h"
#include "font8x8_basic.h"

static const char *TAG = "RESPONDER: ";

// Apuntador global
NODE_Package *pkg1;
NODE_Package pkga, pkgb;
SSD1306_t dev;
char msg_oled[20];

#define PI 3.14159265358979323846f
#define AVG_BUFF_SIZE 20
#define I2C_MASTER_SCL_IO 22        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 21        /*!< GPIO number used for I2C master data  */
#define I2C_FREQ_HZ 100000          /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000
#define SAMPLE_SIZE 2000
#define I2C_PORT I2C_NUM_0
#define ESP_CHANNEL 1

#define HEADER_NODE_A 0x1A
#define HEADER_NODE_B 0x1B
#define END_NODE_A 0x11
#define END_NODE_B 0x22
#define CMD_SHOWTEMP_A 0xEE
#define CMD_SHOWTEMP_B 0xEE

// static uint8_t peer_mac[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; //  AP (40:91:51:bf:f5:94)
//   STA  (40:22:d8:ee:6d:a5)
static esp_err_t init_wifi(void)
{
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    esp_wifi_init(&wifi_init_config);
    esp_netif_init();
    esp_event_loop_create_default();
    nvs_flash_init();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi init completed");
    return ESP_OK;
}

void recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    // char message[24];

    char *message = (char *)data;
    StringToPackage(pkg1, message);
    // Revisamos si llego correctamente
    if (checkCrc32(pkg1->crc32, message) == 1)
    {
        // Revisamos de quien viene
        ESP_LOGI(TAG, "ESP_NOW_RECEIVED_SUCCESS");

        if (pkg1->header == HEADER_NODE_A)
        {
            pkga.command = pkg1->command;
            pkga.length = pkg1->length;
            pkga.data[0] = pkg1->data[0];
            pkga.data[1] = pkg1->data[1];
            pkga.data[2] = pkg1->data[2];
            pkga.data[3] = pkg1->data[3];
        }
        else if (pkg1->header == HEADER_NODE_B)
        {
            pkgb.command = pkg1->command;
            pkgb.length = pkg1->length;
            pkgb.data[0] = pkg1->data[0];
            pkgb.data[1] = pkg1->data[1];
            pkgb.data[2] = pkg1->data[2];
            pkgb.data[3] = pkg1->data[3];
        }
        else
        {
            ESP_LOGW(TAG, "NODO FAIL");
        }
    }
    else
    {
        ESP_LOGW(TAG, "ESP_NOW_RECEIVED_FAIL");
    }

    if (gpio_get_level(GPIO_NUM_2) == 1) // MOSTRAR NODO A
    {
        gpio_set_level(GPIO_NUM_12, 0);
        snprintf(msg_oled, sizeof(msg_oled), "-> %d C", pkga.data[3]);
        ssd1306_clear_screen(&dev, false);
        ssd1306_display_text(&dev, 0, "      NODO A      ", 18, true);
        ssd1306_display_text(&dev, 2, "Temperatura:", 12, false);
        ssd1306_display_text(&dev, 4, msg_oled, strlen(msg_oled), false);
        ESP_LOGI(TAG, "NODO A");
    }
    else // Mostrar NODO B
    {
        gpio_set_level(GPIO_NUM_12, 1);
        snprintf(msg_oled, sizeof(msg_oled), "-> %d C", pkgb.data[3]);
        ssd1306_clear_screen(&dev, false);
        ssd1306_display_text(&dev, 0, "      NODO B      ", 18, true);
        ssd1306_display_text(&dev, 2, "Temperatura:", 12, false);
        ssd1306_display_text(&dev, 4, msg_oled, strlen(msg_oled), false);
        ESP_LOGI(TAG, "NODO B");
    }

    ESP_LOGI(TAG, "Data received" MACSTR "%s", MAC2STR(esp_now_info->src_addr), data);
}

void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGI(TAG, "ESP_NOW_SEND_SUCCESS");
    }
    else
    {
        ESP_LOGW(TAG, "ESP_NOW_SEND_FAIL");
    }
}

static esp_err_t init_esp_now(void)
{
    esp_now_init();
    esp_now_register_recv_cb(recv_cb);
    esp_now_register_send_cb(send_cb);

    ESP_LOGI(TAG, "esp now init completed");
    return ESP_OK;
}

void app_main(void)
{
    pkg1 = (NODE_Package *)malloc(sizeof(NODE_Package));
    pkga.header = HEADER_NODE_A;
    pkga.end = END_NODE_A;
    pkgb.header = HEADER_NODE_B;
    pkgb.end = END_NODE_B;

    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_2, GPIO_PULLUP_ONLY);

    gpio_reset_pin(GPIO_NUM_12);
    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "INTERFACE is i2c");
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64);
    ssd1306_contrast(&dev, 0xff);

    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(init_esp_now());
    // ESP_ERROR_CHECK(register_peer(peer_mac));
}