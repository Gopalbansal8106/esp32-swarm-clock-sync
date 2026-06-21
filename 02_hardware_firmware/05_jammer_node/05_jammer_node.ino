#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Built-in BOOT button on almost all ESP32 dev boards
#define BOOT_BUTTON_PIN 0 

// Jamming intensity levels
enum JamLevel {
    JAM_OFF = 0,
    JAM_LIGHT = 1,
    JAM_HEAVY = 2,
    JAM_FULL = 3
};

JamLevel currentLevel = JAM_OFF;
uint32_t lastButtonPress = 0;

// Universal broadcast MAC address
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Maximum ESP-NOW packet size is 250 bytes. We send massive packets to maximize airtime/congestion.
uint8_t garbageData[250]; 

void printStatus() {
    Serial.print("📡 Current Jamming Intensity: ");
    switch(currentLevel) {
        case JAM_OFF: Serial.println("0% (OFF) - Swarm is breathing normally"); break;
        case JAM_LIGHT: Serial.println("33% (LIGHT) - Mild latency jitter injected"); break;
        case JAM_HEAVY: Serial.println("66% (HEAVY) - Severe CSMA/CA congestion"); break;
        case JAM_FULL: Serial.println("100% (FULL THROTTLE) - Maximum channel destruction!"); break;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Initialize Wi-Fi in Station mode on Channel 13 (Must exactly match the Swarm!)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Force ESP32 to Wi-Fi Channel 13 natively
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(13, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(78); // 78 = 19.5 dBm (ABSOLUTE MAXIMUM HARDWARE TX POWER)
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error initializing ESP-NOW");
        return;
    }

    // Register universal broadcast peer
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = 13;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Failed to add broadcast peer");
        return;
    }

    // Pre-fill garbage array with random radio noise
    for (int i = 0; i < 250; i++) {
        garbageData[i] = random(0, 256);
    }

    Serial.println("\n=============================================");
    Serial.println("   💀 ESP32 Swarm Jammer Node Ready 💀");
    Serial.println("=============================================");
    Serial.println("Press the BOOT button (GPIO 0) on the ESP32 to cycle through jamming levels.");
    Serial.println();
    printStatus();
}

void handleButton() {
    // Read the physical BOOT button
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (millis() - lastButtonPress > 300) { // 300ms mechanical debounce
            lastButtonPress = millis();
            
            // Cycle to the next level
            int next = (int)currentLevel + 1;
            if (next > 3) next = 0;
            currentLevel = (JamLevel)next;
            
            printStatus();
        }
    }
}

void loop() {
    handleButton();

    if (currentLevel != JAM_OFF) {
        // Send a massive 250-byte garbage packet into the air to collide with swarm packets
        esp_err_t result = esp_now_send(broadcastMac, garbageData, sizeof(garbageData));
        
        // Control the physical radio intensity by adjusting the resting time between blasts
        if (currentLevel == JAM_LIGHT) {
            delay(50); // Small rest allows swarm to successfully sneak some packets through
        } else if (currentLevel == JAM_HEAVY) {
            delay(10); // Very tiny rest, causing severe traffic jams and moderate packet loss
        } else if (currentLevel == JAM_FULL) {
            // MAX POWER: Burst 10 packets instantly into the hardware queue!
            // This ensures the Wi-Fi antenna is 100% saturated and NEVER goes idle.
            for(int i=0; i<10; i++) {
                esp_now_send(broadcastMac, garbageData, sizeof(garbageData));
            }
            // 1ms breath to let the hardware process the massive queue and prevent a crash
            delay(1); 
        }
    } else {
        // If OFF, just rest and wait for button presses
        delay(50);
    }
}
