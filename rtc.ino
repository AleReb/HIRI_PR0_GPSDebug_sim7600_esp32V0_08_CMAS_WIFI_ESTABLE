// -------------------- Debug --------------------
void printRtcOnce() {
  if (!rtcOK) return;
  DateTime now = rtc.now();
  Serial.printf("[RTC] %04d/%02d/%02d %02d:%02d:%02d epoch=%lu\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second(),
                (unsigned long)now.unixtime());
}
static uint8_t monthFromAbbrev(const char* m) {
  if (!m) return 1;
  if (!strncmp(m, "Jan", 3)) return 1;
  if (!strncmp(m, "Feb", 3)) return 2;
  if (!strncmp(m, "Mar", 3)) return 3;
  if (!strncmp(m, "Apr", 3)) return 4;
  if (!strncmp(m, "May", 3)) return 5;
  if (!strncmp(m, "Jun", 3)) return 6;
  if (!strncmp(m, "Jul", 3)) return 7;
  if (!strncmp(m, "Aug", 3)) return 8;
  if (!strncmp(m, "Sep", 3)) return 9;
  if (!strncmp(m, "Oct", 3)) return 10;
  if (!strncmp(m, "Nov", 3)) return 11;
  if (!strncmp(m, "Dec", 3)) return 12;
  return 1;
}

// Epoch de tiempo de compilación (__DATE__ y __TIME__)
static uint32_t compileUnixTime() {
  const char* d = __DATE__;  // "Mmm dd yyyy"
  const char* t = __TIME__;  // "HH:MM:SS"

  char mon[4] = { 0 };
  int day = 1, year = 2000, hh = 0, mm = 0, ss = 0;

  if (sscanf(d, "%3s %d %d", mon, &day, &year) != 3) {
    return MIN_VALID_EPOCH;
  }
  (void)sscanf(t, "%d:%d:%d", &hh, &mm, &ss);

  struct tm tmv = {};
  tmv.tm_year = year - 1900;
  tmv.tm_mon = monthFromAbbrev(mon) - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hh;
  tmv.tm_min = mm;
  tmv.tm_sec = ss;
  return (uint32_t)mktime(&tmv);
}

// -------------------- Modem Time --------------------

static bool getModemEpoch(uint32_t& epoch_out) {
  String resp;
  if (!sendAtSync("+CCLK?", resp, 2000)) return false;

  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) return false;

  String ts = resp.substring(q1 + 1, q2);  // ej: "25/07/30,14:22:10+08"

  int s1 = ts.indexOf('/');
  int s2 = ts.indexOf('/', s1 + 1);
  int c = ts.indexOf(',');
  int k1 = ts.indexOf(':', c);
  int k2 = ts.indexOf(':', k1 + 1);
  if (s1 < 0 || s2 < 0 || c < 0 || k1 < 0 || k2 < 0) return false;

  int yy = ts.substring(0, s1).toInt() + 2000;
  int mo = ts.substring(s1 + 1, s2).toInt();
  int dd = ts.substring(s2 + 1, c).toInt();
  int hh = ts.substring(c + 1, k1).toInt();
  int mm = ts.substring(k1 + 1, k2).toInt();
  int ss = ts.substring(k2 + 1, k2 + 3).toInt();

  struct tm t = {};
  t.tm_year = yy - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = dd;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  time_t e = mktime(&t);
  if (e < MIN_VALID_EPOCH) return false;

  epoch_out = (uint32_t)e;
  return true;
}

// -------------------- Smart Sync --------------------
void syncRtcSmart() {
  if (!rtc.begin()) {
    Serial.println("[RTC] Not found");
    rtcOK = false;
    return;
  }
  rtcOK = true;

  // Cargar contador de syncs desde Preferences
  prefs.begin("rtc", false);
  rtcModemSyncCount = prefs.getUChar("syncCnt", 0);
  prefs.end();
  Serial.printf("[RTC] Modem sync count: %u/%u\n", rtcModemSyncCount, MAX_MODEM_SYNC_COUNT);

  // Obtener hora actual del RTC
  DateTime rtcNow = rtc.now();
  uint32_t rtcEpoch = rtcNow.unixtime();

  // Solo usar compile time si RTC perdió potencia o tiene hora inválida
  if (rtc.lostPower() || rtcEpoch < MIN_VALID_EPOCH) {
    uint32_t ctEpoch = compileUnixTime();
    rtc.adjust(DateTime(ctEpoch));
    Serial.println("[RTC] Initialized with compile time (lost power or invalid)");
    Serial.printf("[RTC] Compile time: %lu\n", (unsigned long)ctEpoch);
  } else {
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d (epoch=%lu)\n",
                  rtcNow.year(), rtcNow.month(), rtcNow.day(),
                  rtcNow.hour(), rtcNow.minute(), rtcNow.second(),
                  (unsigned long)rtcEpoch);
  }

  // Intentar sincronizar con modem (solo si no alcanzó límite)
  if (rtcModemSyncCount < MAX_MODEM_SYNC_COUNT) {
    syncRtcFromModem();
  } else {
    Serial.println("[RTC] Max modem sync count reached, skipping network sync");
    rtcNetSyncPending = false;
  }
}

// -------------------- Reset Modem Sync Counter --------------------
// Resetea el contador de sincronizaciones con el modem
// Útil si quieres forzar nuevas sincronizaciones después de alcanzar el límite
void resetModemSyncCounter() {
  rtcModemSyncCount = 0;
  prefs.begin("rtc", false);
  prefs.putUChar("syncCnt", 0);
  prefs.end();
  Serial.println("[RTC] Modem sync counter reset to 0");
}

// -------------------- Sync from Modem (llamable manualmente) --------------------
// Esta función sincroniza el RTC con la hora del modem celular
// Puede ser llamada manualmente o desde syncRtcSmart()
// Retorna true si la sincronización fue exitosa
bool syncRtcFromModem() {
  if (!rtcOK) {
    Serial.println("[RTC] Cannot sync: RTC not available");
    return false;
  }

  // Habilitar actualización automática de zona horaria
  (void)atRun("+CTZU=1", "OK", "ERROR", 1000);
  (void)atRun("+CTZR=1", "OK", "ERROR", 1000);

  uint32_t modemEpoch = 0;
  bool haveModem = getModemEpoch(modemEpoch);

  if (!haveModem) {
    Serial.println("[RTC] Modem time not ready; will retry in loop");
    rtcNetSyncPending = true;
    rtcNextProbeMs = millis() + RTC_PROBE_PERIOD_MS;
    return false;
  }

  // Comparar con RTC actual
  uint32_t rtcEpoch = rtc.now().unixtime();
  long diff = (long)modemEpoch - (long)rtcEpoch;

  Serial.printf("[RTC] rtc=%lu modem=%lu diff=%ld s\n",
                (unsigned long)rtcEpoch, (unsigned long)modemEpoch, diff);

  // Solo sincronizar si la diferencia supera el umbral
  if (abs(diff) > RTC_SYNC_THRESHOLD) {
    rtc.adjust(DateTime(modemEpoch));
    Serial.println("[RTC] Synchronized to modem clock");

    // Incrementar y guardar contador
    rtcModemSyncCount++;
    prefs.begin("rtc", false);
    prefs.putUChar("syncCnt", rtcModemSyncCount);
    prefs.end();
    Serial.printf("[RTC] Sync count updated: %u/%u\n", rtcModemSyncCount, MAX_MODEM_SYNC_COUNT);

    rtcNetSyncPending = false;
    return true;
  } else {
    Serial.println("[RTC] Within threshold; no sync needed");
    rtcNetSyncPending = false;
    return false;
  }
}
