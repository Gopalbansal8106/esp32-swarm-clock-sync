// ============================================================
//  jammer_firmware.ino  —  ESP32 Interference / Packet Loss Demo Node
//
//  This ESP32 floods the 2.4 GHz channel with ESP-NOW broadcast
//  packets to simulate a real-world interference environment.
//  Move it CLOSER to the sync nodes to increase packet loss.
//  Press BOOT button to cycle through 3 interference levels:
//    Level 0 = OFF (idle)
//    Level 1 = LOW  (1 pkt/100ms)
//    Level 2 = HIGH (1 pkt/10ms — aggressive flooding)
//
//  No MAC address config needed — broadcasts to ff:ff:ff:ff:ff:ff
//  Wire: GPIO 27 → 220Ω → Red LED → GND
// ============================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

#define LED_PIN    27
#define BUTTON_PIN  0   // BOOT button, active LOW
#define DEBOUNCE_MS 200

// Jammer payload (random noise — sync nodes will discard invalid packets)
struct JamPayload {
    uint8_t noise[10];
};

uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
volatile bool buttonPressed = false;
uint32_t lastButtonMs  = 0;
uint32_t lastJamTime   = 0;
uint32_t lastBlinkTime = 0;
bool     blinkState    = false;
uint32_t pktsJammed    = 0;

// 0 = OFF, 1 = LOW, 2 = HIGH
uint8_t jamLevel = 0;

// Interval between jammer packets (ms) per level
const uint32_t JAM_INTERVALS[] = {0, 100, 10};
const char*    JAM_LABELS[]    = {"OFF", "LOW", "HIGH"};

void IRAM_ATTR onButtonPress() { buttonPressed = true; }

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== JAMMER NODE ===");

    pinMode(LED_PIN,    OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(BUTTON_PIN, onButtonPress, FALLING);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.print("Jammer MAC: "); Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed");
        while (1) { delay(300); digitalWrite(LED_PIN, !digitalRead(LED_PIN)); }
    }

    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.printf("Jammer ready. Level: %s\nPress BOOT to cycle levels.\n", JAM_LABELS[jamLevel]);
}

void loop() {
    uint32_t now = millis();

    // Handle button
    if (buttonPressed) {
        buttonPressed = false;
        if (now - lastButtonMs >= DEBOUNCE_MS) {
            lastButtonMs = now;
            jamLevel = (jamLevel + 1) % 3;
            pktsJammed = 0;
            Serial.printf("[BTN] Jam level: %s\n", JAM_LABELS[jamLevel]);
        }
    }

    // Send jam packets
    if (jamLevel > 0 && (now - lastJamTime >= JAM_INTERVALS[jamLevel])) {
        lastJamTime = now;
        JamPayload pkt;
        for (int i = 0; i < 10; i++) pkt.noise[i] = random(0, 255);
        esp_now_send(broadcastAddr, (uint8_t*)&pkt, sizeof(pkt));
        pktsJammed++;

        if (pktsJammed % 50 == 0) {
            Serial.printf("[JAM] Level=%s  Packets sent: %u\n", JAM_LABELS[jamLevel], pktsJammed);
        }
    }

    // LED blink rate reflects jam level
    uint32_t blinkRate = (jamLevel == 0) ? 1000 : (jamLevel == 1) ? 300 : 80;
    if (now - lastBlinkTime >= blinkRate) {
        lastBlinkTime = now;
        blinkState    = !blinkState;
        digitalWrite(LED_PIN, jamLevel == 0 ? LOW : blinkState);
    }
}
