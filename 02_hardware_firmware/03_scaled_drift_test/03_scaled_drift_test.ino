// ============================================================================
//  03_scaled_drift_test.ino  —  One-Way Sync with Scaled Drift Telemetry
// ============================================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

struct SyncPacket {
    uint8_t  sender_id;
    uint32_t master_us;  // Master's current virtual clock value
    bool     sync_active; // Is sync currently enabled?
} __attribute__((packed));

// Global variables
int  myId = THIS_NODE_ID;
bool isLeader = (myId == 0);

uint32_t lastReportTime = 0;
uint32_t bootTime_us = 0;

// Clock values
int32_t  clockOffset_us = 0; // The sync offset correction
float    artDriftFactor = 0;

// Toggle state (Node 0 controls this)
bool syncActiveGlobal = true;

// Peer tracking for monitor
uint32_t peerVirtualTime[NODE_COUNT] = {0};
uint32_t peerRecvMs[NODE_COUNT] = {0};

// Get the local synchronized virtual clock time (corrected and artificially drifted)
inline uint32_t getLocalVirtualClock() {
    uint32_t elapsed = micros() - bootTime_us;
    // Apply the artificial drift multiplier
    uint32_t drifted_time = bootTime_us + (uint32_t)(elapsed * artDriftFactor);
    // Apply the correction offset
    return drifted_time - clockOffset_us;
}

// Volatile debug variables for slave diagnostics
volatile uint32_t debugLastRecvMs = 0;
volatile int32_t  debugLastRawOffset = 0;
volatile uint32_t debugLastMasterUs = 0;
volatile int      debugPktCount = 0;

// ESP-NOW Receive Callback
void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len == sizeof(SyncPacket)) {
        SyncPacket pkt;
        memcpy(&pkt, data, sizeof(SyncPacket));
        
        if (pkt.sender_id == 0) {
            syncActiveGlobal = pkt.sync_active;
            
            if (syncActiveGlobal) {
                // Adjust our offset to align our virtual clock with the master's virtual clock
                uint32_t localVirtualTime = getLocalVirtualClock();
                int32_t raw_offset = (int32_t)(localVirtualTime - pkt.master_us - 1200);
                
                // If offset is very large (>100ms, like on bootup), perform a hard sync instantly.
                // Otherwise, use a soft step (half steps) to filter network jitter.
                if (abs(raw_offset) > 100000) {
                    clockOffset_us += raw_offset;
                } else {
                    clockOffset_us += raw_offset * 0.5f;
                }
                
                debugLastRawOffset = raw_offset;
                debugLastMasterUs = pkt.master_us;
                debugLastRecvMs = millis();
                debugPktCount++;
            }
        }

        // Store peer info for Node 0 print table
        if (pkt.sender_id < NODE_COUNT) {
            peerVirtualTime[pkt.sender_id] = pkt.master_us;
            peerRecvMs[pkt.sender_id] = millis();
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    bootTime_us = micros();
    
    // Calculate local artificial drift multiplier: (1.0 + PPM/1,000,000)
    artDriftFactor = 1.0f + (ARTIFICIAL_DRIFT_PPM[myId] / 1000000.0f);
    
    Serial.printf("\n\n=== Scaled Drift Test Booting Node #%d ===\n", myId);
    Serial.printf("  Artificial Drift: %.1f PPM (Multiplier: %.6f)\n", 
                  ARTIFICIAL_DRIFT_PPM[myId], artDriftFactor);

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

    // Node 0 listens to Serial commands to toggle sync state
    if (isLeader) {
        Serial.println("👉 Type '1' in Serial Monitor to enable Sync, '0' to disable.");
    }
    Serial.println("✅ Setup Complete. Running Swarm Clock Sync Loop...");
}

void loop() {
    uint32_t now = millis();

    // Node 0 checks if the user sent a serial toggle command
    if (isLeader && Serial.available() > 0) {
        char ch = Serial.read();
        if (ch == '1') {
            syncActiveGlobal = true;
            Serial.println("\n🟢 COMMAND: SYNCHRONIZATION ENABLED");
        } else if (ch == '0') {
            syncActiveGlobal = false;
            Serial.println("\n🔴 COMMAND: SYNCHRONIZATION DISABLED");
        }
    }

    // LEADER (Node 0): Broadcasts its virtual clock time
    if (isLeader && (now - lastReportTime >= REPORT_INTERVAL_MS)) {
        lastReportTime = now;

        SyncPacket pkt;
        pkt.sender_id = myId;
        pkt.master_us = getLocalVirtualClock();
        pkt.sync_active = syncActiveGlobal;

        // Broadcast to all slaves
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
                uint32_t estimated_peer_us = peerVirtualTime[i] + (time_since_rx_ms * 1000);
                int32_t delta = (int32_t)(getLocalVirtualClock() - estimated_peer_us);
                
                if (!first) Serial.print(",");
                first = false;
                Serial.printf("{\"id\":%d,\"alive\":true,\"synced\":%s,\"offset\":%d}", 
                              i, (abs(delta) < 5000 && syncActiveGlobal) ? "true" : "false", delta);
            }
        }
        Serial.println("]}");

        // Print the live swarm monitor table
        int32_t total_abs_error = 0;
        int active_slaves = 0;

        for (int i = 1; i < NODE_COUNT; i++) {
            if (now - peerRecvMs[i] < 2000) {
                uint32_t time_since_rx_ms = now - peerRecvMs[i];
                uint32_t estimated_peer_us = peerVirtualTime[i] + (time_since_rx_ms * 1000);
                int32_t delta = (int32_t)(getLocalVirtualClock() - estimated_peer_us);
                total_abs_error += abs(delta);
                active_slaves++;
            }
        }

        float avg_error_ms = 0.0f;
        if (active_slaves > 0) {
            avg_error_ms = (float)total_abs_error / active_slaves / 1000.0f; // convert to ms
        }

        Serial.println("==================================================");
        Serial.printf("⏰ Swarm Sync Monitor (Sync Status: %s)\n", syncActiveGlobal ? "ACTIVE (Synced)" : "DISABLED (Drifting)");
        Serial.printf("   Master Time: %u us | Avg Swarm Error: %.3f ms\n", getLocalVirtualClock(), avg_error_ms);
        Serial.println("==================================================");
        
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == 0) {
                Serial.printf("  [Node 0] Master   : %u us\n", getLocalVirtualClock());
            } else {
                if (now - peerRecvMs[i] < 2000) {
                    uint32_t time_since_rx_ms = now - peerRecvMs[i];
                    uint32_t estimated_peer_us = peerVirtualTime[i] + (time_since_rx_ms * 1000);
                    int32_t delta = (int32_t)(getLocalVirtualClock() - estimated_peer_us);
                    Serial.printf("  [Node %d] Virtual: %10u us | Sync Offset Error: %6d us\n", 
                                  i, estimated_peer_us, delta);
                } else {
                    Serial.printf("  [Node %d] OFFLINE\n", i);
                }
            }
        }
        Serial.println("==================================================\n");
    }

    // SLAVES (Nodes 1, 2, 3): Send their local virtual clock time back to the master
    if (!isLeader && (now - lastReportTime >= REPORT_INTERVAL_MS)) {
        lastReportTime = now;

        SyncPacket pkt;
        pkt.sender_id = myId;
        pkt.master_us = getLocalVirtualClock();
        pkt.sync_active = syncActiveGlobal;

        // Send to Node 0
        esp_now_send(NODE_MACS[0], (uint8_t*)&pkt, sizeof(pkt));
    }

    // Diagnostics for SLAVES: print when sync packets are received
    static int lastDebugPktCount = 0;
    if (!isLeader && debugPktCount != lastDebugPktCount) {
        lastDebugPktCount = debugPktCount;
        Serial.printf("[SLAVE DEBUG] Recv packet #%d from Node 0 | Master Time: %u us | Raw Offset: %d us | My Offset: %d us\n",
                      debugPktCount, debugLastMasterUs, debugLastRawOffset, clockOffset_us);
    }
}
