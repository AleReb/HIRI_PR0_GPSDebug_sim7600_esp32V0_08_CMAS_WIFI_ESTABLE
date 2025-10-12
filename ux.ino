// -------------------- Bitmap Icons --------------------
// Satellite icon 8x8, 1 bit/pixel, LSB first
static const unsigned char PROGMEM satelit_bitmap[8] = {
  0x06,
  0x46,
  0x36,
  0x38,
  0x1C,
  0xED,
  0xE1,
  0x06
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
  u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
  u8g2.drawGlyph(90, 9, 0x00FD);  // wifi
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(98, 9);
  u8g2.print(csq);

  // Battery
  drawBatteryDynamic(115, 3, batV);
}
