// -------------------- Configuration System --------------------
// Sistema de configuración persistente en flash (Preferences)
// Configuración accesible por comandos seriales
// NOTA: struct SystemConfig y variable config están declaradas en el archivo principal

// -------------------- Load Configuration --------------------
void loadConfig() {
  prefs.begin("config", false);

  // SD
  config.sdAutoMount = prefs.getBool("sdAuto", false);
  config.sdSavePeriod = prefs.getUInt("sdSavePer", 3000);

  // HTTP
  config.httpSendPeriod = prefs.getUInt("httpPer", 3000);
  config.httpTimeout = prefs.getUShort("httpTO", 15);

  // Display
  config.oledAutoOff = prefs.getBool("oledOff", false);
  config.oledTimeout = prefs.getUInt("oledTO", 120000);

  // Power
  config.ledEnabled = prefs.getBool("ledEn", true);
  config.ledBrightness = prefs.getUChar("ledBr", 50);

  // Autostart
  config.autostart = prefs.getBool("autoStart", false);
  config.autostartWaitGps = prefs.getBool("autoGPS", false);
  config.autostartGpsTimeout = prefs.getUShort("autoGPSTO", 600);

  // GNSS Mode
  config.gnssMode = prefs.getUChar("gnssMode", 15);

  prefs.end();

  Serial.println("[CONFIG] Loaded from flash");
  Serial.printf("[CONFIG] SD auto-mount: %s, save period: %lums\n",
                config.sdAutoMount ? "ON" : "OFF", config.sdSavePeriod);
  Serial.printf("[CONFIG] HTTP send period: %lums, timeout: %us\n",
                config.httpSendPeriod, config.httpTimeout);
  Serial.printf("[CONFIG] OLED auto-off: %s, timeout: %lums\n",
                config.oledAutoOff ? "ON" : "OFF", config.oledTimeout);
  Serial.printf("[CONFIG] LED enabled: %s, brightness: %u%%\n",
                config.ledEnabled ? "YES" : "NO", config.ledBrightness);
  Serial.printf("[CONFIG] Autostart: %s, wait GPS: %s, GPS timeout: %us\n",
                config.autostart ? "YES" : "NO",
                config.autostartWaitGps ? "YES" : "NO",
                config.autostartGpsTimeout);
  Serial.printf("[CONFIG] GNSS mode: %u\n", config.gnssMode);
}

// -------------------- Save Configuration --------------------
void saveConfig() {
  prefs.begin("config", false);

  // SD
  prefs.putBool("sdAuto", config.sdAutoMount);
  prefs.putUInt("sdSavePer", config.sdSavePeriod);

  // HTTP
  prefs.putUInt("httpPer", config.httpSendPeriod);
  prefs.putUShort("httpTO", config.httpTimeout);

  // Display
  prefs.putBool("oledOff", config.oledAutoOff);
  prefs.putUInt("oledTO", config.oledTimeout);

  // Power
  prefs.putBool("ledEn", config.ledEnabled);
  prefs.putUChar("ledBr", config.ledBrightness);

  // Autostart
  prefs.putBool("autoStart", config.autostart);
  prefs.putBool("autoGPS", config.autostartWaitGps);
  prefs.putUShort("autoGPSTO", config.autostartGpsTimeout);

  // GNSS Mode
  prefs.putUChar("gnssMode", config.gnssMode);

  prefs.end();

  Serial.println("[CONFIG] Saved to flash");
}

// -------------------- Reset to Defaults --------------------
void configSetDefaults() {
  config.sdAutoMount = false;         // NO montar en boot
  config.sdSavePeriod = 3000;         // 3 segundos

  config.httpSendPeriod = 3000;       // 3 segundos
  config.httpTimeout = 15;            // 15 segundos

  config.oledAutoOff = false;         // Siempre encendida
  config.oledTimeout = 120000;        // 2 minutos

  config.ledEnabled = true;           // LED habilitado
  config.ledBrightness = 50;          // 50% brillo

  config.autostart = false;           // NO autostart
  config.autostartWaitGps = false;    // NO esperar GPS
  config.autostartGpsTimeout = 600;   // 10 minutos (default)

  config.gnssMode = 15;               // Todas las constelaciones (default)

  Serial.println("[CONFIG] Reset to defaults");
}

// -------------------- Print Configuration --------------------
void printConfig() {
  Serial.println("=== HIRI PRO Configuration ===");

  Serial.println("\n[SD Card]");
  Serial.printf("  Auto-mount on boot: %s\n", config.sdAutoMount ? "ON" : "OFF");
  Serial.printf("  Save period:        %lu ms (%lu s)\n",
                config.sdSavePeriod, config.sdSavePeriod / 1000);

  Serial.println("\n[HTTP Transmission]");
  Serial.printf("  Send period:        %lu ms (%lu s)\n",
                config.httpSendPeriod, config.httpSendPeriod / 1000);
  Serial.printf("  Timeout:            %u seconds\n", config.httpTimeout);

  Serial.println("\n[Display OLED]");
  Serial.printf("  Auto-off:           %s\n", config.oledAutoOff ? "ON" : "OFF");
  Serial.printf("  Timeout:            %lu ms (%lu s)\n",
                config.oledTimeout, config.oledTimeout / 1000);

  Serial.println("\n[Power/LED]");
  Serial.printf("  NeoPixel enabled:   %s\n", config.ledEnabled ? "YES" : "NO");
  Serial.printf("  Brightness:         %u%%\n", config.ledBrightness);

  Serial.println("\n[Autostart]");
  Serial.printf("  Autostart:          %s\n", config.autostart ? "YES" : "NO");
  Serial.printf("  Wait for GPS fix:   %s\n", config.autostartWaitGps ? "YES" : "NO");
  Serial.printf("  GPS timeout:        %u seconds (%u min)\n",
                config.autostartGpsTimeout, config.autostartGpsTimeout / 60);

  Serial.println("\n[GNSS Configuration]");
  Serial.printf("  Mode:               %u ", config.gnssMode);
  switch(config.gnssMode) {
    case 1:  Serial.println("(GPS only)"); break;
    case 3:  Serial.println("(GPS + GLONASS)"); break;
    case 5:  Serial.println("(GPS + BEIDOU)"); break;
    case 7:  Serial.println("(GPS + GLONASS + BEIDOU)"); break;
    case 15: Serial.println("(ALL: GPS + GLONASS + GALILEO + BEIDOU)"); break;
    default: Serial.println("(Unknown)"); break;
  }

  Serial.println();
}

// -------------------- Print SD Info --------------------
void printSDInfo() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Card Info ===");
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  Serial.printf("Card size:    %llu MB\n", cardSize);
  Serial.printf("Total:        %llu MB\n", totalBytes);
  Serial.printf("Used:         %llu MB\n", usedBytes);
  Serial.printf("Free:         %llu MB\n", totalBytes - usedBytes);
  Serial.printf("Current file: %s\n", csvFileName.c_str());
}

// -------------------- Print SD File List --------------------
void printSDFileList() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Files ===");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root");
    return;
  }

  File file = root.openNextFile();
  int count = 0;
  while (file) {
    if (!file.isDirectory()) {
      Serial.printf("  %s - %lu bytes\n", file.name(), (unsigned long)file.size());
      count++;
    }
    file = root.openNextFile();
  }
  Serial.printf("Total: %d files\n", count);
}

// -------------------- Apply LED Configuration --------------------
void applyLEDConfig() {
  if (!config.ledEnabled) {
    pixels.clear();
    pixels.show();
    Serial.println("[LED] Disabled by config");
  } else {
    // Convertir porcentaje a 0-255
    uint8_t brightness = map(config.ledBrightness, 0, 100, 0, 255);
    pixels.setBrightness(brightness);
    Serial.printf("[LED] Brightness set to %u%% (%u/255)\n", config.ledBrightness, brightness);
  }
}

// -------------------- Clear SD Card --------------------
void clearSDCard() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted - cannot clear");
    return;
  }

  Serial.println("[SD] Starting to delete all files...");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root directory");
    return;
  }

  int deletedCount = 0;
  int failedCount = 0;

  // First pass: collect all filenames
  String fileNames[50];  // Max 50 files
  int fileCount = 0;

  File file = root.openNextFile();
  while (file && fileCount < 50) {
    if (!file.isDirectory()) {
      fileNames[fileCount++] = String("/") + String(file.name());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  // Second pass: delete all collected files
  for (int i = 0; i < fileCount; i++) {
    Serial.print("[SD] Deleting: ");
    Serial.print(fileNames[i]);

    if (SD.remove(fileNames[i].c_str())) {
      Serial.println(" - OK");
      deletedCount++;
    } else {
      Serial.println(" - FAILED");
      failedCount++;
    }
  }

  Serial.println("[SD] Clear complete:");
  Serial.printf("  Deleted: %d files\n", deletedCount);
  if (failedCount > 0) {
    Serial.printf("  Failed:  %d files\n", failedCount);
  }

  // Reset CSV filename in preferences since all files were deleted
  csvFileName = "";
  prefs.begin("system", false);
  prefs.putString("csvFile", "");
  prefs.end();
  Serial.println("[SD] CSV filename cleared from memory");
}
