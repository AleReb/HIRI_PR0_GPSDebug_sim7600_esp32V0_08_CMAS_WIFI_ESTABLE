# HIRI PRO - Air Quality Monitoring Station

Sistema de monitoreo de calidad del aire con GPS, sensor de partículas PMS5003, y transmisión de datos vía red celular GSM/LTE.

## Características

- **Sensor de partículas:** PMS5003 (PM1.0, PM2.5, PM10)
- **GPS/GNSS:** SIM7600 con soporte XTRA (A-GPS)
- **Conectividad:** GSM/LTE con reconexión automática robusta
- **Almacenamiento:** Tarjeta SD con registro de datos y errores
- **Sensores adicionales:** DS3231 RTC, SHT31 (temperatura/humedad), batería LiPo
- **Display:** OLED SSD1306 128x64
- **Indicador LED:** NeoPixel RGB con gradiente según nivel PM2.5
- **WiFi:** Servidor web para descarga de datos desde SD

## Hardware

### Componentes principales
- **MCU:** ESP32 DevKit
- **Módulo celular:** SIM7600G/H
- **Sensor PM:** PMS5003
- **RTC:** DS3231
- **Sensor T/H:** SHT31
- **Display:** OLED SSD1306 I2C
- **LED:** NeoPixel WS2812B
- **Almacenamiento:** MicroSD card module

### Pines
```cpp
// Módulo SIM7600
#define MODEM_TX      27
#define MODEM_RX      26
#define MODEM_PWRKEY  4
#define MODEM_DTR     32
#define MODEM_FLIGHT  25

// PMS5003
#define pms_TX        5
#define pms_RX        18

// Periféricos
#define BAT_PIN       35
#define NEOPIX_PIN    12
#define BUTTON_PIN_1  19  // START/STOP streaming
#define BUTTON_PIN_2  23  // WiFi mode toggle

// I2C (OLED + RTC + SHT31)
// Default ESP32 I2C: SDA=21, SCL=22

// SD Card (HSPI)
#define SD_SCLK       14
#define SD_MISO       2
#define SD_MOSI       15
#define SD_CS         13
```

## Software

### Dependencias
```ini
[dependencies]
Adafruit NeoPixel
Adafruit SHT31
RTClib
TinyGSMClient
U8g2lib
SoftwareSerial
```

### Estructura del proyecto
```
HIRI_PR0_GPSDebug_sim7600_esp32V0_08_CMAS_WIFI_ESTABLE/
├── HIRI_PR0_GPSDebug_sim7600_esp32V0_08_CMAS_WIFI_ESTABLE.ino  # Código principal
├── animacion.ino          # Animación de inicio OLED
├── gps.ino                # Funciones GNSS y parseo NMEA
├── helpers.ino            # Utilidades generales
├── http.ino               # Cliente HTTP y reconexión de red
├── pms.ino                # Lectura del sensor PMS5003
├── rtc.ino                # Sincronización RTC con red
├── SD.ino                 # Funciones de almacenamiento en SD
├── ux.ino                 # Interfaz de usuario (OLED/LED)
├── wifi.ino               # Servidor web para descarga de datos
└── README.md              # Este archivo
```

### Ciclo de operación

1. **Lectura de sensores (cada 3s):**
   - PMS5003: PM1.0, PM2.5, PM10, temperatura, humedad
   - SHT31: Temperatura y humedad externas
   - GPS: Posición, velocidad, satélites, HDOP
   - Sistema: Batería, señal celular (CSQ)

2. **Almacenamiento local (siempre):**
   - Datos guardados en SD en formato CSV
   - Archivo diario: `/HP{ID}_{DD_MM_YYYY}.csv`
   - Log de errores: `/errors.csv`
   - Transmisiones fallidas: `/failed_tx.csv`

3. **Transmisión HTTP (cada 3s si hay red):**
   - Endpoint: `http://api-sensores.cmasccp.cl/insertarMedicion`
   - Timeout: 15 segundos
   - Reintentos automáticos con backoff en caso de fallo de red

## Mejoras v0.3.9.2 (Branch feature/robust-http-transmission)

### Sistema de transmisión HTTP robusto

#### Timeout HTTP ajustado
- **Antes:** 2.5s (muy corto para redes lentas)
- **Ahora:** 15s (permite ~5 lecturas de sensores antes de timeout)
- **Configurable:** Variable `MAX_HTTP_WAIT_MS` en `http.ino:88`

#### Reconexión PDP con backoff inteligente
Evita bloqueos cuando el dispositivo viaja entre redes celulares o pierde señal:

- **Timeout por intento:** 30s con alimentación de watchdog cada 1s
- **Backoff después de N fallos:** 15s de espera tras 5 fallos consecutivos
- **Reset automático:** Contador de fallos se resetea al primer éxito

**Variables configurables** (`http.ino:2-7`):
```cpp
MAX_PDP_FAILS_BEFORE_BACKOFF = 5    // Fallos antes de activar backoff
PDP_BACKOFF_MS = 15000               // Tiempo de espera (15s)
PDP_RECONNECT_TIMEOUT_MS = 30000     // Timeout por intento (30s)
```

#### CSV de transmisiones fallidas
- **Archivo:** `/failed_tx.csv`
- **Formato:** `timestamp,error_type,url`
- **Propósito:** Debug de problemas de red sin perder registro de intentos
- **Nota:** NO se reintenta automáticamente para evitar desfase de datos

#### Garantía de almacenamiento local
- `saveCSVData()` se ejecuta **siempre**, independiente del resultado de HTTP
- **Prioridad:** Leer sensores > Guardar SD > Transmitir HTTP
- Los datos locales están garantizados incluso si falla la transmisión

#### Contador de guardados SD vs HTTP exitosos
Display OLED muestra el formato `sdSaves/httpSuccess`:
- **sdSaves:** Total de guardados exitosos en tarjeta SD
- **httpSuccess:** Total de transmisiones HTTP exitosas al servidor
- **Ejemplo:** `432/415` = 432 datos guardados en SD, 415 subidos exitosamente
- **Persistencia:** Ambos contadores se guardan en flash y sobreviven reinicios por watchdog

### Beneficios
✅ Reduce bloqueos al viajar entre redes celulares
✅ Evita resets por watchdog durante reconexiones largas
✅ Permite debug de problemas de red sin perder datos locales
✅ Visibilidad en tiempo real del ratio guardado SD / éxito HTTP
✅ Todas las constantes son ajustables para pruebas de campo

## Uso

### Botones
- **BTN1 (GPIO 19):** START/STOP streaming y logging a SD
- **BTN2 (GPIO 23):** Activar/desactivar modo WiFi (descarga de datos)

### Modo WiFi
Cuando se activa el modo WiFi:
1. El dispositivo crea un AP con SSID: `HIRIPRO_{ID}`
2. Contraseña: `12345678`
3. IP del servidor: `192.168.4.1`
4. Abrir en navegador: `http://192.168.4.1`
5. Permite descargar archivos CSV de la SD

### Indicador LED (NeoPixel)
Color según nivel de PM2.5:
- **Verde:** < 15 µg/m³ (bueno)
- **Amarillo:** 15-25 µg/m³ (moderado)
- **Naranja:** 25-50 µg/m³ (insalubre para grupos sensibles)
- **Rojo:** > 50 µg/m³ (insalubre)
- **Apagado:** Durante transmisión HTTP

## Configuración

### APN
Editar en archivo principal (línea 164-166):
```cpp
const char apn[] = "gigsky-02";
const char gprsUser[] = "";
const char gprsPass[] = "";
```

### ID del dispositivo
Editar en archivo principal (línea 256):
```cpp
const char* DEVICE_ID_STR = "03";  // ID único del dispositivo
```

### Endpoint API
Editar en archivo principal (línea 243-251):
```cpp
const char* API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
const char* IDS_SENSORES = "415,415,415,...";
const char* IDS_VARIABLES = "3,6,7,8,9,...";
```

## Compilación

### VSCode + Arduino Maker Workshop (recomendado)
1. Abrir proyecto en VSCode
2. Instalar extensión "Arduino Maker Workshop"
3. Seleccionar perfil: `hiripro`
4. Comando: `Arduino: Verify` (Ctrl+Alt+R)

### Arduino IDE
1. Instalar librerías requeridas
2. Seleccionar placa: **ESP32 Dev Module**
3. Configuración:
   - Upload Speed: 115200
   - Flash Frequency: 80MHz
   - Partition Scheme: Default 4MB
4. Verificar/Subir

## Desarrollo

### Gitflow
```bash
# Branch principal
git checkout main

# Crear feature
git checkout -b feature/nueva-funcionalidad

# Hacer cambios y commit
git add .
git commit -m "feat: descripción"

# Subir branch
git push -u origin feature/nueva-funcionalidad

# Merge después de pruebas
git checkout main
git merge feature/nueva-funcionalidad
git push
```

### Branch actual
- **main:** Versión estable v0.3.9
- **feature/robust-http-transmission:** Mejoras de robustez HTTP + contador SD (v0.3.9.2)

## Troubleshooting

### El dispositivo se resetea constantemente
- Verificar alimentación (batería > 3.6V)
- Revisar `/errors.csv` para identificar causa
- Ajustar `WDT_TIMEOUT` si es necesario (línea 181)

### No transmite datos
- Verificar señal celular (CSQ > 10)
- Revisar APN de operador
- Consultar `/failed_tx.csv` para ver intentos fallidos
- Ajustar `MAX_HTTP_WAIT_MS` si red es muy lenta

### SD no guarda datos
- Verificar tarjeta SD formateada en FAT32
- Verificar conexiones SPI
- Revisar serial monitor para errores "[SD]"

### GPS no obtiene fix
- Verificar antena GPS conectada
- Esperar TTFF (Time To First Fix): 30-120s en frío
- Revisar si XTRA está activo: `xtraLastOk` en serial

## Autor

**Alejandro Rebolledo**

## Licencia

Creative Commons 4.0

---

**Versión:** 0.3.9.2 (feature/robust-http-transmission)
**Última actualización:** Octubre 2025
