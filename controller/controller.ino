#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>

// Motor Pins
const unsigned int IN1 = 14;
const unsigned int IN2 = 48;
const unsigned int EN_5V = 21;

int currentGear = 0;
int lastGear = -1;  // Hilfsvariable, um Spam zu vermeiden
bool forward = true;
bool deviceFound = false;  // Umbenannt von 'connected' für mehr Klarheit

#define bleServerName "TrainSense Server"
BLEUUID serviceUUID("181A");
BLEUUID gearUUID("2A56");
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
BLEAddress* pServerAddress = nullptr;

// Callback: Wird aufgerufen, wenn neue Daten vom Server gepusht werden
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length,
                           bool isNotify) {
    currentGear = pData[0];
    Serial.print("[BLE] Update empfangen! Neuer Gang: ");
    Serial.println(currentGear);
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("[Scan] Gerät gefunden: ");
        Serial.println(advertisedDevice.getName().c_str());

        if (advertisedDevice.getName() == bleServerName) {
            Serial.println("[Scan] Treffer! TrainSense Server gefunden.");
            advertisedDevice.getScan()->stop();
            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            deviceFound = true;
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);  // Kurz warten, damit der Serial Monitor bereit ist
    Serial.println("--- TrainSense Client Start ---");

    pinMode(EN_5V, OUTPUT);
    digitalWrite(EN_5V, HIGH);
    Serial.println("[Setup] 5V Ausgang aktiviert.");

    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);

    BLEDevice::init("TrainSense Client");
    Serial.println("[BLE] Initialisierung abgeschlossen. Starte Scan...");

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(30);
}

bool connectToServer() {
    Serial.print("[Connect] Versuche Verbindung zu: ");
    Serial.println(pServerAddress->toString().c_str());

    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(*pServerAddress)) {
        Serial.println("[Fehler] Verbindung zum Server fehlgeschlagen.");
        return false;
    }
    Serial.println("[Connect] Mit Server verbunden.");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("[Fehler] Service UUID (181A) nicht gefunden.");
        return false;
    }
    Serial.println("[Connect] Service gefunden.");

    pRemoteCharacteristic = pRemoteService->getCharacteristic(gearUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("[Fehler] Charakteristik (2A56) nicht gefunden.");
        return false;
    }
    Serial.println("[Connect] Charakteristik gefunden.");

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        Serial.println("[Connect] Notifications erfolgreich abonniert.");
    }

    return true;
}

int gearToAnalog(int gear) {
    if (gear == 1) return 128;
    if (gear == 2) return 192;
    if (gear == 3) return 255;
    return 0;
}

void loop() {
    // Verbindungsaufbau falls Gerät gefunden wurde
    if (deviceFound && pRemoteCharacteristic == nullptr) {
        if (connectToServer()) {
            Serial.println("[Status] System bereit. Warte auf Daten...");
        } else {
            Serial.println("[Status] Reconnect in 5 Sekunden...");
            delay(5000);
        }
    }

    // Motorsteuerung & Status-Meldung nur bei Änderung
    int speed = gearToAnalog(currentGear);

    if (currentGear != lastGear) {
        Serial.print("[Motor] Modus: ");
        Serial.print(forward ? "Vorwärts" : "Rückwärts");
        Serial.print(" | Gang: ");
        Serial.print(currentGear);
        Serial.print(" | PWM: ");
        Serial.println(speed);
        lastGear = currentGear;  // Merken, damit wir nicht doppelt loggen
    }

    if (forward) {
        analogWrite(IN1, speed);
        digitalWrite(IN2, LOW);
    } else {
        analogWrite(IN2, speed);
        digitalWrite(IN1, LOW);
    }

    delay(100);
}