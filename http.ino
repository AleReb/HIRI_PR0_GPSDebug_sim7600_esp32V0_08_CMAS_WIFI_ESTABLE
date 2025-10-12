// -------------------- HTTP helpers --------------------
bool ensurePdpAndNet() {
  String dummy;
  (void)sendAtSync("+CGDCONT=1,\"IP\",\"gigsky-02\"", dummy, 2000);

  if (!modem.isGprsConnected()) {
    Serial.println("[NET] PDP down, reconnecting...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println("[NET] PDP reconnect FAIL");
      return false;
      hasRed = false;
    }
  }

  String r;
  if (!sendAtSync("+NETOPEN?", r, 2000) || r.indexOf("+NETOPEN: 1") < 0) {
    if (!sendAtSync("+NETOPEN", r, 10000)) {
      Serial.println("[NET] NETOPEN FAIL");
      return false;
      hasRed = false;
    }
  }
  return true;
  hasRed = true;
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
    // Timeout ADAPTATIVO basado en batería: 10s si batería baja, 2.5s normal
    const uint32_t MAX_HTTP_WAIT_MS = (batV < 3.6) ? 10000 : 2500;

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
