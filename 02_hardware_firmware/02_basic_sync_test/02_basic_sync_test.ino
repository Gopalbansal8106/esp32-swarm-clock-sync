// ============================================================================
//  02_basic_sync_test.ino  —  ESP-NOW One-Way Sync Test
// ============================================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

struct SyncPacket {
    uint8_t  sender_id;  // ID of the sender
    uint32_t master_us;  // Master's current raw clock value
} __attribute__((packed));

// Global variables
int myId = THIS_NODE_ID;
bool isLeader = (myId == 0); // Node 0 is the fixed master in this test

uint32_t lastReportTime = 0;
int32_t  clockOffset_us = 0; // The offset adjustment applied to micros()
uint32_t lastRecvMs = 0;

// Table to store the last reported time of each node for monitor print
uint32_t peerCorrectedTime[NODE_COUNT] = {0};
uint32_t peerRecvMs[NODE_COUNT] = {0};

// Get the local synchronized virtual clock time (corrected)
inline uint32_t getLocalTime() {
    return (uint32_t)((int64_t)micros() - clockOffset_us);
}

// ESP-NOW Receive Callback
void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len == sizeof(SyncPacket)) {
        SyncPacket pkt;
        memcpy(&pkt, data, sizeof(SyncPacket));
        
        uint32_t rxTime_us = micros(); // Capture local arrival time
        
        if (pkt.sender_id == 0) {
            // SLAVE ACTION: Update local offset using the master time
            // offset = local_arrival - master_sent_time - baseline_latency (1200 us)
            int32_t raw_offset = (int32_t)(rxTime_us - clockOffset_us - pkt.master_us - 1200);
            
            // If offset is very large (>100ms), perform a hard sync instantly.
            // Otherwise, adjust offset gradually (50% step) to smooth network jitter.
            if (abs(raw_offset) > 100000) {
                clockOffset_us += raw_offset;
            } else {
                clockOffset_us += raw_offset * 0.5f;
            }
            lastRecvMs = millis();
        }

        // Keep track of node metrics on the master (for monitor printing)
        if (pkt.sender_id < NODE_COUNT) {
            peerCorrectedTime[pkt.sender_id] = pkt.master_us; 
            peerRecvMs[pkt.sender_id] = millis();
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.printf("\n\n=== One-Way Sync Test Booting Node #%d ===\n", myId);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed!");
        while (1) delay(100);
    }
    esp_now_register_recv_cb(onReceive);

    // Register peers
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, NODE_MACS[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    Serial.println("✅ Setup Complete. Synchronization Active.");
}

void loop() {
    uint32_t now = millis();

    // LEADER (Node 0): Periodically broadcasts the master clock time
    if (isLeader && (now - lastReportTime >= REPORT_INTERVAL_MS)) {
        lastReportTime = now;

        SyncPacket pkt;
        pkt.sender_id = myId;
        pkt.master_us = getLocalTime();

        // Broadcast to all slave nodes
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == myId) continue;
            esp_now_send(NODE_MACS[i], (uint8_t*)&pkt, sizeof(pkt));
        }

        // Print JSON data for the serial bridge
        Serial.print("{\"node_id\":0,\"is_leader\":true,\"sync_mode\":0,\"offset\":0,\"drift\":0.0,\"packets_sent\":0,\"packets_lost\":0,\"peers\":[");
        bool first = true;
        for (int i = 1; i < NODE_COUNT; i++) {
            if (now - peerRecvMs[i] < 2000) {
                uint32_t time_since_rx_ms = now - peerRecvMs[i];
                uint32_t estimated_peer_us = peerCorrectedTime[i] + (time_since_rx_ms * 1000);
                int32_t delta = (int32_t)(getLocalTime() - estimated_peer_us);
                
                if (!first) Serial.print(",");
                first = false;
                Serial.printf("{\"id\":%d,\"alive\":true,\"synced\":%s,\"offset\":%d}", 
                              i, (abs(delta) < 5000) ? "true" : "false", delta);
            }
        }
        Serial.println("]}");

        // Print the active telemetry table on Node 0
        Serial.println("==================================================");
        Serial.printf("⏰ Sync Monitor (Node 0 Master Time: %u us)\n", getLocalTime());
        Serial.println("==================================================");
        
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == 0) {
                Serial.printf("  [Node 0] Master   : %u us\n", getLocalTime());
            } else {
                if (now - peerRecvMs[i] < 2000) {
                    uint32_t time_since_rx_ms = now - peerRecvMs[i];
                    uint32_t estimated_peer_us = peerCorrectedTime[i] + (time_since_rx_ms * 1000);
                    
                    int32_t delta = (int32_t)(getLocalTime() - estimated_peer_us);
                    Serial.printf("  [Node %d] Corrected: %10u us | Sync Offset Error: %6d us\n", 
                                  i, estimated_peer_us, delta);
                } else {
                    Serial.printf("  [Node %d] OFFLINE\n", i);
                }
            }
        }
        Serial.println("==================================================\n");
    }

    // SLAVES (Nodes 1, 2, 3): Send their local corrected time back to the master so it can monitor them
    if (!isLeader && (now - lastReportTime >= REPORT_INTERVAL_MS)) {
        lastReportTime = now;

        SyncPacket pkt;
        pkt.sender_id = myId;
        pkt.master_us = getLocalTime();

        // Send to Node 0
        esp_now_send(NODE_MACS[0], (uint8_t*)&pkt, sizeof(pkt));
    }
}
