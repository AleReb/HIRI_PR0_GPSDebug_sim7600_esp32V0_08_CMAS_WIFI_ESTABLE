// -------------------- GPS parsing helpers --------------------
void splitSentence(const String& sentence, char delimiter, String fields[], int expectedFields) {
  int fieldIndex = 0, start = 0, end = sentence.indexOf(delimiter);
  while (end != -1 && fieldIndex < expectedFields) {
    fields[fieldIndex++] = sentence.substring(start, end);
    start = end + 1;
    end = sentence.indexOf(delimiter, start);
  }
  if (fieldIndex < expectedFields) fields[fieldIndex++] = sentence.substring(start);
}

float convertToDecimal(String coord, String dir) {
  int degDigits = (dir == "N" || dir == "S") ? 2 : 3;
  if ((int)coord.length() < degDigits) return NAN;
  int degrees = coord.substring(0, degDigits).toInt();
  float minutes = coord.substring(degDigits).toFloat();
  float dec = degrees + minutes / 60.0f;
  if (dir == "S" || dir == "W") dec = -dec;
  return dec;
}

// -------------------- GNSS bring-up + watchdog --------------------
static inline void resetGnssFlagsAfterStart() {
  gnssStartMs = millis();
  // Reset de estado TTFF
  gnssFixReported = false;
  ttffPrinted = false;
  haveFix = false;
  fixQLast = 0;
  lastFixMs = 0;
}

void parseGSA(const String& s) {
  // $xxGSA,mode,fixType,sv1,...,sv12,PDOP,HDOP,VDOP*CS
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  gsaFixType = f[2].toInt();  // 1/2/3
  uint8_t used = 0;
  for (int i = 3; i <= 14; ++i)
    if (f[i].length() > 0) used++;
  gsaSatsUsed = used;

  gsaPdop = f[15].length() ? f[15].toFloat() : NAN;
  gsaHdop2 = f[16].length() ? f[16].toFloat() : NAN;
  gsaVdop = f[17].length() ? f[17].toFloat() : NAN;
}

void parseGSV(const String& s) {
  // $xxGSV,total,msgnum,sats, (prn,elev,az,snr) x up to 4
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  int total = f[1].toInt();
  int num = f[2].toInt();
  int sats = f[3].toInt();
  if (num == 1) {
    gsvSnrAcc = 0;
    gsvSnrCnt = 0;
    gsvSnrMax = 0;
  }

  for (int i = 4; i + 3 < N; i += 4) {
    if (f[i + 3].length()) {
      int snr = f[i + 3].toInt();
      if (snr > 0) {
        gsvSnrAcc += snr;
        gsvSnrCnt++;
        if (snr > gsvSnrMax) gsvSnrMax = snr;
      }
    }
  }

  if (num == total || total == 0) {
    gsvSatsInView = sats;
    gsvSnrAvg = (gsvSnrCnt > 0) ? (gsvSnrAcc / gsvSnrCnt) : 0;
    gsvLastMs = millis();
  }
}

void parseGGA(const String& s) {
  const int N = 15;
  String f[N];
  splitSentence(s, ',', f, N);
  String lat = f[2], latD = f[3], lon = f[4], lonD = f[5];
  String fixQ = f[6];
  satellitesStr = f[7];
  hdopStr = f[8];
  String alt = f[9];

  lastGgaMs = millis();
  fixQLast = (uint8_t)fixQ.toInt();
  if (fixQLast >= 1) {
    haveFix = true;
    lastFixMs = lastGgaMs;
  }

  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsAlt = alt;
  gpsStatus = (fixQ.toInt() >= 1) ? "Fix" : "NoFix";
}

void parseRMC(const String& s) {
  const int N = 12;
  String f[N];
  splitSentence(s, ',', f, N);
  String t = f[1], st = f[2], lat = f[3], latD = f[4], lon = f[5], lonD = f[6], date = f[9];
  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsStatus = (st == "A") ? "Fix" : "NoFix";
  if (t.length() >= 6) gpsTime = t.substring(0, 2) + ":" + t.substring(2, 4) + ":" + t.substring(4, 6);
  if (date.length() == 6) gpsDate = date.substring(0, 2) + "/" + date.substring(2, 4) + "/20" + date.substring(4, 6);
}

void parseVTG(const String& s) {
  const int N = 10;
  String f[N];
  splitSentence(s, ',', f, N);
  String kmh = (f[7].length() > 0) ? f[7] : "0.0";
  gpsSpeedKmh = kmh;
}

// Talker-agnostic (GP/GN/GA/GL/BD/GB/QZ...): detecta tipo por [3..5]
void parseNMEA(const String& line) {
  lastNmeaMs = millis();
  nmeaCount1s++;
  lastNmeaSeenMs = lastNmeaMs;  // heartbeat NMEA

  if (line.length() < 7 || line.charAt(0) != '$') return;
  String typ = line.substring(3, 6);

  if (typ == "GGA") parseGGA(line);
  else if (typ == "RMC") parseRMC(line);
  else if (typ == "VTG") parseVTG(line);
  else if (typ == "GSA") parseGSA(line);
  else if (typ == "GSV") parseGSV(line);
}

// -------------------- GNSS bring-up functions --------------------
// Intentar configurar el modo GNSS según config, con fallback automático
bool setGnssAllWithFallback() {
  // Intentar primero el modo configurado por el usuario
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "+CGNSSMODE=%u,1", config.gnssMode);

  if (atRun(cmd, "OK", "ERROR", 2000)) {
    Serial.print("[GNSS] Mode set: ");
    Serial.print(config.gnssMode);
    switch(config.gnssMode) {
      case 1:  Serial.println(" (GPS only)"); break;
      case 3:  Serial.println(" (GPS + GLONASS)"); break;
      case 5:  Serial.println(" (GPS + BEIDOU)"); break;
      case 7:  Serial.println(" (GPS + GLONASS + BEIDOU)"); break;
      case 15: Serial.println(" (ALL: GPS + GLONASS + GALILEO + BEIDOU)"); break;
      default: Serial.println(" (Unknown)"); break;
    }
    return true;
  }

  // Si falla el modo configurado, intentar fallback con modos más simples
  Serial.print("[GNSS] Configured mode ");
  Serial.print(config.gnssMode);
  Serial.println(" failed, trying fallback...");

  const char* fallbackTries[] = {
    "+CGNSSMODE=7,1",   // GPS+GLONASS+BEIDOU
    "+CGNSSMODE=3,1",   // GPS+GLONASS
    "+CGNSSMODE=1,1"    // GPS only
  };

  for (size_t i = 0; i < sizeof(fallbackTries) / sizeof(fallbackTries[0]); ++i) {
    if (atRun(fallbackTries[i], "OK", "ERROR", 2000)) {
      Serial.print("[GNSS] Fallback mode set: ");
      Serial.println(fallbackTries[i]);
      return true;
    }
  }

  Serial.println("[GNSS] All attempts to set constellation FAILED");
  return false;
}

void gnssBringUp() {
  Serial.println("[GNSS] Bring-up...");
  atRun("+CGPS=0", "OK", "ERROR", 5000);
  atRun("+CGNSSPWR=1", "OK", "ERROR", 2000);

  (void)setGnssAllWithFallback();  // multi-constelación con fallback

  atRun("+CGPSNMEA=200191", "OK", "ERROR", 2000);
  atRun("+CGPSNMEARATE=1", "OK", "ERROR", 2000);
  atRun("+CGPS=1", "OK", "ERROR", 8000);
  // NMEA Output to AT port (RMC,GGA,VTG on AT port)
  atRun("+CGPSINFOCFG=1,31", "OK", "ERROR", 2000);
  Serial.println("[GNSS] ON (NMEA 1 Hz)");
  resetGnssFlagsAfterStart();
}

void gnssHotRestart() {
  Serial.println("[GNSS] HOT restart");
  atRun("+CGPSRST=0", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

void gnssWarmRestart() {
  Serial.println("[GNSS] WARM restart");
  atRun("+CGPSRST=1", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

void gnssWatchdog() {
  if (haveFix) return;
  uint32_t alive = millis() - gnssStartMs;
  if (alive > 300000UL) {
    gnssWarmRestart();
  } else if (alive > 120000UL) {
    gnssHotRestart();
  }
}

// -------------------- Async GNSS diag & debug (non-blocking) --------------------
void gnssDiagTick() {
  const uint32_t now = millis();

  // Calcular tasa NMEA cada 1s
  if (now - nmeaRefMs >= 1000) {
    nmeaRefMs = now;
    nmeaRate = nmeaCount1s;  // aprox Hz
    nmeaCount1s = 0;
  }

  // Edades (en s)
  uint32_t ageNmea = (lastNmeaMs ? (now - lastNmeaMs) / 1000UL : 9999);
  uint32_t ageGga = (lastGgaMs ? (now - lastGgaMs) / 1000UL : 9999);
  uint32_t ageFix = (lastFixMs ? (now - lastFixMs) / 1000UL : 9999);
  (void)ageFix;

  // Re-armar salida NMEA si se silenció (>5s sin NMEA)
  if (ageNmea > 5 && !at.active) {
    Serial.println("[GNSSDIAG] No NMEA >5s -> re-enable NMEA 1Hz");
    (void)atRun("+CGPSNMEA=200191", "OK", "ERROR", 1200);
    (void)atRun("+CGPSNMEARATE=1", "OK", "ERROR", 1200);
  }

  // Si >15s sin NMEA, asegura GNSS ON (idempotente; barato)
  if (ageNmea > 15 && !at.active) {
    (void)atRun("+CGNSSPWR=1", "OK", "ERROR", 1200);
    (void)atRun("+CGPS=1", "OK", "ERROR", 3000);
  }

  // Imprimir cada 5s un resumen legible (solo Serial)
  static uint32_t nextPrint = 0;
  if (now >= nextPrint) {
    nextPrint = now + 5000;
    Serial.printf("[GNSSDIAG] rate=%u Hz  ageNMEA=%lus  ageGGA=%lus  fixQ=%u  sats=%s  HDOP=%s  status=%s\n",
                  nmeaRate, (unsigned long)ageNmea, (unsigned long)ageGga,
                  (unsigned)fixQLast, satellitesStr.c_str(), hdopStr.c_str(), gpsStatus.c_str());

    // Pequeño resumen de GSA/GSV por Serial
    if (!isnan(gsaPdop) || !isnan(gsaHdop2)) {
      Serial.printf("[GSA] fixType=%u used=%u PDOP=%.1f HDOP=%.1f VDOP=%.1f\n",
                    gsaFixType, gsaSatsUsed, gsaPdop, gsaHdop2, gsaVdop);
    }
    Serial.printf("[GSV] inView=%u SNR(avg)=%.1f SNR(max)=%.1f age=%lus\n",
                  (unsigned)gsvSatsInView, gsvSnrAvg, gsvSnrMax,
                  (unsigned)((gsvLastMs ? now - gsvLastMs : 0) / 1000UL));
  }

  // TTFF (solo una vez por ciclo de encendido/restart)
  if (!ttffPrinted && haveFix && lastFixMs && gnssStartMs) {
    uint32_t ttff = (lastFixMs >= gnssStartMs) ? (lastFixMs - gnssStartMs) / 1000UL : 0;
    Serial.printf("[GNSS] TTFF = %lu s\n", (unsigned long)ttff);
    ttffPrinted = true;
  }
}

void gnssDebugPollAsync() {
  const uint32_t PERIOD_MS = 5000;
  if (millis() < gnssDbgNextAt) return;
  if (at.active) return;

  bool done = false, ok = false;
  (void)atTick(done, ok);

  switch (gnssDbgState) {
    case GNSS_DBG_IDLE:
      gnssDbgState = GNSS_DBG_ASK_STATUS;
      break;
    case GNSS_DBG_ASK_STATUS:
      atBegin("+CGPSSTATUS?", "OK", "ERROR", 1200);
      gnssDbgState = GNSS_DBG_WAIT_STATUS;
      break;
    case GNSS_DBG_WAIT_STATUS:
      if ((bool)atTick(done, ok)) {
        Serial.println(ok ? "[GNSS] +CGPSSTATUS? OK" : "[GNSS] +CGPSSTATUS? FAIL");
        Serial.print(at.resp);
        gnssDbgState = GNSS_DBG_ASK_INFO;
      }
      break;
    case GNSS_DBG_ASK_INFO:
      atBegin("+CGPSINFO", "OK", "ERROR", 1200);
      gnssDbgState = GNSS_DBG_WAIT_INFO;
      break;
    case GNSS_DBG_WAIT_INFO:
      if ((bool)atTick(done, ok)) {
        Serial.println(ok ? "[GNSS] +CGPSINFO OK" : "[GNSS] +CGPSINFO FAIL");
        Serial.print(at.resp);
        Serial.printf("[GNSS] Status=%s Sats=%s HDOP=%s Alt=%sm Spd=%skm/h Lat=%s Lon=%s\n",
                      gpsStatus.c_str(), satellitesStr.c_str(), hdopStr.c_str(),
                      gpsAlt.c_str(), gpsSpeedKmh.c_str(), gpsLat.c_str(), gpsLon.c_str());
        gnssDbgState = GNSS_DBG_IDLE;
        gnssDbgNextAt = millis() + PERIOD_MS;
      }
      break;
  }
}

// -------------------- XTRA / AGNSS --------------------
bool detectAndEnableXtra() {
  String r;
  if (!sendAtSync("+CGPSXE=?", r, 2000)) {
    Serial.println("[XTRA] Not supported");
    return false;
  }
  if (!sendAtSync("+CGPSXE=1", r, 2000)) {
    Serial.println("[XTRA] Enable FAIL");
    return false;
  }
  Serial.println("[XTRA] Enabled");
  return true;
}

bool downloadXtraOnce() {
  if (!modem.isGprsConnected()) {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println("[XTRA] PDP reconnect FAIL");
      return false;
    }
  }
  String r;
  (void)sendAtSync("+CGPSXD=?", r, 2000);
  bool ok = sendAtSync("+CGPSXD=1", r, 120000);
  Serial.println(ok ? "[XTRA] Download OK" : "[XTRA] Download FAIL");
  xtraLastOk = ok;
  return ok;
}

void downloadXtraIfDue() {
  if (!xtraSupported) return;
  if (millis() - lastXtraDownload < XTRA_REFRESH_MS) return;
  if (downloadXtraOnce()) lastXtraDownload = millis();
}
