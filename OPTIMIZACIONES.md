# Optimizaciones implementadas para estabilidad HTTP/GPS

## Problema identificado
Dispositivo **sin SHT31** se queda pegado durante transmisión HTTP cada 3s, mientras dispositivo **con SHT31** es estable.

**Hipótesis principal:** La lectura I2C del SHT31 (~15ms) crea "breathing room" que permite al modem procesar correctamente. Sin este delay, el loop principal sobrecarga el UART.

---

## Cambios implementados

### 1. Buffer UART aumentado (HIRI_PR0_GPSDebug...ino:23)
```cpp
#define TINY_GSM_RX_BUFFER 4096  // Antes: 2048
```
**Razón:** Evitar overflow de buffer cuando el modem recibe datos rápido.

---

### 2. Rate limiting de atTick() (HIRI_PR0_GPSDebug...ino:709-715)
```cpp
static uint32_t lastAtTick = 0;
if (millis() - lastAtTick >= 50) {  // Max 20 Hz (cada 50ms)
  lastAtTick = millis();
  bool d, o;
  (void)atTick(d, o);
}
```
**Razón:** Dispositivo sin SHT31 llamaba atTick() continuamente (miles de veces/seg), sobrecargando el modem. Ahora limitado a 20 Hz.

---

### 3. Intervalos independientes HTTP vs SD (HIRI_PR0_GPSDebug...ino:162-166)
```cpp
const uint32_t STREAM_PERIOD_MS = 3000;   // HTTP cada 3s (crítico)
const uint32_t SD_SAVE_PERIOD_MS = 2000;  // SD cada 2s (alineado con GPS 1Hz)
uint32_t lastSdSave = 0;
```
**Razón:** Desacoplar guardado SD de transmisión HTTP. GPS actualiza a 1Hz, SD guarda cada 2s (2 posiciones), HTTP transmite cada 3s.

---

### 4. Watchdog reset dentro de HTTP (http.ino:87)
```cpp
while (!actionDone) {
  esp_task_wdt_reset();  // ← CRÍTICO: evita timeout watchdog (60s)
  if (atTick(actionDone, actionOk)) break;
  // ...
}
```
**Razón:** Si HTTP tarda >60s (señal mala + batería baja), watchdog reiniciaba dispositivo. Ahora resetea watchdog en cada iteración.

---

### 5. Timeout HTTP adaptativo (http.ino:84)
```cpp
// Antes: const uint32_t MAX_HTTP_WAIT_MS = 2500;
const uint32_t MAX_HTTP_WAIT_MS = (batV < 3.6) ? 10000 : 2500;
```
**Razón:** Con batería baja (<3.6V), modem responde más lento. Timeout de 10s permite completar transmisión sin abortar prematuramente.

---

### 6. Logging mejorado con contexto (http.ino:93-95)
```cpp
uint32_t elapsed = millis() - startTime;
Serial.printf("[HTTP][TIMEOUT] After %lu ms (bat=%.2fV)\n", elapsed, batV);
logError("HTTP_TIMEOUT", "HTTPACTION",
         "Timeout=" + String(elapsed) + "ms bat=" + String(batV, 2) + "V");
```
**Razón:** Captura voltaje de batería y tiempo real de timeout en `/errors.csv` para análisis de correlación en campo.

---

### 7. Header de error log expandido (SD.ino:29)
```cpp
f.println("timestamp,errorType,errorCode,rawResponse,operator,technology,signalQuality,registrationStatus,batteryV,uptime_s");
```
**Razón:** Permite correlacionar errores con: operador celular, tecnología (2G/3G/LTE), CSQ, batería, uptime.

---

## Próximos pasos recomendados

### A. Completar lógica de guardado SD independiente
Actualmente `lastSdSave` está declarado pero no usado en el loop principal. Agregar en loop:

```cpp
if (millis() - lastSdSave >= SD_SAVE_PERIOD_MS) {
  lastSdSave = millis();
  saveCSVData();
}
```

### B. Pre-transmission diagnostics (opcional)
Antes de cada HTTP, loggear estado:
```cpp
Serial.printf("[HTTP][PRE] bat=%.2fV csq=%d gprs=%d\n",
              batV, csq, modem.isGprsConnected());
```

### C. Validación en campo
Monitorear `/errors.csv` para patrones:
- ¿Timeouts correlacionan con `batV < 3.6`?
- ¿Errores aumentan con CSQ bajo?
- ¿Problemas en horarios específicos (congestión red)?

---

## Archivos modificados
- `HIRI_PR0_GPSDebug_sim7600_esp32V0_08_CMAS.ino` (líneas 23, 162-166, 709-715)
- `http.ino` (líneas 84, 87, 93-95)
- `SD.ino` (línea 29)

---

## Notas críticas
✅ Mantiene transmisión HTTP cada 3s (requisito crítico)
✅ Buffer 2x más grande para UART
✅ Rate limiting reduce presión sobre modem
✅ Timeout adaptativo según batería
✅ Watchdog no reinicia durante HTTP lento
✅ Logging detallado para debugging en campo
