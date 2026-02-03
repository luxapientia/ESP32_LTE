/**************************************************************
 * Voice API test – ESP32 + SIM7070E (MIKROE-6287)
 *
 * Hardware: ESP32 (e.g. DevKitC, WROOM-32D/32E), MIKROE-6287 LTE IoT 17 Click (SIM7070E)
 * SIM: Croatia local (APN internet.ht.ht, PIN 5576)
 *
 * Tests HTTPS workflow for Loop voice API (Supabase Edge Functions):
 *   POST /voice-upload-url (parent default or friend with token_uid)
 *
 * Requires: ArduinoHttpClient
 *   https://github.com/arduino-libraries/ArduinoHttpClient
 *
 * Wiring (LTE IoT 17 Click / MIKROE-6287, mikroBUS):
 *   ESP32 GPIO 17 (TX) -> Click RX (mikroBUS TX)
 *   ESP32 GPIO 16 (RX) <- Click TX (mikroBUS RX)
 *   ESP32 GPIO 18      -> PWR (pin 1 AN): power on modem
 *   ESP32 GPIO 4       -> CTS (pin 15 INT): drive LOW so modem can send (required)
 *   ESP32 GPIO 23      -> RST (pin 2): optional reset
 *   Datasheet: default UART 115200 bps, hardware flow control (CTS/RTS).
 *   If raw test shows NO bytes: (1) Swap UART wires: swap GPIO 16 and 17.
 *   (2) STAT LED ON? If not, comment out MODEM_PWRKEY_ACTIVE_LOW. (3) CTS: GPIO4->INT.
 **************************************************************/

#define TINY_GSM_MODEM_SIM7070

#define SerialMon Serial

// LTE IoT 17 Click (MIKROE-6287): UART, power, flow control
#define MODEM_RX     16   // ESP32 RX  <- Click TX (mikroBUS RX)
#define MODEM_TX     17   // ESP32 TX  -> Click RX (mikroBUS TX)
#define MODEM_PWRKEY 18   // PWR (pin 1): power on (see MODEM_PWRKEY_ACTIVE_LOW)
#define MODEM_CTS    4    // CTS (pin 15 INT): drive LOW so modem can send (required!)
#define MODEM_RESET  23   // RST (pin 2): optional
#define SerialAT Serial2

// SIM7070 on many boards: PWRKEY = active LOW (hold LOW ~1.5s to power on).
// If modem never responds, try toggling this (comment ↔ uncomment).
#define MODEM_PWRKEY_ACTIVE_LOW

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif

#define TINY_GSM_DEBUG SerialMon
#define MODEM_BAUD 115200  // LTE IoT 17 Click default (no autobaud)

#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

// Croatia SIM (Hrvatski Telekom). If unlock still fails, disable PIN on SIM (phone) and use ""
#define GSM_PIN "5576"
const char apn[]      = "internet.ht.ht";
const char gprsUser[] = "";
const char gprsPass[] = "";

// Loop API – device auth
#define API_BASE_HOST "rmhfhawfcyutzdtwsfnj.supabase.co"
#define API_BASE_PATH "/functions/v1"
const int  apiPort    = 443;

// CHILD A device token (device owner)
const char deviceToken[] = "device_b6525ecf-f009-4068-baa1-c26f5057e6ae";

// CHILD B NFC token (friend) – use when testing friend message flow
#define CHILD_B_NFC_TOKEN "1D4F9B190D1080"

// Test mode: 0 = parent default (no NFC), 1 = friend message (send token_uid)
#define TEST_MODE 0

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm        modem(debugger);
#else
TinyGsm        modem(SerialAT);
#endif

TinyGsmClientSecure client(modem);
HttpClient          http(client, API_BASE_HOST, apiPort);

// Power on modem via PWRKEY. SIM7070 often uses active-LOW (hold LOW ~1.5s).
// Toggle MODEM_PWRKEY_ACTIVE_LOW above if modem still never responds.
void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
#ifdef MODEM_PWRKEY_ACTIVE_LOW
  digitalWrite(MODEM_PWRKEY, HIGH);  // idle
  delay(100);
  SerialMon.println(F("PWRKEY on (active LOW)..."));
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1500);
  digitalWrite(MODEM_PWRKEY, HIGH);
#else
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  SerialMon.println(F("PWRKEY on (active HIGH)..."));
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1500);
  digitalWrite(MODEM_PWRKEY, LOW);
#endif
  SerialMon.println(F("Waiting for modem boot (15s)..."));
  delay(15000);
}

void setup() {
  SerialMon.begin(115200);
  delay(1000);
  SerialMon.println();
  SerialMon.println(F("Voice API test – ESP32 + SIM7070E"));
  SerialMon.println(F("================================"));

  // Power on modem before UART (required for LTE IoT 17 Click)
  modemPowerOn();

  // Drive CTS LOW so modem can send (datasheet: hardware flow control)
  pinMode(MODEM_CTS, OUTPUT);
  digitalWrite(MODEM_CTS, LOW);

  // ESP32: Serial2 @ 115200 (LTE IoT 17 Click default)
  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  SerialMon.println(F("Initializing modem..."));
  if (!modem.restart()) {
    SerialMon.println(F("restart failed, try init()"));
    modem.init();
  }
  delay(3000);  // let SIM settle before CPIN

  String modemInfo = modem.getModemInfo();
  SerialMon.print(F("Modem: "));
  SerialMon.println(modemInfo);

  // SIM unlock: try whenever PIN is set (SIM7070 may report status late)
  if (GSM_PIN[0]) {
    SerialMon.println(F("Unlocking SIM..."));
    for (int i = 0; i < 3; i++) {
      if (modem.simUnlock(GSM_PIN)) {
        SerialMon.println(F("SIM unlocked"));
        break;
      }
      SerialMon.println(F("SIM unlock failed, retry..."));
      delay(2000);
    }
    delay(1000);
  }
}

void loop() {
  SerialMon.println();
  // "Unhandled: 5)" etc. = modem URC (e.g. +CREG: 0,5 = registered); safe to ignore.
  SerialMon.println(F("Waiting for network..."));
  if (!modem.waitForNetwork(120000L)) {
    SerialMon.println(F("Network timeout"));
    delay(10000);
    return;
  }
  SerialMon.println(F("Network OK"));

  SerialMon.print(F("Connecting to APN "));
  SerialMon.print(apn);
  SerialMon.println(F("..."));
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println(F("GPRS connect fail"));
    delay(10000);
    return;
  }
  SerialMon.println(F("GPRS connected"));

  // --- Test 1: POST /voice-upload-url ---
  const char* path = API_BASE_PATH "/voice-upload-url";

  // Parent default (no token_uid) or friend (with token_uid)
  String jsonBody;
  if (TEST_MODE == 0) {
    jsonBody = F("{\"content_type\":\"audio/m4a\",\"file_ext\":\"m4a\",\"duration_ms\":5200}");
  } else {
    jsonBody = "{\"content_type\":\"audio/m4a\",\"file_ext\":\"m4a\",\"duration_ms\":5200,\"token_uid\":\"" CHILD_B_NFC_TOKEN "\"}";
  }

  SerialMon.println(F("POST /voice-upload-url..."));
  http.connectionKeepAlive();

  http.beginRequest();
  http.post(path);
  http.sendHeader("Authorization", String("Bearer ") + deviceToken);
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", jsonBody.length());
  http.beginBody();
  http.print(jsonBody);
  http.endRequest();

  int statusCode = http.responseStatusCode();
  String responseBody = http.responseBody();

  SerialMon.print(F("Status: "));
  SerialMon.println(statusCode);
  SerialMon.print(F("Response: "));
  SerialMon.println(responseBody);

  http.stop();
  modem.gprsDisconnect();
  SerialMon.println(F("Disconnected. Next run in 30s."));
  delay(30000);
}
