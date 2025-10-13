// -------------------- Serial Commands --------------------
// Sistema de comandos seriales para debug y mantenimiento
// Uso: Enviar comando por serial monitor (115200 baud, newline)
// Ejemplo: "help" → muestra lista de comandos

void processSerialCommand() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) return;

  Serial.println("\n>>> Command: " + cmd);

  // -------------------- HELP --------------------
  if (cmd == "help" || cmd == "?") {
    Serial.println(F("=== HIRI PRO Serial Commands ==="));
    Serial.println(F("\n[RTC/Time]"));
    Serial.println(F("  rtc         - Show current RTC time"));
    Serial.println(F("  rtcsync     - Force sync RTC with modem"));
    Serial.println(F("  rtcreset    - Reset modem sync counter"));
    Serial.println(F("  modemtime   - Show modem time only"));

    Serial.println(F("\n[Counters]"));
    Serial.println(F("  counters    - Show SD/HTTP counters"));
    Serial.println(F("  resetcnt    - Reset both counters to 0"));
    Serial.println(F("  stats       - Show detailed statistics"));

    Serial.println(F("\n[SD Card]"));
    Serial.println(F("  sdinfo      - Show SD card info"));
    Serial.println(F("  sdlist      - List files on SD"));
    Serial.println(F("  sdnew       - Create new CSV file"));

    Serial.println(F("\n[Network/Modem]"));
    Serial.println(F("  netinfo     - Show network info"));
    Serial.println(F("  csq         - Show signal quality"));

    Serial.println(F("\n[System]"));
    Serial.println(F("  sysinfo     - Show system info"));
    Serial.println(F("  reboot      - Reboot ESP32"));
    Serial.println(F("  mem         - Show memory usage"));

    Serial.println(F("\n[Streaming]"));
    Serial.println(F("  start       - Start streaming"));
    Serial.println(F("  stop        - Stop streaming"));

    Serial.println(F("\n"));
  }

  // -------------------- RTC COMMANDS --------------------
  else if (cmd == "rtc") {
    if (!rtcOK) {
      Serial.println("[RTC] Not available");
      return;
    }
    DateTime now = rtc.now();
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    Serial.printf("[RTC] Epoch: %lu\n", (unsigned long)now.unixtime());
    Serial.printf("[RTC] Temperature: %.2f°C\n", rtc.getTemperature());
    Serial.printf("[RTC] Modem syncs: %u/3\n", rtcModemSyncCount);
  }

  else if (cmd == "rtcsync") {
    Serial.println("[RTC] Forcing sync with modem...");
    if (syncRtcFromModem()) {
      Serial.println("[RTC] ✓ Synchronized successfully");
    } else {
      Serial.println("[RTC] ✗ Sync failed (modem not ready or diff < 20s)");
    }
  }

  else if (cmd == "rtcreset") {
    resetModemSyncCounter();
    Serial.println("[RTC] ✓ Modem sync counter reset (can sync 3 more times)");
  }

  else if (cmd == "modemtime") {
    uint32_t modemEpoch = 0;
    if (getModemEpoch(modemEpoch)) {
      time_t t = modemEpoch;
      struct tm* timeinfo = localtime(&t);
      Serial.printf("[MODEM] Time: %04d-%02d-%02d %02d:%02d:%02d (epoch=%lu)\n",
                    timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                    (unsigned long)modemEpoch);
    } else {
      Serial.println("[MODEM] Time not available");
    }
  }

  // -------------------- COUNTER COMMANDS --------------------
  else if (cmd == "counters") {
    Serial.println("=== Counters ===");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
  }

  else if (cmd == "resetcnt") {
    Serial.println("[COUNTERS] Resetting to 0...");
    sendCounter = 0;
    sdSaveCounter = 0;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", 0);
    prefs.putUInt("sdCnt", 0);
    prefs.end();
    Serial.println("[COUNTERS] ✓ Reset complete");
  }

  else if (cmd == "stats") {
    Serial.println("=== System Statistics ===");
    Serial.printf("Version:      %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    Serial.printf("Streaming:    %s\n", streaming ? "ON" : "OFF");
    Serial.printf("SD logging:   %s\n", loggingEnabled ? "ON" : "OFF");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
    Serial.printf("Battery:      %.2fV\n", batV);
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("GPS:          %s\n", gpsStatus.c_str());
    Serial.printf("Satellites:   %s\n", satellitesStr.c_str());
    Serial.printf("HDOP:         %s\n", hdopStr.c_str());
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  // -------------------- SD COMMANDS --------------------
  else if (cmd == "sdinfo") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println("=== SD Card Info ===");
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    Serial.printf("Card size:  %llu MB\n", cardSize);
    Serial.printf("Total:      %llu MB\n", totalBytes);
    Serial.printf("Used:       %llu MB\n", usedBytes);
    Serial.printf("Free:       %llu MB\n", totalBytes - usedBytes);
    Serial.printf("Current file: %s\n", csvFileName.c_str());
  }

  else if (cmd == "sdlist") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
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
        Serial.printf("%s - %lu bytes\n", file.name(), (unsigned long)file.size());
        count++;
      }
      file = root.openNextFile();
    }
    Serial.printf("Total: %d files\n", count);
  }

  else if (cmd == "sdnew") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println("[SD] Creating new CSV file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    Serial.printf("[SD] ✓ New file created: %s\n", csvFileName.c_str());
    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  // -------------------- NETWORK COMMANDS --------------------
  else if (cmd == "netinfo") {
    Serial.println("=== Network Info ===");
    Serial.printf("Operator:     %s\n", networkOperator.c_str());
    Serial.printf("Technology:   %s\n", networkTech.c_str());
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("Registration: %s\n", registrationStatus.c_str());
    Serial.printf("PDP connected:%s\n", modem.isGprsConnected() ? "YES" : "NO");
    Serial.printf("Network conn: %s\n", modem.isNetworkConnected() ? "YES" : "NO");
  }

  else if (cmd == "csq") {
    int newCsq = modem.getSignalQuality();
    Serial.printf("[MODEM] Signal Quality: %d", newCsq);
    if (newCsq == 0) Serial.println(" (no signal)");
    else if (newCsq < 10) Serial.println(" (marginal)");
    else if (newCsq < 15) Serial.println(" (ok)");
    else if (newCsq < 20) Serial.println(" (good)");
    else Serial.println(" (excellent)");
  }

  // -------------------- SYSTEM COMMANDS --------------------
  else if (cmd == "sysinfo") {
    Serial.println("=== System Info ===");
    Serial.printf("Firmware:     %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Chip model:   %s\n", ESP.getChipModel());
    Serial.printf("Chip cores:   %d\n", ESP.getChipCores());
    Serial.printf("CPU freq:     %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size:   %lu MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  else if (cmd == "mem") {
    Serial.println("=== Memory Usage ===");
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Heap size:    %lu bytes\n", ESP.getHeapSize());
    Serial.printf("Min free heap:%lu bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Max alloc:    %lu bytes\n", ESP.getMaxAllocHeap());
  }

  else if (cmd == "reboot") {
    Serial.println("[SYSTEM] Rebooting in 2 seconds...");
    delay(2000);
    ESP.restart();
  }

  // -------------------- STREAMING COMMANDS --------------------
  else if (cmd == "start") {
    if (streaming) {
      Serial.println("[STREAM] Already running");
    } else {
      Serial.println("[STREAM] Starting via serial command...");
      streaming = true;
      loggingEnabled = SDOK;
      Serial.printf("[STREAM] ✓ Started (SD logging: %s)\n", loggingEnabled ? "ON" : "OFF");
    }
  }

  else if (cmd == "stop") {
    if (!streaming) {
      Serial.println("[STREAM] Already stopped");
    } else {
      Serial.println("[STREAM] Stopping...");
      streaming = false;
      loggingEnabled = false;
      Serial.println("[STREAM] ✓ Stopped");
    }
  }

  // -------------------- UNKNOWN COMMAND --------------------
  else {
    Serial.println("[CMD] Unknown command. Type 'help' for list.");
  }

  Serial.println();
}
