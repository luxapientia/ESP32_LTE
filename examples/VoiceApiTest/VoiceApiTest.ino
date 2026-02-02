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
 * Wiring (MIKROE-6287):
 *   ESP32 GPIO 17 (TX) -> LTE RX
 *   ESP32 GPIO 16 (RX) <- LTE TX
 *   ESP32 GPIO 18      -> PWRKEY (must pulse to power on modem)
 *   ESP32 GPIO 23       -> RESET (optional)
 **************************************************************/

#define TINY_GSM_MODEM_SIM7070

#define SerialMon Serial

// MIKROE-6287: UART and power pins
#define MODEM_RX   16   // ESP32 RX  <- LTE TX
#define MODEM_TX   17   // ESP32 TX  -> LTE RX
#define MODEM_PWRKEY 18 // PWRKEY: pulse high ~1.5s to power on
#define MODEM_RESET  23 // RESET (optional): low = reset
#define SerialAT Serial2

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif

#define TINY_GSM_DEBUG SerialMon
#define GSM_AUTOBAUD_MIN 9600
#define GSM_AUTOBAUD_MAX 115200

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

// Power on modem via PWRKEY (SIM7070: hold high ~1.5s, then release).
// If modem never responds, try inverting: HIGH<->LOW in this function.
void modemPowerOn() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  SerialMon.println(F("PWRKEY on..."));
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1500);
  digitalWrite(MODEM_PWRKEY, LOW);
  SerialMon.println(F("Waiting for modem boot (8s)..."));
  delay(8000);
}

void setup() {
  SerialMon.begin(115200);
  delay(1000);
  SerialMon.println();
  SerialMon.println(F("Voice API test – ESP32 + SIM7070E"));
  SerialMon.println(F("================================"));

  // Power on modem before UART (required for MIKROE-6287)
  modemPowerOn();

  // ESP32: Serial2 with explicit RX/TX pins for modem
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

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
