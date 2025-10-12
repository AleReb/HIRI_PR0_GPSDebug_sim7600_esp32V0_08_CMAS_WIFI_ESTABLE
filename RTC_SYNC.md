# Sistema de sincronización RTC mejorado

## Problema resuelto
El RTC estaba adelantado ~3 horas (probablemente UTC vs hora local) y solo se actualizaba si estaba **atrasado** respecto a la hora de compilación.

## Solución implementada: Opción 1 + 3

### **Fase 1: Setup rápido (no bloqueante)**

En `syncRtcSmart()` dentro de `rtc.ino`:

1. **Carga contador de syncs desde Preferences**
   ```cpp
   prefs.begin("rtc", false);
   rtcModemSyncCount = prefs.getUChar("syncCnt", 0);
   prefs.end();
   ```

2. **Ventana de tolerancia ±1 hora**
   ```cpp
   long drift = (long)rtcEpoch - (long)ctEpoch;
   if (abs(drift) > 3600) {  // 3600s = 1 hora
     Serial.printf("[RTC] Drift detected: %ld s, adjusting to compile time\n", drift);
     rtc.adjust(DateTime(ctEpoch));
   }
   ```
   - Si RTC está adelantado o atrasado >1h respecto a compile time, ajusta
   - **No se actualiza en cada compilación** (solo si drift > 1h)

3. **Intento de sync con modem (si tiene red)**
   ```cpp
   if (rtcModemSyncCount < MAX_MODEM_SYNC_COUNT) {
     if (getModemEpoch(modemEpoch)) {
       // Sync con hora real de red celular
       rtc.adjust(DateTime(modemEpoch));
       rtcModemSyncCount++;
     }
   }
   ```
   - Solo intenta si contador < 3
   - No bloqueante: si modem no está listo, setea `rtcNetSyncPending = true`

---

### **Fase 2: Loop background (reintentos inteligentes)**

En `loop()` dentro del main `.ino`:

```cpp
if (rtcNetSyncPending && rtcModemSyncCount < MAX_MODEM_SYNC_COUNT
    && modem.isNetworkConnected()
    && millis() - lastModemSyncAttempt >= MODEM_SYNC_INTERVAL_MS) {

  // Intenta sync con modem
  if (getModemEpoch(modemEpoch)) {
    rtc.adjust(DateTime(modemEpoch));
    rtcModemSyncCount++;  // Incrementa contador
  }
}
```

**Condiciones para retry:**
- ✅ Modem tiene red conectada (`modem.isNetworkConnected()`)
- ✅ No alcanzó límite de 3 syncs exitosos
- ✅ Han pasado 10 minutos desde último intento (evita saturar modem)
- ✅ WiFi no está activo (evita conflictos)

---

## Parámetros configurables

### Variables globales (main .ino:106-110)
```cpp
static uint8_t rtcModemSyncCount = 0;           // Contador de syncs exitosos
static uint32_t lastModemSyncAttempt = 0;       // Timestamp último intento
const uint32_t MODEM_SYNC_INTERVAL_MS = 600000; // 10 minutos entre intentos
const uint8_t MAX_MODEM_SYNC_COUNT = 3;         // Máximo 3 syncs exitosos
```

### Persistencia (ESP32 Preferences)
```cpp
Preferences prefs;
prefs.begin("rtc", false);
prefs.putUChar("syncCnt", rtcModemSyncCount);  // Guarda contador
uint8_t count = prefs.getUChar("syncCnt", 0);  // Lee contador
prefs.end();
```

---

## Comportamiento esperado

### **Primera vez que arranca** (contador = 0)
1. Setup: Ajusta RTC si drift > 1h
2. Setup: Intenta sync con modem (si tiene red)
   - ✅ Exitoso → contador = 1, RTC ajustado a hora real
   - ❌ Falla → `rtcNetSyncPending = true`, reintenta en loop

### **Segundo arranque** (contador = 1)
1. Setup: Lee contador = 1 desde Preferences
2. Setup: Ajusta RTC si drift > 1h (respecto a compile time)
3. Setup: Intenta sync con modem
   - ✅ Exitoso → contador = 2

### **Después de 3 syncs exitosos** (contador = 3)
```
[RTC] Modem sync count: 3/3
[RTC] Max modem sync count reached, skipping network sync
```
- Ya no intenta sync con modem
- Solo ajusta si drift > 1h respecto a compile time

---

## Ventajas del sistema

✅ **No bloqueante**: Boot rápido, sync en background
✅ **Tolerante a adelantos**: Corrige ±1h (no solo atrasos)
✅ **Hora real de red**: Usa tiempo celular (más preciso que compile time)
✅ **Límite de intentos**: Solo 3 syncs (evita desgaste EEPROM)
✅ **Persistente**: Contador sobrevive a reinicios
✅ **Rate limiting**: 10 min entre intentos (no satura modem)

---

## Debug / Testing

### Ver contador actual
```cpp
prefs.begin("rtc", false);
uint8_t count = prefs.getUChar("syncCnt", 0);
Serial.printf("RTC sync count: %u\n", count);
prefs.end();
```

### Resetear contador (para testing)
```cpp
prefs.begin("rtc", false);
prefs.putUChar("syncCnt", 0);
prefs.end();
Serial.println("RTC sync count reset to 0");
```

### Mensajes Serial esperados

**Setup (primera vez):**
```
[RTC] Modem sync count: 0/3
[RTC] Drift detected: -10800 s, adjusting to compile time
[RTC] rtc=1735344000 modem=1735354800 diff=10800 s
[RTC] Synchronized to modem clock
[RTC] Sync count updated: 1/3
```

**Loop (retry):**
```
[RTC][retry] rtc=1735354800 modem=1735354820 diff=20 s
[RTC][retry] Within threshold; no sync
```

**Al alcanzar límite:**
```
[RTC] Modem sync count: 3/3
[RTC] Max modem sync count reached, skipping network sync
```

---

## Archivos modificados

1. **HIRI_PR0_GPSDebug_sim7600_esp32V0_08_CMAS.ino**
   - Línea 18: `#include <Preferences.h>`
   - Línea 96: `Preferences prefs;`
   - Líneas 106-110: Variables sync modem
   - Líneas 667-703: Lógica retry en loop

2. **rtc.ino**
   - Líneas 90-162: Función `syncRtcSmart()` mejorada
   - Líneas 52-88: Función `getModemEpoch()` (ya existía)

---

## Consideraciones

- **Zona horaria**: `getModemEpoch()` obtiene hora UTC desde el modem. Si necesitas hora local, agregar offset en `mktime()`.
- **Batería RTC**: Si batería CR2032 del DS3231 muere, `rtc.lostPower()` detecta y resetea a compile time.
- **Compile time viejo**: Si subes código compilado hace días, primera vez ajustará a tiempo viejo (pero modem sync corrige inmediatamente).

---

## Reset manual del contador (si es necesario)

Si quieres forzar nuevos syncs después de alcanzar el límite:

```cpp
// En setup() o mediante comando serial:
prefs.begin("rtc", false);
prefs.putUChar("syncCnt", 0);
prefs.end();
Serial.println("[RTC] Sync counter RESET");
```
