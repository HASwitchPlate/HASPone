////////////////////////////////////////////////////////////////////////////////////////////////////
//           _____ _____ _____ _____
//          |  |  |  _  |   __|  _  |
//          |     |     |__   |   __|
//          |__|__|__|__|_____|__|
//        Home Automation Switch Plate
// https://github.com/aderusha/HASwitchPlate
//
// Copyright (c) 2022 Allen Derusha allen@derusha.org
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this hardware,
// software, and associated documentation files (the "Product"), to deal in the Product without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Product, and to permit persons to whom the
// Product is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Product.
//
// THE PRODUCT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE PRODUCT OR THE USE OR OTHER DEALINGS IN THE PRODUCT.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <LittleFS.h>
#include <EEPROM.h>
#include <EspSaveCrash.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <MQTT.h>
#include <SoftwareSerial.h>
#include <ESP8266Ping.h>
//BME680 ADD begin
#include "bsec.h"
//BME680 ADD end
//MAX44009 ADD begin
#include <MAX44009.h>
MAX44009 light;
//MAX44009 ADD end

////////////////////////////////////////////////////////////////////////////////////////////////////
// These defaults may be overwritten with values saved by the web interface
char wifiSSID[32] = "";
char wifiPass[64] = "";
char mqttServer[128] = "";
char mqttPort[6] = "1883";
char mqttUser[128] = "";
char mqttPassword[128] = "";
char mqttFingerprint[60] = "";
char haspNode[16] = "plate01";
char groupName[16] = "plates";
char hassDiscovery[128] = "homeassistant";
char configUser[32] = "admin";
char configPassword[32] = "";
char motionPinConfig[3] = "0";
char nextionBaud[7] = "115200";

////////////////////////////////////////////////////////////////////////////////////////////////////
const float haspVersion = 1.055;                       // Current HASPone software release version
const uint16_t mqttMaxPacketSize = 2048;              // Size of buffer for incoming MQTT message
byte nextionReturnBuffer[128];                        // Byte array to pass around data coming from the panel
uint8_t nextionReturnIndex = 0;                       // Index for nextionReturnBuffer
int8_t nextionActivePage = -1;                        // Track active LCD page
bool lcdConnected = false;                            // Set to true when we've heard something from the LCD
const char wifiConfigPass[9] = "hasplate";            // First-time config WPA2 password
const char wifiConfigAP[14] = "HASwitchPlate";        // First-time config SSID
bool shouldSaveConfig = false;                        // Flag to save json config to LittleFS
bool nextionReportPage0 = false;                      // If false, don't report page 0 sendme
const unsigned long updateCheckInterval = 43200000;   // Time in msec between update checks (12 hours)
unsigned long updateCheckTimer = updateCheckInterval; // Timer for update check
unsigned long updateCheckFirstRun = 60000;            // First-run check offset
bool updateEspAvailable = false;                      // Flag for update check to report new ESP FW version
float updateEspAvailableVersion;                      // Float to hold the new ESP FW version number
bool updateLcdAvailable = false;                      // Flag for update check to report new LCD FW version
unsigned long debugTimer = 0;                         // Clock for debug performance profiling
bool debugSerialEnabled = true;                       // Enable USB serial debug output
const unsigned long debugSerialBaud = 115200;         // Desired baud rate for serial debug output
bool debugTelnetEnabled = false;                      // Enable telnet debug output
bool nextionBufferOverrun = false;                    // Set to true if an overrun error was encountered
bool nextionAckEnable = false;                        // Wait for each Nextion command to be acked before continuing
bool nextionAckReceived = false;                      // Ack was received
bool rebootOnp0b1 = false;                            // When true, reboot device on button press of p[0].b[1]
bool rebootOnLongPress = true;                        // When true, reboot device on long press of any button
unsigned long rebootOnLongPressTimer = 0;             // Clock for long press reboot timer
unsigned long rebootOnLongPressTimeout = 10000;       // Timeout value for long press reboot timer
const unsigned long nextionAckTimeout = 1000;         // Timeout to wait for an ack before throwing error
unsigned long nextionAckTimer = 0;                    // Timer to track Nextion ack
const unsigned long telnetInputMax = 128;             // Size of user input buffer for user telnet session
bool motionEnabled = false;                           // Motion sensor is enabled
bool mdnsEnabled = true;                              // mDNS enabled
bool ignoreTouchWhenOff = false;                      // Ignore touch events when backlight is off and instead send mqtt msg
bool beepEnabled = false;                             // Keypress beep enabled
unsigned long beepOnTime = 1000;                      // milliseconds of on-time for beep
unsigned long beepOffTime = 1000;                     // milliseconds of off-time for beep
unsigned int beepCounter;                             // Count the number of beeps
uint8_t beepPin = D2;                                 // define beep pin output
uint8_t motionPin = 0;                                // GPIO input pin for motion sensor if connected and enabled
bool motionActive = false;                            // Motion is being detected
const unsigned long motionLatchTimeout = 1000;        // Latch time for motion sensor
const unsigned long motionBufferTimeout = 100;        // Trigger threshold time for motion sensor
unsigned long lcdVersion = 0;                         // Int to hold current LCD FW version number
unsigned long updateLcdAvailableVersion;              // Int to hold the new LCD FW version number
bool lcdVersionQueryFlag = false;                     // Flag to set if we've queried lcdVersion
const String lcdVersionQuery = "p[0].b[2].val";       // Object ID for lcdVersion in HMI
uint8_t lcdBacklightDim = 0;                          // Backlight dimmer value
bool lcdBacklightOn = 0;                              // Backlight on/off
bool lcdBacklightQueryFlag = false;                   // Flag to set if we've queried lcdBacklightDim
bool startupCompleteFlag = false;                     // Startup process has completed
const unsigned long statusUpdateInterval = 60000;    // Time in msec between publishing MQTT status updates (5 minutes)
unsigned long statusUpdateTimer = 0;                  // Timer for status update
const unsigned long connectTimeout = 300;             // Timeout for WiFi and MQTT connection attempts in seconds
const unsigned long reConnectTimeout = 60;            // Timeout for WiFi reconnection attempts in seconds
byte espMac[6];                                       // Byte array to store our MAC address
bool mqttTlsEnabled = false;                          // Enable MQTT client TLS connections
bool mqttPingCheck = false;                           // MQTT broker ping check result
bool mqttPortCheck = false;                           // MQTT broke port check result
String mqttClientId;                                  // Auto-generated MQTT ClientID
String mqttGetSubtopic;                               // MQTT subtopic for incoming commands requesting .val
String mqttStateTopic;                                // MQTT topic for outgoing panel interactions
String mqttStateJSONTopic;                            // MQTT topic for outgoing panel interactions in JSON format
String mqttCommandTopic;                              // MQTT topic for incoming panel commands
String mqttGroupCommandTopic;                         // MQTT topic for incoming group panel commands
String mqttStatusTopic;                               // MQTT topic for publishing device connectivity state
String mqttSensorTopic;                               // MQTT topic for publishing device information in JSON format
String mqttLightCommandTopic;                         // MQTT topic for incoming panel backlight on/off commands
String mqttLightStateTopic;                           // MQTT topic for outgoing panel backlight on/off state
String mqttLightBrightCommandTopic;                   // MQTT topic for incoming panel backlight dimmer commands
String mqttLightBrightStateTopic;                     // MQTT topic for outgoing panel backlight dimmer state
String mqttMotionStateTopic;                          // MQTT topic for outgoing motion sensor state
String nextionModel;                                  // Record reported model number of LCD panel
const byte nextionSuffix[] = {0xFF, 0xFF, 0xFF};      // Standard suffix for Nextion commands
uint8_t nextionMaxPages = 11;                         // Maximum number of pages in Nextion project
uint32_t tftFileSize = 0;                             // Filesize for TFT firmware upload
const uint8_t nextionResetPin = D6;                   // Pin for Nextion power rail switch (GPIO12/D6)
const unsigned long nextionSpeeds[] = {2400,
                                       4800,
                                       9600,
                                       19200,
                                       31250,
                                       38400,
                                       57600,
                                       115200,
                                       230400,
                                       250000,
                                       256000,
                                       512000,
                                       921600};                                       // Valid serial speeds for Nextion communication
const uint8_t nextionSpeedsLength = sizeof(nextionSpeeds) / sizeof(nextionSpeeds[0]); // Size of our list of speeds
//ADC ADD begin
unsigned int uint_voltageraw = 0;
float float_voltage = 0.0;
//ADC ADD end
//BME680 ADD begin
const uint8_t bsec_config_iaq[] = {
#include "config/generic_33v_3s_4d/bsec_iaq.txt"
};

#define STATE_SAVE_PERIOD	UINT32_C(360 * 60 * 1000) // 360 minutes - 4 times a day
// Create an object of the class Bsec
Bsec iaqSensor;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;
float float_bme680_rawtemperature = 0;
float float_bme680_rawpressure = 0;
float float_bme680_rawhumidity = 0;
float float_bme680_rawgasresistor = 0;
float float_bme680_iaq = 0;
float float_bme680_staticiaq = 0;
float float_bme680_iaqaccuracy = 0;
float float_bme680_co2equivalent = 0;
float float_bme680_breathvocequivalent = 0;
float float_bme680_compensatedtemperature = 0;
float float_bme680_compensatedhumidity = 0;
String string_bme680_errocode = "0";
String outputBME680;
unsigned long statusUpdateIntervalBME680 = 2000;
unsigned long statusUpdateTimerBME680 = 0;
// Helper functions declarations
// Helper functions declarations
void checkIaqSensorStatus(void);
void errLeds(void);
void loadState(void);
void updateState(void);
//BME680 ADD end


WiFiClientSecure mqttClientSecure;        // TLS-enabled WiFiClient for MQTT
WiFiClient wifiClient;                    // Standard WiFiClient
MQTTClient mqttClient(mqttMaxPacketSize); // MQTT client
ESP8266WebServer webServer(80);           // Admin web server on port 80
ESP8266HTTPUpdateServer httpOTAUpdate;    // Arduino OTA server
WiFiServer telnetServer(23);              // Telnet server (if enabled)
WiFiClient telnetClient;                  // Telnet client
MDNSResponder::hMDNSService hMDNSService; // mDNS
EspSaveCrash SaveCrash;                   // Save crash details to flash

// URL for auto-update check of "version.json"
const char UPDATE_URL[] PROGMEM = "https://raw.githubusercontent.com/HASwitchPlate/HASPone/main/update/version.json";
// Additional CSS style to match Hass theme
const char HASP_STYLE[] PROGMEM = "<style>button{background-color:#03A9F4;}body{width:60%;margin:auto;}input:invalid{border:1px solid red;}input[type=checkbox]{width:20px;}.wrap{text-align:left;display:inline-block;min-width:260px;max-width:1000px}</style>";
// Default link to compiled Arduino firmware image
String espFirmwareUrl = "https://raw.githubusercontent.com/HASwitchPlate/HASPone/main/Arduino_Sketch/HASwitchPlate.ino.d1_mini.bin";
// Default link to compiled Nextion firmware images
String lcdFirmwareUrl = "https://raw.githubusercontent.com/HASwitchPlate/HASPone/main/Nextion_HMI/HASwitchPlate.tft";

////////////////////////////////////////////////////////////////////////////////////////////////////
void setup()
{ // System setup
  debugPrint(String(F("\n\n================================================================================\n")));
  debugPrintln(String(F("SYSTEM: Starting HASPone v")) + String(haspVersion));
  debugPrintln(String(F("SYSTEM: heapFree: ")) + String(ESP.getFreeHeap()) + String(F(" heapMaxFreeBlockSize: ")) + String(ESP.getMaxFreeBlockSize()));
  debugPrintln(String(F("SYSTEM: Last reset reason: ")) + String(ESP.getResetInfo()));
  if (SaveCrash.count())
  {
    debugPrint(String(F("SYSTEM: Crashdump data discovered:")));
    debugPrintCrash();
  }
  debugPrint(String(F("================================================================================\n\n")));

  pinMode(nextionResetPin, OUTPUT);    // Take control over the power switch for the LCD
  digitalWrite(nextionResetPin, HIGH); // Power on the LCD
  configRead();                        // Check filesystem for a saved config.json
  Serial.begin(atoi(nextionBaud));     // Serial - LCD RX (after swap), debug TX
  Serial1.begin(atoi(nextionBaud));    // Serial1 - LCD TX, no RX
  Serial.swap();                       // Swap to allow hardware UART comms to LCD

  if (!nextionConnect())
  {
    if (lcdConnected)
    {
      debugPrintln(F("HMI: LCD responding but initialization wasn't completed. Continuing program load anyway."));
    }
    else
    {
      debugPrintln(F("HMI: LCD not responding, continuing program load"));
    }
  }

  espWifiConnect(); // Start up networking

  if ((configPassword[0] != '\0') && (configUser[0] != '\0'))
  { // Start the webserver with our assigned password if it's been configured...
    httpOTAUpdate.setup(&webServer, "/update", configUser, configPassword);
  }
  else
  { // or without a password if not
    httpOTAUpdate.setup(&webServer, "/update");
  }

  webServer.on("/", webHandleRoot);
  webServer.on("/saveConfig", webHandleSaveConfig);
  webServer.on("/resetConfig", webHandleResetConfig);
  webServer.on("/resetBacklight", webHandleResetBacklight);
  webServer.on("/firmware", webHandleFirmware);
  webServer.on("/espfirmware", webHandleEspFirmware);
  webServer.on(
      "/lcdupload", HTTP_POST, []()
      { webServer.send(200); },
      webHandleLcdUpload);
  webServer.on("/tftFileSize", webHandleTftFileSize);
  webServer.on("/lcddownload", webHandleLcdDownload);
  webServer.on("/lcdOtaSuccess", webHandleLcdUpdateSuccess);
  webServer.on("/lcdOtaFailure", webHandleLcdUpdateFailure);
  webServer.on("/reboot", webHandleReboot);
  webServer.onNotFound(webHandleNotFound);
  webServer.begin();
  debugPrintln(String(F("HTTP: Server started @ http://")) + WiFi.localIP().toString());

  espSetupOta(); // Start OTA firmware update

  motionSetup(); // Setup motion sensor if configured

  mqttConnect(); // Connect to MQTT

  if (mdnsEnabled)
  { // Setup mDNS service discovery if enabled
    hMDNSService = MDNS.addService(haspNode, "http", "tcp", 80);
    if (debugTelnetEnabled)
    {
      MDNS.addService(haspNode, "telnet", "tcp", 23);
    }
    MDNS.addServiceTxt(hMDNSService, "app_name", "HASwitchPlate");
    MDNS.addServiceTxt(hMDNSService, "app_version", String(haspVersion).c_str());
    MDNS.update();
  }

  if (beepEnabled)
  { // Setup beep/tactile output if configured
    pinMode(beepPin, OUTPUT);
  }

  if (debugTelnetEnabled)
  { // Setup telnet server for remote debug output
    telnetServer.setNoDelay(true);
    telnetServer.begin();
    debugPrintln(String(F("TELNET: debug server enabled at telnet:")) + WiFi.localIP().toString());
  }
  //BME680 ADD begin
  EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1); // 1st address for the length
  Wire.begin();

  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);
  outputBME680 = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  debugPrintln(String(outputBME680));
  checkIaqSensorStatus();

  iaqSensor.setConfig(bsec_config_iaq);
  checkIaqSensorStatus();

  loadState();
  
  bsec_virtual_sensor_t sensorList1[5] = {
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
  };

  iaqSensor.updateSubscription(sensorList1, 5, BSEC_SAMPLE_RATE_ULP);
  checkIaqSensorStatus();
  
  bsec_virtual_sensor_t sensorList2[5] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(sensorList2, 5, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();
  

  //BME680 ADD end
  //MAX44009 ADD begin
  if(light.begin())
  {
    debugPrintln(String(F("Could not find a valid MAX44009 sensor, check wiring!")));
  }
  //MAX44009 ADD end
  //ADC ADD begin
  pinMode(A0, INPUT);
  //ADC ADD end
  debugPrintln(F("SYSTEM: System init complete."));
}

//BME680 ADD begin
void bme680Handle(){

    if (iaqSensor.run()) {
  debugPrintln(String(F("Sampling bme680")));
      float_bme680_rawtemperature = iaqSensor.rawTemperature;
      
float_bme680_rawpressure = iaqSensor.pressure;
float_bme680_rawhumidity = iaqSensor.rawHumidity;
float_bme680_rawgasresistor = iaqSensor.gasResistance;
float_bme680_iaq = iaqSensor.iaq;
float_bme680_iaqaccuracy = iaqSensor.iaqAccuracy;
float_bme680_staticiaq = iaqSensor.staticIaq;
float_bme680_co2equivalent = iaqSensor.co2Equivalent;
float_bme680_breathvocequivalent = iaqSensor.breathVocEquivalent;
float_bme680_compensatedtemperature = iaqSensor.temperature;
float_bme680_compensatedhumidity = iaqSensor.humidity;
       updateState();

  } else {
    checkIaqSensorStatus();
  }


}
    //BME680 ADD end



////////////////////////////////////////////////////////////////////////////////////////////////////
void loop()
{ // Main execution loop
  while ((WiFi.status() != WL_CONNECTED) || (WiFi.localIP().toString() == "0.0.0.0"))
  { // Check WiFi is connected and that we have a valid IP, retry until we do.
    if (WiFi.status() == WL_CONNECTED)
    { // If we're currently connected, disconnect so we can try again
      WiFi.disconnect();
    }
    espWifiReconnect();
  }

  if (!mqttClient.connected())
  { // Check MQTT connection
    debugPrintln(String(F("MQTT: not connected, connecting.")));
    mqttConnect();
  }
  if ((millis() - statusUpdateTimerBME680) >= statusUpdateIntervalBME680){
  //BME680 ADD begin
  statusUpdateTimerBME680 = millis();
  bme680Handle();
  //BME680 ADD end
  }
  
  
  nextionHandleInput();     // Nextion serial communications loop
  mqttClient.loop();        // MQTT client loop
  ArduinoOTA.handle();      // Arduino OTA loop
  webServer.handleClient(); // webServer loop
  telnetHandleClient();     // telnet client loop
  motionHandle();           // motion sensor loop
  beepHandle();             // beep feedback loop

  if (mdnsEnabled)
  {
    MDNS.update();
  }

  if ((millis() - statusUpdateTimer) >= statusUpdateInterval)
  { // Run periodic status update
    statusUpdateTimer = millis();
    mqttStatusUpdate();
  }

  if (((millis() - updateCheckTimer) >= updateCheckInterval) && (millis() > updateCheckFirstRun))
  { // Run periodic update check
    updateCheckTimer = millis();
    if (updateCheck())
    { // Publish new status if updateCheck() worked and reset the timer
      statusUpdateTimer = millis();
      mqttStatusUpdate();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions

////////////////////////////////////////////////////////////////////////////////////////////////////
void mqttConnect()
{ // MQTT connection and subscriptions

  static bool mqttFirstConnect = true; // For the first connection, we want to send an OFF/ON state to
                                       // trigger any automations, but skip that if we reconnect while
                                       // still running the sketch
  rebootOnp0b1 = true;
  static uint8_t mqttReconnectCount = 0;
  unsigned long mqttConnectTimer = 0;
  const unsigned long mqttConnectTimeout = 5000;

  // Check to see if we have a broker configured and notify the user if not
  if (strcmp(mqttServer, "") == 0)
  {
    nextionSendCmd("page 0");
    nextionSetAttr("p[0].b[1].font", "6");
    nextionSetAttr("p[0].b[1].txt", "\"WiFi Connected!\\r " + String(WiFi.SSID()) + "\\rIP: " + WiFi.localIP().toString() + "\\r\\rConfigure MQTT:\\rhttp://" + WiFi.localIP().toString() + "\"");
    while (strcmp(mqttServer, "") == 0)
    { // Handle other stuff while we're waiting for MQTT to be configured
      yield();
      nextionHandleInput();     // Nextion serial communications loop
      ArduinoOTA.handle();      // Arduino OTA loop
      webServer.handleClient(); // webServer loop
      telnetHandleClient();     // telnet client loop
      motionHandle();           // motion sensor loop
      beepHandle();             // beep feedback loop
    }
  }

  if (mqttTlsEnabled)
  { // Create MQTT service object with TLS connection
    mqttClient.begin(mqttServer, atoi(mqttPort), mqttClientSecure);
    if (strcmp(mqttFingerprint, "") == 0)
    {
      debugPrintln(String(F("MQTT: Configuring MQTT TLS connection without fingerprint validation.")));
      mqttClientSecure.setInsecure();
    }
    else
    {
      debugPrintln(String(F("MQTT: Configuring MQTT TLS connection with fingerprint validation.")));
      mqttClientSecure.allowSelfSignedCerts();
      mqttClientSecure.setFingerprint(mqttFingerprint);
    }
    mqttClientSecure.setBufferSizes(512, 512);
  }
  else
  { // Create MQTT service object without TLS connection
    debugPrintln(String(F("MQTT: Configuring MQTT connection without TLS.")));
    mqttClient.begin(mqttServer, atoi(mqttPort), wifiClient);
  }

  mqttClient.onMessage(mqttProcessInput); // Setup MQTT callback function

  // MQTT topic string definitions
  mqttStateTopic = "hasp/" + String(haspNode) + "/state";
  mqttStateJSONTopic = "hasp/" + String(haspNode) + "/state/json";
  mqttCommandTopic = "hasp/" + String(haspNode) + "/command";
  mqttGroupCommandTopic = "hasp/" + String(groupName) + "/command";
  mqttStatusTopic = "hasp/" + String(haspNode) + "/status";
  mqttSensorTopic = "hasp/" + String(haspNode) + "/sensor";
  mqttLightCommandTopic = "hasp/" + String(haspNode) + "/light/switch";
  mqttLightStateTopic = "hasp/" + String(haspNode) + "/light/state";
  mqttLightBrightCommandTopic = "hasp/" + String(haspNode) + "/brightness/set";
  mqttLightBrightStateTopic = "hasp/" + String(haspNode) + "/brightness/state";
  mqttMotionStateTopic = "hasp/" + String(haspNode) + "/motion/state";

  const String mqttCommandSubscription = mqttCommandTopic + "/#";
  const String mqttGroupCommandSubscription = mqttGroupCommandTopic + "/#";
  const String mqttLightSubscription = mqttLightCommandTopic + "/#";
  const String mqttLightBrightSubscription = mqttLightBrightCommandTopic + "/#";

  // Generate an MQTT client ID as haspNode + our MAC address
  mqttClientId = String(haspNode) + "-" + String(espMac[0], HEX) + String(espMac[1], HEX) + String(espMac[2], HEX) + String(espMac[3], HEX) + String(espMac[4], HEX) + String(espMac[5], HEX);
  nextionSendCmd("page 0");
  nextionSetAttr("p[0].b[1].font", "6");
  nextionSetAttr("p[0].b[1].txt", "\"WiFi Connected!\\r " + String(WiFi.SSID()) + "\\rIP: " + WiFi.localIP().toString() + "\\r\\rMQTT Connecting:\\r " + String(mqttServer) + "\"");
  if (mqttTlsEnabled)
  {
    debugPrintln(String(F("MQTT: Attempting connection to broker ")) + String(mqttServer) + String(F(" on port ")) + String(mqttPort) + String(F(" with TLS enabled as clientID ")) + mqttClientId);
  }
  else
  {
    debugPrintln(String(F("MQTT: Attempting connection to broker ")) + String(mqttServer) + String(F(" on port ")) + String(mqttPort) + String(F(" with TLS disabled as clientID ")) + mqttClientId);
  }

  // Set keepAlive, cleanSession, timeout
  mqttClient.setOptions(30, true, mqttConnectTimeout);

  // declare LWT
  mqttClient.setWill(mqttStatusTopic.c_str(), "OFF", true, 1);

  while (!mqttClient.connected())
  { // Loop until we're connected to MQTT
    mqttConnectTimer = millis();
    mqttClient.connect(mqttClientId.c_str(), mqttUser, mqttPassword, false);

    if (mqttClient.connected())
    { // Attempt to connect to broker, setting last will and testament
      // Update panel with MQTT status
      nextionSetAttr("p[0].b[1].txt", "\"WiFi Connected!\\r " + String(WiFi.SSID()) + "\\rIP: " + WiFi.localIP().toString() + "\\r\\rMQTT Connected:\\r " + String(mqttServer) + "\"");
      debugPrintln(F("MQTT: connected"));

      // Reset our diagnostic booleans
      mqttPingCheck = true;
      mqttPortCheck = true;

      // Subscribe to our incoming topics
      if (mqttClient.subscribe(mqttCommandSubscription))
      {
        debugPrintln(String(F("MQTT: subscribed to ")) + mqttCommandSubscription);
      }
      if (mqttClient.subscribe(mqttGroupCommandSubscription))
      {
        debugPrintln(String(F("MQTT: subscribed to ")) + mqttGroupCommandSubscription);
      }
      if (mqttClient.subscribe(mqttLightSubscription))
      {
        debugPrintln(String(F("MQTT: subscribed to ")) + mqttLightSubscription);
      }
      if (mqttClient.subscribe(mqttLightBrightSubscription))
      {
        debugPrintln(String(F("MQTT: subscribed to ")) + mqttLightBrightSubscription);
      }

      // Publish discovery configuration
      mqttDiscovery();

      // Publish backlight status
      if (lcdBacklightOn)
      {
        debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'ON'")));
        mqttClient.publish(mqttLightStateTopic, "ON", true, 1);
      }
      else
      {
        debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'OFF'")));
        mqttClient.publish(mqttLightStateTopic, "OFF", true, 1);
      }
      debugPrintln(String(F("MQTT OUT: '")) + mqttLightBrightStateTopic + String(F("' : ")) + String(lcdBacklightDim));
      mqttClient.publish(mqttLightBrightStateTopic, String(lcdBacklightDim), true, 1);

      if (mqttFirstConnect)
      { // Force any subscribed clients to toggle OFF/ON when we first connect to
        // make sure we get a full panel refresh at power on.  Sending OFF,
        // "ON" will be sent by the mqttStatusTopic subscription action below.
        mqttFirstConnect = false;
        debugPrintln(String(F("MQTT OUT: '")) + mqttStatusTopic + "' : 'OFF'");
        mqttClient.publish(mqttStatusTopic, "OFF", true, 0);
      }

      if (mqttClient.subscribe(mqttStatusTopic))
      {
        debugPrintln(String(F("MQTT: subscribed to ")) + mqttStatusTopic);
      }
      mqttClient.loop();
    }
    else
    { // Retry until we give up and restart after connectTimeout seconds
      mqttReconnectCount++;
      if (mqttReconnectCount * mqttConnectTimeout * 6 > (connectTimeout * 1000))
      {
        debugPrintln(String(F("MQTT: connection attempt ")) + String(mqttReconnectCount) + String(F(" failed with rc: ")) + String(mqttClient.returnCode()) + String(F(" and error: ")) + String(mqttClient.lastError()) + String(F(". Restarting device.")));
        espReset();
      }
      yield();
      webServer.handleClient();

      String mqttCheckResult = "Ping: FAILED";
      String mqttCheckResultNextion = "MQTT Check...";

      debugPrintln(String(F("MQTT: connection attempt ")) + String(mqttReconnectCount) + String(F(" failed with rc ")) + String(mqttClient.returnCode()) + String(F(" and error: ")) + String(mqttClient.lastError()));
      nextionSetAttr("p[0].b[1].txt", String(F("\"WiFi Connected!\\r ")) + String(WiFi.SSID()) + String(F("\\rIP: ")) + WiFi.localIP().toString() + String(F("\\r\\rMQTT Failed:\\r ")) + String(mqttServer) + String(F("\\rRC: ")) + String(mqttClient.returnCode()) + String(F("   Error: ")) + String(mqttClient.lastError()) + String(F("\\r")) + mqttCheckResultNextion + String(F("\"")));

      mqttPingCheck = Ping.ping(mqttServer, 4);
      yield();
      webServer.handleClient();
      mqttPortCheck = wifiClient.connect(mqttServer, atoi(mqttPort));
      yield();
      webServer.handleClient();

      mqttCheckResultNextion = "Ping: ";
      if (mqttPingCheck)
      {
        mqttCheckResult = "Ping: SUCCESS";
        mqttCheckResultNextion = "Ping: ";
      }
      if (mqttPortCheck)
      {
        mqttCheckResult += " Port: SUCCESS";
        mqttCheckResultNextion += " Port: ";
      }
      else
      {
        mqttCheckResult += " Port: FAILED";
        mqttCheckResultNextion += " Port: ";
      }
      debugPrintln(String(F("MQTT: connection checks: ")) + mqttCheckResult + String(F(". Trying again in 30 seconds.")));
      nextionSetAttr("p[0].b[1].txt", String(F("\"WiFi Connected!\\r ")) + String(WiFi.SSID()) + String(F("\\rIP: ")) + WiFi.localIP().toString() + String(F("\\r\\rMQTT Failed:\\r ")) + String(mqttServer) + String(F("\\rRC: ")) + String(mqttClient.returnCode()) + String(F("   Error: ")) + String(mqttClient.lastError()) + String(F("\\r")) + mqttCheckResultNextion + String(F("\"")));

      while (millis() < (mqttConnectTimer + (mqttConnectTimeout * 6)))
      {
        yield();
        nextionHandleInput();     // Nextion serial communications loop
        ArduinoOTA.handle();      // Arduino OTA loop
        webServer.handleClient(); // webServer loop
        telnetHandleClient();     // telnet client loop
        motionHandle();           // motion sensor loop
        beepHandle();             // beep feedback loop
      }
    }
  }
  rebootOnp0b1 = false;
  if (nextionActivePage < 0)
  { // We never picked up a message giving us a page number, so we'll just go to the default page
    debugPrintln(String(F("DEBUG: NextionActivePage not received from MQTT, setting to 0")));
    String mqttButtonJSONEvent = String(F("{\"event\":\"page\",\"value\":0}"));
    debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent, false, 0);
    String mqttPageTopic = mqttStateTopic + "/page";
    debugPrintln(String(F("MQTT OUT: '")) + mqttPageTopic + String(F("' : '0'")));
    mqttClient.publish(mqttPageTopic, "0", false, 0);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void mqttProcessInput(String &strTopic, String &strPayload)
{ // Handle incoming commands from MQTT

  // strTopic: homeassistant/haswitchplate/devicename/command/p[1].b[4].txt
  // strPayload: "Lights On"
  // subTopic: p[1].b[4].txt

  // Incoming Namespace (replace /device/ with /group/ for group commands)
  // '[...]/device/command' -m '' == No command requested, respond with mqttStatusUpdate()
  // '[...]/device/command' -m 'dim=50' == nextionSendCmd("dim=50")
  // '[...]/device/command/json' -m '["dim=5", "page 1"]' == nextionSendCmd("dim=50"), nextionSendCmd("page 1")
  // '[...]/device/command/p[1].b[4].txt' -m '' == nextionGetAttr("p[1].b[4].txt")
  // '[...]/device/command/p[1].b[4].txt' -m '"Lights On"' == nextionSetAttr("p[1].b[4].txt", "\"Lights On\"")
  // '[...]/device/brightness/set' -m '50' == nextionSendCmd("dims=50")
  // '[...]/device/light/switch' -m 'OFF' == nextionSendCmd("dims=0")
  // '[...]/device/command/page' -m '1' == nextionSendCmd("page 1")
  // '[...]/device/command/statusupdate' -m '' == mqttStatusUpdate()
  // '[...]/device/command/discovery' -m '' == call mqttDiscovery()
  // '[...]/device/command/lcdupdate' -m 'http://192.168.0.10/local/HASwitchPlate.tft' == nextionOtaStartDownload("http://192.168.0.10/local/HASwitchPlate.tft")
  // '[...]/device/command/lcdupdate' -m '' == nextionOtaStartDownload("lcdFirmwareUrl")
  // '[...]/device/command/espupdate' -m 'http://192.168.0.10/local/HASwitchPlate.ino.d1_mini.bin' == espStartOta("http://192.168.0.10/local/HASwitchPlate.ino.d1_mini.bin")
  // '[...]/device/command/espupdate' -m '' == espStartOta("espFirmwareUrl")
  // '[...]/device/command/beep' -m '100,200,3' == beep on for 100msec, off for 200msec, repeat 3 times
  // '[...]/device/command/hassdiscovery' -m 'homeassistant' == hassDiscovery = homeassistant
  // '[...]/device/command/nextionmaxpages' -m '11' == nextionmaxpages = 11
  // '[...]/device/command/nextionbaud' -m '921600' == nextionBaud = 921600
  // '[...]/device/command/debugserialenabled' -m 'true' == enable serial debug output
  // '[...]/device/command/debugtelnetenabled' -m 'true' == enable telnet debug output
  // '[...]/device/command/mdnsenabled' -m 'true' == enable mDNS responder
  // '[...]/device/command/beepenabled' -m 'true' == enable beep output on keypress
  // '[...]/device/command/ignoretouchwhenoff' -m 'true' == disable actions on keypress

  debugPrintln(String(F("MQTT IN: '")) + strTopic + String(F("' : '")) + strPayload + String(F("'")));

  if (((strTopic == mqttCommandTopic) || (strTopic == mqttGroupCommandTopic)) && (strPayload == ""))
  {                     // '[...]/device/command' -m '' = No command requested, respond with mqttStatusUpdate()
    mqttStatusUpdate(); // return status JSON via MQTT
  }
  else if (strTopic == mqttCommandTopic || strTopic == mqttGroupCommandTopic)
  { // '[...]/device/command' -m 'dim=50' == nextionSendCmd("dim=50")
    nextionSendCmd(strPayload);
  }
  else if (strTopic == (mqttCommandTopic + "/page") || strTopic == (mqttGroupCommandTopic + "/page"))
  { // '[...]/device/command/page' -m '1' == nextionSendCmd("page 1")
    if (strPayload == "")
    {
      nextionSendCmd("sendme");
    }
    else
    {
      nextionActivePage = strPayload.toInt();
      nextionSendCmd("page " + strPayload);
    }
  }
  else if (strTopic == (mqttCommandTopic + "/json") || strTopic == (mqttGroupCommandTopic + "/json"))
  { // '[...]/device/command/json' -m '["dim=5", "page 1"]' = nextionSendCmd("dim=50"), nextionSendCmd("page 1")
    if (strPayload != "")
    {
      nextionParseJson(strPayload); // Send to nextionParseJson()
    }
  }
  else if (strTopic == (mqttCommandTopic + "/statusupdate") || strTopic == (mqttGroupCommandTopic + "/statusupdate"))
  {                     // '[...]/device/command/statusupdate' == mqttStatusUpdate()
    mqttStatusUpdate(); // return status JSON via MQTT
  }
  else if (strTopic == (mqttCommandTopic + "/discovery") || strTopic == (mqttGroupCommandTopic + "/discovery"))
  {                  // '[...]/device/command/discovery' == mqttDiscovery()
    mqttDiscovery(); // send Home Assistant discovery message via MQTT
  }
  else if (strTopic == (mqttCommandTopic + "/hassdiscovery") || strTopic == (mqttGroupCommandTopic + "/hassdiscovery"))
  {                                             // '[...]/device/command/hassdiscovery' -m 'homeassistant' == hassDiscovery = homeassistant
    strPayload.toCharArray(hassDiscovery, 128); // set hassDiscovery to value provided in payload
    configSave();
    mqttDiscovery(); // send Home Assistant discovery message on new discovery topic via MQTT
  }
  else if ((strTopic == (mqttCommandTopic + "/nextionmaxpages") || strTopic == (mqttGroupCommandTopic + "/nextionmaxpages")) && (strPayload.toInt() < 256) && (strPayload.toInt() > 0))
  {                                       // '[...]/device/command/nextionmaxpages' -m '11' == nextionmaxpages = 11
    nextionMaxPages = strPayload.toInt(); // set nextionMaxPages to value provided in payload
    configSave();
    mqttDiscovery(); // send Home Assistant discovery message via MQTT
  }
  else if ((strTopic == (mqttCommandTopic + "/nextionbaud") || strTopic == (mqttGroupCommandTopic + "/nextionbaud")) &&
           ((strPayload.toInt() == 2400) ||
            (strPayload.toInt() == 4800) ||
            (strPayload.toInt() == 9600) ||
            (strPayload.toInt() == 19200) ||
            (strPayload.toInt() == 31250) ||
            (strPayload.toInt() == 38400) ||
            (strPayload.toInt() == 57600) ||
            (strPayload.toInt() == 115200) ||
            (strPayload.toInt() == 230400) ||
            (strPayload.toInt() == 250000) ||
            (strPayload.toInt() == 256000) ||
            (strPayload.toInt() == 512000) ||
            (strPayload.toInt() == 921600)))
  {                                         // '[...]/device/command/nextionbaud' -m '921600' == nextionBaud = 921600
    strPayload.toCharArray(nextionBaud, 7); // set nextionBaud to value provided in payload
    nextionAckEnable = false;
    nextionSendCmd("bauds=" + strPayload); // send baud rate to nextion
    nextionAckEnable = true;
    Serial.flush();
    Serial1.flush();
    Serial.end();
    Serial1.end();
    Serial.begin(atoi(nextionBaud));  // Serial - LCD RX (after swap), debug TX
    Serial1.begin(atoi(nextionBaud)); // Serial1 - LCD TX, no RX
    Serial.swap();                    // Swap to allow hardware UART comms to LCD
    configSave();
  }
  else if (strTopic == (mqttCommandTopic + "/debugserialenabled") || strTopic == (mqttGroupCommandTopic + "/debugserialenabled"))
  { // '[...]/device/command/debugserialenabled' -m 'true' == enable serial debug output
    if (strPayload.equalsIgnoreCase("true"))
    {
      debugSerialEnabled = true;
      configSave();
    }
    else if (strPayload.equalsIgnoreCase("false"))
    {
      debugSerialEnabled = false;
      configSave();
    }
  }
  else if (strTopic == (mqttCommandTopic + "/debugtelnetenabled") || strTopic == (mqttGroupCommandTopic + "/debugtelnetenabled"))
  { // '[...]/device/command/debugtelnetenabled' -m 'true' == enable telnet debug output
    if (strPayload.equalsIgnoreCase("true"))
    {
      debugTelnetEnabled = true;
      configSave();
    }
    else if (strPayload.equalsIgnoreCase("false"))
    {
      debugTelnetEnabled = false;
      configSave();
    }
  }
  else if (strTopic == (mqttCommandTopic + "/mdnsenabled") || strTopic == (mqttGroupCommandTopic + "/mdnsenabled"))
  { // '[...]/device/command/mdnsenabled' -m 'true' == enable mDNS responder
    if (strPayload.equalsIgnoreCase("true"))
    {
      mdnsEnabled = true;
      configSave();
    }
    else if (strPayload.equalsIgnoreCase("false"))
    {
      mdnsEnabled = false;
      configSave();
    }
  }
  else if (strTopic == (mqttCommandTopic + "/beepenabled") || strTopic == (mqttGroupCommandTopic + "/beepenabled"))
  { // '[...]/device/command/beepenabled' -m 'true' == enable beep output on keypress
    if (strPayload.equalsIgnoreCase("true"))
    {
      beepEnabled = true;
      configSave();
    }
    else if (strPayload.equalsIgnoreCase("false"))
    {
      beepEnabled = false;
      configSave();
    }
  }
  else if (strTopic == (mqttCommandTopic + "/ignoretouchwhenoff") || strTopic == (mqttGroupCommandTopic + "/ignoretouchwhenoff"))
  { // '[...]/device/command/ignoretouchwhenoff' -m 'true' == disable actions on keypress
    if (strPayload.equalsIgnoreCase("true"))
    {
      ignoreTouchWhenOff = true;
      configSave();
    }
    else if (strPayload.equalsIgnoreCase("false"))
    {
      ignoreTouchWhenOff = false;
      configSave();
    }
  }
  else if (strTopic == (mqttCommandTopic + "/lcdupdate") || strTopic == (mqttGroupCommandTopic + "/lcdupdate"))
  { // '[...]/device/command/lcdupdate' -m 'http://192.168.0.10/local/HASwitchPlate.tft' == nextionOtaStartDownload("http://192.168.0.10/local/HASwitchPlate.tft")
    if (strPayload == "")
    {
      nextionOtaStartDownload(lcdFirmwareUrl);
    }
    else
    {
      nextionOtaStartDownload(strPayload);
    }
  }
  else if (strTopic == (mqttCommandTopic + "/espupdate") || strTopic == (mqttGroupCommandTopic + "/espupdate"))
  { // '[...]/device/command/espupdate' -m 'http://192.168.0.10/local/HASwitchPlate.ino.d1_mini.bin' == espStartOta("http://192.168.0.10/local/HASwitchPlate.ino.d1_mini.bin")
    if (strPayload == "")
    {
      espStartOta(espFirmwareUrl);
    }
    else
    {
      espStartOta(strPayload);
    }
  }
  else if (strTopic == (mqttCommandTopic + "/reboot") || strTopic == (mqttGroupCommandTopic + "/reboot"))
  { // '[...]/device/command/reboot' == reboot microcontroller
    debugPrintln(F("MQTT: Rebooting device"));
    espReset();
  }
  else if (strTopic == (mqttCommandTopic + "/lcdreboot") || strTopic == (mqttGroupCommandTopic + "/lcdreboot"))
  { // '[...]/device/command/lcdreboot' == reboot LCD panel
    debugPrintln(F("MQTT: Rebooting LCD"));
    nextionReset();
  }
  else if (strTopic == (mqttCommandTopic + "/factoryreset") || strTopic == (mqttGroupCommandTopic + "/factoryreset"))
  { // '[...]/device/command/factoryreset' == clear all saved settings
    configClearSaved();
  }
  else if (strTopic == (mqttCommandTopic + "/beep") || strTopic == (mqttGroupCommandTopic + "/beep"))
  { // '[...]/device/command/beep' == activate beep function
    String mqttvar1 = getSubtringField(strPayload, ',', 0);
    String mqttvar2 = getSubtringField(strPayload, ',', 1);
    String mqttvar3 = getSubtringField(strPayload, ',', 2);

    beepOnTime = mqttvar1.toInt();
    beepOffTime = mqttvar2.toInt();
    beepCounter = mqttvar3.toInt();
  }
  else if (strTopic == (mqttCommandTopic + "/crashtest"))
  { // '[...]/device/command/crashtest' -m 'divzero' == divide by zero
    if (strPayload == "divzero")
    {
      debugPrintln(String(F("DEBUG: attempt to divide by zero")));
      int result, zero;
      zero = 0;
      result = 1 / zero;
      debugPrintln(String(F("DEBUG: div zero result: ")) + String(result));
    }
    else if (strPayload == "nullptr")
    { // '[...]/device/command/crashtest' -m 'nullptr' == dereference a null pointer
      debugPrintln(String(F("DEBUG: attempt to dereference null pointer")));
      int *nullPointer = NULL;
      debugPrintln(String(F("DEBUG: dereference null pointer: ")) + String(*nullPointer));
    }
    else if (strPayload == "wdt")
    { // '[...]/device/command/crashtest' -m 'wdt' == trigger soft WDT
      debugPrintln(String(F("DEBUG: enter tight loop and cause WDT")));
      while (true)
      {
      }
    }
  }
  else if (strTopic.startsWith(mqttCommandTopic) && (strPayload == ""))
  { // '[...]/device/command/p[1].b[4].txt' -m '' == nextionGetAttr("p[1].b[4].txt")
    String subTopic = strTopic.substring(mqttCommandTopic.length() + 1);
    mqttGetSubtopic = "/" + subTopic;
    nextionGetAttr(subTopic);
  }
  else if (strTopic.startsWith(mqttGroupCommandTopic) && (strPayload == ""))
  { // '[...]/group/command/p[1].b[4].txt' -m '' == nextionGetAttr("p[1].b[4].txt")
    String subTopic = strTopic.substring(mqttGroupCommandTopic.length() + 1);
    mqttGetSubtopic = "/" + subTopic;
    nextionGetAttr(subTopic);
  }
  else if (strTopic.startsWith(mqttCommandTopic))
  { // '[...]/device/command/p[1].b[4].txt' -m '"Lights On"' == nextionSetAttr("p[1].b[4].txt", "\"Lights On\"")
    String subTopic = strTopic.substring(mqttCommandTopic.length() + 1);
    nextionSetAttr(subTopic, strPayload);
  }
  else if (strTopic.startsWith(mqttGroupCommandTopic))
  { // '[...]/group/command/p[1].b[4].txt' -m '"Lights On"' == nextionSetAttr("p[1].b[4].txt", "\"Lights On\"")
    String subTopic = strTopic.substring(mqttGroupCommandTopic.length() + 1);
    nextionSetAttr(subTopic, strPayload);
  }
  else if (strTopic == mqttLightBrightCommandTopic)
  { // change the brightness from the light topic
    nextionSetAttr("dim", strPayload);
    nextionSetAttr("dims", "dim");
    lcdBacklightDim = strPayload.toInt();
    debugPrintln(String(F("MQTT OUT: '")) + mqttLightBrightStateTopic + String(F("' : '")) + strPayload + String(F("'")));
    mqttClient.publish(mqttLightBrightStateTopic, strPayload, true, 0);
  }
  else if (strTopic == mqttLightCommandTopic && strPayload == "OFF")
  { // set the panel dim OFF from the light topic, saving current dim level first
    nextionSetAttr("dims", "dim");
    nextionSetAttr("dim", "0");
    lcdBacklightOn = 0;
    debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'OFF'")));
    mqttClient.publish(mqttLightStateTopic, "OFF", true, 0);
  }
  else if (strTopic == mqttLightCommandTopic && strPayload == "ON")
  { // set the panel dim ON from the light topic, restoring saved dim level
    nextionSetAttr("dim", "dims");
    nextionSetAttr("sleep", "0");
    lcdBacklightOn = 1;
    debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'ON'")));
    mqttClient.publish(mqttLightStateTopic, "ON", true, 0);
  }
  else if (strTopic == mqttStatusTopic && strPayload == "OFF")
  { // catch a dangling LWT from a previous connection if it appears
    debugPrintln(String(F("MQTT OUT: '")) + mqttStatusTopic + String(F("' : 'ON'")));
    mqttClient.publish(mqttStatusTopic, "ON", true, 0);
    mqttClient.publish(mqttStateJSONTopic, String(F("{\"event_type\":\"hasp_device\",\"event\":\"online\"}")));
    debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F(" : {\"event_type\":\"hasp_device\",\"event\":\"online\"}")));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void mqttStatusUpdate()
{ 
  //ADC ADD begin
  uint_voltageraw = analogRead(A0);
  float_voltage = uint_voltageraw / 1023.0;
  float_voltage = float_voltage * 5.9;
  debugPrintln("Sampling Voltage: " + String(float_voltage));
  //ADC ADD end
  // Periodically publish system status
  String mqttSensorPayload = "{";
  mqttSensorPayload += String(F("\"espVersion\":")) + String(haspVersion) + String(F(","));
  if (updateEspAvailable)
  {
    mqttSensorPayload += String(F("\"updateEspAvailable\":true,"));
  }
  else
  {
    mqttSensorPayload += String(F("\"updateEspAvailable\":false,"));
  }
  if (lcdConnected)
  {
    mqttSensorPayload += String(F("\"lcdConnected\":true,"));
  }
  else
  {
    mqttSensorPayload += String(F("\"lcdConnected\":false,"));
  }
  mqttSensorPayload += String(F("\"lcdVersion\":\"")) + String(lcdVersion) + String(F("\","));
  if (updateLcdAvailable)
  {
    mqttSensorPayload += String(F("\"updateLcdAvailable\":true,"));
  }
  else
  {
    mqttSensorPayload += String(F("\"updateLcdAvailable\":false,"));
  }
  mqttSensorPayload += String(F("\"espUptime\":")) + String(long(millis() / 1000)) + String(F(","));
  mqttSensorPayload += String(F("\"signalStrength\":")) + String(WiFi.RSSI()) + String(F(","));
  mqttSensorPayload += String(F("\"haspName\":\"")) + String(haspNode) + String(F("\","));
  mqttSensorPayload += String(F("\"haspIP\":\"")) + WiFi.localIP().toString() + String(F("\","));
  mqttSensorPayload += String(F("\"haspClientID\":\"")) + mqttClientId + String(F("\","));
  mqttSensorPayload += String(F("\"haspMac\":\"")) + String(espMac[0], HEX) + String(F(":")) + String(espMac[1], HEX) + String(F(":")) + String(espMac[2], HEX) + String(F(":")) + String(espMac[3], HEX) + String(F(":")) + String(espMac[4], HEX) + String(F(":")) + String(espMac[5], HEX) + String(F("\","));
  mqttSensorPayload += String(F("\"haspManufacturer\":\"HASwitchPlate\",\"haspModel\":\"HASPone v1.0.0\","));
  mqttSensorPayload += String(F("\"heapFree\":")) + String(ESP.getFreeHeap()) + String(F(","));
  mqttSensorPayload += String(F("\"heapFragmentation\":")) + String(ESP.getHeapFragmentation()) + String(F(","));
  mqttSensorPayload += String(F("\"heapMaxFreeBlockSize\":")) + String(ESP.getMaxFreeBlockSize()) + String(F(","));
  mqttSensorPayload += String(F("\"espCore\":\"")) + String(ESP.getCoreVersion()) + String(F("\""));
  //bme680 add begin
  mqttSensorPayload += String(F(",")) + String(F("\"bme680_rawtemperature\":")) + String(float_bme680_rawtemperature) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_rawpressure\":")) + String(float_bme680_rawpressure) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_rawhumidity\":")) + String(float_bme680_rawhumidity) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_rawgasresistor\":")) + String(float_bme680_rawgasresistor) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_iaq\":")) + String(float_bme680_iaq) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_staticiaq\":")) + String(float_bme680_staticiaq) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_iaqaccuracy\":")) + String(float_bme680_iaqaccuracy) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_co2equivalent\":")) + String(float_bme680_co2equivalent) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_breathvocequivalent\":")) + String(float_bme680_breathvocequivalent) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_compensatedtemperature\":")) + String(float_bme680_compensatedtemperature) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_compensatedhumidity\":")) + String(float_bme680_compensatedhumidity) + String(F(","));
  mqttSensorPayload += String(F("\"bme680_errorcode\":")) + string_bme680_errocode + String(F(","));
  //bme680 add end
  //max44009 add begin
  mqttSensorPayload += String(F("\"lux\":")) + String(light.get_lux()) + String(F(","));
  //max44009 add end
  //ADC add begin
  mqttSensorPayload += String(F("\"adc\":")) + String(float_voltage) + String(F(""));
  //ADC add end
  mqttSensorPayload += "}";

  // Publish sensor JSON
  mqttClient.publish(mqttSensorTopic, mqttSensorPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttSensorTopic + String(F("' : '")) + mqttSensorPayload + String(F("'")));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void mqttDiscovery()
{ // Publish Home Assistant discovery messages

  String macAddress = String(espMac[0], HEX) + String(F(":")) + String(espMac[1], HEX) + String(F(":")) + String(espMac[2], HEX) + String(F(":")) + String(espMac[3], HEX) + String(F(":")) + String(espMac[4], HEX) + String(F(":")) + String(espMac[5], HEX);

  // light discovery for backlight
  String mqttDiscoveryTopic = String(hassDiscovery) + String(F("/light/")) + String(haspNode) + String(F("/config"));
  String mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" backlight\",\"command_topic\":\"")) + mqttLightCommandTopic + String(F("\",\"state_topic\":\"")) + mqttLightStateTopic + String(F("\",\"brightness_state_topic\":\"")) + mqttLightBrightStateTopic + String(F("\",\"brightness_command_topic\":\"")) + mqttLightBrightCommandTopic + String(F("\",\"availability_topic\":\"")) + mqttStatusTopic + String(F("\",\"brightness_scale\":100,\"unique_id\":\"")) + mqttClientId + String(F("-backlight\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"payload_available\":\"ON\",\"payload_not_available\":\"OFF\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // sensor discovery for device telemetry
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/sensor/")) + String(haspNode) + String(F("/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" sensor\",\"json_attributes_topic\":\"")) + mqttSensorTopic + String(F("\",\"state_topic\":\"")) + mqttStatusTopic + String(F("\",\"unique_id\":\"")) + mqttClientId + String(F("-sensor\",\"icon\":\"mdi:cellphone-text\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // number discovery for active page
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/number/")) + String(haspNode) + String(F("/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" active page\",\"command_topic\":\"")) + mqttCommandTopic + String(F("/page\",\"state_topic\":\"")) + mqttStateTopic + String(F("/page\",\"step\":1,\"min\":0,\"max\":")) + String(nextionMaxPages) + String(F(",\"retain\":true,\"optimistic\":true,\"icon\":\"mdi:page-next-outline\",\"unique_id\":\"")) + mqttClientId + String(F("-page\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // AlwaysOn topic for RGB lights
  mqttClient.publish((String(F("hasp/")) + String(haspNode) + String(F("/alwayson"))), "ON", true, 1);
  debugPrintln(String(F("MQTT OUT: 'hasp/")) + String(haspNode) + String(F("/alwayson' : 'ON'")));

  // rgb light discovery for selectedforegroundcolor
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/light/")) + String(haspNode) + String(F("/selectedforegroundcolor/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" selected foreground color\",\"command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/selectedforegroundcolor/switch\",\"state_topic\":\"hasp/")) + String(haspNode) + String(F("/alwayson\",\"rgb_command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/selectedforegroundcolor/rgb\",\"rgb_command_template\":\"{{(red|bitwise_and(248)*256)+(green|bitwise_and(252)*8)+(blue|bitwise_and(248)/8)|int }}\",\"retain\":true,\"unique_id\":\"")) + mqttClientId + String(F("-selectedforegroundcolor\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // rgb light discovery for selectedbackgroundcolor
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/light/")) + String(haspNode) + String(F("/selectedbackgroundcolor/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" selected background color\",\"command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/selectedbackgroundcolor/switch\",\"state_topic\":\"hasp/")) + String(haspNode) + String(F("/alwayson\",\"rgb_command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/selectedbackgroundcolor/rgb\",\"rgb_command_template\":\"{{(red|bitwise_and(248)*256)+(green|bitwise_and(252)*8)+(blue|bitwise_and(248)/8)|int }}\",\"retain\":true,\"unique_id\":\"")) + mqttClientId + String(F("-selectedbackgroundcolor\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // rgb light discovery for unselectedforegroundcolor
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/light/")) + String(haspNode) + String(F("/unselectedforegroundcolor/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" unselected foreground color\",\"command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/unselectedforegroundcolor/switch\",\"state_topic\":\"hasp/")) + String(haspNode) + String(F("/alwayson\",\"rgb_command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/unselectedforegroundcolor/rgb\",\"rgb_command_template\":\"{{(red|bitwise_and(248)*256)+(green|bitwise_and(252)*8)+(blue|bitwise_and(248)/8)|int }}\",\"retain\":true,\"unique_id\":\"")) + mqttClientId + String(F("-unselectedforegroundcolor\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  // rgb light discovery for unselectedbackgroundcolor
  mqttDiscoveryTopic = String(hassDiscovery) + String(F("/light/")) + String(haspNode) + String(F("/unselectedbackgroundcolor/config"));
  mqttDiscoveryPayload = String(F("{\"name\":\"")) + String(haspNode) + String(F(" unselected background color\",\"command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/unselectedbackgroundcolor/switch\",\"state_topic\":\"hasp/")) + String(haspNode) + String(F("/alwayson\",\"rgb_command_topic\":\"hasp/")) + String(haspNode) + String(F("/light/unselectedbackgroundcolor/rgb\",\"rgb_command_template\":\"{{(red|bitwise_and(248)*256)+(green|bitwise_and(252)*8)+(blue|bitwise_and(248)/8)|int }}\",\"retain\":true,\"unique_id\":\"")) + mqttClientId + String(F("-unselectedbackgroundcolor\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
  mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
  debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));

  if (motionEnabled)
  { // binary_sensor for motion
    String mqttDiscoveryTopic = String(hassDiscovery) + String(F("/binary_sensor/")) + String(haspNode) + String(F("-motion/config"));
    String mqttDiscoveryPayload = String(F("{\"device_class\":\"motion\",\"name\":\"")) + String(haspNode) + String(F(" motion\",\"state_topic\":\"")) + mqttMotionStateTopic + String(F("\",\"unique_id\":\"")) + mqttClientId + String(F("-motion\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device\":{\"identifiers\":[\"")) + mqttClientId + String(F("\"],\"name\":\"")) + String(haspNode) + String(F("\",\"manufacturer\":\"HASwitchPlate\",\"model\":\"HASPone v1.0.0\",\"sw_version\":")) + String(haspVersion) + String(F("}}"));
    mqttClient.publish(mqttDiscoveryTopic, mqttDiscoveryPayload, true, 1);
    debugPrintln(String(F("MQTT OUT: '")) + mqttDiscoveryTopic + String(F("' : '")) + String(mqttDiscoveryPayload) + String(F("'")));
  }

}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionHandleInput()
{ // Handle incoming serial data from the Nextion panel
  // This will collect serial data from the panel and place it into the global buffer
  // nextionReturnBuffer[nextionReturnIndex]
  unsigned long handlerTimeout = millis() + 100;
  bool nextionCommandComplete = false;
  static uint8_t nextionTermByteCnt = 0; // counter for our 3 consecutive 0xFFs

  while (Serial.available() && !nextionCommandComplete && (millis() < handlerTimeout))
  {
    byte nextionCommandByte = Serial.read();
    if (nextionCommandByte == 0xFF)
    { // check to see if we have one of 3 consecutive 0xFF which indicates the end of a command
      nextionTermByteCnt++;
      if (nextionTermByteCnt >= 3)
      { // We have received a complete command
        lcdConnected = true;
        nextionCommandComplete = true;
        nextionTermByteCnt = 0; // reset counter
      }
    }
    else
    {
      nextionTermByteCnt = 0; // reset counter if a non-term byte was encountered
    }
    nextionReturnBuffer[nextionReturnIndex] = nextionCommandByte;
    nextionReturnIndex++;
    if (nextionCommandComplete)
    {
      nextionAckReceived = true;
      nextionProcessInput();
    }
    yield();
  }
  if (millis() > handlerTimeout)
  {
    debugPrintln(String(F("HMI ERROR: nextionHandleInput timeout")));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionProcessInput()
{ // Process complete incoming serial command from the Nextion panel
  // Command reference: https://www.itead.cc/wiki/Nextion_Instruction_Set#Format_of_Device_Return_Data
  // tl;dr: command byte, command data, 0xFF 0xFF 0xFF

  if (nextionReturnBuffer[0] == 0x01)
  { // 	Instruction Successful - quietly ignore this as it will be returned after every command issued,
    //  and processing it + spitting out serial output is a huge drag on performance if serial debug is enabled.

    // debugPrintln(String(F("HMI IN: [Instruction Successful] 0x")) + String(nextionReturnBuffer[0], HEX));
    // if (mqttClient.connected())
    // {
    //   String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Instruction Successful\"}"));
    //   mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
    //   debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    // }
    nextionReturnIndex = 0; // Done handling the buffer, reset index back to 0
    return;                 // skip the rest of the tests below and return immediately
  }

  debugPrintln(String(F("HMI IN: [")) + String(nextionReturnIndex) + String(F(" bytes]: ")) + printHex8(nextionReturnBuffer, nextionReturnIndex));

  if (nextionReturnBuffer[0] == 0x00 && nextionReturnBuffer[1] == 0x00 && nextionReturnBuffer[2] == 0x00)
  { // Nextion Startup
    debugPrintln(String(F("HMI IN: [Nextion Startup] 0x00 0x00 0x00")));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x00 0x00 0x00\",\"return_code_description\":\"Nextion Startup\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x00)
  { // Invalid Instruction
    debugPrintln(String(F("HMI IN: [Invalid Instruction] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Instruction\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x02)
  { // Invalid Component ID
    debugPrintln(String(F("HMI IN: [Invalid Component ID] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Component ID\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x03)
  { // Invalid Page ID
    debugPrintln(String(F("HMI IN: [Invalid Page ID] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Page ID\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x04)
  { // Invalid Picture ID
    debugPrintln(String(F("HMI IN: [Invalid Picture ID] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Picture ID\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x05)
  { // Invalid Font ID
    debugPrintln(String(F("HMI IN: [Invalid Font ID	] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Font ID	\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x06)
  { // Invalid File Operation
    debugPrintln(String(F("HMI IN: [Invalid File Operation] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid File Operation\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x09)
  { // Invalid CRC
    debugPrintln(String(F("HMI IN: [Invalid CRC] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid CRC\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x11)
  { // Invalid Baud rate Setting
    debugPrintln(String(F("HMI IN: [Invalid Baud rate Setting] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Baud rate Setting\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x12)
  { // Invalid Waveform ID or Channel #
    debugPrintln(String(F("HMI IN: [Invalid Waveform ID or Channel #] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Waveform ID or Channel #\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1A)
  { // Invalid Variable name or attribute
    debugPrintln(String(F("HMI IN: [Invalid Variable name or attribute] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Variable name or attribute\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1B)
  { // Invalid Variable Operation
    debugPrintln(String(F("HMI IN: [Invalid Variable Operation] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Variable Operation\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1C)
  { // Assignment failed to assign
    debugPrintln(String(F("HMI IN: [Assignment failed to assign] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Assignment failed to assign\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1D)
  { // EEPROM Operation failed
    debugPrintln(String(F("HMI IN: [EEPROM Operation failed] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"EEPROM Operation failed\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1E)
  { // Invalid Quantity of Parameters
    debugPrintln(String(F("HMI IN: [Invalid Quantity of Parameters] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Invalid Quantity of Parameters\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x1F)
  { // IO Operation failed
    debugPrintln(String(F("HMI IN: [IO Operation failed] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"IO Operation failed\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x20)
  { // Escape Character Invalid
    debugPrintln(String(F("HMI IN: [Escape Character Invalid] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Escape Character Invalid\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x23)
  { // Variable name too long
    debugPrintln(String(F("HMI IN: [Variable name too long] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Variable name too long\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x24)
  { // Serial Buffer Overflow
    debugPrintln(String(F("HMI IN: [Serial Buffer Overflow] 0x")) + String(nextionReturnBuffer[0], HEX));
    if (mqttClient.connected())
    {
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextion_return_data\",\"return_code\":\"0x")) + String(nextionReturnBuffer[0], HEX) + String(F("\",\"return_code_description\":\"Serial Buffer Overflow\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }

  else if (nextionReturnBuffer[0] == 0x65)
  { // Handle incoming touch command
    // 0x65+Page ID+Component ID+TouchEvent+End
    // Return this data when the touch event created by the user is pressed.
    // Definition of TouchEvent: Press Event 0x01, Release Event 0X00
    // Example: 0x65 0x00 0x02 0x01 0xFF 0xFF 0xFF
    // Meaning: Touch Event, Page 0, Object 2, Press
    String nextionPage = String(nextionReturnBuffer[1]);
    String nextionButtonID = String(nextionReturnBuffer[2]);
    byte nextionButtonAction = nextionReturnBuffer[3];

    if (nextionButtonAction == 0x01)
    {
      debugPrintln(String(F("HMI IN: [Button ON] 'p[")) + nextionPage + "].b[" + nextionButtonID + "]'");
      if (mqttClient.connected())
      {
        // Only process touch events if screen backlight is on and configured to do so.
        if (ignoreTouchWhenOff && !lcdBacklightOn)
        {
          String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_press_disabled\",\"event\":\"p[")) + nextionPage + String(F("].b[")) + nextionButtonID + String(F("]\",\"value\":\"ON\"}"));
          mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
          debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
        }
        else
        {
          String mqttButtonTopic = mqttStateTopic + "/p[" + nextionPage + "].b[" + nextionButtonID + "]";
          mqttClient.publish(mqttButtonTopic, "ON");
          debugPrintln(String(F("MQTT OUT: '")) + mqttButtonTopic + "' : 'ON'");
          String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_short_press\",\"event\":\"p[")) + nextionPage + String(F("].b[")) + nextionButtonID + String(F("]\",\"value\":\"ON\"}"));
          mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
          debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
        }
      }
      if (beepEnabled)
      {
        beepOnTime = 500;
        beepOffTime = 100;
        beepCounter = 1;
      }
      if (rebootOnp0b1 && (nextionPage == "0") && (nextionButtonID == "1"))
      {
        debugPrintln(String(F("HMI IN: p[0].b[1] pressed during HASPone configuration, rebooting.")));
        espReset();
      }
      if (rebootOnLongPress)
      {
        rebootOnLongPressTimer = millis();
      }
    }
    else if (nextionButtonAction == 0x00)
    {
      debugPrintln(String(F("HMI IN: [Button OFF] 'p[")) + nextionPage + "].b[" + nextionButtonID + "]'");
      if (mqttClient.connected())
      {
        // Only process touch events if screen backlight is on and configured to do so.
        if (ignoreTouchWhenOff && !lcdBacklightOn)
        {
          String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_release_disabled\",\"event\":\"p[")) + nextionPage + String(F("].b[")) + nextionButtonID + String(F("]\",\"value\":\"ON\"}"));
          mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
          debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
        }
        else
        {
          String mqttButtonTopic = mqttStateTopic + "/p[" + nextionPage + "].b[" + nextionButtonID + "]";
          mqttClient.publish(mqttButtonTopic, "OFF");
          debugPrintln(String(F("MQTT OUT: '")) + mqttButtonTopic + "' : 'OFF'");
          String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_short_release\",\"event\":\"p[")) + nextionPage + String(F("].b[")) + nextionButtonID + String(F("]\",\"value\":\"OFF\"}"));
          mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
          debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
          // Now see if this object has a .val that might have been updated.  Works for sliders,
          // two-state buttons, etc, returns 0 for normal buttons
          mqttGetSubtopic = "/p[" + nextionPage + "].b[" + nextionButtonID + "].val";
          // This right here is dicey.  We're done w/ this command so reset the index allowing this to be kinda-reentrant
          // because the call to nextionGetAttr is going to call us back.
          nextionReturnIndex = 0;
          nextionGetAttr("p[" + nextionPage + "].b[" + nextionButtonID + "].val");
        }
      }
      if (rebootOnLongPress && (millis() - rebootOnLongPressTimer > rebootOnLongPressTimeout))
      {
        debugPrintln(String(F("HMI IN: Button long press, rebooting.")));
        espReset();
      }
      rebootOnLongPressTimer = millis();
    }
  }
  else if (nextionReturnBuffer[0] == 0x66)
  { // Handle incoming "sendme" page number
    // 0x66+PageNum+End
    // Example: 0x66 0x02 0xFF 0xFF 0xFF
    // Meaning: page 2
    String nextionPage = String(nextionReturnBuffer[1]);
    debugPrintln(String(F("HMI IN: [sendme Page] '")) + nextionPage + String(F("'")));
    if ((nextionPage != "0") || nextionReportPage0)
    { // If we have a new page AND ( (it's not "0") OR (we've set the flag to report 0 anyway) )

      if (mqttClient.connected())
      {
        String mqttButtonJSONEvent = String(F("{\"event\":\"page\",\"value\":")) + nextionPage + String(F("}"));
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
        mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
        String mqttPageTopic = mqttStateTopic + "/page";
        debugPrintln(String(F("MQTT OUT: '")) + mqttPageTopic + String(F("' : '")) + nextionPage + String(F("'")));
        mqttClient.publish(mqttPageTopic, nextionPage, false, 0);
      }
    }
  }
  else if (nextionReturnBuffer[0] == 0x67 || nextionReturnBuffer[0] == 0x68)
  { // Handle touch coordinate data
    // 0X67+Coordinate X High+Coordinate X Low+Coordinate Y High+Coordinate Y Low+TouchEvent+End
    // Example: 0X67 0X00 0X7A 0X00 0X1E 0X01 0XFF 0XFF 0XFF
    // Meaning: Coordinate (122,30), Touch Event: Press
    // issue Nextion command "sendxy=1" to enable this output
    // 0x68 is the same, but returned when the screen touch has awakened the screen from sleep
    uint16_t xCoord = nextionReturnBuffer[1];
    xCoord = xCoord * 256 + nextionReturnBuffer[2];
    uint16_t yCoord = nextionReturnBuffer[3];
    yCoord = yCoord * 256 + nextionReturnBuffer[4];
    String xyCoord = String(xCoord) + String(',') + String(yCoord);
    byte nextionTouchAction = nextionReturnBuffer[5];
    if (nextionTouchAction == 0x01)
    {
      debugPrintln(String(F("HMI IN: [Touch ON] '")) + xyCoord + String(F("'")));
      if (mqttClient.connected())
      {
        String mqttTouchTopic = mqttStateTopic + "/touchOn";
        mqttClient.publish(mqttTouchTopic, xyCoord);
        debugPrintln(String(F("MQTT OUT: '")) + mqttTouchTopic + String(F("' : '")) + xyCoord + String(F("'")));
        String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_short_press\",\"event\":\"touchxy\",\"touch_event\":\"ON\",\"touchx\":\"")) + String(xCoord) + String(F("\",\"touchy\":\"")) + String(yCoord) + String(F("\",\"screen_state\":\""));
        if (nextionReturnBuffer[0] == 0x67)
        {
          mqttButtonJSONEvent += "awake\"}";
        }
        else
        {
          mqttButtonJSONEvent += "asleep\"}";
        }
        mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
      }
    }
    else if (nextionTouchAction == 0x00)
    {
      debugPrintln(String(F("HMI IN: [Touch OFF] '")) + xyCoord + String(F("'")));
      if (mqttClient.connected())
      {
        String mqttTouchTopic = mqttStateTopic + "/touchOff";
        mqttClient.publish(mqttTouchTopic, xyCoord);
        debugPrintln(String(F("MQTT OUT: '")) + mqttTouchTopic + String(F("' : '")) + xyCoord + String(F("'")));
        String mqttButtonJSONEvent = String(F("{\"event_type\":\"button_short_press\",\"event\":\"touchxy\",\"touch_event\":\"OFF\",\"touchx\":\"")) + String(xCoord) + String(F("\",\"touchy\":\"")) + String(yCoord) + String(F("\",\"screen_state\":\""));
        if (nextionReturnBuffer[0] == 0x67)
        {
          mqttButtonJSONEvent += "awake\"}";
        }
        else
        {
          mqttButtonJSONEvent += "asleep\"}";
        }
        mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
      }
    }
  }
  else if (nextionReturnBuffer[0] == 0x70)
  { // Handle get string return
    // 0x70+ASCII string+End
    // Example: 0x70 0x41 0x42 0x43 0x44 0x31 0x32 0x33 0x34 0xFF 0xFF 0xFF
    // Meaning: String data, ABCD1234
    String getString;
    for (int i = 1; i < nextionReturnIndex - 3; i++)
    { // convert the payload into a string
      getString += (char)nextionReturnBuffer[i];
    }
    debugPrintln(String(F("HMI IN: [String Return] '")) + getString + String(F("'")));
    if (mqttClient.connected())
    {
      if (mqttGetSubtopic == "")
      { // If there's no outstanding request for a value, publish to mqttStateTopic
        mqttClient.publish(mqttStateTopic, getString);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateTopic + String(F("' : '")) + getString + String(F("'")));
      }
      else
      { // Otherwise, publish the to saved mqttGetSubtopic and then reset mqttGetSubtopic
        String mqttReturnTopic = mqttStateTopic + mqttGetSubtopic;
        mqttClient.publish(mqttReturnTopic, getString);
        debugPrintln(String(F("MQTT OUT: '")) + mqttReturnTopic + String(F("' : '")) + getString + String(F("'")));
        String mqttButtonJSONEvent = String(F("{\"event\":\"")) + mqttGetSubtopic.substring(1) + String(F("\",\"value\":\"")) + getString + String(F("\"}"));
        mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
        mqttGetSubtopic = "";
      }
    }
  }
  else if (nextionReturnBuffer[0] == 0x71)
  { // Handle get int return
    // 0x71+byte1+byte2+byte3+byte4+End (4 byte little endian)
    // Example: 0x71 0x7B 0x00 0x00 0x00 0xFF 0xFF 0xFF
    // Meaning: Integer data, 123
    long getInt = nextionReturnBuffer[4];
    getInt = getInt * 256 + nextionReturnBuffer[3];
    getInt = getInt * 256 + nextionReturnBuffer[2];
    getInt = getInt * 256 + nextionReturnBuffer[1];
    String getString = String(getInt);
    debugPrintln(String(F("HMI IN: [Int Return] '")) + getString + String(F("'")));

    if (lcdVersionQueryFlag)
    {
      lcdVersion = getInt;
      lcdVersionQueryFlag = false;
      debugPrintln(String(F("HMI IN: lcdVersion '")) + String(lcdVersion) + String(F("'")));
    }
    else if (lcdBacklightQueryFlag)
    {
      lcdBacklightDim = getInt;
      lcdBacklightQueryFlag = false;
      if (lcdBacklightDim > 0)
      {
        lcdBacklightOn = 1;
      }
      else
      {
        lcdBacklightOn = 0;
      }
      debugPrintln(String(F("HMI IN: lcdBacklightDim '")) + String(lcdBacklightDim) + String(F("'")));
    }
    else if (mqttGetSubtopic == "")
    {
      if (mqttClient.connected())
      {
        mqttClient.publish(mqttStateTopic, getString);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateTopic + String(F("' : '")) + getString + String(F("'")));
      }
    }
    // Otherwise, publish the to saved mqttGetSubtopic and then reset mqttGetSubtopic
    else
    {
      if (mqttClient.connected())
      {
        String mqttReturnTopic = mqttStateTopic + mqttGetSubtopic;
        mqttClient.publish(mqttReturnTopic, getString);
        debugPrintln(String(F("MQTT OUT: '")) + mqttReturnTopic + String(F("' : '")) + getString + String(F("'")));
        String mqttButtonJSONEvent = String(F("{\"event\":\"")) + mqttGetSubtopic.substring(1) + String(F("\",\"value\":")) + getString + String(F("}"));
        mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
      }
      mqttGetSubtopic = "";
    }
  }
  else if (nextionReturnBuffer[0] == 0x63 && nextionReturnBuffer[1] == 0x6f && nextionReturnBuffer[2] == 0x6d && nextionReturnBuffer[3] == 0x6f && nextionReturnBuffer[4] == 0x6b)
  { // Catch 'comok' response to 'connect' command: https://www.itead.cc/blog/nextion-hmi-upload-protocol
    String comokField;
    uint8_t comokFieldCount = 0;
    byte comokFieldSeperator = 0x2c; // ","

    for (uint8_t i = 0; i <= nextionReturnIndex; i++)
    { // cycle through each byte looking for our field seperator
      if (nextionReturnBuffer[i] == comokFieldSeperator)
      { // Found the end of a field, so do something with it.  Maybe.
        if (comokFieldCount == 2)
        {
          nextionModel = comokField;
          debugPrintln(String(F("HMI IN: nextionModel: ")) + nextionModel);
        }
        comokFieldCount++;
        comokField = "";
      }
      else
      {
        comokField += String(char(nextionReturnBuffer[i]));
      }
    }
  }
  else if (nextionReturnBuffer[0] == 0x86)
  { // Returned when Nextion enters sleep automatically. Using sleep=1 will not return an 0x86
    // 0x86+End
    if (mqttClient.connected())
    {
      lcdBacklightOn = 0;
      mqttClient.publish(mqttLightStateTopic, "OFF", true, 1);
      debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'OFF'")));
      String mqttButtonJSONEvent = String(F("{\"event\":\"sleep\",\"value\":\"ON\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x87)
  { // Returned when Nextion leaves sleep automatically. Using sleep=0 will not return an 0x87
    // 0x87+End
    if (mqttClient.connected())
    {
      lcdBacklightOn = 1;
      mqttClient.publish(mqttLightStateTopic, "ON", true, 1);
      debugPrintln(String(F("MQTT OUT: '")) + mqttLightStateTopic + String(F("' : 'ON'")));
      mqttClient.publish(mqttLightBrightStateTopic, String(lcdBacklightDim), true, 1);
      debugPrintln(String(F("MQTT OUT: '")) + mqttLightBrightStateTopic + String(F("' : ")) + String(lcdBacklightDim));
      String mqttButtonJSONEvent = String(F("{\"event\":\"sleep\",\"value\":\"OFF\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else if (nextionReturnBuffer[0] == 0x88)
  { // Returned when Nextion powers on
    // 0x88+End
    debugPrintln(F("HMI: Nextion panel connected."));
  }
  nextionReturnIndex = 0; // Done handling the buffer, reset index back to 0
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionSendCmd(const String &nextionCmd)
{ // Send a raw command to the Nextion panel
  Serial1.print(nextionCmd);
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  Serial1.flush();
  debugPrintln(String(F("HMI OUT: ")) + nextionCmd);

  if (nextionAckEnable)
  {
    nextionAckReceived = false;
    nextionAckTimer = millis();

    while ((!nextionAckReceived) && (millis() - nextionAckTimer < nextionAckTimeout))
    {
      nextionHandleInput();
    }
    if (!nextionAckReceived)
    {
      debugPrintln(String(F("HMI ERROR: Nextion Ack timeout")));
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextionError\",\"value\":\"Nextion Ack timeout\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else
  {
    nextionHandleInput();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionSetAttr(const String &hmiAttribute, const String &hmiValue)
{ // Set the value of a Nextion component attribute
  Serial1.print(hmiAttribute);
  Serial1.print("=");
  Serial1.print(hmiValue);
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  Serial1.flush();
  debugPrintln(String(F("HMI OUT: '")) + hmiAttribute + "=" + hmiValue + String(F("'")));
  if (nextionAckEnable)
  {
    nextionAckReceived = false;
    nextionAckTimer = millis();

    while ((!nextionAckReceived) || (millis() - nextionAckTimer > nextionAckTimeout))
    {
      nextionHandleInput();
    }
    if (!nextionAckReceived)
    {
      debugPrintln(String(F("HMI ERROR: Nextion Ack timeout")));
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextionError\",\"value\":\"Nextion Ack timeout\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else
  {
    nextionHandleInput();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionGetAttr(const String &hmiAttribute)
{ // Get the value of a Nextion component attribute
  // This will only send the command to the panel requesting the attribute, the actual
  // return of that value will be handled by nextionProcessInput and placed into mqttGetSubtopic
  Serial1.print("get ");
  Serial1.print(hmiAttribute);
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  Serial1.flush();
  debugPrintln(String(F("HMI OUT: 'get ")) + hmiAttribute + String(F("'")));
  if (nextionAckEnable)
  {
    nextionAckReceived = false;
    nextionAckTimer = millis();

    while ((!nextionAckReceived) || (millis() - nextionAckTimer > nextionAckTimeout))
    {
      nextionHandleInput();
    }
    if (!nextionAckReceived)
    {
      debugPrintln(String(F("HMI ERROR: Nextion Ack timeout")));
      String mqttButtonJSONEvent = String(F("{\"event\":\"nextionError\",\"value\":\"Nextion Ack timeout\"}"));
      mqttClient.publish(mqttStateJSONTopic, mqttButtonJSONEvent);
      debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F("' : '")) + mqttButtonJSONEvent + String(F("'")));
    }
  }
  else
  {
    nextionHandleInput();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionParseJson(const String &strPayload)
{ // Parse an incoming JSON array into individual Nextion commands
  DynamicJsonDocument nextionCommands(mqttMaxPacketSize + 1024);
  DeserializationError jsonError = deserializeJson(nextionCommands, strPayload);

  if (jsonError)
  { // Couldn't parse incoming JSON command
    String jsonErrorDescription = String(F("Failed to parse incoming JSON command with error:")) + String(jsonError.c_str()) + String(F(" memoryUsage: ")) + String(nextionCommands.memoryUsage()) + String(F(" capacity: ")) + String(nextionCommands.capacity());
    debugPrintln(String(F("MQTT: [ERROR] ")) + jsonErrorDescription);
    mqttClient.publish(mqttStateJSONTopic, String(F("{\"event\":\"jsonError\",\"event_source\":\"nextionParseJson()\",\"event_description\":\"")) + jsonErrorDescription + String(F("\"}")));
  }
  else
  {
    for (uint8_t i = 0; i < nextionCommands.size(); i++)
    {
      nextionSendCmd(nextionCommands[i]);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionOtaStartDownload(const String &lcdOtaUrl)
{ // Upload firmware to the Nextion LCD via HTTP download

  uint32_t lcdOtaFileSize = 0;
  String lcdOtaNextionCmd;
  uint32_t lcdOtaChunkCounter = 0;
  uint16_t lcdOtaPartNum = 0;
  uint32_t lcdOtaTransferred = 0;
  uint8_t lcdOtaPercentComplete = 0;
  const uint32_t lcdOtaTimeout = 30000; // timeout for receiving new data in milliseconds
  static uint32_t lcdOtaTimer = 0;      // timer for lcdOtaTimeout

  HTTPClient lcdOtaHttp;
  WiFiClientSecure lcdOtaWifiSecure;
  WiFiClient lcdOtaWifi;
  if (lcdOtaUrl.startsWith(F("https")))
  {
    debugPrintln("LCDOTA: Attempting firmware update from HTTPS host: " + lcdOtaUrl);

    lcdOtaHttp.begin(lcdOtaWifiSecure, lcdOtaUrl);
    lcdOtaWifiSecure.setInsecure();
    lcdOtaWifiSecure.setBufferSizes(512, 512);
  }
  else
  {
    debugPrintln("LCDOTA: Attempting firmware update from HTTP host: " + lcdOtaUrl);
    lcdOtaHttp.begin(lcdOtaWifi, lcdOtaUrl);
  }

  int lcdOtaHttpReturn = lcdOtaHttp.GET();
  if (lcdOtaHttpReturn > 0)
  { // HTTP header has been sent and Server response header has been handled
    debugPrintln(String(F("LCDOTA: HTTP GET return code:")) + String(lcdOtaHttpReturn));
    if (lcdOtaHttpReturn == HTTP_CODE_OK)
    {                                                 // file found at server
      int32_t lcdOtaRemaining = lcdOtaHttp.getSize(); // get length of document (is -1 when Server sends no Content-Length header)
      lcdOtaFileSize = lcdOtaRemaining;
      static uint16_t lcdOtaParts = (lcdOtaRemaining / 4096) + 1;
      static const uint16_t lcdOtaBufferSize = 1024; // upload data buffer before sending to UART
      static uint8_t lcdOtaBuffer[lcdOtaBufferSize] = {};

      debugPrintln(String(F("LCDOTA: File found at Server. Size ")) + String(lcdOtaRemaining) + String(F(" bytes in ")) + String(lcdOtaParts) + String(F(" 4k chunks.")));

      WiFiUDP::stopAll(); // Keep mDNS responder and MQTT traffic from breaking things
      if (mqttClient.connected())
      {
        debugPrintln(F("LCDOTA: LCD firmware upload starting, closing MQTT connection."));
        mqttClient.publish(mqttStatusTopic, "OFF", true, 0);
        debugPrintln(String(F("MQTT OUT: '")) + mqttStatusTopic + String(F("' : 'OFF'")));
        mqttClient.disconnect();
      }

      WiFiClient *stream = lcdOtaHttp.getStreamPtr();      // get tcp stream
      Serial1.write(nextionSuffix, sizeof(nextionSuffix)); // Send empty command
      Serial1.flush();
      nextionHandleInput();
      String lcdOtaNextionCmd = "whmi-wri " + String(lcdOtaFileSize) + "," + String(nextionBaud) + ",0";
      debugPrintln(String(F("LCDOTA: Sending LCD upload command: ")) + lcdOtaNextionCmd);
      Serial1.print(lcdOtaNextionCmd);
      Serial1.write(nextionSuffix, sizeof(nextionSuffix));
      Serial1.flush();

      if (nextionOtaResponse())
      {
        debugPrintln(F("LCDOTA: LCD upload command accepted."));
      }
      else
      {
        debugPrintln(F("LCDOTA: LCD upload command FAILED.  Restarting device."));
        espReset();
      }
      debugPrintln(F("LCDOTA: Starting update"));
      lcdOtaTimer = millis();
      while (lcdOtaHttp.connected() && (lcdOtaRemaining > 0 || lcdOtaRemaining == -1))
      {                                                // Write incoming data to panel as it arrives
        uint16_t lcdOtaHttpSize = stream->available(); // get available data size

        if (lcdOtaHttpSize)
        {
          uint16_t lcdOtaChunkSize = 0;
          if ((lcdOtaHttpSize <= lcdOtaBufferSize) && (lcdOtaHttpSize <= (4096 - lcdOtaChunkCounter)))
          {
            lcdOtaChunkSize = lcdOtaHttpSize;
          }
          else if ((lcdOtaBufferSize <= lcdOtaHttpSize) && (lcdOtaBufferSize <= (4096 - lcdOtaChunkCounter)))
          {
            lcdOtaChunkSize = lcdOtaBufferSize;
          }
          else
          {
            lcdOtaChunkSize = 4096 - lcdOtaChunkCounter;
          }
          stream->readBytes(lcdOtaBuffer, lcdOtaChunkSize);
          Serial1.flush();                              // make sure any previous writes the UART have completed
          Serial1.write(lcdOtaBuffer, lcdOtaChunkSize); // now send buffer to the UART
          lcdOtaChunkCounter += lcdOtaChunkSize;
          if (lcdOtaChunkCounter >= 4096)
          {
            Serial1.flush();
            lcdOtaPartNum++;
            lcdOtaTransferred += lcdOtaChunkCounter;
            lcdOtaPercentComplete = (lcdOtaTransferred * 100) / lcdOtaFileSize;
            lcdOtaChunkCounter = 0;
            if (nextionOtaResponse())
            { // We've completed a chunk
              debugPrintln(String(F("LCDOTA: Part ")) + String(lcdOtaPartNum) + String(F(" OK, ")) + String(lcdOtaPercentComplete) + String(F("% complete")));
              lcdOtaTimer = millis();
            }
            else
            {
              debugPrintln(String(F("LCDOTA: Part ")) + String(lcdOtaPartNum) + String(F(" FAILED, ")) + String(lcdOtaPercentComplete) + String(F("% complete")));
              debugPrintln(F("LCDOTA: failure"));
              delay(2000); // extra delay while the LCD does its thing
              espReset();
            }
          }
          else
          {
            delay(20);
          }
          if (lcdOtaRemaining > 0)
          {
            lcdOtaRemaining -= lcdOtaChunkSize;
          }
        }
        delay(10);
        if ((lcdOtaTimer > 0) && ((millis() - lcdOtaTimer) > lcdOtaTimeout))
        { // Our timer expired so reset
          debugPrintln(F("LCDOTA: ERROR: LCD upload timeout.  Restarting."));
          espReset();
        }
      }
      lcdOtaPartNum++;
      lcdOtaTransferred += lcdOtaChunkCounter;
      if ((lcdOtaTransferred == lcdOtaFileSize) && nextionOtaResponse())
      {
        debugPrintln(String(F("LCDOTA: Success, wrote ")) + String(lcdOtaTransferred) + String(F(" of ")) + String(tftFileSize) + String(F(" bytes.")));
        uint32_t lcdOtaDelay = millis();
        debugPrintln(F("LCDOTA: Waiting 5 seconds to allow LCD to apply updates we've sent."));
        while ((millis() - lcdOtaDelay) < 5000)
        { // extra 5sec delay while the LCD handles any local firmware updates from new versions of code sent to it
          webServer.handleClient();
          yield();
        }
        espReset();
      }
      else
      {
        debugPrintln(String(F("LCDOTA: Failure, lcdOtaTransferred: ")) + String(lcdOtaTransferred) + String(F(" lcdOtaFileSize: ")) + String(lcdOtaFileSize));
        espReset();
      }
    }
  }
  else
  {
    debugPrintln(String(F("LCDOTA: HTTP GET failed, error code ")) + lcdOtaHttp.errorToString(lcdOtaHttpReturn));
    espReset();
  }
  lcdOtaHttp.end();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool nextionOtaResponse()
{                                               // Monitor the serial port for a 0x05 response within our timeout
  unsigned long nextionCommandTimeout = 2000;   // timeout for receiving termination string in milliseconds
  unsigned long nextionCommandTimer = millis(); // record current time for our timeout
  bool otaSuccessVal = false;
  while ((millis() - nextionCommandTimer) < nextionCommandTimeout)
  {
    if (Serial.available())
    {
      byte inByte = Serial.read();
      if (inByte == 0x5)
      {
        otaSuccessVal = true;
        break;
      }
    }
    else
    {
      delay(1);
    }
  }
  return otaSuccessVal;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool nextionConnect()
{
  const unsigned long nextionCheckTimeout = 2000; // Max time in msec for nextion connection check
  unsigned long nextionCheckTimer = millis();     // Timer for nextion connection checks

  Serial1.write(nextionSuffix, sizeof(nextionSuffix));

  if (!lcdConnected)
  { // Check for some traffic from our LCD
    debugPrintln(F("HMI: Waiting for LCD connection"));
    while (((millis() - nextionCheckTimer) <= nextionCheckTimeout) && !lcdConnected)
    {
      nextionHandleInput();
    }
  }
  if (!lcdConnected)
  { // No response from the display using the configured speed, so scan all possible speeds
    nextionSetSpeed();

    nextionCheckTimer = millis(); // Reset our timer
    debugPrintln(F("HMI: Waiting again for LCD connection"));
    while (((millis() - nextionCheckTimer) <= nextionCheckTimeout) && !lcdConnected)
    {
      Serial1.write(nextionSuffix, sizeof(nextionSuffix));
      nextionHandleInput();
    }
    if (!lcdConnected)
    {
      debugPrintln(F("HMI: LCD connection timed out"));
      return false;
    }
  }

  // Query backlight status.  This should always succeed under simulation or non-HASPone HMI
  lcdBacklightQueryFlag = true;
  debugPrintln(F("HMI: Querying LCD backlight status"));
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  nextionSendCmd("get dim");
  while (((millis() - nextionCheckTimer) <= nextionCheckTimeout) && lcdBacklightQueryFlag)
  {
    nextionHandleInput();
  }
  if (lcdBacklightQueryFlag)
  { // Our flag is still set, meaning we never got a response.
    debugPrintln(F("HMI: LCD backlight query timed out"));
    lcdBacklightQueryFlag = false;
    return false;
  }

  // We are now communicating with the panel successfully.  Enable ACK checking for all future commands.
  nextionAckEnable = true;
  nextionSendCmd("bkcmd=3");

  // This check depends on the HMI having been designed with a version number in the object
  // defined in lcdVersionQuery.  It's OK if this fails, it just means the HMI project is
  // not utilizing the version capability that the HASPone project makes use of.
  lcdVersionQueryFlag = true;
  debugPrintln(F("HMI: Querying LCD firmware version number"));
  nextionSendCmd("get " + lcdVersionQuery);
  while (((millis() - nextionCheckTimer) <= nextionCheckTimeout) && lcdVersionQueryFlag)
  {
    nextionHandleInput();
  }
  if (lcdVersionQueryFlag)
  { // Our flag is still set, meaning we never got a response.  This should only happen if
    // there's a problem.  Non-HASPone projects should pass this check with lcdVersion = 0
    debugPrintln(F("HMI: LCD version query timed out"));
    lcdVersionQueryFlag = false;
    return false;
  }

  if (nextionModel.length() == 0)
  { // Check for LCD model via `connect`.  The Nextion simulator does not support this command,
    // so if we're running under that environment this process should timeout.
    debugPrintln(F("HMI: Querying LCD model information"));
    nextionSendCmd("connect");
    while (((millis() - nextionCheckTimer) <= nextionCheckTimeout) && (nextionModel.length() == 0))
    {
      nextionHandleInput();
    }
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionSetSpeed()
{
  debugPrintln(String(F("HMI: No Nextion response, attempting to set serial speed to ")) + String(nextionBaud));
  for (unsigned int nextionSpeedsIndex = 0; nextionSpeedsIndex < nextionSpeedsLength; nextionSpeedsIndex++)
  {
    debugPrintln(String(F("HMI: Sending bauds=")) + String(nextionBaud) + " @" + String(nextionSpeeds[nextionSpeedsIndex]) + " baud");
    Serial1.flush();
    Serial1.begin(nextionSpeeds[nextionSpeedsIndex]);
    Serial1.write(nextionSuffix, sizeof(nextionSuffix));
    Serial1.write(nextionSuffix, sizeof(nextionSuffix));
    Serial1.write(nextionSuffix, sizeof(nextionSuffix));
    Serial1.print("bauds=" + String(nextionBaud));
    Serial1.write(nextionSuffix, sizeof(nextionSuffix));
    Serial1.flush();
  }
  Serial1.begin(atoi(nextionBaud));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionReset()
{
  debugPrintln(F("HMI: Rebooting LCD"));
  digitalWrite(nextionResetPin, LOW);
  Serial1.print("rest");
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  Serial1.flush();
  delay(100);
  digitalWrite(nextionResetPin, HIGH);

  unsigned long lcdResetTimer = millis();
  const unsigned long lcdResetTimeout = 5000;

  lcdConnected = false;
  while (!lcdConnected && (millis() < (lcdResetTimer + lcdResetTimeout)))
  {
    nextionHandleInput();
  }
  if (lcdConnected)
  {
    debugPrintln(F("HMI: Rebooting LCD completed"));
    if (nextionActivePage)
    {
      nextionSendCmd("page " + String(nextionActivePage));
    }
  }
  else
  {
    debugPrintln(F("ERROR: Rebooting LCD completed, but LCD is not responding."));
  }
  mqttClient.publish(mqttStatusTopic, "OFF", true, 0);
  debugPrintln(String(F("MQTT OUT: '")) + mqttStatusTopic + String(F("' : 'OFF'")));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextionUpdateProgress(const unsigned int &progress, const unsigned int &total)
{
  uint8_t progressPercent = (float(progress) / float(total)) * 100;
  nextionSetAttr("p[0].b[4].val", String(progressPercent));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espWifiConnect()
{ // Connect to WiFi
  rebootOnp0b1 = true;

  nextionSetAttr("p[0].b[1].font", "6");
  if (lcdVersion < 1 || lcdVersion > 2)
  {
    nextionSendCmd("page 0");
  }

  WiFi.persistent(false);
  enableWiFiAtBootTime();
  WiFi.macAddress(espMac);            // Read our MAC address and save it to espMac
  WiFi.hostname(haspNode);            // Assign our hostname before connecting to WiFi
  WiFi.setAutoReconnect(true);        // Tell WiFi to autoreconnect if connection has dropped
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable WiFi sleep modes to prevent occasional disconnects
  WiFi.mode(WIFI_STA);                // Set the radio to Station

  if (String(wifiSSID) == "")
  { // If the sketch has no hard-coded wifiSSID, attempt to use saved creds or use WiFiManager to collect required information from the user.

    // First, check if we have saved wifi creds and try to connect manually.
    if (WiFi.SSID() != "")
    {
      nextionSetAttr("p[0].b[1].txt", "\"WiFi Connecting...\\r " + String(WiFi.SSID()) + "\"");
      unsigned long connectTimer = millis() + 10000;

      debugPrintln(String(F("WIFI: Connecting to previously-saved SSID: ")) + String(WiFi.SSID()));

      WiFi.begin();
      while ((WiFi.status() != WL_CONNECTED) && (millis() < connectTimer))
      {
        yield();
      }

      unsigned int connectCounter = 0;
      unsigned int connectRetries = 4;
      unsigned int connectTime = 10000;
      while ((WiFi.status() != WL_CONNECTED) && (connectCounter <= connectRetries))
      {
        connectCounter++;
        debugPrintln(String(F("WIFI: Connect failed, retry attempt ")) + String(connectCounter));
        WiFi.mode(WIFI_OFF); // Force the radio off, and then
        delay(100);
        WiFi.mode(WIFI_STA); // toggle it back on again
        connectTimer = millis() + connectTime;
        WiFi.begin();
        while ((WiFi.status() != WL_CONNECTED) && (millis() < connectTimer))
        {
          yield();
        }

        if (WiFi.localIP().toString() == "(IP unset)")
        { // Check if we have our IP yet
          debugPrintln(F("WIFI: Failed to lease address from DHCP, disconnecting and trying again"));
          WiFi.disconnect();
        }
      }
    }

    if (WiFi.status() != WL_CONNECTED)
    { // We gave it a shot, still couldn't connect, so let WiFiManager run to make one last
      // connection attempt and then flip to AP mode to collect credentials from the user.
      WiFi.persistent(true);
      WiFiManagerParameter custom_haspNodeHeader("<br/><b>HASPone Node</b>");
      WiFiManagerParameter custom_haspNode("haspNode", "<br/>Node Name <small>(required: lowercase letters, numbers, and _ only)</small>", haspNode, 15, " maxlength=15 required pattern='[a-z0-9_]*'");
      WiFiManagerParameter custom_groupName("groupName", "Group Name <small>(required)</small>", groupName, 15, " maxlength=15 required");
      WiFiManagerParameter custom_mqttHeader("<br/><br/><b>MQTT</b>");
      WiFiManagerParameter custom_mqttServer("mqttServer", "<br/>MQTT Broker <small>(required, IP address is preferred)</small>", mqttServer, 127, " maxlength=127");
      WiFiManagerParameter custom_mqttPort("mqttPort", "MQTT Port <small>(required)</small>", mqttPort, 5, " maxlength=5 type='number'");
      WiFiManagerParameter custom_mqttUser("mqttUser", "MQTT User <small>(optional)</small>", mqttUser, 127, " maxlength=127");
      WiFiManagerParameter custom_mqttPassword("mqttPassword", "MQTT Password <small>(optional)</small>", mqttPassword, 127, " maxlength=127 type='password'");
      String mqttTlsEnabled_value = "F";
      if (mqttTlsEnabled)
      {
        mqttTlsEnabled_value = "T";
      }
      String mqttTlsEnabled_checked = "type=\"checkbox\"";
      if (mqttTlsEnabled)
      {
        mqttTlsEnabled_checked = "type=\"checkbox\" checked=\"true\"";
      }
      WiFiManagerParameter custom_mqttTlsEnabled("mqttTlsEnabled", "MQTT TLS enabled:", mqttTlsEnabled_value.c_str(), 2, mqttTlsEnabled_checked.c_str());
      WiFiManagerParameter custom_mqttFingerprint("mqttFingerprint", "</br>MQTT TLS Fingerprint <small>(optional, enter as 01:23:AB:CD, etc)</small>", mqttFingerprint, 59, " min length=59 maxlength=59");
      WiFiManagerParameter custom_configHeader("<br/><br/><b>Admin access</b>");
      WiFiManagerParameter custom_configUser("configUser", "<br/>Config User <small>(required)</small>", configUser, 15, " maxlength=31");
      WiFiManagerParameter custom_configPassword("configPassword", "Config Password <small>(optional)</small>", configPassword, 31, " maxlength=31 type='password'");
      WiFiManagerParameter custom_hassHeader("<br/><br/><b>Home Assistant integration</b>");
      WiFiManagerParameter custom_hassDiscovery("hassDiscovery", "<br/>Home Assistant Discovery topic <small>(required, should probably be \"homeassistant\")</small>", hassDiscovery, 127, " maxlength=127");

      WiFiManager wifiManager;
      wifiManager.setSaveConfigCallback(configSaveCallback); // set config save notify callback
      wifiManager.setCustomHeadElement(HASP_STYLE);          // add custom style
      wifiManager.addParameter(&custom_haspNodeHeader);
      wifiManager.addParameter(&custom_haspNode);
      wifiManager.addParameter(&custom_groupName);
      wifiManager.addParameter(&custom_mqttHeader);
      wifiManager.addParameter(&custom_mqttServer);
      wifiManager.addParameter(&custom_mqttPort);
      wifiManager.addParameter(&custom_mqttUser);
      wifiManager.addParameter(&custom_mqttPassword);
      wifiManager.addParameter(&custom_mqttTlsEnabled);
      wifiManager.addParameter(&custom_mqttFingerprint);
      wifiManager.addParameter(&custom_configHeader);
      wifiManager.addParameter(&custom_configUser);
      wifiManager.addParameter(&custom_configPassword);
      wifiManager.addParameter(&custom_hassHeader);
      wifiManager.addParameter(&custom_hassDiscovery);

      // Timeout config portal after connectTimeout seconds, useful if configured wifi network was temporarily unavailable
      wifiManager.setTimeout(connectTimeout);

      wifiManager.setAPCallback(espWifiConfigCallback);

      // Fetches SSID and pass from EEPROM and tries to connect
      // If it does not connect it starts an access point with the specified name
      // and goes into a blocking loop awaiting configuration.
      if (!wifiManager.autoConnect(wifiConfigAP, wifiConfigPass))
      { // Reset and try again
        debugPrintln(F("WIFI: Failed to connect and hit timeout"));
        espReset();
      }

      // Read updated parameters
      strcpy(mqttServer, custom_mqttServer.getValue());
      strcpy(mqttPort, custom_mqttPort.getValue());
      strcpy(mqttUser, custom_mqttUser.getValue());
      strcpy(mqttPassword, custom_mqttPassword.getValue());
      if (strcmp(custom_mqttTlsEnabled.getValue(), "T") == 0)
      {
        mqttTlsEnabled = true;
      }
      else
      {
        mqttTlsEnabled = false;
      }
      strcpy(mqttFingerprint, custom_mqttFingerprint.getValue());
      strcpy(haspNode, custom_haspNode.getValue());
      strcpy(groupName, custom_groupName.getValue());
      strcpy(configUser, custom_configUser.getValue());
      strcpy(configPassword, custom_configPassword.getValue());
      strcpy(hassDiscovery, custom_hassDiscovery.getValue());
      if (shouldSaveConfig)
      { // Save the custom parameters to FS
        configSave();
      }
    }
  }
  else
  { // wifiSSID has been defined, so attempt to connect to it
    debugPrintln(String(F("Connecting to WiFi network: ")) + String(wifiSSID));
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPass);

    unsigned long wifiReconnectTimer = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(1);
      if (millis() >= (wifiReconnectTimer + (connectTimeout * 1000)))
      { // If we've been trying to reconnect for connectTimeout seconds, reboot and try again
        debugPrintln(F("WIFI: Failed to connect and hit timeout"));
        espReset();
      }
    }
  }

  // If you get here you have connected to WiFi
  nextionSetAttr("p[0].b[1].font", "6");
  nextionSetAttr("p[0].b[1].txt", "\"WiFi Connected!\\r " + String(WiFi.SSID()) + "\\rIP: " + WiFi.localIP().toString() + "\"");
  debugPrintln(String(F("WIFI: Connected successfully and assigned IP: ")) + WiFi.localIP().toString());
  if (nextionActivePage)
  {
    nextionSendCmd("page " + String(nextionActivePage));
  }

  rebootOnp0b1 = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espWifiReconnect()
{ // Existing WiFi connection dropped, try to reconnect
  debugPrintln(F("Reconnecting to WiFi network..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPass);

  unsigned long wifiReconnectTimer = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1);
    if (millis() >= (wifiReconnectTimer + (reConnectTimeout * 1000)))
    { // If we've been trying to reconnect for reConnectTimeout seconds, reboot and try again
      debugPrintln(F("WIFI: Failed to reconnect and hit timeout"));
      espReset();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espWifiConfigCallback(WiFiManager *myWiFiManager)
{ // Notify the user that we're entering config mode
  debugPrintln(F("WIFI: Failed to connect to assigned AP, entering config mode"));
  if (lcdVersion < 1 || lcdVersion > 2)
  {
    nextionSendCmd("page 0");
  }
  nextionSetAttr("p[0].b[1].font", "6");
  nextionSetAttr("p[0].b[1].txt", "\" HASPone Setup\\r AP: " + String(wifiConfigAP) + "\\rPassword: " + String(wifiConfigPass) + "\\r\\r\\r\\r\\r\\r\\r  http://192.168.4.1\"");
  nextionSendCmd("vis 3,1");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espSetupOta()
{ // Update ESP firmware from network via Arduino OTA

  ArduinoOTA.setHostname(haspNode);
  ArduinoOTA.setPassword(configPassword);
  ArduinoOTA.setRebootOnSuccess(false);

  ArduinoOTA.onStart([]()
                     {
                       debugPrintln(F("ESP OTA: update start"));
                       nextionSetAttr("p[0].b[1].txt", "\"\\rHASPone update:\\r\\r\\r \"");
                       nextionSendCmd("page 0");
                       nextionSendCmd("vis 4,1"); });
  ArduinoOTA.onEnd([]()
                   {
                     debugPrintln(F("ESP OTA: update complete"));
                     nextionSetAttr("p[0].b[1].txt", "\"\\rHASPone update:\\r\\r Complete!\\rRestarting.\"");
                     nextionSendCmd("vis 4,1");
                     delay(1000);
                     espReset(); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { nextionUpdateProgress(progress, total); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
                       debugPrintln(String(F("ESP OTA: ERROR code ")) + String(error));
                       if (error == OTA_AUTH_ERROR)
                         debugPrintln(F("ESP OTA: ERROR - Auth Failed"));
                       else if (error == OTA_BEGIN_ERROR)
                         debugPrintln(F("ESP OTA: ERROR - Begin Failed"));
                       else if (error == OTA_CONNECT_ERROR)
                         debugPrintln(F("ESP OTA: ERROR - Connect Failed"));
                       else if (error == OTA_RECEIVE_ERROR)
                         debugPrintln(F("ESP OTA: ERROR - Receive Failed"));
                       else if (error == OTA_END_ERROR)
                         debugPrintln(F("ESP OTA: ERROR - End Failed"));
                       nextionSendCmd("vis 4,0");
                       nextionSetAttr("p[0].b[1].txt", "\"HASPone update:\\r FAILED\\rerror: " + String(error) + "\"");
                       delay(1000);
                       nextionSendCmd("page " + String(nextionActivePage)); });
  ArduinoOTA.begin();
  debugPrintln(F("ESP OTA: Over the Air firmware update ready"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espStartOta(const String &espOtaUrl)
{ // Update ESP firmware from HTTP/HTTPS URL

  nextionSetAttr("p[0].b[1].txt", "\"\\rHASPone update:\\r\\r\\r \"");
  nextionSendCmd("page 0");
  nextionSendCmd("vis 4,1");

  WiFiUDP::stopAll(); // Keep mDNS responder from breaking things
  delay(1);
  ESPhttpUpdate.rebootOnUpdate(false);
  ESPhttpUpdate.onProgress(nextionUpdateProgress);
  t_httpUpdate_return espOtaUrlReturnCode;
  if (espOtaUrl.startsWith(F("https")))
  {
    debugPrintln(String(F("ESPFW: Attempting firmware update from HTTPS host: ")) + espOtaUrl);
    WiFiClientSecure wifiEspOtaClientSecure;
    wifiEspOtaClientSecure.setInsecure();
    wifiEspOtaClientSecure.setBufferSizes(512, 512);
    espOtaUrlReturnCode = ESPhttpUpdate.update(wifiEspOtaClientSecure, espOtaUrl);
  }
  else
  {
    debugPrintln(String(F("ESPFW: Attempting firmware update from HTTP host: ")) + espOtaUrl);
    espOtaUrlReturnCode = ESPhttpUpdate.update(wifiClient, espOtaUrl);
  }

  switch (espOtaUrlReturnCode)
  {
  case HTTP_UPDATE_FAILED:
    debugPrintln(String(F("ESPFW: HTTP_UPDATE_FAILED error ")) + String(ESPhttpUpdate.getLastError()) + " " + ESPhttpUpdate.getLastErrorString());
    nextionSendCmd("vis 4,0");
    nextionSetAttr("p[0].b[1].txt", "\"HASPone update:\\r FAILED\\rerror: " + ESPhttpUpdate.getLastErrorString() + "\"");
    break;

  case HTTP_UPDATE_NO_UPDATES:
    debugPrintln(F("ESPFW: HTTP_UPDATE_NO_UPDATES"));
    nextionSendCmd("vis 4,0");
    nextionSetAttr("p[0].b[1].txt", "\"HASPone update:\\rNo update\"");
    break;

  case HTTP_UPDATE_OK:
    debugPrintln(F("ESPFW: HTTP_UPDATE_OK"));
    nextionSetAttr("p[0].b[1].txt", "\"\\rHASPone update:\\r\\r Complete!\\rRestarting.\"");
    nextionSendCmd("vis 4,1");
    delay(1000);
    espReset();
  }
  delay(1000);
  nextionSendCmd("page " + String(nextionActivePage));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void espReset()
{
  debugPrintln(F("RESET: HASPone reset"));
  if (mqttClient.connected())
  {
    mqttClient.publish(mqttStateJSONTopic, String(F("{\"event_type\":\"hasp_device\",\"event\":\"offline\"}")));
    debugPrintln(String(F("MQTT OUT: '")) + mqttStateJSONTopic + String(F(" : {\"event_type\":\"hasp_device\",\"event\":\"offline\"}")));
    mqttClient.publish(mqttStatusTopic, "OFF", true, 0);
    mqttClient.disconnect();
    debugPrintln(String(F("MQTT OUT: '")) + mqttStatusTopic + String(F("' : 'OFF'")));
  }
  debugPrintln(F("HMI: Rebooting LCD"));
  digitalWrite(nextionResetPin, LOW);
  Serial1.print("rest");
  Serial1.write(nextionSuffix, sizeof(nextionSuffix));
  Serial1.flush();
  delay(500);
  ESP.reset();
  delay(5000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void configRead()
{ // Read saved config.json from LittleFS
  debugPrintln(F("LittleFS: mounting LittleFS"));
  if (LittleFS.begin())
  {
    if (LittleFS.exists("/config.json"))
    { // File exists, reading and loading
      debugPrintln(F("LittleFS: reading /config.json"));
      // debugPrintFile("/config.json");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile)
      {
        DynamicJsonDocument jsonConfigValues(1536);
        DeserializationError jsonError = deserializeJson(jsonConfigValues, configFile);

        if (jsonError)
        { // Couldn't parse the saved config
          debugPrintln(String(F("LittleFS: [ERROR] Failed to parse /config.json: ")) + String(jsonError.c_str()));
        }
        else
        {
          if (!jsonConfigValues["mqttServer"].isNull())
          {
            strcpy(mqttServer, jsonConfigValues["mqttServer"]);
          }
          if (!jsonConfigValues["mqttPort"].isNull())
          {
            strcpy(mqttPort, jsonConfigValues["mqttPort"]);
          }
          if (!jsonConfigValues["mqttUser"].isNull())
          {
            strcpy(mqttUser, jsonConfigValues["mqttUser"]);
          }
          if (!jsonConfigValues["mqttPassword"].isNull())
          {
            strcpy(mqttPassword, jsonConfigValues["mqttPassword"]);
          }
          if (!jsonConfigValues["mqttFingerprint"].isNull())
          {
            strcpy(mqttFingerprint, jsonConfigValues["mqttFingerprint"]);
          }
          if (!jsonConfigValues["haspNode"].isNull())
          {
            strcpy(haspNode, jsonConfigValues["haspNode"]);
          }
          if (!jsonConfigValues["groupName"].isNull())
          {
            strcpy(groupName, jsonConfigValues["groupName"]);
          }
          if (!jsonConfigValues["configUser"].isNull())
          {
            strcpy(configUser, jsonConfigValues["configUser"]);
          }
          if (!jsonConfigValues["configPassword"].isNull())
          {
            strcpy(configPassword, jsonConfigValues["configPassword"]);
          }
          if (!jsonConfigValues["hassDiscovery"].isNull())
          {
            strcpy(hassDiscovery, jsonConfigValues["hassDiscovery"]);
          }
          if (strcmp(hassDiscovery, "") == 0)
          { // Cover off any edge case where this value winds up being empty
            debugPrintln(F("LittleFS: [WARNING] /config.json has empty hassDiscovery value, setting to 'homeassistant'"));
            strcpy(hassDiscovery, "homeassistant");
          }
          if (!jsonConfigValues["nextionBaud"].isNull())
          {
            strcpy(nextionBaud, jsonConfigValues["nextionBaud"]);
          }
          if (strcmp(nextionBaud, "") == 0)
          { // Cover off any edge case where this value winds up being empty
            debugPrintln(F("LittleFS: [WARNING] /config.json has empty nextionBaud value, setting to '115200'"));
            strcpy(nextionBaud, "115200");
          }
          if (!jsonConfigValues["nextionMaxPages"].isNull())
          {
            nextionMaxPages = jsonConfigValues["nextionMaxPages"];
          }
          if (nextionMaxPages < 1)
          { // Cover off any edge case where this value winds up being zero or negative
            debugPrintln(F("LittleFS: [WARNING] /config.json has nextionMaxPages value of zero or negative, setting to '11'"));
            nextionMaxPages = 11;
          }
          if (!jsonConfigValues["motionPinConfig"].isNull())
          {
            strcpy(motionPinConfig, jsonConfigValues["motionPinConfig"]);
          }
          if (!jsonConfigValues["mqttTlsEnabled"].isNull())
          {
            if (jsonConfigValues["mqttTlsEnabled"])
            {
              mqttTlsEnabled = true;
            }
            else
            {
              mqttTlsEnabled = false;
            }
          }
          if (!jsonConfigValues["debugSerialEnabled"].isNull())
          {
            if (jsonConfigValues["debugSerialEnabled"])
            {
              debugSerialEnabled = true;
            }
            else
            {
              debugSerialEnabled = false;
            }
          }
          if (!jsonConfigValues["debugTelnetEnabled"].isNull())
          {
            if (jsonConfigValues["debugTelnetEnabled"])
            {
              debugTelnetEnabled = true;
            }
            else
            {
              debugTelnetEnabled = false;
            }
          }
          if (!jsonConfigValues["mdnsEnabled"].isNull())
          {
            if (jsonConfigValues["mdnsEnabled"])
            {
              mdnsEnabled = true;
            }
            else
            {
              mdnsEnabled = false;
            }
          }
          if (!jsonConfigValues["beepEnabled"].isNull())
          {
            if (jsonConfigValues["beepEnabled"])
            {
              beepEnabled = true;
            }
            else
            {
              beepEnabled = false;
            }
          }
          if (!jsonConfigValues["ignoreTouchWhenOff"].isNull())
          {
            if (jsonConfigValues["ignoreTouchWhenOff"])
            {
              ignoreTouchWhenOff = true;
            }
            else
            {
              ignoreTouchWhenOff = false;
            }
          }
          String jsonConfigValuesStr;
          serializeJson(jsonConfigValues, jsonConfigValuesStr);
          debugPrintln(String(F("LittleFS: read ")) + String(configFile.size()) + String(F(" bytes and parsed json:")) + jsonConfigValuesStr);
        }
      }
      else
      {
        debugPrintln(F("LittleFS: [ERROR] Failed to read /config.json"));
      }
    }
    else
    {
      debugPrintln(F("LittleFS: [WARNING] /config.json not found, will be created on first config save"));
    }
  }
  else
  {
    debugPrintln(F("LittleFS: [ERROR] Failed to mount FS"));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void configSaveCallback()
{ // Callback notifying us of the need to save config
  debugPrintln(F("LittleFS: Configuration changed, flagging for save"));
  shouldSaveConfig = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void configSave()
{ // Save the custom parameters to config.json
  debugPrintln(F("LittleFS: Saving config"));
  DynamicJsonDocument jsonConfigValues(2048);

  jsonConfigValues["mqttServer"] = mqttServer;
  jsonConfigValues["mqttPort"] = mqttPort;
  jsonConfigValues["mqttUser"] = mqttUser;
  jsonConfigValues["mqttPassword"] = mqttPassword;
  jsonConfigValues["mqttTlsEnabled"] = mqttTlsEnabled;
  jsonConfigValues["mqttFingerprint"] = mqttFingerprint;
  jsonConfigValues["haspNode"] = haspNode;
  jsonConfigValues["groupName"] = groupName;
  jsonConfigValues["configUser"] = configUser;
  jsonConfigValues["configPassword"] = configPassword;
  jsonConfigValues["hassDiscovery"] = hassDiscovery;
  jsonConfigValues["nextionBaud"] = nextionBaud;
  jsonConfigValues["nextionMaxPages"] = nextionMaxPages;
  jsonConfigValues["motionPinConfig"] = motionPinConfig;
  jsonConfigValues["debugSerialEnabled"] = debugSerialEnabled;
  jsonConfigValues["debugTelnetEnabled"] = debugTelnetEnabled;
  jsonConfigValues["mdnsEnabled"] = mdnsEnabled;
  jsonConfigValues["beepEnabled"] = beepEnabled;
  jsonConfigValues["ignoreTouchWhenOff"] = ignoreTouchWhenOff;

  debugPrintln(String(F("LittleFS: mqttServer = ")) + String(mqttServer));
  debugPrintln(String(F("LittleFS: mqttPort = ")) + String(mqttPort));
  debugPrintln(String(F("LittleFS: mqttUser = ")) + String(mqttUser));
  debugPrintln(String(F("LittleFS: mqttPassword = ")) + String(mqttPassword));
  debugPrintln(String(F("LittleFS: mqttTlsEnabled = ")) + String(mqttTlsEnabled));
  debugPrintln(String(F("LittleFS: mqttFingerprint = ")) + String(mqttFingerprint));
  debugPrintln(String(F("LittleFS: haspNode = ")) + String(haspNode));
  debugPrintln(String(F("LittleFS: groupName = ")) + String(groupName));
  debugPrintln(String(F("LittleFS: configUser = ")) + String(configUser));
  debugPrintln(String(F("LittleFS: configPassword = ")) + String(configPassword));
  debugPrintln(String(F("LittleFS: hassDiscovery = ")) + String(hassDiscovery));
  debugPrintln(String(F("LittleFS: nextionBaud = ")) + String(nextionBaud));
  debugPrintln(String(F("LittleFS: nextionMaxPages = ")) + String(nextionMaxPages));
  debugPrintln(String(F("LittleFS: motionPinConfig = ")) + String(motionPinConfig));
  debugPrintln(String(F("LittleFS: debugSerialEnabled = ")) + String(debugSerialEnabled));
  debugPrintln(String(F("LittleFS: debugTelnetEnabled = ")) + String(debugTelnetEnabled));
  debugPrintln(String(F("LittleFS: mdnsEnabled = ")) + String(mdnsEnabled));
  debugPrintln(String(F("LittleFS: beepEnabled = ")) + String(beepEnabled));
  debugPrintln(String(F("LittleFS: ignoreTouchWhenOff = ")) + String(ignoreTouchWhenOff));

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile)
  {
    debugPrintln(F("LittleFS: Failed to open config file for writing"));
  }
  else
  {
    serializeJson(jsonConfigValues, configFile);
    configFile.print("\n\n\n");
    configFile.flush();
    delay(10);
    configFile.close();
  }
  debugPrintFile("/config.json");
  shouldSaveConfig = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void configClearSaved()
{ // Clear out all local storage
  nextionSetAttr("dims", "100");
  nextionSendCmd("page 0");
  nextionSetAttr("p[0].b[1].txt", "\"Resetting\\rsystem...\"");
  debugPrintln(F("RESET: Formatting LittleFS"));
  LittleFS.format();
  debugPrintln(F("RESET: Clearing WiFiManager settings..."));
  WiFi.disconnect();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  EEPROM.begin(512);
  debugPrintln(F("Clearing EEPROM..."));
  for (uint16_t i = 0; i < EEPROM.length(); i++)
  {
    EEPROM.write(i, 0);
  }
  nextionSetAttr("p[0].b[1].txt", "\"Rebooting\\rsystem...\"");
  debugPrintln(F("RESET: Rebooting device"));
  espReset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleNotFound()
{ // webServer 404
  debugPrintln(String(F("HTTP: Sending 404 to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " 404");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(404, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>404: File Not Found</h1>One of us appears to have done something horribly wrong.<hr/><b>URI: </b>"));
  webServer.sendContent(webServer.uri());
  webServer.sendContent(F("<br/><b>Method: </b>"));
  webServer.sendContent((webServer.method() == HTTP_GET) ? F("GET") : F("POST"));
  webServer.sendContent(F("<br/><b>Arguments: </b>"));
  webServer.sendContent(String(webServer.args()));
  for (uint8_t i = 0; i < webServer.args(); i++)
  {
    webServer.sendContent(F("<br/><b>"));
    webServer.sendContent(String(webServer.argName(i)));
    webServer.sendContent(F(":</b> "));
    webServer.sendContent(String(webServer.arg(i)));
  }
  webServer.sendContent("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleRoot()
{ // http://plate01/
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending root page to client connected from: ")) + webServer.client().remoteIP().toString());

  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode));
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent(httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);

  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F("</h1>"));

  webServer.sendContent(F("<form method='POST' action='saveConfig'>"));
  webServer.sendContent(F("<b>WiFi SSID</b> <i><small>(required)</small></i><input id='wifiSSID' required name='wifiSSID' maxlength=32 placeholder='WiFi SSID' value='"));
  webServer.sendContent(WiFi.SSID());
  webServer.sendContent(F("'><br/><b>WiFi Password</b> <i><small>(required)</small></i><input id='wifiPass' required name='wifiPass' type='password' maxlength=64 placeholder='WiFi Password' value='********'>"));
  webServer.sendContent(F("<br/><br/><b>HASPone Node Name</b> <i><small>(required. lowercase letters, numbers, and _ only)</small></i><input id='haspNode' required name='haspNode' maxlength=15 placeholder='HASPone Node Name' pattern='[a-z0-9_]*' value='"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F("'><br/><b>Group Name</b> <i><small>(required)</small></i><input id='groupName' required name='groupName' maxlength=15 placeholder='Group Name' value='"));
  webServer.sendContent(groupName);
  webServer.sendContent(F("'><br/><br/><b>MQTT Broker</b> <i><small>(required, IP address is preferred)</small></i><input id='mqttServer' required name='mqttServer' maxlength=127 placeholder='mqttServer' value='"));
  if (strcmp(mqttServer, "") != 0)
  {
    webServer.sendContent(mqttServer);
  }
  webServer.sendContent(F("'><br/><b>MQTT Port</b> <i><small>(required)</small></i><input id='mqttPort' required name='mqttPort' type='number' maxlength=5 placeholder='mqttPort' value='"));
  if (strcmp(mqttPort, "") != 0)
  {
    webServer.sendContent(mqttPort);
  }
  webServer.sendContent(F("'><br/><b>MQTT User</b> <i><small>(optional)</small></i><input id='mqttUser' name='mqttUser' maxlength=127 placeholder='mqttUser' value='"));
  if (strcmp(mqttUser, "") != 0)
  {
    webServer.sendContent(mqttUser);
  }
  webServer.sendContent(F("'><br/><b>MQTT Password</b> <i><small>(optional)</small></i><input id='mqttPassword' name='mqttPassword' type='password' maxlength=127 placeholder='mqttPassword' value='"));
  if (strcmp(mqttUser, "") != 0)
  {
    webServer.sendContent(F("********"));
  }
  webServer.sendContent(F("'>"));

  webServer.sendContent(F("<br/><b>MQTT TLS enabled:</b><input id='mqttTlsEnabled' name='mqttTlsEnabled' type='checkbox'"));
  if (mqttTlsEnabled)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><b>MQTT TLS Fingerpint</b> <i><small>(leave blank to disable fingerprint checking)</small></i><input id='mqttFingerprint' name='mqttFingerprint' maxlength=59 minlength=59 placeholder='01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11:12:13:14' value='"));
  if (strcmp(mqttFingerprint, "") != 0)
  {
    webServer.sendContent(mqttFingerprint);
  }
  webServer.sendContent(F("'>"));

  webServer.sendContent(F("<br/><br/><b>HASPone Admin Username</b> <i><small>(optional)</small></i><input id='configUser' name='configUser' maxlength=31 placeholder='Admin User' value='"));
  if (strcmp(configUser, "") != 0)
  {
    webServer.sendContent(configUser);
  }
  webServer.sendContent(F("'><br/><b>HASPone Admin Password</b> <i><small>(optional)</small></i><input id='configPassword' name='configPassword' type='password' maxlength=31 placeholder='Admin User Password' value='"));
  if (strcmp(configPassword, "") != 0)
  {
    webServer.sendContent(F("********"));
  }
  webServer.sendContent(F("'><br/><br/><b>Home Assistant discovery topic</b> <i><small>(required, should probably be \"homeassistant\")</small></i><input id='hassDiscovery' name='hassDiscovery' maxlength=127 placeholder='homeassistant' value='"));
  if (strcmp(hassDiscovery, "") != 0)
  {
    webServer.sendContent(hassDiscovery);
  }
  webServer.sendContent(F("'><br/><b>Nextion project page count</b> <i><small>(required, probably \"11\")</small></i><input id='nextionMaxPages' required name='nextionMaxPages' type='number' maxlength=2 placeholder='nextionMaxPages' value='"));
  if (nextionMaxPages != 0)
  {
    webServer.sendContent(String(nextionMaxPages));
  }
  webServer.sendContent(F("'><br/><hr>"));
  // Big menu of possible serial speeds
  if ((lcdVersion != 1) && (lcdVersion != 2))
  { // HASPone lcdVersion 1 and 2 have `bauds=115200` in the pre-init script of page 0.  Don't show this option if either of those two versions are running.
    webServer.sendContent(F("<b>LCD Serial Speed:&nbsp;</b><select id='nextionBaud' name='nextionBaud'>"));

    for (unsigned int nextionSpeedsIndex = 0; nextionSpeedsIndex < nextionSpeedsLength; nextionSpeedsIndex++)
    {
      char nextionSpeedItem[sizeof(unsigned long) * 3 + 2];
      snprintf(nextionSpeedItem, sizeof nextionSpeedItem, "%ld", nextionSpeeds[nextionSpeedsIndex]);
      webServer.sendContent(F("<option value='"));
      webServer.sendContent(nextionSpeedItem);
      webServer.sendContent(F("'"));
      if (strcmp(nextionSpeedItem, nextionBaud) == 0)
      {
        webServer.sendContent(F(" selected"));
      }
      webServer.sendContent(F(">"));
      webServer.sendContent(nextionSpeedItem);
      webServer.sendContent(F("</option>"));
    }
    webServer.sendContent(F("</select><br/>"));
  }

  webServer.sendContent(F("<b>USB serial debug output enabled:</b><input id='debugSerialEnabled' name='debugSerialEnabled' type='checkbox'"));
  if (debugSerialEnabled)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><b>Telnet debug output enabled:</b><input id='debugTelnetEnabled' name='debugTelnetEnabled' type='checkbox'"));
  if (debugTelnetEnabled)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><b>mDNS enabled:</b><input id='mdnsEnabled' name='mdnsEnabled' type='checkbox'"));
  if (mdnsEnabled)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><b>Motion Sensor Pin:&nbsp;</b><select id='motionPinConfig' name='motionPinConfig'><option value='0'"));
  if (!motionPin)
  {
    webServer.sendContent(F(" selected"));
  }
  webServer.sendContent(F(">disabled/not installed</option><option value='D0'"));
  if (motionPin == D0)
  {
    webServer.sendContent(F(" selected"));
  }
  webServer.sendContent(F(">D0</option><option value='D1'"));
  if (motionPin == D1)
  {
    webServer.sendContent(F(" selected"));
  }
  webServer.sendContent(F(">D1</option></select>"));
  webServer.sendContent(F("<br/><b>Keypress beep enabled on D2:</b><input id='beepEnabled' name='beepEnabled' type='checkbox'"));
  if (beepEnabled)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><b>Ignore touchevents when backlight is off:</b><input id='ignoreTouchWhenOff' name='ignoreTouchWhenOff' type='checkbox'"));
  if (ignoreTouchWhenOff)
  {
    webServer.sendContent(F(" checked='checked'"));
  }
  webServer.sendContent(F("><br/><hr><button type='submit'>save settings</button></form>"));

  if (updateEspAvailable)
  {
    webServer.sendContent(F("<br/><hr><font color='green'><center><h3>HASPone Update available!</h3></center></font>"));
    webServer.sendContent(F("<form method='get' action='espfirmware'>"));
    webServer.sendContent(F("<input id='espFirmwareURL' type='hidden' name='espFirmware' value='"));
    webServer.sendContent(espFirmwareUrl);
    webServer.sendContent(F("'><button type='submit'>update HASPone to v"));
    webServer.sendContent(String(updateEspAvailableVersion));
    webServer.sendContent(F("</button></form>"));
  }

  webServer.sendContent(F("<hr><form method='get' action='firmware'>"));
  webServer.sendContent(F("<button type='submit'>update firmware</button></form>"));

  webServer.sendContent(F("<hr><form method='get' action='reboot'>"));
  webServer.sendContent(F("<button type='submit'>reboot device</button></form>"));

  webServer.sendContent(F("<hr><form method='get' action='resetBacklight'>"));
  webServer.sendContent(F("<button type='submit'>reset lcd backlight</button></form>"));

  webServer.sendContent(F("<hr><form method='get' action='resetConfig'>"));
  webServer.sendContent(F("<button type='submit'>factory reset settings</button></form>"));

  webServer.sendContent(F("<hr><b>MQTT Status: </b>"));
  if (mqttClient.connected())
  { // Check MQTT connection
    webServer.sendContent(F("Connected"));
  }
  else
  {
    webServer.sendContent(F("<font color='red'><b>Disconnected</b></font><br/><b>MQTT return code:</b> "));
    webServer.sendContent(String(mqttClient.returnCode()));
    webServer.sendContent(F("<br/><b>MQTT last error:</b> "));
    webServer.sendContent(String(mqttClient.lastError()));
    webServer.sendContent(F("<br/><b>MQTT broker ping check:</b> "));
    if (mqttPingCheck)
    {
      webServer.sendContent(F("<font color='green'>SUCCESS</font>"));
    }
    else
    {
      webServer.sendContent(F("<font color='red'>FAILED</font>"));
    }
    webServer.sendContent(F("<br/><b>MQTT broker port check:</b> "));
    if (mqttPortCheck)
    {
      webServer.sendContent(F("<font color='green'>SUCCESS</font>"));
    }
    else
    {
      webServer.sendContent(F("<font color='red'>FAILED</font>"));
    }
  }
  webServer.sendContent(F("<br/><b>MQTT ClientID: </b>"));
  if (mqttClientId != "")
  {
    webServer.sendContent(mqttClientId);
  }
  webServer.sendContent(F("<br/><b>HASPone FW Version: </b>"));
  webServer.sendContent(String(haspVersion));
  webServer.sendContent(F("<br/><b>LCD Model: </b>"));
  if (nextionModel != "")
  {
    webServer.sendContent(nextionModel);
  }
  webServer.sendContent(F("<br/><b>LCD FW Version: </b>"));
  webServer.sendContent(String(lcdVersion));
  webServer.sendContent(F("<br/><b>LCD Active Page: </b>"));
  webServer.sendContent(String(nextionActivePage));
  webServer.sendContent(F("<br/><b>LCD Serial Speed: </b>"));
  webServer.sendContent(nextionBaud);
  webServer.sendContent(F("<br/><b>CPU Frequency: </b>"));
  webServer.sendContent(String(ESP.getCpuFreqMHz()));
  webServer.sendContent(F("MHz"));
  webServer.sendContent(F("<br/><b>Sketch Size: </b>"));
  webServer.sendContent(String(ESP.getSketchSize()));
  webServer.sendContent(F(" bytes"));
  webServer.sendContent(F("<br/><b>Free Sketch Space: </b>"));
  webServer.sendContent(String(ESP.getFreeSketchSpace()));
  webServer.sendContent(F(" bytes"));
  webServer.sendContent(F("<br/><b>Heap Free: </b>"));
  webServer.sendContent(String(ESP.getFreeHeap()));
  webServer.sendContent(F("<br/><b>Heap Fragmentation: </b>"));
  webServer.sendContent(String(ESP.getHeapFragmentation()));
  webServer.sendContent(F("<br/><b>ESP core version: </b>"));
  webServer.sendContent(ESP.getCoreVersion());
  webServer.sendContent(F("<br/><b>IP Address: </b>"));
  webServer.sendContent(WiFi.localIP().toString());
  webServer.sendContent(F("<br/><b>Signal Strength: </b>"));
  webServer.sendContent(String(WiFi.RSSI()));
  webServer.sendContent(F("<br/><b>Uptime: </b>"));
  webServer.sendContent(String(long(millis() / 1000)));
  webServer.sendContent(F("<br/><b>Last reset: </b>"));
  webServer.sendContent(ESP.getResetInfo());
  webServer.sendContent(F("<br/><b>Motion Pin: </b>"));
  webServer.sendContent(String(motionPin));
  webServer.sendContent(F("<br/><b>Motion Enabled: </b>"));
  webServer.sendContent(String(motionEnabled));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleSaveConfig()
{ // http://plate01/saveConfig
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /saveConfig page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " Saving configuration");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);

  bool shouldSaveWifi = false;
  // Check required values
  if ((webServer.arg("wifiSSID") != "") && (webServer.arg("wifiSSID") != String(WiFi.SSID())))
  { // Handle WiFi SSID
    shouldSaveConfig = true;
    shouldSaveWifi = true;
    webServer.arg("wifiSSID").toCharArray(wifiSSID, 32);
  }
  if ((webServer.arg("wifiPass") != String(wifiPass)) && (webServer.arg("wifiPass") != String("********")))
  { // Handle WiFi password
    shouldSaveConfig = true;
    shouldSaveWifi = true;
    webServer.arg("wifiPass").toCharArray(wifiPass, 64);
  }

  if (webServer.arg("haspNode") != "" && webServer.arg("haspNode") != String(haspNode))
  { // Handle haspNode
    shouldSaveConfig = true;
    String lowerHaspNode = webServer.arg("haspNode");
    lowerHaspNode.toLowerCase();
    lowerHaspNode.toCharArray(haspNode, 16);
  }
  if (webServer.arg("groupName") != "" && webServer.arg("groupName") != String(groupName))
  { // Handle groupName
    shouldSaveConfig = true;
    webServer.arg("groupName").toCharArray(groupName, 16);
  }

  if (webServer.arg("mqttServer") != "" && webServer.arg("mqttServer") != String(mqttServer))
  { // Handle mqttServer
    shouldSaveConfig = true;
    webServer.arg("mqttServer").toCharArray(mqttServer, 128);
  }
  if (webServer.arg("mqttPort") != "" && webServer.arg("mqttPort") != String(mqttPort))
  { // Handle mqttPort
    shouldSaveConfig = true;
    webServer.arg("mqttPort").toCharArray(mqttPort, 6);
  }
  if (webServer.arg("mqttUser") != String(mqttUser))
  { // Handle mqttUser
    shouldSaveConfig = true;
    webServer.arg("mqttUser").toCharArray(mqttUser, 128);
  }
  if (webServer.arg("mqttPassword") != String("********"))
  { // Handle mqttPassword
    shouldSaveConfig = true;
    webServer.arg("mqttPassword").toCharArray(mqttPassword, 128);
  }
  if ((webServer.arg("mqttTlsEnabled") == String("on")) && !mqttTlsEnabled)
  { // mqttTlsEnabled was disabled but should now be enabled
    shouldSaveConfig = true;
    mqttTlsEnabled = true;
  }
  else if ((webServer.arg("mqttTlsEnabled") == String("")) && mqttTlsEnabled)
  { // mqttTlsEnabled was enabled but should now be disabled
    shouldSaveConfig = true;
    mqttTlsEnabled = false;
  }
  if (webServer.arg("mqttFingerprint") != String(mqttFingerprint))
  { // Handle mqttFingerprint
    shouldSaveConfig = true;
    webServer.arg("mqttFingerprint").toCharArray(mqttFingerprint, 60);
  }
  if (webServer.arg("configUser") != String(configUser))
  { // Handle configUser
    shouldSaveConfig = true;
    webServer.arg("configUser").toCharArray(configUser, 32);
  }
  if (webServer.arg("configPassword") != String("********"))
  { // Handle configPassword
    shouldSaveConfig = true;
    webServer.arg("configPassword").toCharArray(configPassword, 32);
  }
  if (webServer.arg("hassDiscovery") != String(hassDiscovery))
  { // Handle hassDiscovery
    shouldSaveConfig = true;
    webServer.arg("hassDiscovery").toCharArray(hassDiscovery, 128);
  }
  if ((webServer.arg("nextionMaxPages") != String(nextionMaxPages)) && (webServer.arg("nextionMaxPages").toInt() < 256) && (webServer.arg("nextionMaxPages").toInt() > 0))
  {
    shouldSaveConfig = true;
    nextionMaxPages = webServer.arg("nextionMaxPages").toInt();
  }
  if (webServer.arg("nextionBaud") != String(nextionBaud))
  { // Handle nextionBaud
    shouldSaveConfig = true;
    webServer.arg("nextionBaud").toCharArray(nextionBaud, 7);
  }
  if (webServer.arg("motionPinConfig") != String(motionPinConfig))
  { // Handle motionPinConfig
    shouldSaveConfig = true;
    webServer.arg("motionPinConfig").toCharArray(motionPinConfig, 3);
  }
  if ((webServer.arg("debugSerialEnabled") == String("on")) && !debugSerialEnabled)
  { // debugSerialEnabled was disabled but should now be enabled
    shouldSaveConfig = true;
    debugSerialEnabled = true;
  }
  else if ((webServer.arg("debugSerialEnabled") == String("")) && debugSerialEnabled)
  { // debugSerialEnabled was enabled but should now be disabled
    shouldSaveConfig = true;
    debugSerialEnabled = false;
  }
  if ((webServer.arg("debugTelnetEnabled") == String("on")) && !debugTelnetEnabled)
  { // debugTelnetEnabled was disabled but should now be enabled
    shouldSaveConfig = true;
    debugTelnetEnabled = true;
  }
  else if ((webServer.arg("debugTelnetEnabled") == String("")) && debugTelnetEnabled)
  { // debugTelnetEnabled was enabled but should now be disabled
    shouldSaveConfig = true;
    debugTelnetEnabled = false;
  }
  if ((webServer.arg("mdnsEnabled") == String("on")) && !mdnsEnabled)
  { // mdnsEnabled was disabled but should now be enabled
    shouldSaveConfig = true;
    mdnsEnabled = true;
  }
  else if ((webServer.arg("mdnsEnabled") == String("")) && mdnsEnabled)
  { // mdnsEnabled was enabled but should now be disabled
    shouldSaveConfig = true;
    mdnsEnabled = false;
  }
  if ((webServer.arg("beepEnabled") == String("on")) && !beepEnabled)
  { // beepEnabled was disabled but should now be enabled
    shouldSaveConfig = true;
    beepEnabled = true;
  }
  else if ((webServer.arg("beepEnabled") == String("")) && beepEnabled)
  { // beepEnabled was enabled but should now be disabled
    shouldSaveConfig = true;
    beepEnabled = false;
  }
  if ((webServer.arg("ignoreTouchWhenOff") == String("on")) && !ignoreTouchWhenOff)
  { // ignoreTouchWhenOff was disabled but should now be enabled
    shouldSaveConfig = true;
    ignoreTouchWhenOff = true;
  }
  else if ((webServer.arg("ignoreTouchWhenOff") == String("")) && ignoreTouchWhenOff)
  { // ignoreTouchWhenOff was enabled but should now be disabled
    shouldSaveConfig = true;
    ignoreTouchWhenOff = false;
  }

  if (shouldSaveConfig)
  { // Config updated, notify user and trigger write to LittleFS

    webServer.sendContent(F("<meta http-equiv='refresh' content='15;url=/' />"));
    webServer.sendContent_P(HTTP_HEAD_END);
    webServer.sendContent(F("<h1>"));
    webServer.sendContent(haspNode);
    webServer.sendContent(F("</h1>"));
    webServer.sendContent(F("<br/>Saving updated configuration values and restarting device"));
    webServer.sendContent_P(HTTP_END);
    webServer.sendContent(F(""));
    configSave();
    if (shouldSaveWifi)
    {
      debugPrintln(String(F("CONFIG: Attempting connection to SSID: ")) + webServer.arg("wifiSSID"));
      espWifiConnect();
    }
    espReset();
  }
  else
  { // No change found, notify user and link back to config page
    webServer.sendContent(F("<meta http-equiv='refresh' content='3;url=/' />"));
    webServer.sendContent_P(HTTP_HEAD_END);
    webServer.sendContent(F("<h1>"));
    webServer.sendContent(haspNode);
    webServer.sendContent(F("</h1>"));
    webServer.sendContent(F("<br/>No changes found, returning to <a href='/'>home page</a>"));
    webServer.sendContent_P(HTTP_END);
    webServer.sendContent(F(""));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleResetConfig()
{ // http://plate01/resetConfig
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /resetConfig page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " Resetting configuration");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);

  if (webServer.arg("confirm") == "yes")
  { // User has confirmed, so reset everything
    webServer.sendContent(F("<h1>"));
    webServer.sendContent(haspNode);
    webServer.sendContent(F("</h1><b>Resetting all saved settings and restarting device into WiFi AP mode</b>"));
    webServer.sendContent_P(HTTP_END);
    webServer.sendContent("");
    delay(100);
    configClearSaved();
  }
  else
  {
    webServer.sendContent(F("<h1>Warning</h1><b>This process will reset all settings to the default values and restart the device.  You may need to connect to the WiFi AP displayed on the panel to re-configure the device before accessing it again."));
    webServer.sendContent(F("<br/><hr><br/><form method='get' action='resetConfig'>"));
    webServer.sendContent(F("<br/><br/><button type='submit' name='confirm' value='yes'>reset all settings</button></form>"));
    webServer.sendContent(F("<br/><hr><br/><form method='get' action='/'>"));
    webServer.sendContent(F("<button type='submit'>return home</button></form>"));
    webServer.sendContent_P(HTTP_END);
    webServer.sendContent("");
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleResetBacklight()
{ // http://plate01/resetBacklight
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /resetBacklight page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " Backlight reset");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent(F("<meta http-equiv='refresh' content='3;url=/' />"));
  webServer.sendContent_P(HTTP_HEAD_END);

  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F("</h1>"));
  webServer.sendContent(F("<br/>Resetting backlight to 100%"));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
  debugPrintln(F("HTTP: Resetting backlight to 100%"));
  nextionSetAttr("dims", "100");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleFirmware()
{ // http://plate01/firmware
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /firmware page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " Firmware updates");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);

  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" Firmware updates</h1><b>Note:</b> If updating firmware for both the ESP8266 and the Nextion LCD, you'll want to update the ESP8266 first followed by the Nextion LCD<br/><hr/>"));

  // Display main firmware page
  webServer.sendContent(F("<form method='get' action='/espfirmware'>"));
  if (updateEspAvailable)
  {
    webServer.sendContent(F("<font color='green'><b>HASPone ESP8266 update available!</b></font>"));
  }
  webServer.sendContent(F("<br/><b>Update ESP8266 from URL</b>"));
  webServer.sendContent(F("<br/><input id='espFirmwareURL' name='espFirmware' value='"));
  webServer.sendContent(espFirmwareUrl);
  webServer.sendContent(F("'><br/><br/><button type='submit'>Update ESP from URL</button></form>"));

  webServer.sendContent(F("<br/><form method='POST' action='/update' enctype='multipart/form-data'>"));
  webServer.sendContent(F("<b>Update ESP8266 from file</b><input type='file' id='espSelect' name='espSelect' accept='.bin'>"));
  webServer.sendContent(F("<br/><br/><button type='submit' id='espUploadSubmit' onclick='ackEspUploadSubmit()'>Update ESP from file</button></form>"));

  webServer.sendContent(F("<br/><br/><hr><h1>WARNING!</h1>"));
  webServer.sendContent(F("<b>Nextion LCD firmware updates can be risky.</b> If interrupted, the LCD will display an error message until a successful firmware update has completed. "));
  webServer.sendContent(F("<br/><br/><i>Note: Failed LCD firmware updates on HASPone hardware prior to v1.0 may require a hard power cycle of the device, via a circuit breaker or by physically disconnecting the device.</i>"));

  webServer.sendContent(F("<br/><hr><form method='get' action='lcddownload'>"));
  if (updateLcdAvailable)
  {
    webServer.sendContent(F("<font color='green'><b>HASPone LCD update available!</b></font>"));
  }
  webServer.sendContent(F("<br/><b>Update Nextion LCD from URL</b><small><i> http only</i></small>"));
  webServer.sendContent(F("<br/><input id='lcdFirmware' name='lcdFirmware' value='"));
  webServer.sendContent(lcdFirmwareUrl);
  webServer.sendContent(F("'><br/><br/><button type='submit'>Update LCD from URL</button></form>"));

  webServer.sendContent(F("<br/><form method='POST' action='/lcdupload' enctype='multipart/form-data'>"));
  webServer.sendContent(F("<br/><b>Update Nextion LCD from file</b><input type='file' id='lcdSelect' name='files[]' accept='.tft'/>"));
  webServer.sendContent(F("<br/><br/><button type='submit' id='lcdUploadSubmit' onclick='ackLcdUploadSubmit()'>Update LCD from file</button></form>"));

  // Javascript to collect the filesize of the LCD upload and send it to /tftFileSize
  webServer.sendContent(F("<script>function handleLcdFileSelect(evt) {"));
  webServer.sendContent(F("var uploadFile = evt.target.files[0];"));
  webServer.sendContent(F("document.getElementById('lcdUploadSubmit').innerHTML = 'Upload LCD firmware ' + uploadFile.name;"));
  webServer.sendContent(F("var tftFileSize = '/tftFileSize?tftFileSize=' + uploadFile.size;"));
  webServer.sendContent(F("var xhttp = new XMLHttpRequest();xhttp.open('GET', tftFileSize, true);xhttp.send();}"));
  webServer.sendContent(F("function ackLcdUploadSubmit() {document.getElementById('lcdUploadSubmit').innerHTML = 'Uploading LCD firmware...';}"));
  webServer.sendContent(F("function handleEspFileSelect(evt) {var uploadFile = evt.target.files[0];document.getElementById('espUploadSubmit').innerHTML = 'Upload ESP firmware ' + uploadFile.name;}"));
  webServer.sendContent(F("function ackEspUploadSubmit() {document.getElementById('espUploadSubmit').innerHTML = 'Uploading ESP firmware...';}"));
  webServer.sendContent(F("document.getElementById('lcdSelect').addEventListener('change', handleLcdFileSelect, false);"));
  webServer.sendContent(F("document.getElementById('espSelect').addEventListener('change', handleEspFileSelect, false);</script>"));

  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleEspFirmware()
{ // http://plate01/espfirmware
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }

  debugPrintln(String(F("HTTP: Sending /espfirmware page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " ESP8266 firmware update");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent(F("<meta http-equiv='refresh' content='60;url=/' />"));
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" ESP8266 firmware update</h1>"));
  webServer.sendContent(F("<br/>Updating ESP firmware from: "));
  webServer.sendContent(webServer.arg("espFirmware"));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");

  debugPrintln("ESPFW: Attempting ESP firmware update from: " + String(webServer.arg("espFirmware")));
  espStartOta(webServer.arg("espFirmware"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleLcdUpload()
{ // http://plate01/lcdupload
  // Upload firmware to the Nextion LCD via HTTP upload

  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }

  static uint32_t lcdOtaTransferred = 0;
  static uint32_t lcdOtaRemaining;
  static uint16_t lcdOtaParts;
  const uint32_t lcdOtaTimeout = 30000; // timeout for receiving new data in milliseconds
  static uint32_t lcdOtaTimer = 0;      // timer for upload timeout

  HTTPUpload &upload = webServer.upload();

  if (tftFileSize == 0)
  {
    debugPrintln(String(F("LCDOTA: FAILED, no filesize sent.")));
    String httpHeader = FPSTR(HTTP_HEAD_START);
    httpHeader.replace("{v}", "HASPone " + String(haspNode) + " LCD update error");
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "text/html", httpHeader);
    webServer.sendContent_P(HTTP_SCRIPT);
    webServer.sendContent_P(HTTP_STYLE);
    webServer.sendContent_P(HASP_STYLE);
    webServer.sendContent(F("<meta http-equiv='refresh' content='5;url=/firmware' />"));
    webServer.sendContent_P(HTTP_HEAD_END);
    webServer.sendContent(F("<h1>"));
    webServer.sendContent(haspNode);
    webServer.sendContent(F(" LCD update FAILED</h1>"));
    webServer.sendContent(F("No update file size reported.  You must use a modern browser with Javascript enabled."));
    webServer.sendContent_P(HTTP_END);
    webServer.sendContent("");
  }
  else if ((lcdOtaTimer > 0) && ((millis() - lcdOtaTimer) > lcdOtaTimeout))
  { // Our timer expired so reset
    debugPrintln(F("LCDOTA: ERROR: LCD upload timeout.  Restarting."));
    espReset();
  }
  else if (upload.status == UPLOAD_FILE_START)
  {
    WiFiUDP::stopAll(); // Keep mDNS responder from breaking things

    debugPrintln(String(F("LCDOTA: Attempting firmware upload")));
    debugPrintln(String(F("LCDOTA: upload.filename: ")) + String(upload.filename));
    debugPrintln(String(F("LCDOTA: TFTfileSize: ")) + String(tftFileSize));

    lcdOtaRemaining = tftFileSize;
    lcdOtaParts = (lcdOtaRemaining / 4096) + 1;
    debugPrintln(String(F("LCDOTA: File upload beginning. Size ")) + String(lcdOtaRemaining) + String(F(" bytes in ")) + String(lcdOtaParts) + String(F(" 4k chunks.")));

    Serial1.write(nextionSuffix, sizeof(nextionSuffix)); // Send empty command to LCD
    Serial1.flush();
    nextionHandleInput();

    String lcdOtaNextionCmd = "whmi-wri " + String(tftFileSize) + "," + String(nextionBaud) + ",0";
    debugPrintln(String(F("LCDOTA: Sending LCD upload command: ")) + lcdOtaNextionCmd);
    Serial1.print(lcdOtaNextionCmd);
    Serial1.write(nextionSuffix, sizeof(nextionSuffix));
    Serial1.flush();

    if (nextionOtaResponse())
    {
      debugPrintln(F("LCDOTA: LCD upload command accepted"));
    }
    else
    {
      debugPrintln(F("LCDOTA: LCD upload command FAILED."));
      espReset();
    }
    lcdOtaTimer = millis();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  { // Handle upload data
    static int lcdOtaChunkCounter = 0;
    static uint16_t lcdOtaPartNum = 0;
    static int lcdOtaPercentComplete = 0;
    static const uint16_t lcdOtaBufferSize = 1024; // upload data buffer before sending to UART
    static uint8_t lcdOtaBuffer[lcdOtaBufferSize] = {};
    uint16_t lcdOtaUploadIndex = 0;
    int32_t lcdOtaPacketRemaining = upload.currentSize;

    while (lcdOtaPacketRemaining > 0)
    { // Write incoming data to panel as it arrives
      // determine chunk size as lowest value of lcdOtaPacketRemaining, lcdOtaBufferSize, or 4096 - lcdOtaChunkCounter
      uint16_t lcdOtaChunkSize = 0;
      if ((lcdOtaPacketRemaining <= lcdOtaBufferSize) && (lcdOtaPacketRemaining <= (4096 - lcdOtaChunkCounter)))
      {
        lcdOtaChunkSize = lcdOtaPacketRemaining;
      }
      else if ((lcdOtaBufferSize <= lcdOtaPacketRemaining) && (lcdOtaBufferSize <= (4096 - lcdOtaChunkCounter)))
      {
        lcdOtaChunkSize = lcdOtaBufferSize;
      }
      else
      {
        lcdOtaChunkSize = 4096 - lcdOtaChunkCounter;
      }

      for (uint16_t i = 0; i < lcdOtaChunkSize; i++)
      { // Load up the UART buffer
        lcdOtaBuffer[i] = upload.buf[lcdOtaUploadIndex];
        lcdOtaUploadIndex++;
      }
      Serial1.flush();                              // Clear out current UART buffer
      Serial1.write(lcdOtaBuffer, lcdOtaChunkSize); // And send the most recent data
      lcdOtaChunkCounter += lcdOtaChunkSize;
      lcdOtaTransferred += lcdOtaChunkSize;
      if (lcdOtaChunkCounter >= 4096)
      {
        Serial1.flush();
        lcdOtaPartNum++;
        lcdOtaPercentComplete = (lcdOtaTransferred * 100) / tftFileSize;
        lcdOtaChunkCounter = 0;
        if (nextionOtaResponse())
        {
          debugPrintln(String(F("LCDOTA: Part ")) + String(lcdOtaPartNum) + String(F(" OK, ")) + String(lcdOtaPercentComplete) + String(F("% complete")));
        }
        else
        {
          debugPrintln(String(F("LCDOTA: Part ")) + String(lcdOtaPartNum) + String(F(" FAILED, ")) + String(lcdOtaPercentComplete) + String(F("% complete")));
        }
      }
      else
      {
        delay(10);
      }
      if (lcdOtaRemaining > 0)
      {
        lcdOtaRemaining -= lcdOtaChunkSize;
      }
      if (lcdOtaPacketRemaining > 0)
      {
        lcdOtaPacketRemaining -= lcdOtaChunkSize;
      }
    }

    if (lcdOtaTransferred >= tftFileSize)
    {
      if (nextionOtaResponse())
      {
        debugPrintln(String(F("LCDOTA: Success, wrote ")) + String(lcdOtaTransferred) + " of " + String(tftFileSize) + " bytes.");
        webServer.sendHeader("Location", "/lcdOtaSuccess");
        webServer.send(303);
        uint32_t lcdOtaDelay = millis();
        while ((millis() - lcdOtaDelay) < 5000)
        { // extra 5sec delay while the LCD handles any local firmware updates from new versions of code sent to it
          webServer.handleClient();
          delay(1);
        }
        espReset();
      }
      else
      {
        debugPrintln(F("LCDOTA: Failure"));
        webServer.sendHeader("Location", "/lcdOtaFailure");
        webServer.send(303);
        uint32_t lcdOtaDelay = millis();
        while ((millis() - lcdOtaDelay) < 1000)
        { // extra 1sec delay for client to grab failure page
          webServer.handleClient();
          delay(1);
        }
        espReset();
      }
    }
    lcdOtaTimer = millis();
  }
  else if (upload.status == UPLOAD_FILE_END)
  { // Upload completed
    if (lcdOtaTransferred >= tftFileSize)
    {
      if (nextionOtaResponse())
      { // YAY WE DID IT
        debugPrintln(String(F("LCDOTA: Success, wrote ")) + String(lcdOtaTransferred) + " of " + String(tftFileSize) + " bytes.");
        webServer.sendHeader("Location", "/lcdOtaSuccess");
        webServer.send(303);
        uint32_t lcdOtaDelay = millis();
        while ((millis() - lcdOtaDelay) < 5000)
        { // extra 5sec delay while the LCD handles any local firmware updates from new versions of code sent to it
          webServer.handleClient();
          yield();
        }
        espReset();
      }
      else
      {
        debugPrintln(F("LCDOTA: Failure"));
        webServer.sendHeader("Location", "/lcdOtaFailure");
        webServer.send(303);
        uint32_t lcdOtaDelay = millis();
        while ((millis() - lcdOtaDelay) < 1000)
        { // extra 1sec delay for client to grab failure page
          webServer.handleClient();
          yield();
        }
        espReset();
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  { // Something went kablooey
    debugPrintln(F("LCDOTA: ERROR: upload.status returned: UPLOAD_FILE_ABORTED"));
    debugPrintln(F("LCDOTA: Failure"));
    webServer.sendHeader("Location", "/lcdOtaFailure");
    webServer.send(303);
    uint32_t lcdOtaDelay = millis();
    while ((millis() - lcdOtaDelay) < 1000)
    { // extra 1sec delay for client to grab failure page
      webServer.handleClient();
      yield();
    }
    espReset();
  }
  else
  { // Something went weird, we should never get here...
    debugPrintln(String(F("LCDOTA: upload.status returned: ")) + String(upload.status));
    debugPrintln(F("LCDOTA: Failure"));
    webServer.sendHeader("Location", "/lcdOtaFailure");
    webServer.send(303);
    uint32_t lcdOtaDelay = millis();
    while ((millis() - lcdOtaDelay) < 1000)
    { // extra 1sec delay for client to grab failure page
      webServer.handleClient();
      yield();
    }
    espReset();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleLcdUpdateSuccess()
{ // http://plate01/lcdOtaSuccess
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /lcdOtaSuccess page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " LCD firmware update success");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent(F("<meta http-equiv='refresh' content='15;url=/' />"));
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" LCD update success</h1>"));
  webServer.sendContent(F("Restarting HASwitchPlate to apply changes..."));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleLcdUpdateFailure()
{ // http://plate01/lcdOtaFailure
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /lcdOtaFailure page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " LCD firmware update failed");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent(F("<meta http-equiv='refresh' content='15;url=/' />"));
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" LCD update failed :(</h1>"));
  webServer.sendContent(F("Restarting HASwitchPlate to apply changes..."));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleLcdDownload()
{ // http://plate01/lcddownload
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /lcddownload page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " LCD firmware update");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" LCD update</h1>"));
  webServer.sendContent(F("<br/>Updating LCD firmware from: "));
  webServer.sendContent(webServer.arg("lcdFirmware"));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
  nextionOtaStartDownload(webServer.arg("lcdFirmware"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleTftFileSize()
{ // http://plate01/tftFileSize
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /tftFileSize page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " TFT Filesize");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_HEAD_END);
  tftFileSize = webServer.arg("tftFileSize").toInt();
  debugPrintln(String(F("WEB: Received tftFileSize: ")) + String(tftFileSize));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void webHandleReboot()
{ // http://plate01/reboot
  if (configPassword[0] != '\0')
  { // Request HTTP auth if configPassword is set
    if (!webServer.authenticate(configUser, configPassword))
    {
      return webServer.requestAuthentication();
    }
  }
  debugPrintln(String(F("HTTP: Sending /reboot page to client connected from: ")) + webServer.client().remoteIP().toString());
  String httpHeader = FPSTR(HTTP_HEAD_START);
  httpHeader.replace("{v}", "HASPone " + String(haspNode) + " reboot");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", httpHeader);
  webServer.sendContent_P(HTTP_SCRIPT);
  webServer.sendContent_P(HTTP_STYLE);
  webServer.sendContent_P(HASP_STYLE);
  webServer.sendContent(F("<meta http-equiv='refresh' content='10;url=/' />"));
  webServer.sendContent_P(HTTP_HEAD_END);
  webServer.sendContent(F("<h1>"));
  webServer.sendContent(haspNode);
  webServer.sendContent(F(" Reboot</h1>"));
  webServer.sendContent(F("<br/>Rebooting device"));
  webServer.sendContent_P(HTTP_END);
  webServer.sendContent("");
  nextionSendCmd("page 0");
  nextionSetAttr("p[0].b[1].txt", "\"Rebooting...\"");
  espReset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool updateCheck()
{ // firmware update check
  WiFiClientSecure wifiUpdateClientSecure;
  HTTPClient updateClient;
  debugPrintln(String(F("UPDATE: Checking update URL: ")) + FPSTR(UPDATE_URL));

  wifiUpdateClientSecure.setInsecure();
  wifiUpdateClientSecure.setBufferSizes(512, 512);
  updateClient.begin(wifiUpdateClientSecure, UPDATE_URL);

  int httpCode = updateClient.GET(); // start connection and send HTTP header
  if (httpCode != HTTP_CODE_OK)
  {
    debugPrintln(String(F("UPDATE: Update check failed: ")) + updateClient.errorToString(httpCode));
    return false;
  }

  DynamicJsonDocument updateJson(2048);
  DeserializationError jsonError = deserializeJson(updateJson, updateClient.getString());
  updateClient.end();

  if (jsonError)
  { // Couldn't parse the returned JSON, so bail
    debugPrintln(String(F("UPDATE: JSON parsing failed: ")) + String(jsonError.c_str()));
    mqttClient.publish(mqttStateJSONTopic, String(F("{\"event\":\"jsonError\",\"event_source\":\"updateCheck()\",\"event_description\":\"Failed to parse incoming JSON command with error: ")) + String(jsonError.c_str()) + String(F("\"}")));
    return false;
  }
  else
  {
    if (!updateJson["d1_mini"]["version"].isNull())
    {
      updateEspAvailableVersion = updateJson["d1_mini"]["version"].as<float>();
      debugPrintln(String(F("UPDATE: updateEspAvailableVersion: ")) + String(updateEspAvailableVersion));
      espFirmwareUrl = updateJson["d1_mini"]["firmware"].as<String>();
      if (updateEspAvailableVersion > haspVersion)
      {
        updateEspAvailable = true;
        debugPrintln(String(F("UPDATE: New ESP version available: ")) + String(updateEspAvailableVersion));
      }
    }
    if (nextionModel && !updateJson[nextionModel]["version"].isNull())
    {
      updateLcdAvailableVersion = updateJson[nextionModel]["version"].as<int>();
      debugPrintln(String(F("UPDATE: updateLcdAvailableVersion: ")) + String(updateLcdAvailableVersion));
      lcdFirmwareUrl = updateJson[nextionModel]["firmware"].as<String>();
      if (updateLcdAvailableVersion > lcdVersion)
      {
        updateLcdAvailable = true;
        debugPrintln(String(F("UPDATE: New LCD version available: ")) + String(updateLcdAvailableVersion));
      }
    }
    debugPrintln(F("UPDATE: Update check completed"));
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void motionSetup()
{
  if (strcmp(motionPinConfig, "D0") == 0)
  {
    motionEnabled = true;
    motionPin = D0;
    pinMode(motionPin, INPUT);
  }
  else if (strcmp(motionPinConfig, "D1") == 0)
  {
    motionEnabled = true;
    motionPin = D1;
    pinMode(motionPin, INPUT);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void motionHandle()
{ // Monitor motion sensor
  if (motionEnabled)
  {                                                    // Check on our motion sensor
    static unsigned long motionLatchTimer = 0;         // Timer for motion sensor latch
    static unsigned long motionBufferTimer = millis(); // Timer for motion sensor buffer
    static bool motionActiveBuffer = motionActive;
    bool motionRead = digitalRead(motionPin);

    //debugPrintln("MOTION: Active" + String(motionRead));
    if (motionRead != motionActiveBuffer)
    { // if we've changed state
      motionBufferTimer = millis();
      motionActiveBuffer = motionRead;
    }
    else if (millis() > (motionBufferTimer + motionBufferTimeout))
    {
      if ((motionActiveBuffer && !motionActive) && (millis() > (motionLatchTimer + motionLatchTimeout)))
      {
        motionLatchTimer = millis();
        mqttClient.publish(mqttMotionStateTopic, "ON");
        debugPrintln(String(F("MQTT OUT: '")) + mqttMotionStateTopic + String(F("' : 'ON'")));
        motionActive = motionActiveBuffer;
        debugPrintln("MOTION: Active");
      }
      else if ((!motionActiveBuffer && motionActive) && (millis() > (motionLatchTimer + motionLatchTimeout)))
      {
        motionLatchTimer = millis();
        mqttClient.publish(mqttMotionStateTopic, "OFF");
        debugPrintln(String(F("MQTT OUT: '")) + mqttMotionStateTopic + String(F("' : 'OFF'")));
        motionActive = motionActiveBuffer;
        debugPrintln("MOTION: Inactive");
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void beepHandle()
{ // Handle beep/tactile feedback
  if (beepEnabled)
  {
    static bool beepState = false;           // beep currently engaged
    static unsigned long beepPrevMillis = 0; // store last time beep was updated
    if ((beepState == true) && (millis() - beepPrevMillis >= beepOnTime) && ((beepCounter > 0)))
    {
      beepState = false;         // Turn it off
      beepPrevMillis = millis(); // Remember the time
      analogWrite(beepPin, 254); // start beep for beepOnTime
      if (beepCounter > 0)
      { // Update the beep counter.
        beepCounter--;
      }
    }
    else if ((beepState == false) && (millis() - beepPrevMillis >= beepOffTime) && ((beepCounter >= 0)))
    {
      beepState = true;          // turn it on
      beepPrevMillis = millis(); // Remember the time
      analogWrite(beepPin, 0);   // stop beep for beepOffTime
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void telnetHandleClient()
{ // Basic telnet client handling code from: https://gist.github.com/tablatronix/4793677ca748f5f584c95ec4a2b10303
  if (debugTelnetEnabled)
  { // Only do any of this if we're actually enabled
    static unsigned long telnetInputIndex = 0;
    if (telnetServer.hasClient())
    { // client is connected
      if (!telnetClient || !telnetClient.connected())
      {
        if (telnetClient)
        {
          telnetClient.stop(); // client disconnected
        }
        telnetClient = telnetServer.available(); // ready for new client
        telnetInputIndex = 0;                    // reset input buffer index
      }
      else
      {
        telnetServer.available().stop(); // have client, block new connections
      }
    }
    // Handle client input from telnet connection.
    if (telnetClient && telnetClient.connected() && telnetClient.available())
    { // client input processing
      static char telnetInputBuffer[telnetInputMax];

      if (telnetClient.available())
      {
        char telnetInputByte = telnetClient.read(); // Read client byte
        if (telnetInputByte == 5)
        { // If the telnet client sent a bunch of control commands on connection (which end in ENQUIRY/0x05), ignore them and restart the buffer
          telnetInputIndex = 0;
        }
        else if (telnetInputByte == 13)
        { // telnet line endings should be CRLF: https://tools.ietf.org/html/rfc5198#appendix-C
          // If we get a CR just ignore it
        }
        else if (telnetInputByte == 10)
        {                                          // We've caught a LF (DEC 10), send buffer contents to the Nextion
          telnetInputBuffer[telnetInputIndex] = 0; // null terminate our char array
          nextionSendCmd(String(telnetInputBuffer));
          telnetInputIndex = 0;
        }
        else if (telnetInputIndex < telnetInputMax)
        { // If we have room left in our buffer add the current byte
          telnetInputBuffer[telnetInputIndex] = telnetInputByte;
          telnetInputIndex++;
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void debugPrintln(const String &debugText)
{ // Debug output line of text to our debug targets
  const String debugTimeText = "[+" + String(float(millis()) / 1000, 3) + "s] ";
  if (debugSerialEnabled)
  {
    Serial.print(debugTimeText);
    Serial.println(debugText);
    SoftwareSerial debugSerial(-1, 1); // -1==nc for RX, 1==TX pin
    debugSerial.begin(debugSerialBaud);
    debugSerial.print(debugTimeText);
    debugSerial.println(debugText);
    debugSerial.flush();
  }
  if (debugTelnetEnabled)
  {
    if (telnetClient.connected())
    {
      telnetClient.print(debugTimeText);
      telnetClient.println(debugText);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void debugPrint(const String &debugText)
{ // Debug output a string to our debug targets.
  // Try to avoid using this function if at all possible.  When connected to telnet, printing each
  // character requires a full TCP round-trip + acknowledgement back and execution halts while this
  // happens.  Far better to put everything into a line and send it all out in one packet using
  // debugPrintln.
  if (debugSerialEnabled)
    Serial.print(debugText);
  {
    SoftwareSerial debugSerial(-1, 1); // -1==nc for RX, 1==TX pin
    debugSerial.begin(debugSerialBaud);
    debugSerial.print(debugText);
    debugSerial.flush();
  }
  if (debugTelnetEnabled)
  {
    if (telnetClient.connected())
    {
      telnetClient.print(debugText);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void debugPrintCrash()
{                                    // Debug output line of text to our debug targets
  SoftwareSerial debugSerial(-1, 1); // -1==nc for RX, 1==TX pin
  debugSerial.begin(debugSerialBaud);
  SaveCrash.print(debugSerial);
  SaveCrash.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void debugPrintFile(const String &fileName)
{ // Debug output line of text to our debug targets
  File debugFile = LittleFS.open(fileName, "r");
  if (debugFile)
  {
    uint16_t lineCount = 1;
    while (debugFile.available())
    {
      debugPrintln(F("LittleFS: file:") + fileName + F(" line:") + String(lineCount) + F(" data:") + debugFile.readStringUntil('\n'));
      lineCount++;
    }
    debugFile.close();
  }
  else
  {
    debugPrintln("LittleFS: Error opening file for read: " + fileName);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Submitted by benmprojects to handle "beep" commands. Split
// incoming String by separator, return selected field as String
// Original source: https://arduino.stackexchange.com/a/1237
String getSubtringField(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length();

  for (int i = 0; i <= maxIndex && found <= index; i++)
  {
    if (data.charAt(i) == separator || i == maxIndex)
    {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

////////////////////////////////////////////////////////////////////////////////
String printHex8(byte *data, uint8_t length)
{ // returns input bytes as printable hex values in the format 0x01 0x23 0xFF

  String hex8String;
  for (int i = 0; i < length; i++)
  {
    hex8String += "0x";
    if (data[i] < 0x10)
    {
      hex8String += "0";
    }
    hex8String += String(data[i], HEX);
    if (i != (length - 1))
    {
      hex8String += " ";
    }
  }
  // hex8String.toUpperCase();
  return hex8String;
}

// Helper function definitions
void checkIaqSensorStatus(void)
{
  if (iaqSensor.status != BSEC_OK) {
    if (iaqSensor.status < BSEC_OK) {
      debugPrintln("BSEC error code 1: " + String(iaqSensor.status));
      //for (int i = 0; i < 1000; i++)
      for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
      EEPROM.write(i, 0);

    EEPROM.commit();
        errLeds(); /* Halt in case of failure */
    } else {
      debugPrintln("BSEC warning code : " + String(iaqSensor.status));
    }
  }

  if (iaqSensor.bme680Status != BME680_OK) {
    if (iaqSensor.bme680Status < BME680_OK) {
      debugPrintln("BME680 error code 2: " + String(iaqSensor.bme680Status));
      string_bme680_errocode =String(iaqSensor.bme680Status);
    //for (int i = 0; i < 1000; i++)
        errLeds(); /* Halt in case of failure */
    } else {
      debugPrintln("BME680 warning code : " + String(iaqSensor.bme680Status));
      string_bme680_errocode =String(iaqSensor.bme680Status);
    }
  }
  iaqSensor.status = BSEC_OK;
  string_bme680_errocode = "ok";
}

void errLeds(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}

void loadState(void)
{
  if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE) {
    // Existing state in EEPROM
    debugPrintln(F("Reading state from EEPROM"));
   
    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
      bsecState[i] = EEPROM.read(i + 1);
      String str = String(bsecState[i], HEX);
      debugPrintln(str);
    }

    iaqSensor.setState(bsecState);
    checkIaqSensorStatus();
  } else {
    // Erase the EEPROM with zeroes
    debugPrintln(F("Erasing EEPROM"));

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
      EEPROM.write(i, 0);

    EEPROM.commit();
  }
}

void updateState(void)
{
  bool update = false;
  /* Set a trigger to save the state. Here, the state is saved every STATE_SAVE_PERIOD with the first state being saved once the algorithm achieves full calibration, i.e. iaqAccuracy = 3 */
  if (iaqSensor.iaqAccuracy >= 3) {
     statusUpdateIntervalBME680 = 30000;
     debugPrintln(String("statusUpdateIntervalBME680 set to " + String(statusUpdateIntervalBME680)));
  } else {
     statusUpdateIntervalBME680 = 2000;
     debugPrintln(String("statusUpdateIntervalBME680 set to " + String(statusUpdateIntervalBME680)));
  }
  if (stateUpdateCounter == 0) {
    if (iaqSensor.iaqAccuracy >= 3) {
      update = true;
      stateUpdateCounter++;
    } 
  } else {
    /* Update every STATE_SAVE_PERIOD milliseconds */
    if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis()) {
      update = true;
      stateUpdateCounter++;
    }
  }

  if (update) {
    iaqSensor.getState(bsecState);
    checkIaqSensorStatus();

    debugPrintln(F("Writing state to EEPROM"));

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE ; i++) {
      EEPROM.write(i + 1, bsecState[i]);
      String str = String(bsecState[i], HEX);
      debugPrintln(str);
    }

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
  }
}
