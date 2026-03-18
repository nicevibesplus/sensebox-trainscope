#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
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
    int activeGear = manualMode ? manualGear : bleGear;
    bool connected = isBleConnected();

    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    // Auto-refresh only in Auto mode
    if (!manualMode) html += "<meta http-equiv='refresh' content='3'>";

    html += "<style>body{font-family:sans-serif; text-align:center; padding:20px; background:#f4f4f9;}";
    html +=
        ".card{background:white; padding:20px; border-radius:10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); "
        "margin-bottom:20px;}";
    html +=
        ".btn{display:inline-block; padding:12px 20px; background:#007bff; color:white; text-decoration:none; "
        "border-radius:5px; margin:5px; font-weight:bold;}";
    html += ".btn-mode{background:#6c757d;} .btn-stop{background:#dc3545;}";
    html += ".btn-disabled{background:#ccc; color:#888; cursor:not-allowed;}";
    html +=
        ".status-badge{padding:5px 15px; border-radius:20px; color:white; font-weight:bold; display:inline-block; "
        "margin-bottom:10px;}";
    html += ".bg-online{background:#28a745;} .bg-offline{background:#dc3545;}";
    html +=
        ".console{text-align:left; background:#222; color:#0f0; padding:15px; font-family:monospace; height:180px; "
        "overflow-y:scroll; border-radius:5px; font-size:12px;}</style>";
    html += "</head><body>";

    html += "<h1>TrainSense Monitor</h1>";

    // --- Connection Status Card ---
    html += "<div class='card'>";
    html += "<h3>System Status</h3>";
    if (connected) {
        html += "<span class='status-badge bg-online'>BLE CONNECTED</span>";
    } else {
        html += "<span class='status-badge bg-offline'>BLE DISCONNECTED</span>";
    }
    html += "<p>Control Mode: <strong>" + String(manualMode ? "MANUAL" : "AUTO (BLE)") + "</strong></p>";
    html += "<a href='/toggleMode' class='btn btn-mode'>" + String(manualMode ? "Switch to AUTO" : "Switch to MANUAL") +
            "</a>";
    html += "</div>";

    // --- Motor Output Card ---
    html += "<div class='card'>";
    html += "<h2>Motor Output</h2>";
    html += "<p style='font-size:28px;'>Current Gear: <strong>" + String(activeGear) + "</strong></p>";
    html += "<p>Direction: <strong>" + String(forward ? "FORWARD" : "REVERSE") + "</strong></p>";

    // Manual controls only visible in Manual Mode
    if (manualMode) {
        html += "<hr><h3>Manual Controls</h3>";

        // Invert button hidden if gear > 0
        if (manualGear == 0) {
            html += "<a href='/invert' class='btn'>Invert Direction</a><br><br>";
        } else {
            html += "<span class='btn btn-disabled'>Stop to Invert</span><br><br>";
        }

        html += "<a href='/setGear?v=0' class='btn btn-stop'>STOP (0)</a>";
        html += "<a href='/setGear?v=1' class='btn'>Gear 1</a>";
        html += "<a href='/setGear?v=2' class='btn'>Gear 2</a>";
    }
    html += "</div>";

    html += "<h3>Live Console</h3>";
    html += "<div class='console'>" + webLog + "</div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleToggleMode() {
    manualMode = !manualMode;
    manualGear = 0;
    logToBoth("[System] Mode Toggled to: " + String(manualMode ? "MANUAL" : "AUTO"));
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetGear() {
    if (manualMode && server.hasArg("v")) {
        manualGear = server.arg("v").toInt();
        logToBoth("[Manual] Set Gear to " + String(manualGear));
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleInvert() {
    // Hidden URL safety check
    if (manualMode && manualGear == 0) {
        forward = !forward;
        logToBoth("[Manual] Direction swapped to " + String(forward ? "Forward" : "Reverse"));
    } else {
        logToBoth("[System] Rejecting Invert: Constraints not met.");
    }
    server.sendHeader("Location", "/");
    server.send(303);
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

    logToBoth("[Setup] Configuring WiFi Hotspot...");
    WiFi.softAP(ssid, password);
    logToBoth("[Setup] IP: " + WiFi.softAPIP().toString());

    logToBoth("[Setup] Starting Web Server...");
    server.on("/", handleRoot);
    server.on("/toggleMode", handleToggleMode);
    server.on("/setGear", handleSetGear);
    server.on("/invert", handleInvert);
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