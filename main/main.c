#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

// --- CONFIGURACIÓN ---

// #define WIFI_SSID "iPhone de Cesar"
// #define WIFI_PASS "DenGra9401"

// #define MQTT_BROKER "mqtt://172.20.10.8:1883"
// #define MQTT_TOPIC_PHOTO "iot/camera/photo"

// #define WIFI_SSID "DVD Wifi"
// #define WIFI_PASS "David1234"

#define WIFI_SSID "iPhone de Cesar"
#define WIFI_PASS "DenGra9401"
#define MQTT_BROKER "mqtt://172.20.10.8:1883"
#define MQTT_TOPIC_PHOTO "iot/telemetry"

#define PHOTO_INTERVAL_MS 10000  // 10 segundos
    
// Tag para logs
static const char *TAG = "CAMERA_APP";

// Variables globales
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool wifi_connected = false;
static bool mqtt_connected = false;
static TimerHandle_t photo_timer = NULL;
static TaskHandle_t photo_task_handle = NULL;

// --- CONFIGURACIÓN DE PINES PARA ESP32-S3 CON XDKJ-OV3660 ---
// Nota: Ajusta estos pines según tu módulo específico
#define CAM_PIN_PWDN    -1  // Power down no conectado
#define CAM_PIN_RESET   -1  // Reset no conectado
#define CAM_PIN_XCLK    15  // XCLK
#define CAM_PIN_SIOD    4   // SDA
#define CAM_PIN_SIOC    5   // SCL
#define CAM_PIN_D7      16  // Y9
#define CAM_PIN_D6      17  // Y8
#define CAM_PIN_D5      18  // Y7
#define CAM_PIN_D4      12  // Y6
#define CAM_PIN_D3      10  // Y5
#define CAM_PIN_D2      8   // Y4
#define CAM_PIN_D1      9   // Y3
#define CAM_PIN_D0      11  // Y2
#define CAM_PIN_VSYNC   6   // VSYNC
#define CAM_PIN_HREF    7   // HREF
#define CAM_PIN_PCLK    13  // PCLK

// --- PROTOTIPOS DE FUNCIONES ---
static void wifi_init(void);
static void mqtt_init(void);
static void camera_init(void);
static void capture_and_send_photo(void);
static void photo_task(void *pvParameters);
static void photo_timer_callback(TimerHandle_t xTimer);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**
 * @brief Inicializa WiFi en modo estación
 */
static void wifi_init(void)
{
    ESP_LOGI(TAG, "Inicializando WiFi...");
    
    // Crear interfaz WiFi STA
    esp_netif_create_default_wifi_sta();
    
    // Configuración WiFi por defecto
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Registrar manejadores de eventos
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    
    // Configurar credenciales WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    
    // Aplicar configuración e iniciar WiFi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief Manejador de eventos WiFi
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Conectando a WiFi...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_connected = false;
        esp_wifi_connect();
        ESP_LOGW(TAG, "WiFi desconectado. Reintentando...");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "✓ WiFi conectado exitosamente! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

/**
 * @brief Inicializa cliente MQTT
 */
static void mqtt_init(void)
{
    ESP_LOGI(TAG, "Inicializando MQTT...");
    
    // Configuración del cliente MQTT
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
        .buffer.size = 131072,  // Buffer de 128KB para mensajes grandes
        .buffer.out_size = 8192, // Buffer de salida de 8KB
        .network.timeout_ms = 30000, // Timeout de 30 segundos
    };
    
    // Crear e iniciar cliente MQTT
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/**
 * @brief Manejador de eventos MQTT
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event_id)
    {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT: Intentando conectar al broker...");
            break;
            
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✓ MQTT conectado exitosamente al broker");
            mqtt_connected = true;
            
            // Crear tarea para captura de fotos (solo una vez)
            if (photo_task_handle == NULL)
            {
                xTaskCreate(photo_task, 
                           "photo_task", 
                           4096,  // Stack size: 4KB para los logs
                           NULL, 
                           5,     // Prioridad
                           &photo_task_handle);
                ESP_LOGI(TAG, "Tarea de captura creada");
            }
            
            // Iniciar timer para captura automática de fotos
            if (photo_timer == NULL)
            {
                photo_timer = xTimerCreate("PhotoTimer", 
                                          pdMS_TO_TICKS(PHOTO_INTERVAL_MS),
                                          pdTRUE,  // Auto-reload
                                          NULL,
                                          photo_timer_callback);
                xTimerStart(photo_timer, 0);
                ESP_LOGI(TAG, "Timer de captura iniciado (cada 10 segundos)");
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "✗ MQTT desconectado del broker");
            mqtt_connected = false;
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "✗✗✗ ERROR EN MQTT ✗✗✗");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                ESP_LOGE(TAG, "Error de transporte TCP");
                ESP_LOGE(TAG, "Código errno: %d", event->error_handle->esp_transport_sock_errno);
                
                // Decodificar errores comunes
                switch (event->error_handle->esp_transport_sock_errno)
                {
                    case 113: // EHOSTUNREACH
                        ESP_LOGE(TAG, "▶ CAUSA: Host inalcanzable (EHOSTUNREACH)");
                        ESP_LOGE(TAG, "▶ SOLUCIÓN 1: Verifica que el broker esté corriendo en %s", MQTT_BROKER);
                        ESP_LOGE(TAG, "▶ SOLUCIÓN 2: Verifica que ambos dispositivos estén en la misma red");
                        ESP_LOGE(TAG, "▶ SOLUCIÓN 3: Verifica que no haya firewall bloqueando puerto 1883");
                        break;
                    case 111: // ECONNREFUSED
                        ESP_LOGE(TAG, "▶ CAUSA: Conexión rechazada (ECONNREFUSED)");
                        ESP_LOGE(TAG, "▶ SOLUCIÓN: El broker no está escuchando en el puerto 1883");
                        break;
                    case 110: // ETIMEDOUT
                        ESP_LOGE(TAG, "▶ CAUSA: Timeout de conexión");
                        ESP_LOGE(TAG, "▶ SOLUCIÓN: Verifica la IP del broker y conectividad de red");
                        break;
                    default:
                        ESP_LOGE(TAG, "▶ Error desconocido: %d", event->error_handle->esp_transport_sock_errno);
                        break;
                }
            }
            else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            {
                ESP_LOGE(TAG, "Conexión MQTT rechazada por el broker");
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "✓ Foto enviada exitosamente por MQTT");
            break;
        
        default:
            ESP_LOGD(TAG, "MQTT evento: %d", event_id);
            break;
    }
}

/**
 * @brief Inicializa la cámara OV3660
 */
static void camera_init(void)
{
    ESP_LOGI(TAG, "Inicializando cámara OV3660...");
    ESP_LOGI(TAG, "Memoria libre antes de init: %lu bytes", esp_get_free_heap_size());
    
    // Verificar si PSRAM está disponible
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0)
    {
        ESP_LOGI(TAG, "PSRAM detectado: %zu bytes", psram_size);
    }
    else
    {
        ESP_LOGW(TAG, "⚠️ PSRAM NO DISPONIBLE - Usando resolución reducida");
    }
    
    // Configuración de la cámara
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        
        .xclk_freq_hz = 20000000,           // Frecuencia XCLK: 20MHz
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        
        .pixel_format = PIXFORMAT_JPEG,      // Formato JPEG para envío eficiente
        .frame_size = FRAMESIZE_QVGA,        // 320x240 siempre (tamaño pequeño)
        .jpeg_quality = 30,                  // Calidad JPEG reducida para menor tamaño (0-63, menor = mejor)
        .fb_count = (psram_size > 0) ? 2 : 1,  // 2 buffers con PSRAM, 1 sin PSRAM
        .fb_location = (psram_size > 0) ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,  // Automático según disponibilidad
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY  // Modo de captura
    };
    
    // Inicializar cámara
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "✗ Error al inicializar cámara: 0x%x", err);
        
        // Diagnóstico detallado del error
        switch (err)
        {
            case ESP_ERR_NO_MEM:
                ESP_LOGE(TAG, "Causa: Memoria insuficiente");
                ESP_LOGE(TAG, "Solución: Verifica que PSRAM esté habilitado en menuconfig");
                break;
            case ESP_ERR_NOT_FOUND:
                ESP_LOGE(TAG, "Causa: Cámara no detectada en el bus I2C");
                ESP_LOGE(TAG, "Solución: Verifica conexiones físicas y pines SDA/SCL");
                break;
            case ESP_ERR_NOT_SUPPORTED:
                ESP_LOGE(TAG, "Causa: Configuración no soportada");
                break;
            case ESP_ERR_TIMEOUT:
                ESP_LOGE(TAG, "Causa: Timeout al inicializar");
                ESP_LOGE(TAG, "Solución: Verifica alimentación de la cámara");
                break;
            default:
                ESP_LOGE(TAG, "Causa: Error desconocido");
                break;
        }
        return;
    }
    
    ESP_LOGI(TAG, "✓ Cámara inicializada exitosamente");
    ESP_LOGI(TAG, "Memoria libre después de init: %lu bytes", esp_get_free_heap_size());
    
    // Obtener sensor para ajustes adicionales
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL)
    {
        ESP_LOGI(TAG, "ID del sensor detectado: 0x%x", s->id.PID);
        
        // Ajustes opcionales del sensor
        s->set_brightness(s, 0);     // Brillo: -2 a 2
        s->set_contrast(s, 0);       // Contraste: -2 a 2
        s->set_saturation(s, 0);     // Saturación: -2 a 2
        s->set_whitebal(s, 1);       // Balance de blancos automático
        s->set_awb_gain(s, 1);       // Ganancia AWB automática
        s->set_exposure_ctrl(s, 1);  // Control de exposición automático
        s->set_aec2(s, 0);           // AEC DSP
        s->set_gain_ctrl(s, 1);      // Control de ganancia automático
        s->set_agc_gain(s, 0);       // Ganancia AGC
        s->set_bpc(s, 0);            // Corrección de píxeles negros
        s->set_wpc(s, 1);            // Corrección de píxeles blancos
        s->set_raw_gma(s, 1);        // Gamma raw
        s->set_lenc(s, 1);           // Corrección de lentes
        s->set_hmirror(s, 0);        // Espejo horizontal
        s->set_vflip(s, 0);          // Voltear vertical
        
        ESP_LOGI(TAG, "Configuración del sensor aplicada");
    }
    else
    {
        ESP_LOGE(TAG, "No se pudo obtener el sensor de la cámara");
    }
}

/**
 * @brief Captura una foto y la envía por MQTT
 */
static void capture_and_send_photo(void)
{
    // Verificar que MQTT esté conectado
    if (!mqtt_connected)
    {
        ESP_LOGW(TAG, "MQTT no conectado. Esperando conexión...");
        return;
    }
    
    ESP_LOGI(TAG, "Capturando foto...");
    
    // Capturar imagen
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "✗ Error al capturar foto (memoria libre: %lu bytes)", esp_get_free_heap_size());
        
        // Intentar reinicializar la cámara
        ESP_LOGW(TAG, "Reinicializando cámara...");
        esp_camera_deinit();
        vTaskDelay(pdMS_TO_TICKS(1000));
        camera_init();
        return;
    }
    
    ESP_LOGI(TAG, "✓ Foto capturada exitosamente (tamaño: %zu bytes)", fb->len);
    
    // Verificar tamaño de la imagen
    if (fb->len > 30000) // Si es mayor a 30KB, es demasiado grande
    {
        ESP_LOGE(TAG, "Imagen demasiado grande (%zu bytes), no se puede enviar", fb->len);
        esp_camera_fb_return(fb);
        return;
    }
    
    // Calcular tamaño necesario para base64 (4/3 del tamaño original + padding)
    size_t base64_len = 0;
    mbedtls_base64_encode(NULL, 0, &base64_len, fb->buf, fb->len);
    
    // Asignar memoria para base64
    char *base64_buf = (char *)malloc(base64_len + 1);
    if (!base64_buf)
    {
        ESP_LOGE(TAG, "✗ Error al asignar memoria para base64");
        esp_camera_fb_return(fb);
        return;
    }
    
    // Codificar imagen a base64
    size_t output_len = 0;
    int ret = mbedtls_base64_encode((unsigned char *)base64_buf, base64_len, &output_len, fb->buf, fb->len);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "✗ Error al codificar a base64: %d", ret);
        free(base64_buf);
        esp_camera_fb_return(fb);
        return;
    }
    base64_buf[output_len] = '\0';
    
    // Liberar buffer de la cámara (ya no lo necesitamos)
    esp_camera_fb_return(fb);
    
    // Crear JSON con la imagen en base64
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "✗ Error al crear objeto JSON");
        free(base64_buf);
        return;
    }
    
    cJSON_AddStringToObject(root, "device_id", "access_control_camera");
    cJSON_AddStringToObject(root, "access_method", "camera");
    cJSON_AddStringToObject(root, "img", base64_buf);
    
    // Convertir JSON a string
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str)
    {
        ESP_LOGE(TAG, "✗ Error al serializar JSON");
        cJSON_Delete(root);
        free(base64_buf);
        return;
    }
    
    ESP_LOGI(TAG, "JSON creado (tamaño: %d bytes)", strlen(json_str));
    
    // Enviar JSON por MQTT
    ESP_LOGI(TAG, "Enviando foto en JSON al topic: %s", MQTT_TOPIC_PHOTO);
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, 
                                         MQTT_TOPIC_PHOTO, 
                                         json_str, 
                                         0,     // Longitud 0 = calcular automáticamente
                                         0,     // QoS 0 para entrega sin garantía
                                         0);    // No retain
    
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "✗ Error al enviar foto por MQTT");
    }
    else
    {
        ESP_LOGI(TAG, "Foto en cola para envío (msg_id: %d)", msg_id);
    }
    
    // Liberar memoria
    cJSON_Delete(root);
    free(json_str);
    free(base64_buf);
}

/**
 * @brief Tarea dedicada para capturar y enviar fotos
 */
static void photo_task(void *pvParameters)
{
    while (1)
    {
        // Esperar notificación del timer
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // Ejecutar captura y envío
        capture_and_send_photo();
    }
}

/**
 * @brief Callback del timer para captura automática
 */
static void photo_timer_callback(TimerHandle_t xTimer)
{
    // En lugar de ejecutar directamente, notificar a la tarea
    if (photo_task_handle != NULL)
    {
        xTaskNotifyGive(photo_task_handle);
    }
}

/**
 * @brief Función principal
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Iniciando aplicación de cámara ESP32-S3 ===");
    
    // 1. Inicializar NVS (necesario para WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS inicializado");
    
    // 2. Inicializar red
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "✓ Network stack inicializado");
    
    // 3. Inicializar cámara
    camera_init();
    
    // 4. Inicializar WiFi
    wifi_init();
    
    // 5. Inicializar MQTT (se conectará cuando WiFi esté listo)
    mqtt_init();
    
    ESP_LOGI(TAG, "Sistema inicializado. Esperando conexiones...");
    ESP_LOGI(TAG, "La captura automática iniciará después de conectar MQTT");
}