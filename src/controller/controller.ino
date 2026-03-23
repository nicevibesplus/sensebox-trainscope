#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

// --- Configuration ---
const char* ssid = "TrainSense";
const char* password = "pw123456";

// Motor Pins
const unsigned int IN1 = 14;
const unsigned int IN2 = 48;
const unsigned int EN_5V = 21;

// BLE Constants
#define bleServerName "TrainSense Server"
BLEUUID serviceUUID("181A");
BLEUUID gearUUID("2A56");

// --- Global Variables ---
WebServer server(80);
int bleGear = 0;
int manualGear = 0;
bool manualMode = false;
int lastAppliedGear = -1;
bool forward = true;
bool deviceFound = false;
String webLog = "";

// BLE Globals
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
BLEAddress* pServerAddress = nullptr;

// --- Helper: Log to both Serial and Web ---
void logToBoth(String msg) {
    Serial.println(msg);
    webLog = "[" + String(millis() / 1000) + "s] " + msg + "<br>" + webLog;
    if (webLog.length() > 2500) webLog = webLog.substring(0, 2500);
}

bool isBleConnected() {
    return (pClient != nullptr && pClient->isConnected());
}

// --- BLE Callbacks ---
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length,
                           bool isNotify) {
    bleGear = pData[0];
    if (!manualMode) {
        logToBoth("[BLE] Notification: Setting Gear to " + String(bleGear));
    }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        logToBoth("[Scan] Found device: " + String(advertisedDevice.getName().c_str()));
        if (advertisedDevice.getName() == bleServerName) {
            logToBoth("[Scan] SUCCESS: Target " + String(bleServerName) + " matched.");
            advertisedDevice.getScan()->stop();
            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            deviceFound = true;
        }
    }
};

// --- Web Server Routes ---
void handleRoot() {
    if (!LittleFS.exists("/index.html")) {
        server.send(404, "text/plain", "Error: LittleFS file not found. Did you upload the data folder?");
        return;
    }
    // Just serve the raw file. The JavaScript will fetch the data separately.
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
}

void handleStatus() {
    int activeGear = manualMode ? manualGear : bleGear;
    bool connected = isBleConnected();

    // Clean up logs so they don't break the JSON format
    String safeLogs = webLog;
    safeLogs.replace("\"", "\\\"");

    // Build a manual JSON string
    String json = "{";
    json += "\"gear\":" + String(activeGear) + ",";
    json += "\"mode\":\"" + String(manualMode ? "MANUAL" : "AUTO (BLE)") + "\",";
    json += "\"status\":\"" + String(connected ? "BLE CONNECTED" : "BLE SEARCHING...") + "\",";
    json += "\"online\":" + String(connected ? "true" : "false") + ",";
    json += "\"dir\":\"" + String(forward ? "FORWARD" : "REVERSE") + "\",";
    json += "\"ctrlClass\":\"" + String(manualMode ? "" : "disabled") + "\",";
    json += "\"invClass\":\"" + String((manualMode && activeGear == 0) ? "" : "disabled") + "\",";
    json += "\"logs\":\"" + safeLogs + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

void handleToggleMode() {
    manualMode = !manualMode;
    manualGear = 0;
    logToBoth("[System] Mode Toggled to: " + String(manualMode ? "MANUAL" : "AUTO"));
    server.send(200, "text/plain", "OK");  // No more 303 Redirects!
}

void handleSetGear() {
    if (manualMode && server.hasArg("v")) {
        manualGear = server.arg("v").toInt();
        logToBoth("[Manual] Set Gear to " + String(manualGear));
    }
    server.send(200, "text/plain", "OK");
}

void handleInvert() {
    if (manualMode && manualGear == 0) {
        forward = !forward;
        logToBoth("[Manual] Direction swapped to " + String(forward ? "Forward" : "Reverse"));
    } else {
        logToBoth("[System] Rejecting Invert: Constraints not met.");
    }
    server.send(200, "text/plain", "OK");
}

// --- BLE Logic ---
bool connectToServer() {
    logToBoth("[BLE] Attempting connection to Server...");

    if (pClient == nullptr) {
        pClient = BLEDevice::createClient();
    }

    if (!pClient->connect(*pServerAddress)) {
        logToBoth("[BLE] ERROR: Physical connection failed.");
        return false;
    }

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        logToBoth("[BLE] ERROR: Service 181A not found.");
        pClient->disconnect();
        return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(gearUUID);
    if (pRemoteCharacteristic == nullptr) {
        logToBoth("[BLE] ERROR: Characteristic 2A56 not found.");
        pClient->disconnect();
        return false;
    }

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        logToBoth("[BLE] SUCCESS: Ready for data.");
    }
    return true;
}

int gearToAnalog(int gear) {
    if (gear == 1) return 192;
    if (gear == 2) return 255;
    return 0;
}

// --- Main Setup ---
void setup() {
    Serial.begin(115200);
    delay(2000);

    logToBoth("========================================");
    logToBoth("    TRAINSENSE CLIENT BOOT SEQUENCE     ");
    logToBoth("========================================");

    logToBoth("[Setup] Initializing Motor Hardware...");
    pinMode(EN_5V, OUTPUT);
    digitalWrite(EN_5V, HIGH);
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);

    // Initialize LittleFS
    logToBoth("[Setup] Mounting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("[Setup] LittleFS Mount Failed");
    }

    logToBoth("[Setup] Configuring WiFi Hotspot...");
    WiFi.softAP(ssid, password);
    logToBoth("[Setup] IP: " + WiFi.softAPIP().toString());

    logToBoth("[Setup] Starting Web Server...");
    server.on("/", handleRoot);
    server.on("/toggleMode", handleToggleMode);
    server.on("/setGear", handleSetGear);
    server.on("/invert", handleInvert);
    server.on("/status", handleStatus);
    server.begin();

    logToBoth("[Setup] Initializing BLE Stack...");
    BLEDevice::init("TrainSense Client");

    logToBoth("[Setup] Starting BLE Scan (30s)...");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(30, false);

    logToBoth("[Setup] Startup Complete.");
    logToBoth("----------------------------------------");
}

void loop() {
    server.handleClient();

    // Reconnection Logic
    if (deviceFound && !isBleConnected()) {
        pRemoteCharacteristic = nullptr;
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry > 5000) {
            lastRetry = millis();
            if (connectToServer()) {
                logToBoth("[System] BLE Reconnected.");
            }
        }
    }

    int targetGear = manualMode ? manualGear : (isBleConnected() ? bleGear : 0);
    int speed = gearToAnalog(targetGear);

    if (targetGear != lastAppliedGear) {
        logToBoth("[Motor] Applied Gear: " + String(targetGear));
        lastAppliedGear = targetGear;
    }

    if (forward) {
        analogWrite(IN1, speed);
        digitalWrite(IN2, LOW);
    } else {
        analogWrite(IN2, speed);
        digitalWrite(IN1, LOW);
    }

    delay(50);
}