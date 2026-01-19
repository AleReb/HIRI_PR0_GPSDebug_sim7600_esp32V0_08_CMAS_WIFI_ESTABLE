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
    Serial.println(F("  sdclear     - Delete all files on SD (WARNING: irreversible!)"));

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

    Serial.println(F("\n[Configuration]"));
    Serial.println(F("  config              - Show all configuration"));
    Serial.println(F("  config sd/http/display/power - Show specific config"));
    Serial.println(F("  set sdauto on/off   - Mount SD on boot"));
    Serial.println(F("  set sdsave 3/60/600/1200 - SD save period (seconds)"));
    Serial.println(F("  set httpsend 3/60/600/1200 - HTTP send period (seconds)"));
    Serial.println(F("  set httptimeout 5-30 - HTTP timeout (seconds)"));
    Serial.println(F("  set oledoff on/off  - OLED auto-off"));
    Serial.println(F("  set oledtime 60/120/180 - OLED timeout (seconds)"));
    Serial.println(F("  set led on/off      - Enable NeoPixel"));
    Serial.println(F("  set ledbright 10/25/50/100 - LED brightness (%)"));
    Serial.println(F("  set autostart on/off - Autostart streaming on boot"));
    Serial.println(F("  set autowaitgps on/off - Wait GPS fix before start"));
    Serial.println(F("  set autogpsto 60-900 - GPS timeout (60s-15min, default: 600s)"));
    Serial.println(F("  set gnssmode 1/3/5/7/15 - GNSS mode (1=GPS, 3=GPS+GLO, 15=ALL)"));
    Serial.println(F("  configreset         - Reset to defaults"));
    Serial.println(F("  configsave          - Save config to flash"));

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

  else if (cmd == "sdclear") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println("[SD] ⚠ WARNING: This will delete ALL files on the SD card!");
    Serial.println("[SD] Type 'sdclear confirm' to proceed");
  }

  else if (cmd == "sdclear confirm") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    clearSDCard();
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

  // -------------------- CONFIGURATION COMMANDS --------------------
  else if (cmd == "config") {
    printConfig();
  }

  else if (cmd == "config sd") {
    Serial.println("=== SD Configuration ===");
    Serial.printf("Auto-mount on boot: %s\n", config.sdAutoMount ? "ON" : "OFF");
    Serial.printf("Save period:        %lu ms (%lu s)\n", config.sdSavePeriod, config.sdSavePeriod / 1000);
  }

  else if (cmd == "config http") {
    Serial.println("=== HTTP Configuration ===");
    Serial.printf("Send period: %lu ms (%lu s)\n", config.httpSendPeriod, config.httpSendPeriod / 1000);
    Serial.printf("Timeout:     %u seconds\n", config.httpTimeout);
  }

  else if (cmd == "config display") {
    Serial.println("=== Display Configuration ===");
    Serial.printf("Auto-off: %s\n", config.oledAutoOff ? "ON" : "OFF");
    Serial.printf("Timeout:  %lu ms (%lu s)\n", config.oledTimeout, config.oledTimeout / 1000);
  }

  else if (cmd == "config power") {
    Serial.println("=== Power/LED Configuration ===");
    Serial.printf("NeoPixel enabled: %s\n", config.ledEnabled ? "YES" : "NO");
    Serial.printf("Brightness:       %u%%\n", config.ledBrightness);
  }

  else if (cmd.startsWith("set ")) {
    String param = cmd.substring(4);
    param.trim();

    // SD auto-mount
    if (param == "sdauto on") {
      config.sdAutoMount = true;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: ON (will mount on next boot)");
    }
    else if (param == "sdauto off") {
      config.sdAutoMount = false;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: OFF");
    }

    // SD save period
    else if (param == "sdsave 3") {
      config.sdSavePeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 3 seconds");
    }
    else if (param == "sdsave 60") {
      config.sdSavePeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 60 seconds");
    }
    else if (param == "sdsave 600") {
      config.sdSavePeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 600 seconds (10 min)");
    }
    else if (param == "sdsave 1200") {
      config.sdSavePeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 1200 seconds (20 min)");
    }

    // HTTP send period
    else if (param == "httpsend 3") {
      config.httpSendPeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 3 seconds");
    }
    else if (param == "httpsend 60") {
      config.httpSendPeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 60 seconds");
    }
    else if (param == "httpsend 600") {
      config.httpSendPeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 600 seconds (10 min)");
    }
    else if (param == "httpsend 1200") {
      config.httpSendPeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 1200 seconds (20 min)");
    }

    // HTTP timeout
    else if (param.startsWith("httptimeout ")) {
      int timeout = param.substring(12).toInt();
      if (timeout >= 5 && timeout <= 30) {
        config.httpTimeout = timeout;
        saveConfig();
        Serial.printf("[CONFIG] HTTP timeout: %d seconds\n", timeout);
      } else {
        Serial.println("[CONFIG] ✗ Timeout must be 5-30 seconds");
      }
    }

    // OLED auto-off
    else if (param == "oledoff on") {
      config.oledAutoOff = true;
      saveConfig();
      Serial.println("[CONFIG] OLED auto-off: ON");
    }
    else if (param == "oledoff off") {
      config.oledAutoOff = false;
      saveConfig();
      u8g2.setPowerSave(0);  // Encender display inmediatamente
      Serial.println("[CONFIG] OLED auto-off: OFF (always on)");
    }

    // OLED timeout
    else if (param == "oledtime 60") {
      config.oledTimeout = 60000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 60 seconds");
    }
    else if (param == "oledtime 120") {
      config.oledTimeout = 120000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 120 seconds (2 min)");
    }
    else if (param == "oledtime 180") {
      config.oledTimeout = 180000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 180 seconds (3 min)");
    }

    // LED enable/disable
    else if (param == "led on") {
      config.ledEnabled = true;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: ON");
    }
    else if (param == "led off") {
      config.ledEnabled = false;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: OFF");
    }

    // LED brightness
    else if (param == "ledbright 10") {
      config.ledBrightness = 10;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 10%");
    }
    else if (param == "ledbright 25") {
      config.ledBrightness = 25;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 25%");
    }
    else if (param == "ledbright 50") {
      config.ledBrightness = 50;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 50%");
    }
    else if (param == "ledbright 100") {
      config.ledBrightness = 100;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 100%");
    }

    // Autostart
    else if (param == "autostart on") {
      config.autostart = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart: ON (will start on next boot)");
    }
    else if (param == "autostart off") {
      config.autostart = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart: OFF");
    }

    // Autostart wait GPS
    else if (param == "autowaitgps on") {
      config.autostartWaitGps = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: ON (will wait for fix)");
    }
    else if (param == "autowaitgps off") {
      config.autostartWaitGps = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: OFF");
    }

    // Autostart GPS timeout
    else if (param.startsWith("autogpsto ")) {
      int val = param.substring(10).toInt();
      if (val >= 60 && val <= 900) {
        config.autostartGpsTimeout = val;
        saveConfig();
        Serial.printf("[CONFIG] Autostart GPS timeout: %u seconds (%u min)\n", val, val / 60);
      } else {
        Serial.println("[CONFIG] ✗ Invalid timeout (must be 60-900 seconds / 1-15 min)");
      }
    }

    // GNSS mode
    else if (param.startsWith("gnssmode ")) {
      int val = param.substring(9).toInt();
      if (val == 1 || val == 3 || val == 5 || val == 7 || val == 15) {
        config.gnssMode = val;
        saveConfig();
        Serial.print("[CONFIG] GNSS mode: ");
        Serial.print(val);
        Serial.print(" ");
        switch(val) {
          case 1:  Serial.println("(GPS only)"); break;
          case 3:  Serial.println("(GPS + GLONASS)"); break;
          case 5:  Serial.println("(GPS + BEIDOU)"); break;
          case 7:  Serial.println("(GPS + GLONASS + BEIDOU)"); break;
          case 15: Serial.println("(ALL: GPS + GLONASS + GALILEO + BEIDOU)"); break;
        }
        Serial.println("[CONFIG] ⚠ Reboot required for GNSS mode change to take effect");
      } else {
        Serial.println("[CONFIG] ✗ Invalid mode (valid: 1, 3, 5, 7, 15)");
      }
    }

    else {
      Serial.println("[CONFIG] ✗ Unknown parameter. Type 'help' for list.");
    }
  }

  else if (cmd == "configreset") {
    Serial.println("[CONFIG] Resetting to defaults...");
    configSetDefaults();
    saveConfig();
    applyLEDConfig();
    Serial.println("[CONFIG] ✓ Reset complete. Reboot to apply all changes.");
  }

  else if (cmd == "configsave") {
    saveConfig();
  }

  // -------------------- UNKNOWN COMMAND --------------------
  else {
    Serial.println("[CMD] Unknown command. Type 'help' for list.");
  }

  Serial.println();
}
