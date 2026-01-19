// -------------------- SD helpers --------------------
String generateCSVFileName() {
  // Genera nombre basado en DEVICE_ID_STR y fecha actual
  // Formato: HP02_DD_MM_YYYY.csv
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  char dateStr[16];
  snprintf(dateStr, sizeof(dateStr), "%02d_%02d_%04d", now.day(), now.month(), now.year());

  String name = "/HP" + String(DEVICE_ID_STR) + "_" + String(dateStr) + ".csv";
  Serial.print("[SD] CSV filename: ");
  Serial.println(name);
  return name;
}

void writeCSVHeader() {
  if (!SDOK) return;
  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    if (f.size() == 0) { // Only write header if file is empty
        f.println("ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,resetReason,pm100");
        Serial.println("[SD] Wrote header to " + csvFileName);
    }
    f.close();
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName + " to write header.");
  }
}

void writeErrorLogHeader() {
  if (!SDOK) return;
  if (SD.exists(logFilePath.c_str())) return;  // Ya existe
  File f = SD.open(logFilePath.c_str(), FILE_WRITE);
  if (f) {
    f.println("timestamp,errorType,errorCode,rawResponse,operator,technology,signalQuality,registrationStatus,batteryV,uptime_s");
    f.close();
    Serial.println("[SD] Error log header created");
  }
}

// Variable global para almacenar la última línea guardada (para visualización en OLED)
extern String lastSavedCSVLine;

bool saveCSVData() {
  if (!SDOK || !loggingEnabled) return false;
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);

  // Detectar cambio de día (muy eficiente: 1 ciclo CPU)
  if (now.day() != lastDayLogged) {
    Serial.println("[SD] Day changed, creating new file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    lastDayLogged = now.day();

    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  char hhmmss[9];
  snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  String sht31Temp = (SHT31OK && !isnan(tempsht31)) ? String(tempsht31, 2) : "0";
  String sht31Humidity = (SHT31OK && !isnan(humsht31)) ? String(humsht31, 2) : "0";
  String line = String(millis()) + "," + hhmmss + "," + gpsDate + "," + gpsLat + "," + gpsLon + "," + gpsAlt + "," + gpsSpeedKmh + "," + String(PM1) + "," + String(PM25) + "," + String(PM10) + "," + (isnan(pmsTempC) ? "0" : String(pmsTempC, 1)) + "," + (isnan(pmsHum) ? "0" : String(pmsHum, 1)) + "," + String(rtcTempC, 2) + "," + String(batV, 2) + "," + String(csq) + "," + satellitesStr + "," + hdopStr + "," + (xtraLastOk ? "1" : "0") + "," + sht31Temp + "," + sht31Humidity + "," + rebootReason + "," + String(SDS198PM100);

  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
    sdSaveCounter++;  // Incrementar contador de guardados exitosos en SD
    lastSavedCSVLine = line;  // Guardar línea para visualización en OLED
    Serial.println(String("[SD] Saved line: ") + line);
    return true;
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName + " for appending.");
    return false;
  }
}

// -------------------- Failed Transmission Log (Debug only) --------------------
// Esta función guarda las transmisiones HTTP fallidas para análisis posterior
// NO se reintenta la transmisión automáticamente para evitar desfase de datos
// Variables globales ajustables para pruebas:
// - failedTxPath: Ruta del archivo CSV (definido en .ino principal)
void saveFailedTransmission(const String& url, const String& errorType) {
  if (!SDOK) return;  // SD no disponible

  DateTime now = rtc.now();
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());

  // Verificar si necesitamos crear el header (primera vez)
  bool needsHeader = !SD.exists(failedTxPath.c_str());

  File f = SD.open(failedTxPath.c_str(), FILE_APPEND);
  if (f) {
    // Escribir header si es la primera línea
    if (needsHeader) {
      f.println("timestamp,error_type,url");
      Serial.println("[FAILED_TX] Created header in " + failedTxPath);
    }

    // Escribir línea de fallo
    // Formato: timestamp,error_type,url (URL entre comillas por si tiene comas)
    String line = String(timestamp) + "," + errorType + ",\"" + url + "\"";
    f.println(line);
    f.close();

    Serial.println("[FAILED_TX] Logged: " + errorType);
  } else {
    Serial.println("[FAILED_TX][ERR] Could not open " + failedTxPath);
  }
}
