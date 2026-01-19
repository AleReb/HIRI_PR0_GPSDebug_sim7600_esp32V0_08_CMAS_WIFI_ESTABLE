/*
 * ESP32 + SIM7600: Robust GNSS + PMS + HTTP GET + XTRA (3-day) + (RTC opcional) + SD-on-start
 * - BTN2: STOP ALL/INIT WIFI
 * - BTN1: STOP/START  (stop streaming and logging) (init SD once, stream every 3 s, log to SD)
 * - PMS: non-blocking parser (ring buffer), prints every ~1s; warns if no frames >5s
 * - GNSS: bring-up (AT+CGPS=1; fallback +CGNSSPWR=1), NMEA 1 Hz, async debug, watchdog (hot/warm)
 * - XTRA: enable + initial download; refresh every 3 days (modem internal)
 * - RTC: sync DS3231 from AT+CCLK? (no se re-inicializa en loop); OLED muestra hora junto a STREAM (si lo usas)
 * - Temperature source: DS3231 internal sensor (rtc.getTemperature())
 * - NeoPixel: smooth gradient by PM2.5 with breakpoints 15/25/50 µg/m³ (50% brightness)
 * - HTTP: Async state machine (non-blocking transmission)
 * - Battery: Non-blocking EMA filter sampling
 * Author: Alejandro Rebolledo | License: Creative Comons 4.0
 * se esta agregando un sensor para probar SDS198
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

// -------------------- VERSION --------------------
String VERSION = "0.3.9.8";  // HTTP robustness + SD save counter display+config, test mosfet en pin 33/al parecer que no funciono

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 4096  // Increased from 2048 for better stability
#define SerialAT Serial1
//#define DUMP_AT_COMMANDS

// -------------------- Pins --------------------
#define UART_BAUD 115200
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define MODEM_DTR 32
#define MODEM_FLIGHT 25

// PMS pins (keep your wiring)
#define pms_TX 5
#define pms_RX 18
// Define los pines para la comunicación serie por software.
#define SDS198RX_PIN 39 // Pin RX del ESP32 conectado al TX del sensor.
#define SDS198TX_PIN 0  // Pin TX del ESP32 (no se usa para recibir datos del sensor).

#define BAT_PIN 35
#define NEOPIX_PIN 12
#define NUMPIXELS 1

#define BUTTON_PIN_1 19  // STOP
#define BUTTON_PIN_2 23  // START
// POWER PIN
#define POWER_PIN 33 // este pin es el libre 
// -------------------- OLED --------------------
#include <Wire.h>
#include <U8g2lib.h>
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// -------------------- PMS y SDS198 --------------------
#include <SoftwareSerial.h>
SoftwareSerial pms(pms_TX, pms_RX);  // (RX, TX) //Plantower PMS5003
uint16_t PM1 = 0, PM25 = 0, PM10 = 0;
float pmsTempC = 0, pmsHum = 0;  // PMS temperature and RH (if sensor variant supports it)

// Crea una instancia de SoftwareSerial para la comunicación con el sensor. SDS198
//SoftwareSerial sdsSerial(SDS198RX_PIN, SDS198TX_PIN); // RX, TX SDS198 se usa solo rx y a demas se implementa hardware serial2
#define sdsSerial Serial2
// Define los bytes de la trama de datos del sensor.
const byte HEADER = 0xAA; // Cabecera de la trama.
const byte CMD    = 0xCF; // Byte de comando.
const byte TAIL   = 0xAB; // Cola de la trama.
int SDS198PM100; //variable global para guardar el valor de PM100 del SDS198
// ring buffer
static uint8_t pmsBuf[64];
static size_t pmsHead = 0;
static uint32_t lastPmsPrint = 0;
int PrintTime = 3000;
static uint32_t lastPmsSeen = 0;

// -------------------- Buttons --------------------
volatile bool btn1Flag = false, btn2Flag = false;

// Latch/arming
volatile bool btn1Armed = true;
volatile bool btn2Armed = true;

// Para verificar liberación estable
uint32_t btn1ReleaseStart = 0;
uint32_t btn2ReleaseStart = 0;

const uint32_t RELEASE_MS = 50;  // estabilidad para rearmar

void IRAM_ATTR isrBtn1() {
  if (btn1Armed) {      // solo si estaba armado
    btn1Flag = true;    // señalamos evento
    btn1Armed = false;  // desarmamos hasta liberar
  }
}
void IRAM_ATTR isrBtn2() {
  if (btn2Armed) {
    btn2Flag = true;
    btn2Armed = false;
  }
}


// -------------------- RTC --------------------
#include <time.h>
#include <RTClib.h>
RTC_DS3231 rtc;
Preferences prefs;
bool rtcOK = false;                  // no re-init en loop; solo se usa si quedó OK en setup
const long RTC_SYNC_THRESHOLD = 20;  // seconds
float rtcTempC = NAN;                // DS3231 internal temperature
// --- RTC sanity & retry ---
static const time_t MIN_VALID_EPOCH = 1672531200;
static const uint32_t MAX_FUTURE_DRIFT = 5UL * 365UL * 24UL * 3600UL;  // 5 años vs compile
static const uint32_t RTC_PROBE_PERIOD_MS = 10000;                     // reintento cada 10 s
static bool rtcNetSyncPending = false;                                 // si true, reintenta CCLK en loop (solo una ventana)
static uint32_t rtcNextProbeMs = 0;
// --- RTC modem sync (máximo 3 veces) ---
static uint8_t rtcModemSyncCount = 0;
static uint32_t lastModemSyncAttempt = 0;
const uint32_t MODEM_SYNC_INTERVAL_MS = 600000;  // 10 minutos entre intentos
const uint8_t MAX_MODEM_SYNC_COUNT = 3;
// -------------------- RTC HELPERS --------------------
char hhmmss[9];  // The size needs to be big enough for "hh:mm:seconds\0" (9 characters).
char dmy[11];    // The size needs to be big enough for "dd/mm/yyyy\0" (11 characters).

// -------------------- SD (lazy init on START) --------------------
#include "SD.h"
#include "SPI.h"
#include "FS.h"
SPIClass spiSD(HSPI);
bool SDOK = false;
bool loggingEnabled = false;
String csvFileName = "";
String logFilePath = "";           // Error log file (will be initialized with device ID in setup)
String failedTxPath = "";          // Failed transmissions log (will be initialized with device ID in setup)
String deviceID = "/HIRIP";        // nombre base de archivo (legacy, no se usa)
static uint8_t lastDayLogged = 0;  // Para detectar cambio de día
const int SD_SCLK = 14, SD_MISO = 2, SD_MOSI = 15, SD_CS = 13;

// -------------------- TinyGsm --------------------
#include <TinyGsmClient.h>
#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

// -------------------- NeoPixel --------------------
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);

// -------------------- SHT31 --------------------
#include "Adafruit_SHT31.h"
bool enableHeater = false;
bool SHT31OK = false;
float tempsht31;
float humsht31;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
// ------------------------ SERVER SETUP AND WIFI--------------------
#include <WiFi.h>
#include <WebServer.h>
#include <ctype.h>  // isalnum()
WebServer server(80);
File uploadFile;

// WiFi SSID dinámico basado en DEVICE_ID_STR (se genera en setup)
String AP_SSID_STR = "";
const char* AP_PASSWORD = "12345678";

bool wifiModeActive = false;
String apIpStr = "0.0.0.0";

// -------------------- APN --------------------
const char apn[] = "gigsky-02";
const char gprsUser[] = "";
const char gprsPass[] = "";

// -------------------- Streaming --------------------
const uint32_t STREAM_PERIOD_MS = 3000;   // HTTP transmission every 3s
const uint32_t SD_SAVE_PERIOD_MS = 3000;  // SD logging every 3s (aligned with GPS rate)
bool streaming = false;
uint32_t lastStream = 0;
uint32_t lastSdSave = 0;
bool wasStreamingBeforeBoot = false;  // Track if was streaming before reboot (for autostart logic)



// SD save feedback
const uint32_t SD_SAVE_DISPLAY_MS = 200;  // Mostrar datos guardados por 0.2 segundos
String lastSavedCSVLine = "";             // Línea CSV completa guardada en SD

// ==================== Display State Machine ====================
enum DisplayState {
  DISP_NORMAL,
  DISP_SD_SAVED
};

volatile DisplayState displayState = DISP_NORMAL;
volatile uint32_t displayStateStartTime = 0;

// -------------------- Watchdog --------------------
#define WDT_TIMEOUT 60  // 25 segundos (para cubrir transmisión de ~3s + margen)
String rebootReason = "Unknown";
String networkOperator = "N/A";
String networkTech = "N/A";
String signalQuality = "0";
String registrationStatus = "N/A";


// -------------------- NMEA / GPS globals --------------------

String gpsLat = "NaN", gpsLon = "NaN";
String gpsTime = "N/A", gpsDate = "N/A";
String satellitesStr = "0", hdopStr = "N/A", gpsAlt = "N/A";
String gpsStatus = "NoFix";
String gpsSpeedKmh = "0.0";

// ---------- GNSS debug metrics ----------
uint32_t lastNmeaSeenMs = 0;   // última vez que vimos una línea $NMEA
uint32_t gnssFixFirstMs = 0;   // TTFF (ms desde gnssStartMs hasta primer fix)
bool gnssFixReported = false;  // no se usa para imprimir, pero lo reseteamos en bring-up

// GSA (used sats / DOPs / fix type)
uint8_t gsaFixType = 0;  // 1=NoFix, 2=2D, 3=3D
uint8_t gsaSatsUsed = 0;
float gsaPdop = NAN, gsaHdop2 = NAN, gsaVdop = NAN;

// GSV (sats in view / SNR promedio y máximo, por “ventana” de mensajes)
uint16_t gsvSatsInView = 0;
float gsvSnrAvg = 0, gsvSnrMax = 0;
uint32_t gsvLastMs = 0;
static float gsvSnrAcc = 0;  // acumulador por secuencia
static int gsvSnrCnt = 0;

// --- GNSS diag ---
static uint32_t lastNmeaMs = 0;   // último NMEA visto (cualquier $)
static uint32_t lastGgaMs = 0;    // última GGA vista
static uint32_t lastFixMs = 0;    // último fix (cuando GGA fixQ>=1)
static uint16_t nmeaCount1s = 0;  // contador en la ventana de 1s
static uint16_t nmeaRate = 0;     // Hz medido
static uint32_t nmeaRefMs = 0;    // referencia para ventana 1s
static uint8_t fixQLast = 0;      // último fix quality de GGA
static bool ttffPrinted = false;  // para imprimir TTFF una sola vez

// -------------------- Signal / battery --------------------
int csq = 0;
bool networkError = false;
float batV = 0.0f;
const int NUM_SAMPLES = 30;
const float alpha = 0.8;  // 0.5~0.2 = respuesta rápida, 0.05 = suave, 0.01 = muy lento
float batteryVoltageAverage = 0;
bool hasRed = false;
// CSQ timing (simple flag-based approach)
const uint32_t CSQ_INTERVAL_NORMAL_MS = 60000 * 20;        // 20 minutos normal
const uint32_t CSQ_INTERVAL_TRANSMITTING_MS = 60000 * 60;  // 1 hora durante transmisión
bool isCurrentlyTransmitting = false;                      // Bandera simple

// Variables para muestreo no bloqueante de batería
static uint32_t lastBatSample = 0;
static float batSampleSum = 0;
static int batSampleCount = 0;
const uint32_t BAT_SAMPLE_INTERVAL_MS = 5;  // Tomar 1 muestra cada 5ms

// -------------------- Measurements API (real endpoint) --------------------
const char* API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
// Must match backend exactly:
//const char* IDS_SENSORES = "401,401,401,401,401,402,402,402,402,402,403,404,405,405,405,405,405";  //sensor 1
//const char* IDS_SENSORES = "406,406,406,406,406,407,407,407,407,407,408,409,410,410,410,410,410";  //sensor 2
//const char* IDS_SENSORES = "415,415,415,415,415,416,416,416,416,416,417,418,419,419,419,419,419,420,420";  //sensor 3 //tiene sht31
//const char* IDS_SENSORES = "448,448,448,448,448,449,449,449,449,449,450,451,452,452,452,452,452,453,453";  //sensor 4   // tiene sds198 
//const char* IDS_SENSORES = "454,454,454,454,454,455,455,455,455,455,456,457,458,458,458,458,458,459,459";  //sensor 5   // tiene sds198 
//const char* IDS_SENSORES = "460,460,460,460,460,461,461,461,461,461,462,463,464,464,464,464,464,467";  //sensor 6   // tiene un sensor SDS198 
//const char* IDS_SENSORES = "468,468,468,468,468,469,469,469,469,469,470,471,472,472,472,472,472";  //sensor 7
//const char* IDS_SENSORES = "478,478,478,478,478,479,479,479,479,479,480,481,482,482,482,482,482,483,483";  //sensor 9 no usado
const char* IDS_SENSORES = "484,484,484,484,484,485,485,485,485,485,486,487,488,488,488,488,488,489,489";  //sensor 10 no usado

const char* IDS_VARIABLES = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44";           //los mismos datos pero cambia el ID-sensor cambia el numero de sensores
const char* IDS_VARIABLESSHT31 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,3,6";  //los mismos datos pero caria el ID-sensor cambia el numero de sensores
const char* IDS_VARIABLES06 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,51";  //los mismos datos pero caria el ID-sensor cambia el numero de sensores
                                                                                     // ur format helpers
String valores;
String url;
// ID string for variable
const char* DEVICE_ID_STR = "10";   // el 06 gatilla accioes especiales como el sensor sds198
// -------------------- Transmission counters --------------------
static uint32_t sendCounter = 0;    // Transmisiones HTTP exitosas
static uint32_t sdSaveCounter = 0;  // Total de guardados en SD (intentos)

// -------------------- XTRA / AGNSS --------------------
static const uint32_t XTRA_REFRESH_MS = 3UL * 24UL * 60UL * 60UL * 1000UL;  // 3 days
uint32_t lastXtraDownload = 0;
bool xtraSupported = false;
bool xtraLastOk = false;

// -------------------- AT session multiplexer --------------------
struct AtSession {
  bool active = false;
  String expect1;  // "OK" or a URC we wait for
  String expect2;  // "ERROR"
  String resp;
  uint32_t deadline = 0;
} at;

void atBegin(const String& cmd, const String& expect1, const String& expect2, uint32_t timeout_ms) {
  modem.stream.print("AT");
  modem.stream.println(cmd);
  at.active = true;
  at.expect1 = expect1;
  at.expect2 = expect2;
  at.resp = "";
  at.deadline = millis() + timeout_ms;
}

bool atTick(bool& done, bool& ok) {
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    // NMEA: never block, always parse
    if (line.charAt(0) == '$') {
      parseNMEA(line);
      continue;
    }

    // AT/URC
    if (!at.active) { /* Optional: Serial.println("[URC] " + line); */
      continue;
    }
    at.resp += line;
    at.resp += "\n";
    if (at.expect1.length() && line.indexOf(at.expect1) >= 0) {
      done = true;
      ok = true;
      at.active = false;
      return true;
    }
    if (at.expect2.length() && line.indexOf(at.expect2) >= 0) {
      done = true;
      ok = (at.expect2 == "OK");
      at.active = false;
      return true;
    }
  }
  if (at.active && millis() > at.deadline) {
    done = true;
    ok = false;
    at.active = false;
    return true;
  }
  done = false;
  ok = false;
  return false;
}

bool atRun(const String& cmd, const String& expect1 = "OK", const String& expect2 = "ERROR", uint32_t timeout_ms = 8000) {
  atBegin(cmd, expect1, expect2, timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok)) break;
    delay(1);
  }
  return ok;
}

bool sendAtSync(const String& cmd, String& resp, uint32_t timeout_ms = 4000) {
  atBegin(cmd, "OK", "ERROR", timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok)) break;
    delay(1);
  }
  resp = at.resp;
  return ok;
}

// -------------------- OLED helpers --------------------
void oledStatus(const String& l1, const String& l2 = "", const String& l3 = "", const String& l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 12);
  u8g2.print(l1);
  u8g2.setCursor(0, 26);
  u8g2.print(l2);
  u8g2.setCursor(0, 40);
  u8g2.print(l3);
  u8g2.setCursor(0, 54);
  u8g2.print(l4);
  u8g2.sendBuffer();
}
//------animacon--------
// Variables para la animación
int logoXOffset = -25;  // Logo entra desde la izquierda
int hiriXOffset = 128;  // "HIRI" entra desde la derecha
int proYOffset = 64;    // "PRO" entra desde abajo

// Posiciones finales ajustables
const int LOGO_FINAL_X = 4;   // Posición final X del logo
const int HIRI_FINAL_X = 48;  // Posición final X de HIRI
const int PRO_FINAL_X = 106;  // Posición final X de PRO (ajústalo aquí)
const int HIRI_FINAL_Y = 44;  // Posición final Y de HIRI
const int PRO_FINAL_Y = 52;   // Posición final Y de PRO (ajústalo aquí)
// -------------------- OLED Auto-Off --------------------
static uint32_t lastOledActivity = 0;  // Timer for OLED auto-off

// -------------------- Configuration System --------------------
struct SystemConfig {
  // SD Card
  bool sdAutoMount;       // Montar SD en boot (default: false)
  uint32_t sdSavePeriod;  // Período guardado SD en ms (default: 3000)

  // HTTP Transmission
  uint32_t httpSendPeriod;  // Período transmisión en ms (default: 3000)
  uint16_t httpTimeout;     // Timeout HTTP en segundos (default: 15)

  // Display OLED
  bool oledAutoOff;      // Apagar OLED automáticamente (default: false)
  uint32_t oledTimeout;  // Timeout en ms (default: 120000 = 2min)

  // Power Management
  bool ledEnabled;        // NeoPixel habilitado (default: true)
  uint8_t ledBrightness;  // Brillo LED: 10, 25, 50, 100 (default: 50%)

  // Autostart
  bool autostart;                // Iniciar streaming/logging al encender (default: false)
  bool autostartWaitGps;         // Esperar GPS fix antes de iniciar (default: false)
  uint16_t autostartGpsTimeout;  // Timeout GPS en segundos (default: 600 = 10min)

  // GNSS Mode
  uint8_t gnssMode;  // Modo GNSS: 1=GPS, 3=GPS+GLO, 5=GPS+BDS, 7=GPS+GLO+BDS, 15=ALL (default: 15)
};

SystemConfig config;

// Forward declarations for config functions
void loadConfig();
void saveConfig();
void configSetDefaults();
void printConfig();
void printSDInfo();
void printSDFileList();
void applyLEDConfig();
void clearSDCard();

// Forward declarations for display functions
void updateDisplayStateMachine();
void renderDisplay();

// -------------------- GNSS variables --------------------
uint32_t gnssStartMs = 0;
bool haveFix = false;

// GNSS debug state machine
enum GnssDbgState { GNSS_DBG_IDLE,
                    GNSS_DBG_ASK_STATUS,
                    GNSS_DBG_WAIT_STATUS,
                    GNSS_DBG_ASK_INFO,
                    GNSS_DBG_WAIT_INFO };
GnssDbgState gnssDbgState = GNSS_DBG_IDLE;
uint32_t gnssDbgNextAt = 0;


void logError(const char* errorType, const String& errorCode, const String& rawResponse) {
  if (!SDOK) return;

  DateTime now = rtc.now();
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());

  String line = String(buf) + "," + String(errorType) + "," + errorCode + ",\""
                + rawResponse + "\"," + networkOperator + "," + networkTech + ","
                + signalQuality + "," + registrationStatus + ","
                + String(batV, 2) + "," + String(millis() / 1000) + "\r\n";

  File f = SD.open(logFilePath.c_str(), FILE_APPEND);
  if (f) {
    f.print(line);
    f.close();
  }

  Serial.print("[ERROR_LOG] ");
  Serial.println(line);
}

void updateNetworkInfo() {
  String resp;
  if (sendAtSync("+COPS?", resp, 3000)) {
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
      int start = resp.indexOf('"', idx);
      int end = resp.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        networkOperator = resp.substring(start + 1, end);
      }
    }
  }

  if (sendAtSync("+COPS?", resp, 3000)) {
    if (resp.indexOf(",7") >= 0) networkTech = "LTE";
    else if (resp.indexOf(",2") >= 0) networkTech = "3G";
    else if (resp.indexOf(",0") >= 0) networkTech = "2G";
    else networkTech = "Unknown";
  }

  signalQuality = String(csq);

  if (sendAtSync("+CREG?", resp, 2000)) {
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) registrationStatus = "Registered";
    else if (resp.indexOf(",2") >= 0) registrationStatus = "Searching";
    else registrationStatus = "NotRegistered";
  }
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN,HIGH);
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 100, 50));
  pixels.show();

  Serial.println();

  // Check reboot reason
  checkRebootReason();
  Serial.println("[BOOT] Reboot reason: " + rebootReason);

  // Restore persistent state after reboot
  prefs.begin("system", false);
  sendCounter = prefs.getUInt("sendCnt", 0);
  sdSaveCounter = prefs.getUInt("sdCnt", 0);
  csvFileName = prefs.getString("csvFile", "");
  bool wasStreaming = prefs.getBool("streaming", false);
  prefs.end();

  // Store in global variable for autostart logic
  wasStreamingBeforeBoot = wasStreaming;

  if (rebootReason.indexOf("Watchdog") >= 0 || rebootReason == "Panic") {
    Serial.println("[BOOT] Recovered from " + rebootReason);
    Serial.println("[BOOT] Send counter (HTTP success): " + String(sendCounter));
    Serial.println("[BOOT] SD save counter (attempts): " + String(sdSaveCounter));
    Serial.println("[BOOT] CSV file: " + csvFileName);

    // Auto-restart streaming if was active
    if (wasStreaming && csvFileName.length() > 0) {
      // Initialize SD immediately
      spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
      SDOK = SD.begin(SD_CS, spiSD);

      if (SDOK) {
        // Check if file still exists, if not create new one
        if (!SD.exists(csvFileName.c_str())) {
          Serial.println("[BOOT] CSV file missing, creating new one");
          csvFileName = generateCSVFileName();
          writeCSVHeader();
          prefs.begin("system", false);
          prefs.putString("csvFile", csvFileName);
          prefs.end();
        } else {
          Serial.println("[BOOT] Resuming with existing CSV: " + csvFileName);
        }

        // Create error log if doesn't exist
        writeErrorLogHeader();

        streaming = true;
        loggingEnabled = true;
        Serial.println("[BOOT] Auto-resumed streaming + SD logging");
      } else {
        Serial.println("[BOOT] SD init failed, streaming disabled");
        streaming = false;
        loggingEnabled = false;
      }
    }
  }

  Serial.println("[BOOT] SIM7600 GNSS+PMS (robust, async debug, SD-on-start)");
  Serial.println("[BOOT] FW VERSION: " + VERSION);

  // -------- Load Configuration --------
  loadConfig();
  applyLEDConfig();  // Apply LED brightness/enable settings

  // -------- Initialize SD file paths with device ID --------
  logFilePath = String("/errors_h") + String(DEVICE_ID_STR) + String(".csv");
  failedTxPath = String("/failed_h") + String(DEVICE_ID_STR) + String(".csv");
  Serial.println("[SD] Error log file: " + logFilePath);
  Serial.println("[SD] Failed TX file: " + failedTxPath);

  // -------- SD Auto-Mount (if enabled) --------
  if (config.sdAutoMount && !SDOK) {
    Serial.println("[SD] Auto-mount enabled, trying to mount...");
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);

    if (SDOK) {
      Serial.println("[SD] ✓ Mounted successfully");
      printSDInfo();      // Show card size, usage
      printSDFileList();  // Show all files

      // Create error log headers if needed
      writeErrorLogHeader();

      // If no CSV file exists yet, create one
      if (csvFileName.length() == 0) {
        csvFileName = generateCSVFileName();
        writeCSVHeader();
        prefs.begin("system", false);
        prefs.putString("csvFile", csvFileName);
        prefs.end();
        Serial.println("[SD] Created initial CSV file: " + csvFileName);
      }
    } else {
      Serial.println("[SD] ✗ Mount failed (check hardware/wiring)");
    }
  } else if (!config.sdAutoMount) {
    Serial.println("[SD] Auto-mount disabled (will mount on START button or streaming)");
  }

  // Generar SSID dinámico basado en DEVICE_ID_STR
  AP_SSID_STR = "HIRIPRO_" + String(DEVICE_ID_STR);
  Serial.println("[BOOT] WiFi SSID: " + AP_SSID_STR);

  u8g2.begin();
  u8g2.setDisplayRotation(U8G2_R2);
  u8g2.setFont(u8g2_font_5x7_tf);

  // Initialize OLED activity timer
  lastOledActivity = millis();

  pixels.setPixelColor(0, pixels.Color(0, 50, 100));
  pixels.show();

  // -------- Boot Animation --------
  while (logoXOffset < LOGO_FINAL_X || hiriXOffset > HIRI_FINAL_X || proYOffset > PRO_FINAL_Y) {
    // Logo se mueve hacia el centro desde la izquierda
    if (logoXOffset < LOGO_FINAL_X) {
      logoXOffset += 4;  // Movimiento hacia la derecha
    }

    // "HIRI" se mueve hacia el centro desde la derecha
    if (hiriXOffset > HIRI_FINAL_X) {
      hiriXOffset -= 4;  // Movimiento hacia la izquierda
    }

    // "PRO" sube desde abajo más lentamente
    if (proYOffset > PRO_FINAL_Y) {
      proYOffset -= 1;  // Velocidad reducida a 1 píxel por frame
    }
    drawAnimation();
    delay(20);  // ~50 FPS
  }

  // Mostrar versión y device ID en pantalla inicial
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(58, 9, VERSION.c_str());
  u8g2.setCursor(0, 55);
  u8g2.print("ID:" + String(DEVICE_ID_STR));
  u8g2.sendBuffer();


  pms.begin(9600);//PMS5003 sensor plantower
//  sdsSerial.begin(9600); //SDS198 sensor
 sdsSerial.begin(9600, SERIAL_8N1, SDS198RX_PIN,-1); //SDS198 sensor hardware serial2
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_1), isrBtn1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), isrBtn2, FALLING);

  // RTC (una sola vez) - Mostrar en línea separada del ID
  Serial.println("[RTC] test");
  if (!rtc.begin()) {
    Serial.println("[RTC] Not found");
    rtcOK = false;
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:FAIL");
    u8g2.sendBuffer();
  } else {
    rtcOK = true;
    Serial.println("[RTC] OK");
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:OK");
    u8g2.sendBuffer();
  }

  // SD Status indicator (if auto-mount is enabled)
  if (config.sdAutoMount) {
    u8g2.setCursor(45, 64);
    if (SDOK) {
      u8g2.print(" SD:OK");
    } else {
      u8g2.print(" SD:FAIL");
    }
    u8g2.sendBuffer();
  }



  Serial.println("SHT31 test");
  if (!sht31.begin(0x44)) {  // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    SHT31OK = false;
  } else {
    //u8g2.setCursor(60, 64);
    u8g2.print(" SHT31:OK");
    SHT31OK = true;
  }

  u8g2.sendBuffer();
  delay(2000);  // Mantener status por 1 segundo
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(300);
  digitalWrite(MODEM_PWRKEY, LOW);
  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);  // DTR LOW para evitar sleep

  oledStatus("MODEM", "Starting...", "", "");
  for (int i = 0; i < 3; i++) {
    while (!modem.testAT(5000)) {
      Serial.println("[MODEM] Pulse PWRKEY");
      pixels.setPixelColor(0, pixels.Color(0, 50, 100));
      pixels.show();
      digitalWrite(MODEM_PWRKEY, HIGH);
      delay(300);
      digitalWrite(MODEM_PWRKEY, LOW);
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
    }
  }
  pixels.setPixelColor(0, pixels.Color(0, 50, 100));
  pixels.show();
  Serial.println("[MODEM] OK");
  oledStatus("MODEM", "OK", "", "");

  // **Evitar eDRX/PSM que estorban TTFF y datos**
  (void)atRun("+CEDRXS=0", "OK", "ERROR", 1500);
  (void)atRun("+CPSMS=0", "OK", "ERROR", 1500);
  // (opcional, si no usas CTS/RTS) deshabilita control de flujo HW
  (void)atRun("+IFC=0,0", "OK", "ERROR", 1500);

  oledStatus("NET", "Attach/PDP...", "", "");
  if (!modem.waitForNetwork(60000)) {
    oledStatus("NET", "Attach FAIL", "", "");
  } else {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) oledStatus("NET", "PDP FAIL", "", "");
    else oledStatus("NET", "PDP OK", "", "");
  }

  // RTC from network (opcional; no re-init en loop)
  //syncRtcSmart(); ///
  //printRtcOnce();

  // XTRA enable + initial download (requiere PDP)
  xtraSupported = detectAndEnableXtra();
  if (xtraSupported) {
    oledStatus("XTRA", "Downloading...", "", "");
    xtraLastOk = downloadXtraOnce();
    lastXtraDownload = millis();
    oledStatus("XTRA", xtraLastOk ? "Done OK" : "Done FAIL", "", "");
  } else {
    oledStatus("XTRA", "Not supported", "", "");
  }

  // GNSS bring-up (secuencia recomendada)
  gnssBringUp();

  // Ensure WiFi is OFF at boot to save power
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  server.stop();  // por si quedó algo previo
    // Enable watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  Serial.println("[WDT] Watchdog enabled (" + String(WDT_TIMEOUT) + "s)");
  Serial.println("[READY]  (init SD, stream, log)  BTN1=STOP/START GSM SD BTN2=STOP/START WIFI SD)");
}

//---------------------- buttons helpers---------
void handleButtons() {
  // --- BTN1: toggle START/STOP ---
  if (btn1Flag) {
    btn1Flag = false;             // consumir evento (ya quedó desarmado en la ISR)
    lastOledActivity = millis();  // Reset OLED timer on button press
    if (config.oledAutoOff) {
      u8g2.setPowerSave(0);  // Wake up display if it was off
    }

    if (streaming) {
      // STOP
      streaming = false;
      loggingEnabled = false;
      Serial.println("[STREAM] STOP + SD logging OFF");

      // Persist streaming state
      prefs.begin("system", false);
      prefs.putBool("streaming", false);
      prefs.end();
    } else {
      // START (inicio manual por usuario)
      streaming = true;
      lastStream = 0;

      // RESETEAR contadores cuando el usuario presiona START manualmente
      // Solo deben continuar desde el último valor si fue reinicio por watchdog
      sendCounter = 0;
      sdSaveCounter = 0;
      Serial.println("[STREAM] Resetting counters to 0 (manual START)");

      if (!SDOK) {
        spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        SDOK = SD.begin(SD_CS, spiSD);
        if (SDOK) {
          csvFileName = generateCSVFileName();
          writeCSVHeader();
          writeErrorLogHeader();  // Crear header de errores si no existe

          // Persist CSV filename
          prefs.begin("system", false);
          prefs.putString("csvFile", csvFileName);
          prefs.end();
        }
        Serial.println(SDOK ? "[SD] Ready" : "[SD] FAIL");
      }
      loggingEnabled = SDOK;

      // Persist streaming state y resetear contadores en flash
      prefs.begin("system", false);
      prefs.putBool("streaming", true);
      prefs.putUInt("sendCnt", 0);  // Guardar 0 en flash para inicio manual
      prefs.putUInt("sdCnt", 0);    // Guardar 0 en flash para inicio manual
      prefs.end();

      Serial.println(String("[STREAM] START + SD logging ") + (loggingEnabled ? "ON" : "OFF"));
    }
  }

  // Rearme BTN1 cuando se libere y esté estable (pull-up: HIGH = suelto)
  if (!btn1Armed) {
    if (digitalRead(BUTTON_PIN_1) == HIGH) {
      if (btn1ReleaseStart == 0) btn1ReleaseStart = millis();
      else if (millis() - btn1ReleaseStart >= RELEASE_MS) {
        btn1Armed = true;  // listo para la próxima pulsación
        btn1ReleaseStart = 0;
      }
    } else {
      btn1ReleaseStart = 0;  // sigue presionado/ruidoso
    }
  }

  // --- BTN2: alterna WiFi AP ---
  if (btn2Flag) {
    btn2Flag = false;             // consumir evento (ya quedó desarmado en la ISR)
    lastOledActivity = millis();  // Reset OLED timer on button press
    if (config.oledAutoOff) {
      u8g2.setPowerSave(0);  // Wake up display if it was off
    }

    if (!wifiModeActive) {
      if (streaming) {
        streaming = false;
        loggingEnabled = false;
        Serial.println("[STREAM] STOP while WiFi ON");
      }
      startWifiApServer();
    } else {
      stopWifiApServer();
    }
  }

  // Rearme BTN2 cuando se libere y esté estable (pull-up: HIGH = suelto)
  if (!btn2Armed) {
    if (digitalRead(BUTTON_PIN_2) == HIGH) {
      if (btn2ReleaseStart == 0) btn2ReleaseStart = millis();
      else if (millis() - btn2ReleaseStart >= RELEASE_MS) {
        btn2Armed = true;  // listo para la próxima pulsación
        btn2ReleaseStart = 0;
      }
    } else {
      btn2ReleaseStart = 0;  // sigue presionado/ruidoso
    }
  }
}

bool FirstLoop = true;
bool rebootReasonLogged = false;
// -------------------- Loop --------------------
void loop() {
  // Reset watchdog cada loop
  esp_task_wdt_reset();

  // Handle buttons always (needed for WiFi toggle and streaming control)
  handleButtons();

  // ===== WIFI MODE: Only handle WiFi server, skip all GSM/GPS/sensors =====
  if (wifiModeActive) {
    // Handle multiple client requests per loop cycle for better responsiveness
    for (int i = 0; i < 5; i++) {
      server.handleClient();
      yield();
    }

    // Update OLED with WiFi info
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(0, 8);
    u8g2.print("WIFI MODE: DOWNLOAD SD");
    u8g2.setCursor(0, 20);
    u8g2.print("SSID: ");
    u8g2.print(AP_SSID_STR);
    u8g2.setCursor(0, 32);
    u8g2.print("PASS: ");
    u8g2.print(AP_PASSWORD);
    u8g2.setCursor(0, 44);
    u8g2.print("IP: " + WiFi.softAPIP().toString());
    u8g2.setCursor(0, 56);
    u8g2.print("Open: http://");
    u8g2.print(apIpStr);
    u8g2.sendBuffer();

    yield();
    return;  // Exit loop early - skip all normal operation
  }

  // ===== NORMAL MODE: GSM/GPS/Sensors operation =====
  haveFix = (gpsStatus == "Fix");
  gnssWatchdog();
  gnssDiagTick();        // diag + TTFF print (una sola vez)
  gnssDebugPollAsync();  // consultas +CGPSSTATUS? / +CGPSINFO

  if (FirstLoop == true) {
    Serial.println("[FIRST LOOP RTC]");
    csq = modem.getSignalQuality();
    networkError = (csq == 99);

    // Log reboot reason if was watchdog or panic
    if (!rebootReasonLogged && SDOK) {
      if (rebootReason.indexOf("Watchdog") >= 0 || rebootReason == "Panic" || rebootReason == "Brownout") {
        Serial.println("[REBOOT] Logging reboot reason: " + rebootReason);
        logError("REBOOT", rebootReason, "Device rebooted");
      }
      rebootReasonLogged = true;
    }

    // Update network info for error logging
    updateNetworkInfo();

    // Print current configuration
    Serial.println("\n[FIRST LOOP] Current Configuration:");
    printConfig();

    // -------- Autostart Logic --------
    if (config.autostart && !streaming && !wasStreamingBeforeBoot) {
      Serial.println("\n[AUTOSTART] Enabled - initiating streaming");

      // Check if we need to wait for GPS fix
      if (config.autostartWaitGps) {
        Serial.printf("[AUTOSTART] Waiting for GPS fix (timeout: %us / %umin)...\n",
                      config.autostartGpsTimeout, config.autostartGpsTimeout / 60);

        uint32_t gpsWaitStart = millis();
        bool gotFix = false;

        // Wait for GPS fix with timeout
        while ((millis() - gpsWaitStart) < (config.autostartGpsTimeout * 1000UL)) {
          // Reset watchdog
          esp_task_wdt_reset();

          // Process NMEA data
          bool d, o;
          atTick(d, o);

          // Check if we have fix
          if (gpsStatus == "Fix") {
            gotFix = true;
            uint32_t waitTime = (millis() - gpsWaitStart) / 1000;
            Serial.printf("[AUTOSTART] GPS fix acquired after %lu seconds\n", waitTime);
            break;
          }

          // Update display every second
          static uint32_t lastDisplay = 0;
          if (millis() - lastDisplay >= 1000) {
            lastDisplay = millis();
            uint32_t elapsed = (millis() - gpsWaitStart) / 1000;
            uint32_t remaining = config.autostartGpsTimeout - elapsed;

            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.setCursor(0, 12);
            u8g2.print("AUTOSTART: Wait GPS");
            u8g2.setCursor(0, 26);
            u8g2.print("Satellites: " + satellitesStr);
            u8g2.setCursor(0, 40);
            u8g2.print("Elapsed: " + String(elapsed) + "s");
            u8g2.setCursor(0, 54);
            u8g2.print("Remaining: " + String(remaining) + "s");
            u8g2.sendBuffer();
          }

          delay(100);
        }

        if (!gotFix) {
          Serial.println("[AUTOSTART] GPS timeout - starting anyway");
        }
      }

      // Initialize SD if not already mounted
      if (!SDOK) {
        Serial.println("[AUTOSTART] Mounting SD card...");
        spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        SDOK = SD.begin(SD_CS, spiSD);

        if (SDOK) {
          Serial.println("[AUTOSTART] SD mounted successfully");

          // Create CSV file if needed
          if (csvFileName.length() == 0) {
            csvFileName = generateCSVFileName();
            writeCSVHeader();
            writeErrorLogHeader();

            prefs.begin("system", false);
            prefs.putString("csvFile", csvFileName);
            prefs.end();

            Serial.println("[AUTOSTART] Created CSV file: " + csvFileName);
          }
        } else {
          Serial.println("[AUTOSTART] SD mount failed - will log to serial only");
        }
      }

      // Start streaming
      streaming = true;
      loggingEnabled = SDOK;
      lastStream = 0;

      // Reset counters for autostart (not a watchdog recovery)
      sendCounter = 0;
      sdSaveCounter = 0;

      // Persist streaming state
      prefs.begin("system", false);
      prefs.putBool("streaming", true);
      prefs.putUInt("sendCnt", 0);
      prefs.putUInt("sdCnt", 0);
      prefs.end();

      Serial.println("[AUTOSTART] Streaming started - SD logging: " + String(loggingEnabled ? "ON" : "OFF"));

      // Show status on display for 2 seconds
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.setCursor(0, 12);
      u8g2.print("AUTOSTART: READY");
      u8g2.setCursor(0, 26);
      u8g2.print("Streaming: ON");
      u8g2.setCursor(0, 40);
      u8g2.print("SD Logging: " + String(loggingEnabled ? "ON" : "OFF"));
      u8g2.setCursor(0, 54);
      u8g2.print("GPS: " + gpsStatus + " Sats:" + satellitesStr);
      u8g2.sendBuffer();
      delay(2000);
    }

    // Retry RTC sync con modem (máximo 3 veces, 10 min entre intentos)
    if (rtcNetSyncPending && rtcModemSyncCount < MAX_MODEM_SYNC_COUNT
        && millis() >= rtcNextProbeMs && !wifiModeActive
        && modem.isNetworkConnected()
        && millis() - lastModemSyncAttempt >= MODEM_SYNC_INTERVAL_MS) {

      lastModemSyncAttempt = millis();
      rtcNextProbeMs = millis() + RTC_PROBE_PERIOD_MS;

      uint32_t modemEpoch = 0;
      if (getModemEpoch(modemEpoch)) {
        uint32_t rtcEpoch = rtcOK ? rtc.now().unixtime() : 0;
        long diff = (long)modemEpoch - (long)rtcEpoch;
        Serial.printf("[RTC][retry] rtc=%lu modem=%lu diff=%ld s\n",
                      (unsigned long)rtcEpoch, (unsigned long)modemEpoch, diff);

        if (rtcOK && abs(diff) > RTC_SYNC_THRESHOLD) {
          rtc.adjust(DateTime(modemEpoch));
          Serial.println("[RTC][retry] Synchronized to modem clock");

          // Incrementar contador
          rtcModemSyncCount++;
          prefs.begin("rtc", false);
          prefs.putUChar("syncCnt", rtcModemSyncCount);
          prefs.end();
          Serial.printf("[RTC][retry] Sync count updated: %u/%u\n", rtcModemSyncCount, MAX_MODEM_SYNC_COUNT);

          if (rtcModemSyncCount >= MAX_MODEM_SYNC_COUNT) {
            rtcNetSyncPending = false;
            Serial.println("[RTC][retry] Max sync count reached");
          }
        } else {
          Serial.println("[RTC][retry] Within threshold; no sync");
        }
      } else {
        Serial.println("[RTC][retry] Modem still not ready");
      }
    }
    csq = modem.getSignalQuality();
    Serial.println("[FIRST LOOP OUT]");
    FirstLoop = false;
  }

  // Continuar con operación normal (GPS/GSM/sensores)
  downloadXtraIfDue();

  // Leer temperatura del RTC sin re-inicializar el chip cada ciclo
  if (rtcOK) rtcTempC = rtc.getTemperature();
  else rtcTempC = NAN;
  // PMS + LED color (non-blocking)
  readPMS();
  byte frame[10]; // Buffer para almacenar la trama de datos.
  // Si se lee una trama válida...bool (); 
  if (readFrameSDS198(frame)) {
    // Extrae el valor de PM100 de la trama.
    // El valor de PM100 se forma con los bytes 5 (MSB) y 4 (LSB) de la trama.
    uint16_t pm100 = (uint16_t)((frame[5] << 8) | frame[4]); // en μg/m3
    // Imprime el valor de PM100 en el monitor serie.
    SDS198PM100=pm100;
    Serial.print("PM100 (TSP): ");
    Serial.print(SDS198PM100);
    Serial.println(" ug/m3");
  }
  // Drain UART / AT; también parsea NMEA (rate-limited to 20 Hz)
  static uint32_t lastAtTick = 0;
  if (millis() - lastAtTick >= 50) {  // Max 20 Hz (every 50ms)
    lastAtTick = millis();
    bool d, o;
    (void)atTick(d, o);
  }

  if (millis() - lastPmsPrint > PrintTime) {
    lastPmsPrint = millis();

    updatePmLed((float)PM25);
    Serial.printf("[PMS] PM1=%u PM2.5=%u PM10=%u  T(PMS)=%.1f  RH(PMS)=%.1f  (RTC T=%.2fC)\n",
                  PM1, PM25, PM10, pmsTempC, pmsHum, rtcTempC);
    DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
    Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d (epoch=%lu)\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second(),
                  (unsigned long)now.unixtime());
    if (millis() - lastPmsSeen > 5000) Serial.println("[PMS] No frames >5s (check sensor power/baud)");

    if (SHT31OK == true) {
      tempsht31 = sht31.readTemperature();
      humsht31 = sht31.readHumidity();

      if (!isnan(tempsht31)) {  // check if 'is not a number'
        Serial.print("Temp *C = ");
        Serial.print(tempsht31);
        Serial.print("\t\t");
      } else {
        Serial.println("Failed to read temperature");
      }

      if (!isnan(humsht31)) {  // check if 'is not a number'
        Serial.print("Hum. % = ");
        Serial.println(humsht31);
      } else {
        Serial.println("Failed to read humidity");
      }
    }
  }

  // Battery & CSQ (lightweight, non-blocking)
  // Muestreo no bloqueante: toma 1 muestra cada 100ms
  if (millis() - lastBatSample >= BAT_SAMPLE_INTERVAL_MS) {
    lastBatSample = millis();

    // Agregar muestra al acumulador
    batSampleSum += analogRead(BAT_PIN);
    batSampleCount++;

    // Cuando se completan NUM_SAMPLES muestras, calcular promedio
    if (batSampleCount >= NUM_SAMPLES) {
      float rawV = (batSampleSum / NUM_SAMPLES / 4095.0f) * 3.3f * 2.0f * 1.15f;

      // Aplicar filtro EMA (Exponential Moving Average)
      if (batteryVoltageAverage == 0) batteryVoltageAverage = rawV;  // Primera lectura
      batteryVoltageAverage = (alpha * rawV) + ((1.0 - alpha) * batteryVoltageAverage);
      batV = batteryVoltageAverage;

      // Reiniciar acumuladores
      batSampleSum = 0;
      batSampleCount = 0;
    }
  }

  // CSQ check con bandera simple
  static uint32_t lastCsqCheck = 0;
  uint32_t csqInterval = isCurrentlyTransmitting ? CSQ_INTERVAL_TRANSMITTING_MS : CSQ_INTERVAL_NORMAL_MS;

  if (millis() - lastCsqCheck > csqInterval) {
    lastCsqCheck = millis();
    csq = modem.getSignalQuality();
    networkError = (csq == 99);
  }

  // -------- Periodic streaming to API --------
  // Use config period instead of hardcoded STREAM_PERIOD_MS
  if (streaming && (millis() - lastStream >= config.httpSendPeriod)) {
    lastStream = millis();
    lastOledActivity = millis();  // Reset OLED timer on transmission activity

    //readPMS();  // Read sensor just before using its data esto no era problema

    // =====================================================
    // PRIORIDAD 1: GUARDAR EN SD PRIMERO (antes de HTTP)
    // =====================================================
    // Guardar datos en SD ANTES de intentar transmitir por HTTP
    // Esto asegura que los datos se guarden localmente incluso si la transmisión falla
    bool sdSaved = saveCSVData();

    // Activar visualización temporal de datos guardados
    if (sdSaved && loggingEnabled) {
      displayState = DISP_SD_SAVED;
      displayStateStartTime = millis();
      Serial.println("[SD] ✓ Data saved successfully - Counter: " + String(sdSaveCounter));
      renderDisplay();
      delay(SD_SAVE_DISPLAY_MS);
    }

    // valores mapping (v1 sin sanitizar, a pedido)
    const String v1 = isnan(pmsTempC) ? "0" : safeFloatStr(pmsTempC);  // Temp PMS (o 0/NaN según sensor)
    const String v2 = isnan(pmsHum) ? "0" : safeFloatStr(pmsHum);      // RH PMS si válido
    const String v3 = safeUIntStr(PM1);                                // PM1
    const String v4 = safeUIntStr(PM25);                               // PM2.5
    const String v5 = safeUIntStr(PM10);                               // PM10
    const String v6 = safeGpsStr(gpsLat);                              // Lat
    const String v7 = safeGpsStr(gpsLon);                              // Lon
    const String v8 = safeIntStr(csq);                                 // CSQ
    const String v9 = (gpsSpeedKmh.length() ? gpsSpeedKmh : "0");      // Spd
    const String v10 = safeSatsStr(satellitesStr);                     // Sats
    const String v11 = safeFloatStr(rtcTempC);                         // RTC Temp
    const String v12 = safeFloatStr(batV);                             // BatV
    const String v13 = safeGpsStr(gpsLat);                             // Lat
    const String v14 = safeGpsStr(gpsLon);                             // Lon
    const String v15 = String(DEVICE_ID_STR);                          // ID
    const String v16 = safeUIntStr(sendCounter + 1);                   // Counter
    const String v17 = loggingEnabled ? "1" : "0";                     // SD
    if (SHT31OK == true) {
      const String v18 = isnan(tempsht31) ? "0" : safeFloatStr(tempsht31);  // Temp SHT31 si válido
      const String v19 = isnan(humsht31) ? "0" : safeFloatStr(humsht31);    // RH SHT31 si válido
      valores = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18 + "," + v19;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES + "&idsVariables=" + IDS_VARIABLESSHT31 + "&valores=" + valores;
    }else if (DEVICE_ID_STR == "06") {
      Serial.println("Enviando datos SDS198 para dispositivo 06");
      const String v18 = safeUIntStr(SDS198PM100);  // PM100 SDS198 si válido
      valores = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17 + "," + v18;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES + "&idsVariables=" + IDS_VARIABLES06 + "&valores=" + valores;
    } else {
      valores = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," + v10 + "," + v11 + "," + v12 + "," + v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17;
      url = String(API_BASE) + "?idsSensores=" + IDS_SENSORES + "&idsVariables=" + IDS_VARIABLES + "&valores=" + valores;
    }

    // =====================================================
    // PRIORIDAD 2: TRANSMITIR POR HTTP (después de guardar SD)
    // =====================================================
    Serial.println("[HTTP] GET " + url);

    // Modo SYNC: Transmisión HTTP bloqueante (con timeout de 15s y watchdog reset)
    isCurrentlyTransmitting = true;  // Activar bandera
    bool ok = httpGet_webhook(url);
    isCurrentlyTransmitting = false;  // Desactivar bandera

    if (ok) {
      sendCounter++;
      Serial.println("[HTTP] ✓ Transmission successful - Counter: " + String(sendCounter));
      // Persist send counter every successful transmission
      prefs.begin("system", false);
      prefs.putUInt("sendCnt", sendCounter);
      prefs.putUInt("sdCnt", sdSaveCounter);  // También persistir SD counter
      prefs.end();
    } else {
      // TRANSMISIÓN FALLIDA: Guardar en CSV de fallos para análisis (NO se reintenta)
      // Esto permite debuggear problemas de red sin perder registro de intentos fallidos
      saveFailedTransmission(url, "HTTP_FAIL");
      Serial.println("[HTTP] ✗ Transmission failed, logged to " + failedTxPath);
    }
  }

  // -------- OLED --------
  // The display is now handled by a state machine.
  updateDisplayStateMachine();
  renderDisplay();

  // -------- OLED Auto-Off Logic --------
  if (config.oledAutoOff) {
    if (millis() - lastOledActivity > config.oledTimeout) {
      u8g2.setPowerSave(1);  // Turn off display after timeout
    }
  }

  // -------- Serial Commands --------
  processSerialCommand();

  yield();
}
