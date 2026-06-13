// ============================================================================
//  04_twoway_kalman_test.ino  —  Two-Way Sync (TPSN) with Kalman Filter
// ============================================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"
#include "../node_firmware/kalman.h"  // Reuse the Kalman filter logic we defined earlier

#define PKT_SYNC_REQ  0x02
#define PKT_SYNC_RESP 0x03

struct SyncPacket {
    uint8_t  pkt_type;
    uint8_t  sender_id;
    uint32_t t1_us;
    uint32_t t2_us;
    uint32_t t3_us;
    int32_t  offset_us;
    float    drift_ppm;
} __attribute__((packed));

// Global variables
int  myId = THIS_NODE_ID;
bool isLeader = (myId == 0);

uint32_t lastReportTime = 0;
uint32_t lastSyncSendTime = 0;
uint32_t bootTime_us = 0;

// Clock values
int32_t  clockOffset_us = 0;
float    artDriftFactor = 0;
double   driftPPM = 0;

// Kalman filter instance
KalmanClock kalman(0.5f, 0.05f, 80.0f);

// Two-way handshake state (Slaves only)
uint32_t pending_T1 = 0;
bool     waitingForResp = false;

// Peer monitor (Node 0 only)
int32_t  peerOffset_us[NODE_COUNT] = {0};
float    peerDrift_ppm[NODE_COUNT] = {0.0f};
uint32_t peerRecvMs[NODE_COUNT] = {0};

// Get local synchronized virtual clock time
inline uint32_t getLocalVirtualClock() {
    uint32_t elapsed = micros() - bootTime_us;
    uint32_t drifted_time = bootTime_us + (uint32_t)(elapsed * artDriftFactor);
    return drifted_time - clockOffset_us;
}

// ESP-NOW Receive Callback
void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    uint32_t rxTimestamp = micros(); // Capture T2 or T4 immediately
    
    if (len == sizeof(SyncPacket)) {
        SyncPacket pkt;
        memcpy(&pkt, data, sizeof(SyncPacket));

        // 1. LEADER ACTION: Process request and send response back
        if (pkt.pkt_type == PKT_SYNC_REQ && isLeader) {
            // Apply drift to captured timestamp to get virtual T2
            uint32_t T2 = bootTime_us + (uint32_t)((rxTimestamp - bootTime_us) * artDriftFactor);
            uint32_t T3 = getLocalVirtualClock(); // T3 is responder send time

            SyncPacket resp;
            resp.pkt_type  = PKT_SYNC_RESP;
            resp.sender_id = myId;
            resp.t1_us     = pkt.t1_us;  // Echo back their T1
            resp.t2_us     = T2;
            resp.t3_us     = T3;
            resp.drift_ppm = 0.0f;
            esp_now_send(NODE_MACS[pkt.sender_id], (uint8_t*)&resp, sizeof(resp));
        }

        // 2. SLAVE ACTION: Process response and run Kalman Filter update
        else if (pkt.pkt_type == PKT_SYNC_RESP && !isLeader && pkt.sender_id == 0) {
            if (!waitingForResp) return;
            waitingForResp = false;

            uint32_t T1 = pkt.t1_us;
            uint32_t T2 = pkt.t2_us;
            uint32_t T3 = pkt.t3_us;
            // Capture T4 in RAW time scale (with artificial drift, but without clockOffset_us)
            uint32_t T4 = bootTime_us + (uint32_t)((rxTimestamp - bootTime_us) * artDriftFactor);

            // Two-Way offset TPSN calculation (raw offset between raw clock and master virtual clock)
            int32_t offset_raw = ((int32_t)(T4 - T3) - (int32_t)(T2 - T1)) / 2;
            uint32_t rtt = (T4 - T1) - (T3 - T2);

            // If offset is very large (>100ms, like on boot), perform a hard sync instantly.
            // Otherwise, update the Kalman filter tracking the absolute offset.
            if (abs(offset_raw - clockOffset_us) > 100000) {
                clockOffset_us = offset_raw;
                kalman.reset();
                driftPPM = 0.0f;
            } else {
                double kalman_offset = kalman.update(offset_raw, rtt);
                clockOffset_us = (int32_t)kalman_offset;
                driftPPM = kalman.driftPPM();
            }

            // Send telemetry report packet back to master for display monitor
            // Report the remaining error (offset_raw - clockOffset_us) so the dashboard sees the actual offset error
            SyncPacket telemetry;
            telemetry.pkt_type  = PKT_SYNC_RESP;
            telemetry.sender_id = myId;
            telemetry.offset_us = offset_raw - clockOffset_us; 
            telemetry.drift_ppm = driftPPM;
            esp_now_send(NODE_MACS[0], (uint8_t*)&telemetry, sizeof(telemetry));

            // Debug print for Slave console
            Serial.printf("[SLAVE DEBUG] RawOffset: %d us | RTT: %u us | FilteredOffset: %d us | Drift: %.2f PPM\n",
                          offset_raw, rtt, clockOffset_us, driftPPM);
        }

        // 3. MONITOR ACTION: Leader receives telemetry from slave
        else if (pkt.pkt_type == PKT_SYNC_RESP && isLeader && pkt.sender_id > 0) {
            if (pkt.sender_id < NODE_COUNT) {
                peerOffset_us[pkt.sender_id] = pkt.offset_us;
                peerDrift_ppm[pkt.sender_id] = pkt.drift_ppm;
                peerRecvMs[pkt.sender_id] = millis();
            }
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    bootTime_us = micros();
    artDriftFactor = 1.0f + (ARTIFICIAL_DRIFT_PPM[myId] / 1000000.0f);
    
    Serial.printf("\n\n=== Two-Way Sync + Kalman Booting Node #%d ===\n", myId);
    Serial.printf("  Drift Setup: %.1f PPM\n", ARTIFICIAL_DRIFT_PPM[myId]);

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

    Serial.println("✅ Setup Complete. Running Two-Way Sync Handshakes...");
}

void loop() {
    uint32_t now = millis();

    // LEADER (Node 0): Print telemetry report console
    if (isLeader && (now - lastReportTime >= REPORT_INTERVAL_MS)) {
        lastReportTime = now;

        int32_t total_abs_error = 0;
        int active_slaves = 0;

        for (int i = 1; i < NODE_COUNT; i++) {
            if (now - peerRecvMs[i] < 2000) {
                total_abs_error += abs(peerOffset_us[i]);
                active_slaves++;
            }
        }

        float avg_error_ms = 0.0f;
        if (active_slaves > 0) {
            avg_error_ms = (float)total_abs_error / active_slaves / 1000.0f;
        }

        // Print JSON data for the serial bridge/dashboard (sync_mode = 1: Two-Way)
        Serial.print("{\"node_id\":0,\"is_leader\":true,\"sync_mode\":1,\"offset\":0,\"drift\":0.0,\"packets_sent\":0,\"packets_lost\":0,\"peers\":[");
        bool first = true;
        for (int i = 1; i < NODE_COUNT; i++) {
            if (now - peerRecvMs[i] < 2000) {
                if (!first) Serial.print(",");
                first = false;
                Serial.printf("{\"id\":%d,\"alive\":true,\"synced\":%s,\"offset\":%d,\"drift\":%.2f}", 
                              i, (abs(peerOffset_us[i]) < 800) ? "true" : "false", peerOffset_us[i], peerDrift_ppm[i]);
            }
        }
        Serial.println("]}");

        Serial.println("==================================================");
        Serial.printf("⏰ Swarm Two-Way Sync Monitor (Kalman Filter ON)\n");
        Serial.printf("   Master Time: %u us | Avg Swarm Error: %.3f ms\n", getLocalVirtualClock(), avg_error_ms);
        Serial.println("==================================================");
        
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == 0) {
                Serial.printf("  [Node 0] Master   : %u us\n", getLocalVirtualClock());
            } else {
                if (now - peerRecvMs[i] < 2000) {
                    Serial.printf("  [Node %d] Estimated Offset: %8d us | Drift: %.1f PPM\n", 
                                  i, peerOffset_us[i], peerDrift_ppm[i]);
                } else {
                    Serial.printf("  [Node %d] OFFLINE\n", i);
                }
            }
        }
        Serial.println("==================================================\n");
    }

    // SLAVES (Nodes 1, 2, 3): Send two-way sync request to leader
    if (!isLeader && (now - lastSyncSendTime >= SYNC_INTERVAL_MS)) {
        if (!waitingForResp) {
            lastSyncSendTime = now;

            SyncPacket req;
            req.pkt_type  = PKT_SYNC_REQ;
            req.sender_id = myId;
            // Send T1 in RAW time scale (with artificial drift, but without clockOffset_us)
            req.t1_us     = bootTime_us + (uint32_t)((micros() - bootTime_us) * artDriftFactor);
            pending_T1   = req.t1_us;
            waitingForResp = true;

            esp_now_send(NODE_MACS[0], (uint8_t*)&req, sizeof(req));
        } else if (now - lastSyncSendTime > 1000) {
            // Timeout handling
            waitingForResp = false;
        }
    }
}
