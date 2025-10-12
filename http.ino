// -------------------- HTTP helpers --------------------
// Variables globales para control de reconexión PDP (ajustables para pruebas)
static uint8_t pdpReconnectFailCount = 0;          // Contador de fallos consecutivos
static uint32_t lastPdpReconnectAttempt = 0;      // Timestamp del último intento
const uint8_t MAX_PDP_FAILS_BEFORE_BACKOFF = 5;   // Máximo de fallos antes de activar backoff
const uint32_t PDP_BACKOFF_MS = 15000;             // Tiempo de espera después de N fallos (15s)
const uint32_t PDP_RECONNECT_TIMEOUT_MS = 30000;  // Timeout por intento de reconexión (30s)

bool ensurePdpAndNet() {
  String dummy;
  (void)sendAtSync("+CGDCONT=1,\"IP\",\"gigsky-02\"", dummy, 2000);

  if (!modem.isGprsConnected()) {
    // PROTECCIÓN CONTRA BLOQUEOS: Si hemos fallado muchas veces, esperar antes de reintentar
    // Esto evita bloqueos cuando se viaja entre redes celulares o en zonas sin cobertura
    if (pdpReconnectFailCount >= MAX_PDP_FAILS_BEFORE_BACKOFF) {
      uint32_t timeSinceLastAttempt = millis() - lastPdpReconnectAttempt;
      if (timeSinceLastAttempt < PDP_BACKOFF_MS) {
        uint32_t remainingBackoff = PDP_BACKOFF_MS - timeSinceLastAttempt;
        Serial.printf("[NET] Too many failures (%d), waiting %lu ms before retry\n",
                      pdpReconnectFailCount, remainingBackoff);
        return false;
      } else {
        // Han pasado 15s, resetear contador y reintentar
        Serial.println("[NET] Backoff period over, resetting fail counter");
        pdpReconnectFailCount = 0;
      }
    }

    Serial.println("[NET] PDP down, reconnecting...");
    lastPdpReconnectAttempt = millis();

    // MINI-LOOP CON WATCHDOG RESET: modem.gprsConnect() puede bloquear 10-60s
    // Alimentamos el watchdog cada 1s para evitar reset del ESP32
    uint32_t reconStart = millis();
    bool reconOk = false;
    while (millis() - reconStart < PDP_RECONNECT_TIMEOUT_MS) {
      esp_task_wdt_reset();  // Evitar watchdog timeout cada 1s

      if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
        reconOk = true;
        break;
      }

      delay(1000);  // Esperar 1s entre intentos internos
    }

    if (!reconOk) {
      pdpReconnectFailCount++;
      Serial.printf("[NET] PDP reconnect FAIL after %lu ms (fail count: %d/%d)\n",
                    PDP_RECONNECT_TIMEOUT_MS, pdpReconnectFailCount, MAX_PDP_FAILS_BEFORE_BACKOFF);
      hasRed = false;
      return false;
    }

    // Éxito: resetear contador de fallos
    pdpReconnectFailCount = 0;
    hasRed = true;
    Serial.println("[NET] PDP reconnected OK");
  }

  String r;
  if (!sendAtSync("+NETOPEN?", r, 2000) || r.indexOf("+NETOPEN: 1") < 0) {
    if (!sendAtSync("+NETOPEN", r, 10000)) {
      Serial.println("[NET] NETOPEN FAIL");
      hasRed = false;
      return false;
    }
  }
  hasRed = true;
  return true;
}

// Helper: parsear +HTTPACTION: 0,200,123
void parseHttpActionResponse(const String& resp, int& code, int& dataLen) {
  code = -1;
  dataLen = -1;
  int p = resp.indexOf("+HTTPACTION:");
  int c1 = resp.indexOf(',', p);
  int c2 = resp.indexOf(',', c1 + 1);
  if (p >= 0 && c1 > 0 && c2 > c1) {
    code = resp.substring(c1 + 1, c2).toInt();
    dataLen = resp.substring(c2 + 1).toInt();
  }
}

// -------------------- HTTP GET (blocking, stable) --------------------
bool httpGet_webhook(const String& fullUrl) {
  Serial.printf("[HTTP][SYNC] URL length = %d\n", fullUrl.length());
  if (fullUrl.length() > 512) {
    Serial.println("[HTTP][WARN] URL >512 chars; SIM7600 +HTTPPARA may fail.");
  }

  if (!ensurePdpAndNet()) {
    logError("HTTP_PDP_FAIL", "ensurePdpAndNet", "PDP/NET setup failed");
    return false;
  }

  bool done = false, ok = false;
  (void)atTick(done, ok);

  (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
  if (!atRun("+HTTPINIT", "OK", "ERROR", 5000)) {
    Serial.println("[HTTP][ERR] HTTPINIT FAIL");
    logError("HTTP_INIT_FAIL", "HTTPINIT", at.resp);
    return false;
  }
  if (!atRun("+HTTPPARA=\"CID\",1", "OK", "ERROR", 2000)) {
    Serial.println("[HTTP][ERR] HTTPPARA CID FAIL");
    logError("HTTP_CID_FAIL", "HTTPPARA_CID", at.resp);
    (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
    return false;
  }
  {
    String cmd = "+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
    if (!atRun(cmd, "OK", "ERROR", 6000)) {
      Serial.println("[HTTP][ERR] Set URL FAIL");
      logError("HTTP_URL_FAIL", "HTTPPARA_URL", at.resp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      return false;
    }
  }

  // Apagar LED para ahorrar energía durante transmisión
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  atBegin("+HTTPACTION=0", "+HTTPACTION:", "ERROR", 90000);
  int httpCode = -1, dataLen = -1;
  {
    bool actionDone = false, actionOk = false;
    uint32_t startTime = millis();
    // Timeout HTTP: 15s permite ~5 lecturas de PMS/GPS (3s cada una) antes de abortar
    // Ajustable para pruebas: reducir si red es muy rápida, aumentar si red es muy lenta
    const uint32_t MAX_HTTP_WAIT_MS = 15000;  // 15 segundos

    while (!actionDone) {
      esp_task_wdt_reset();  // Reset watchdog para evitar timeout durante HTTP

      if (atTick(actionDone, actionOk)) break;

      // Check timeout
      if (millis() - startTime > MAX_HTTP_WAIT_MS) {
        uint32_t elapsed = millis() - startTime;
        Serial.printf("[HTTP][TIMEOUT] After %lu ms (bat=%.2fV)\n", elapsed, batV);
        logError("HTTP_TIMEOUT", "HTTPACTION", "Timeout=" + String(elapsed) + "ms bat=" + String(batV, 2) + "V");
        (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
        updatePmLed((float)PM25);
        return false;
      }

      delay(1);
    }
    String actionResp = at.resp;
    if (!actionOk) {
      Serial.println("[HTTP][ERR] +HTTPACTION did not complete");
      logError("HTTP_ACTION_FAIL", "HTTPACTION", actionResp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      // Restaurar LED antes de salir
      updatePmLed((float)PM25);
      return false;
    }
    parseHttpActionResponse(actionResp, httpCode, dataLen);
    Serial.printf("[HTTP] code=%d len=%d\n", httpCode, dataLen);
    if (httpCode == -1) {
      Serial.println("[HTTP][ERR] Could not parse +HTTPACTION");
      logError("HTTP_PARSE_FAIL", "HTTPACTION", actionResp);
    }
  }

  if (dataLen > 0) {
    String cmd = "+HTTPREAD=0," + String(dataLen);
    String readResp;
    if (sendAtSync(cmd, readResp, 8000)) {
      Serial.println("[HTTP][READ] ----------------");
      Serial.println(readResp);
      Serial.println("[HTTP][READ] ----------------");
    } else {
      Serial.println("[HTTP][WARN] HTTPREAD failed");
    }
  }

  (void)atRun("+HTTPTERM", "OK", "ERROR", 2000);

  // Restaurar LED según nivel de PM2.5
  updatePmLed((float)PM25);

  return (httpCode >= 200 && httpCode < 300);
}
