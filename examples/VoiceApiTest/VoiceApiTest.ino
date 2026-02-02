/**************************************************************
 * Voice API test – ESP32 + SIM7070E (MIKROE-6287)
 *
 * Hardware: ESP32-DevKitC-32E, MIKROE-6287 (SIM7070E)
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
 *   If modem never responds: (1) Yellow STAT LED on Click must turn ON after PWR.
 *   (2) Wire CTS: GPIO4 -> Click INT. (3) Try toggling MODEM_PWRKEY_ACTIVE_LOW.
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
#define GSM_AUTOBAUD_MIN 9600
#define GSM_AUTOBAUD_MAX 460800  // SIM7070 may default to 115200 or higher

#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

// Croatia SIM (Hrvatski Telekom)
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

  // ESP32: Serial2; datasheet says default 115200 bps
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  // Raw UART echo test: send AT every 500ms for 8s, print any bytes from modem.
  // Nothing = no link (check STAT LED, CTS wire, PWRKEY polarity). Garbage = wrong baud/pins.
  #define VOICE_API_RAW_ECHO_TEST
#ifdef VOICE_API_RAW_ECHO_TEST
  SerialMon.println(F("Raw UART @ 115200 (8s, AT every 500ms). Check: STAT LED ON? CTS=GPIO4->INT?"));
  for (unsigned long t = millis(); millis() - t < 8000;) {
    SerialAT.print(F("AT\r\n"));
    for (int k = 0; k < 50; k++) {
      while (SerialAT.available()) { SerialMon.write(SerialAT.read()); }
      delay(10);
    }
    delay(450);
  }
  SerialMon.println(F("\n--- end raw test ---"));
#endif

  SerialMon.println(F("Trying modem (autobaud)..."));
  TinyGsmAutoBaud(SerialAT, GSM_AUTOBAUD_MIN, GSM_AUTOBAUD_MAX);
  delay(1000);

  SerialMon.println(F("Initializing modem..."));
  if (!modem.restart()) {
    SerialMon.println(F("restart failed, try init()"));
    modem.init();
  }
  delay(2000);

  String modemInfo = modem.getModemInfo();
  SerialMon.print(F("Modem: "));
  SerialMon.println(modemInfo);

  if (GSM_PIN[0] && modem.getSimStatus() != 3) {
    SerialMon.println(F("Unlocking SIM..."));
    if (!modem.simUnlock(GSM_PIN)) {
      SerialMon.println(F("SIM unlock failed"));
    }
  }
}

void loop() {
  SerialMon.println();
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
