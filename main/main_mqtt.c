#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_chip_info.h" // Required for detecting ESP32 vs S3
#include "esp_timer.h"     // Required for esp_timer_get_time
#include "mqtt_client.h"
#include "cJSON.h"

// --- CONFIGURATION ---
// Note: Verify your router supports WPA2 legacy mode if using WiFi 6
#define WIFI_SSID "iPhone de Cesar"
#define WIFI_PASS "DenGra9401"
#define MQTT_BROKER "mqtt://172.20.10.8:1883"
#define MQTT_TOPIC "iot/telemetry"

static const char *TAG = "MQTT_APP";

// Helper to get chip model name
const char *get_chip_model()
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    switch (chip_info.model)
    {
    case CHIP_ESP32:
        return "ESP32_Classic";
    case CHIP_ESP32S3:
        return "ESP32_S3";
    case CHIP_ESP32C3:
        return "ESP32_C3";
    default:
        return "ESP_Unknown";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Retrying WiFi...");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED)
    {
        ESP_LOGI(TAG, "MQTT Connected. Sending data...");

        // 1. Create JSON Payload
        cJSON *root = cJSON_CreateObject();

        // Dynamic Device Name based on Chip
        cJSON_AddStringToObject(root, "device", get_chip_model());
        cJSON_AddNumberToObject(root, "uptime_sec", esp_timer_get_time() / 1000000);
        cJSON_AddStringToObject(root, "ssid", WIFI_SSID); // Optional: Send connected SSID

        char *payload = cJSON_PrintUnformatted(root); // Unformatted saves data/bandwidth

        // 2. Publish
        esp_mqtt_client_publish(event->client, MQTT_TOPIC, payload, 0, 1, 0);
        ESP_LOGI(TAG, "Published: %s", payload);

        // Cleanup
        cJSON_Delete(root);
        free(payload);
    }
}

void app_main(void)
{
    // 1. NVS Flash Init (Robust version)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_netif_init();
    esp_event_loop_create_default();

    // 2. WiFi Setup
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false} // Helps with WiFi 6 routers
        },
    };

    // Safer string copying
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // 3. MQTT Setup
    esp_mqtt_client_config_t mqtt_cfg = {.broker.address.uri = MQTT_BROKER};
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}