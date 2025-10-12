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
  File f = SD.open(csvFileName, FILE_WRITE);
  if (f) {
    f.println("ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,resetReason");
    f.close();
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
  String line = String(millis()) + "," + hhmmss + "," + gpsDate + "," + gpsLat + "," + gpsLon + "," + gpsAlt + "," + gpsSpeedKmh + "," + String(PM1) + "," + String(PM25) + "," + String(PM10) + "," + (isnan(pmsTempC) ? "0" : String(pmsTempC, 1)) + "," + (isnan(pmsHum) ? "0" : String(pmsHum, 1)) + "," + String(rtcTempC, 2) + "," + String(batV, 2) + "," + String(csq) + "," + satellitesStr + "," + hdopStr + "," + (xtraLastOk ? "1" : "0") + "," + sht31Temp + "," + sht31Humidity + "," + rebootReason;
  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
    Serial.println(String("[SD] Saved line: ") + line);
    return true;
  }
  return false;
}
