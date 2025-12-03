# Proyecto de Cámara ESP32-S3 con MQTT

Proyecto para prueba de funcionalidad de cámara XDKJ-OV3660 en ESP32-S3 para sistema de reconocimiento facial con DeepStack.

## Descripción

Este proyecto implementa un sistema de captura automática de imágenes usando un microcontrolador ESP32-S3 con cámara OV3660. Las imágenes se capturan cada 10 segundos y se envían por MQTT a un servidor DeepStack para reconocimiento facial.

## Funcionalidad Actual

El código principal (`main/main.c`) implementa las siguientes características:

### Módulos Principales

1. **Inicialización WiFi**
   - Conexión automática a la red WiFi configurada
   - Reconexión automática en caso de desconexión
   - Logs de estado de conexión

2. **Cliente MQTT**
   - Conexión al broker MQTT
   - Publicación de imágenes en el topic `iot/camera/photo`
   - Manejo de eventos de conexión/desconexión

3. **Control de Cámara OV3660**
   - Inicialización y configuración de la cámara
   - Captura de imágenes en formato JPEG (800x600 SVGA)
   - Ajustes automáticos de exposición, balance de blancos y ganancia

4. **Captura Automática**
   - Timer FreeRTOS para captura cada 10 segundos
   - Envío automático de fotos al broker MQTT

### Logs Implementados

El sistema proporciona logs detallados en cada etapa:
- ✓ Conexión exitosa a WiFi con dirección IP
- ✓ Conexión exitosa al broker MQTT
- ✓ Captura de foto exitosa con tamaño en bytes
- ✓ Envío exitoso de foto por MQTT

### Configuración

Edita las siguientes constantes en `main/main.c`:

```c
#define WIFI_SSID "iPhone de Cesar"
#define WIFI_PASS "DenGra9401"
#define MQTT_BROKER "mqtt://172.20.10.8:1883"
#define MQTT_TOPIC_PHOTO "iot/camera/photo"
#define PHOTO_INTERVAL_MS 10000  // 10 segundos
```

### Configuración de Pines

Los pines de la cámara están configurados para el módulo XDKJ-OV3660 con ESP32-S3. Si tu configuración es diferente, ajusta los defines `CAM_PIN_*` en `main/main.c`.

## Estructura del Proyecto

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml      Dependencias (esp32-camera, mqtt_client, cJSON)
│   ├── main.c                 Código principal modular con funciones
│   └── main_mqtt.c            Referencia de código WiFi/MQTT
└── README.md                  Este archivo
```

## Dependencias

El proyecto utiliza las siguientes dependencias (definidas en `main/idf_component.yml`):
- `espressif/esp32-camera`: Driver para cámara ESP32
- `espressif/mqtt_client`: Cliente MQTT
- `espressif/cjson`: Librería JSON

## Cómo Compilar y Flashear

1. Configurar el proyecto para ESP32-S3:
   ```bash
   idf.py set-target esp32s3
   ```

2. Configurar opciones (opcional):
   ```bash
   idf.py menuconfig
   ```

3. Compilar el proyecto:
   ```bash
   idf.py build
   ```

4. Flashear al ESP32-S3:
   ```bash
   idf.py -p COM_PORT flash monitor
   ```
   (Reemplaza `COM_PORT` con tu puerto serial, ej: COM3, COM4, etc.)

## Integración con DeepStack

Este proyecto está diseñado para integrarse con DeepStack:
1. Las fotos se envían al topic MQTT `iot/camera/photo`
2. DeepStack debe estar suscrito a este topic
3. DeepStack procesa la imagen y realiza reconocimiento facial
4. La respuesta de DeepStack debe enviarse a otro topic (implementación futura)

## Notas Técnicas

- **Formato de imagen**: JPEG
- **Resolución**: 800x600 (SVGA)
- **Calidad JPEG**: 12 (rango 0-63, menor valor = mejor calidad)
- **Intervalo de captura**: 10 segundos (configurable)
- **QoS MQTT**: 0 (fire and forget para optimizar rendimiento)

## Referencias

- [Documentación ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [ESP32 Camera Driver](https://github.com/espressif/esp32-camera)
- [DeepStack Face Recognition](https://docs.deepstack.cc/face-recognition/)
