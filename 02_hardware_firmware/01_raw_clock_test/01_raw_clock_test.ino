// ============================================================================
//  01_raw_clock_test.ino  —  ESP-NOW Raw Clock Telemetry Test
// ============================================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

struct ClockPacket {
    uint8_t  sender_id;  // 0, 1, 2, or 3
    uint32_t sender_us;  // Current raw micros() value of the sender
} __attribute__((packed));

// Global variables
int myId = THIS_NODE_ID;
uint32_t lastReportTime = 0;

// Table to store the last received clock time of each peer
uint32_t peerClocks[NODE_COUNT] = {0};
uint32_t peerRecvMs[NODE_COUNT] = {0}; // Local millis() when we last heard from them

// ESP-NOW Receive Callback
void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len == sizeof(ClockPacket)) {
        ClockPacket pkt;
        memcpy(&pkt, data, sizeof(ClockPacket));
        
        if (pkt.sender_id < NODE_COUNT) {
            peerClocks[pkt.sender_id] = pkt.sender_us;
            peerRecvMs[pkt.sender_id] = millis();
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.printf("\n\n=== Raw Clock Test Booting Node #%d ===\n", myId);

    // WiFi in Station Mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // ESP-NOW Init
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed!");
        while (1) delay(100);
    }
    esp_now_register_recv_cb(onReceive);

    // Add all nodes as peers
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, NODE_MACS[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    Serial.println("✅ Setup Complete. Streaming Raw Clock Telemetry...");
}

void loop() {
    uint32_t now = millis();

    // Every 500ms, broadcast our current micros() value to the swarm
    if (now - lastReportTime >= REPORT_INTERVAL_MS) {
        lastReportTime = now;

        ClockPacket pkt;
        pkt.sender_id = myId;
        pkt.sender_us = micros();

        // Broadcast to all peers
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == myId) continue;
            esp_now_send(NODE_MACS[i], (uint8_t*)&pkt, sizeof(pkt));
        }

        // ONLY Node 0 prints the table to the PC Serial Monitor
        if (myId == 0) {
            Serial.println("==================================================");
            Serial.printf("⏰ Telemetry Report (Node 0 Local Time: %u us)\n", micros());
            Serial.println("==================================================");
            
            for (int i = 0; i < NODE_COUNT; i++) {
                if (i == 0) {
                    Serial.printf("  [Node 0] (Self) : %u us\n", micros());
                } else {
                    // Check if peer is active (heard in the last 2 seconds)
                    if (now - peerRecvMs[i] < 2000) {
                        // Estimate what their current micros() is now based on elapsed local time
                        uint32_t time_since_rx_ms = now - peerRecvMs[i];
                        uint32_t estimated_peer_us = peerClocks[i] + (time_since_rx_ms * 1000);
                        
                        int32_t delta = (int32_t)(micros() - estimated_peer_us);
                        Serial.printf("  [Node %d] Raw: %10u us | Delta from Node 0: %10d us\n", 
                                      i, estimated_peer_us, delta);
                    } else {
                        Serial.printf("  [Node %d] OFFLINE\n", i);
                    }
                }
            }
            Serial.println("==================================================\n");
        }
    }
}
