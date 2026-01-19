// -------------------- Bitmap Icons --------------------
// Satellite icon 8x8, 1 bit/pixel, LSB first
static const unsigned char PROGMEM satelit_bitmap[8] = {
  0x06,
  0x6E,
  0x74,
  0x38,
  0x58,
  0xE5,
  0xC1,
  0x07
};


// ——— Cálculo % batería ———
int calcBatteryPercent(float v) {
  if (v >= 4.1) return 100;  // 4.2V = 100%
  if (v <= 3.3) return 0;    // 3.4V = 0% (límite operacional ESP32)
  return (int)((v - 3.4) / 0.8 * 100);  // Rango: 3.4V-4.2V = 0.8V
}

// ——— UI ———
void drawBatteryDynamic(int xPos, int yPos, float v) {
  // Validar voltaje para evitar valores inválidos
  if (isnan(v) || v < 0 || v > 5.0) v = 3.4;

  int pct = calcBatteryPercent(v);
  // Limitar porcentaje entre 0-100
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  float frac = pct / 100.0;
  const uint8_t w = 9, h = 6, tip = 2;
  // Posición ajustable
  uint8_t x = xPos;
  uint8_t y = yPos;
  // Contorno y terminal
  u8g2.drawFrame(x, y, w, h);
  u8g2.drawBox(x + w, y + 2, tip, h - 4);

  // Nivel interno o icono de carga crítica
  if (pct == 0) {
    // si es 0 ponemos la C
    u8g2.setFont(u8g2_font_5x7_tf);
    char s = 'C';
    u8g2.setCursor(x - 6, y + h);
    u8g2.print(s);

    u8g2.drawLine(x + 4, y + 1, x + 2, y + 3);
    u8g2.drawLine(x + 2, y + 3, x + 5, y + 3);
    u8g2.drawLine(x + 5, y + 3, x + 3, y + 5);
  } else {
    // Calcular ancho del relleno y limitar al tamaño del marco
    uint8_t fillWidth = (uint8_t)((w - 2) * frac);
    if (fillWidth > (w - 2)) fillWidth = (w - 2);
    if (fillWidth > 0) {
      u8g2.drawBox(x + 1, y + 1, fillWidth, h - 2);
    }
  }
}


void drawHeader() {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 9, hhmmss);

  // Satellite icon (custom bitmap)
  if(gpsStatus == "Fix") {
    u8g2.drawXBMP(65, 1, 8, 8, satelit_bitmap);  // Bitmap del satélite
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(73, 9);
    u8g2.print(satellitesStr);
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(65, 9, 0x0118);  // Error icon
  }

  // WiFi/Signal icon
  if (networkError) {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(90, 9, 0x0118);  // Error icon at signal location
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(90, 9, 0x00FD);  // wifi
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(98, 9);
    u8g2.print(csq);
  }

  // Battery
  drawBatteryDynamic(115, 3, batV);
}

// ==================== Display State Machine Functions ====================

// Pantalla 1: Datos CSV guardados en SD
void displaySDSaved() {
  u8g2.setFont(u8g2_font_5x7_tf);  // Use smaller font
  u8g2.setCursor(0, 10);
  u8g2.print("SD #" + String(sdSaveCounter) + " GUARDADO:");

  // Dividir CSV en líneas de ~25 caracteres (128px / 5px por carácter)
  int lineHeight = 9;         // Adjusted for 5x7 font
  int startY = 20;            // Adjusted for 5x7 font
  int maxCharsPerLine = 25;   // Adjusted for 5x7 font

  int csvLen = lastSavedCSVLine.length();
  int numLines = 0;
  int startPos = 0;

  while (startPos < csvLen && numLines < 5) {
    int endPos = startPos + maxCharsPerLine;
    if (endPos > csvLen) endPos = csvLen;

    String segment = lastSavedCSVLine.substring(startPos, endPos);
    u8g2.setCursor(0, startY + (numLines * lineHeight));
    u8g2.print(segment);

    startPos = endPos;
    numLines++;
  }
}

// Pantalla 3: Datos normales en tiempo real
void displayNormal() {
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  snprintf(dmy, sizeof(dmy), "%02d/%02d/%04d", now.day(), now.month(), now.year());

  drawHeader();
  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.setCursor(0, 16);
  u8g2.print("Lat:" + gpsLat + " HDOP:" + hdopStr);

  u8g2.setCursor(0, 24);
  u8g2.print("Lon:" + gpsLon + " ");
  u8g2.print(dmy);

  u8g2.setCursor(0, 32);
  u8g2.print("Alt: " + gpsAlt + "m Spd: " + gpsSpeedKmh + "km/h");

  u8g2.setCursor(0, 40);
  u8g2.print("PM2.5:" + String(PM25) + " PM10:" + String(PM10) + " Hpm:" + String(pmsHum, 1));

  u8g2.setCursor(0, 48);
  if (SHT31OK == true) {
    u8g2.print("Ti:" + String(rtcTempC, 1) + " Tpm:" + String(pmsTempC, 1) + " Te:" + String(tempsht31));
  } else if (DEVICE_ID_STR == "06") {
    u8g2.print("Ti:" + String(rtcTempC, 1) + " Tpm:" + String(pmsTempC, 1) + " PM100:" + String(SDS198PM100));
  } else {
    u8g2.print("Ti:" + String(rtcTempC, 1) + " Tpm:" + String(pmsTempC, 1));
  }
  

  u8g2.setCursor(0, 56);
  unsigned long totalSeconds = (unsigned long)(millis() / 1000UL);
  unsigned int seconds = totalSeconds % 60;
  unsigned int minutes = (totalSeconds / 60) % 60;
  unsigned int hours = (totalSeconds / 3600);
  u8g2.print("Bat:" + String(batV, 2) + " CSQ:" + String(csq) + "  ON" + String(hours) + "-" + String(minutes) + "-" + String(seconds));

  u8g2.setCursor(0, 64);
  u8g2.print(streaming ? (loggingEnabled ? "SENT:ON+SD " : "SENT:ON   ") : "SENT:OFF  ");
  u8g2.print(" " + String(sdSaveCounter) + "/" + String(sendCounter) + " ID" + String(DEVICE_ID_STR));
}

// Actualizar máquina de estados del display
void updateDisplayStateMachine() {
  uint32_t elapsed = millis() - displayStateStartTime;

  // Máquina de estados con transiciones automáticas
  switch (displayState) {
    case DISP_SD_SAVED:
      // Después de 700ms, volver a pantalla normal
      if (elapsed >= SD_SAVE_DISPLAY_MS) {
        displayState = DISP_NORMAL;
        displayStateStartTime = millis();
        Serial.println("[DISPLAY] State: SD_SAVED -> NORMAL");
      }
      break;

    case DISP_NORMAL:
      // Se mantiene en NORMAL hasta que se active otra transición
      break;
  }
}

// Renderizar la pantalla según el estado actual
void renderDisplay() {
  u8g2.clearBuffer();

  switch (displayState) {
    case DISP_SD_SAVED:
      displaySDSaved();
      break;

    case DISP_NORMAL:
      displayNormal();
      break;
  }

  u8g2.sendBuffer();
}
