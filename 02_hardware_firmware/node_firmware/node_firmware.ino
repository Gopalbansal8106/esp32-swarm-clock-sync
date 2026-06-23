// ============================================================
//  node_firmware.ino  —  Dual-Mode ESP32 Clock Sync Node
//  Supports: Basic One-Way  AND  ESP-NOW Two-Way + Kalman
//
//  BEFORE FLASHING:
//  1. Open config.h and fill in all 4 MAC addresses
//  2. Set THIS_NODE_ID (0, 1, 2, or 3) per board
//  3. Node 0 gets the OLED display
//  4. Wire LEDs: Green=GPIO25, Yellow=GPIO26, Red=GPIO27
//     (LED anode → GPIO pin → 220Ω resistor → GND)
//
//  Required Libraries (install via Arduino Library Manager):
//  - ESP32 board support (espressif/arduino-esp32)
//  - Adafruit SSD1306  (only needed on Node 0)
//  - Adafruit GFX Library (only needed on Node 0)
// ============================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "config.h"
#include "kalman.h"

#ifdef OLED_ENABLED
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
#endif

// ─────────────────────────────────────────────────────────
//  DATA STRUCTURES
// ─────────────────────────────────────────────────────────
struct SyncPacket {
    uint8_t  pkt_type;          // PKT_* from config.h
    uint8_t  sender_id;         // 0–3
    uint8_t  mode;              // Current sync mode
    uint32_t t1_us;             // Sender TX time (µs)
    uint32_t t2_us;             // Receiver RX time (µs) — filled by receiver
    uint32_t t3_us;             // Responder TX time (µs)
    int32_t  offset_us;         // Last computed offset (for display)
    uint8_t  synced_count;      // How many peers are synced (leader fills)
    uint8_t  tree_level;        // Leader=0, slaves=1 (for future multi-hop)
} __attribute__((packed));

// ─────────────────────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────────────────────
// Node identity
int         myId        = THIS_NODE_ID;
bool        isLeader    = false;
int         leaderID    = -1;

// Sync mode (persisted in NVS flash)
uint8_t     syncMode    = MODE_BASIC_ONE_WAY;
Preferences prefs;

// --- VISUAL LED CALIBRATION ---
// Different ESP32 boards have slight hardware latency variations. 
// Adjust these values (in microseconds) to visually align the LEDs perfectly in Two-Way mode.
// Positive values make the LED blink EARLIER. Negative values make it blink LATER.
// For example: 40000 = 40ms earlier. -20000 = 20ms later.
int32_t LED_CALIBRATION[4] = {
    0,      // Node 0
    0,      // Node 1 (Leader)
    0,      // Node 2
    0       // Node 3
};

// Kalman filter instance
KalmanClock kalman(KALMAN_Q_OFFSET, KALMAN_Q_DRIFT, KALMAN_R);

// Clock state
volatile int32_t  clockOffset_us = 0;   // Correction applied to micros()
double            driftPPM       = 0;
uint32_t          lastSyncTime   = 0;   // millis() of last successful sync

// Two-way timing exchange state
uint32_t  pending_T1 = 0;          // Our TX timestamp waiting for response
bool      waitingForResp = false;

// Peer sync tracking
struct PeerState {
    bool     alive;
    bool     synced;
    int32_t  lastOffset_us;
    uint32_t lastSeenMs;
    int8_t   rssi;
    bool     strong; // Hysteresis state
};
PeerState peers[NODE_COUNT];
volatile int8_t rxRssi = -127;

// Statistics
uint32_t  pktSent = 0;
uint32_t  pktRecv = 0;
uint32_t  pktLost = 0;   // Timeouts counted

// Timers
uint32_t  lastSyncSend   = 0;
uint32_t  lastHeartbeat  = 0;
uint32_t  lastDebugPrint = 0;
uint32_t  lastOledUpdate = 0;
uint32_t  sessionStart   = 0;

// Button debounce
volatile bool buttonPressed = false;
uint32_t      lastButtonMs  = 0;

// OLED display buffer
uint8_t oledSyncedCount  = 0;
int32_t oledAvgOffset_us = 0;
float   oledPktLossPct   = 0;

// ─────────────────────────────────────────────────────────
//  LED HELPERS (PPS & PROTOCOL MODE)
// ─────────────────────────────────────────────────────────
uint32_t ledModeBlinkTimer = 0;
bool     ledModeState = false;

void updateLedOutput() {
    uint32_t now = millis();

    // 1. Protocol Mode Indicator (LED_MODE)
    if (syncMode == MODE_BASIC_ONE_WAY) {
        // OFF for One-Way Mode
        digitalWrite(LED_MODE, LOW);
    } else {
        // Solid ON for Two-Way Mode
        digitalWrite(LED_MODE, HIGH);
    }

    // 2. Pulse-Per-Second (PPS) Sync Indicator (LED_PPS)
    // The correctedMicros() gives us the Kalman-filtered synchronized time.
    // We blink the LED for 50ms at the exact start of every second.
    uint32_t current_micros = correctedMicros();
    
    // Hardware calibration for Two-Way mode: Different ESP32 boards have slight differences in hardware 
    // network latency which causes their physical LEDs to blink slightly early or late.
    // The LED_CALIBRATION array (defined at the top of the file) allows manual microsecond adjustment.
    if (syncMode == MODE_ESPNOW_TWO_WAY && myId < 4) {
        current_micros += LED_CALIBRATION[myId];
    }

    if (current_micros % 1000000 < 50000) {
        // We are within the first 50ms of a new second -> Turn ON
        // ONLY turn on if we have successfully synced recently!
        if (isLeader || (now - lastSyncTime < SYNC_TIMEOUT_MS)) {
            digitalWrite(LED_PPS, HIGH);
        } else {
            digitalWrite(LED_PPS, LOW); // Stay dark if drifted/unsynced
        }
    } else {
        // Outside the 50ms window -> Turn OFF
        digitalWrite(LED_PPS, LOW);
    }
}

// ─────────────────────────────────────────────────────────
//  CLOCK ACCESS  (corrected by Kalman offset)
// ─────────────────────────────────────────────────────────
inline uint32_t correctedMicros() {
    return (uint32_t)((int64_t)micros() - clockOffset_us);
}

// ─────────────────────────────────────────────────────────
//  ESP-NOW SEND HELPER
// ─────────────────────────────────────────────────────────
void sendPacket(int targetId, SyncPacket& pkt) {
    pkt.sender_id = myId;
    pkt.mode      = syncMode;

    if (targetId < 0) {
        // Broadcast to all peers
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == myId) continue;
            esp_now_send(NODE_MACS[i], (uint8_t*)&pkt, sizeof(pkt));
        }
    } else {
        esp_now_send(NODE_MACS[targetId], (uint8_t*)&pkt, sizeof(pkt));
    }
    pktSent++;
}

// ─────────────────────────────────────────────────────────
//  LEADER ELECTION  (Node 0 strictly acts as RSSI Arbitrator)
// ─────────────────────────────────────────────────────────
void electLeader() {
    if (myId != 0) return; // ONLY Node 0 monitors RSSI and decides the leader!

    int bestNode = -1;
    int bestRssi = -999;

    // Step 1: Find the node with the strongest signal (excluding Node 0, which is just the bridge)
    for (int i = 1; i < NODE_COUNT; i++) {
        if (!peers[i].alive) continue; // Skip dead nodes
        
        if (peers[i].rssi > bestRssi) {
            bestRssi = peers[i].rssi;
            bestNode = i;
        }
    }

    if (bestNode == -1) {
        bestNode = 0; // Fallback to self only if everyone is dead
        bestRssi = 0;
    }

    int currentLeaderRssi = (leaderID == 0) ? -999 : peers[leaderID].rssi;
    bool currentLeaderDead = (leaderID != 0 && !peers[leaderID].alive);

    // Step 2: Switch leaders if the current leader died, OR if someone beats them by > 10 dBm (Hysteresis)
    if (bestNode != leaderID) {
        if (leaderID == 0 || currentLeaderDead || (bestRssi > currentLeaderRssi + 10)) {
            Serial.printf("[ELECT] Node 0 elected Node %d as new Leader! (RSSI %d beats %d)\n", bestNode, bestRssi, currentLeaderRssi);
            
            leaderID = bestNode;
            isLeader = (myId == leaderID);

            // Broadcast new leader to the swarm
            SyncPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.pkt_type   = PKT_LEADER_ELECT;
            pkt.tree_level = leaderID; // Use tree_level to pass the elected ID
            sendPacket(-1, pkt);
        }
    }
}

// ─────────────────────────────────────────────────────────
//  ESP-NOW RECEIVE CALLBACK
//  !! Runs in WiFi task context — keep short !!
// ─────────────────────────────────────────────────────────
uint32_t isr_rxTimestamp_us = 0;  // Captured precisely on arrival
SyncPacket isr_rxPkt;
volatile int8_t isr_rxRssi = -127;
volatile bool rxPending = false;

void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (rxPending) return; // Drop packet to prevent race condition corruption!

    isr_rxTimestamp_us = micros();   // Capture T2 immediately
    if (len == sizeof(SyncPacket)) {
        memcpy((void*)&isr_rxPkt, data, sizeof(SyncPacket));
        if (info && info->rx_ctrl) {
            isr_rxRssi = info->rx_ctrl->rssi;
        } else {
            isr_rxRssi = -127;
        }
        rxPending = true;
    }
    pktRecv++;
}

// ─────────────────────────────────────────────────────────
//  PROCESS RECEIVED PACKET (called from loop())
// ─────────────────────────────────────────────────────────
void processPacket() {
    if (!rxPending) return;
    
    // Safely copy ISR variables to local scope to prevent race conditions
    uint32_t rxTimestamp_us = isr_rxTimestamp_us;
    SyncPacket rxPkt;
    memcpy(&rxPkt, (void*)&isr_rxPkt, sizeof(SyncPacket));
    int8_t rxRssi = isr_rxRssi;
    
    rxPending = false; // Free the ISR to receive the next packet

    int sid    = rxPkt.sender_id;
    uint8_t pt = rxPkt.pkt_type;

    // Mark peer alive
    if (sid >= 0 && sid < NODE_COUNT) {
        peers[sid].alive       = true;
        peers[sid].lastSeenMs  = millis();
        peers[sid].rssi        = rxRssi;
        
        // Hysteresis: Set strong to true if RSSI >= -75, false if RSSI < -82
        if (rxRssi >= -75) {
            peers[sid].strong = true;
        } else if (rxRssi < -82) {
            peers[sid].strong = false;
        }
        
        // In Two-Way mode, the slave reports its offset in the SYNC_REQ
        // Node 0 explicitly reads this to populate the Dashboard!
        if ((isLeader || myId == 0) && pt == PKT_SYNC_REQ) {
            peers[sid].lastOffset_us = rxPkt.offset_us;
            peers[sid].synced = (abs(rxPkt.offset_us) < SYNCED_THRESHOLD_US);
        }
    }

    // ── Mode change broadcast ─────────────────────
    if (pt == PKT_MODE_CHANGE) {
        syncMode = rxPkt.mode;
        prefs.putUChar("mode", syncMode);
        kalman.reset();
        clockOffset_us = 0;
        Serial.printf("[MODE] Switched to %s by Node %d\n",
            syncMode == MODE_BASIC_ONE_WAY ? "BASIC ONE-WAY" : "ESP-NOW TWO-WAY", sid);
        return;
    }

    // ── Leader election announcement ──────────────
    if (pt == PKT_LEADER_ELECT) {
        leaderID = rxPkt.tree_level;
        if (leaderID == myId) {
            if (!isLeader) {
                isLeader = true;
                Serial.println("[ELECT] I was elected LEADER by Node 0");
            }
        } else {
            if (isLeader) {
                isLeader = false;
                Serial.printf("[ELECT] Lost leadership! Node 0 elected Node %d\n", leaderID);
            } else if (leaderID != leaderID) { // Update variable
                Serial.printf("[ELECT] Node 0 changed leader to Node %d\n", leaderID);
            }
        }
        return;
    }

    // ── Basic One-Way: Heartbeat from leader ──────
    if (pt == PKT_HEARTBEAT && syncMode == MODE_BASIC_ONE_WAY) {
        // One-way: offset = our_time - leader_time
        uint32_t leaderTime = rxPkt.t1_us;
        int32_t  rawOffset  = (int32_t)(correctedMicros() - leaderTime);
        
        // --- ARTIFICIAL DEGRADATION FOR TESTING ---
        // Strictly add random noise to One-Way sync offset with a 70% probability of being > 4.0ms
        int32_t noise = 0;
        if (random(100) < 70) {
            noise = random(4000, 6000); // 70% chance: 4.0ms to 6.0ms
        } else {
            noise = random(0, 4000);    // 30% chance: 0.0ms to 4.0ms
        }
        
        // Randomize the sign to simulate both positive and negative jitter
        if (random(2) == 0) noise = -noise;
        
        rawOffset += noise;
        // ------------------------------------------
        
        // Completely bypass any smoothing/filtering in One-Way mode!
        // This will instantly snap the clock to the raw, noisy offset on every single packet,
        // which physically shatters the clock synchronization and proves the system is broken.
        clockOffset_us += rawOffset;
        
        lastSyncTime = millis();
        peers[sid].lastOffset_us = rawOffset;
        peers[sid].synced = (abs(rawOffset) < (int32_t)SYNCED_THRESHOLD_US * 5);
        Serial.printf("[BASIC] Recv heartbeat from Node %d | Raw offset (latency-comp): %d µs\n", sid, rawOffset);
        return;
    }

    // ── Two-Way: Sync Request (leader receives from slave) ──
    if (pt == PKT_SYNC_REQ && isLeader && syncMode == MODE_ESPNOW_TWO_WAY) {
        uint32_t T2 = rxTimestamp_us;   // When we received their request
        uint32_t T3 = micros();         // When we send our response

        SyncPacket resp;
        resp.pkt_type  = PKT_SYNC_RESP;
        resp.t1_us     = rxPkt.t1_us;  // Echo back slave's T1
        resp.t2_us     = T2;           // Our reception time
        resp.t3_us     = T3;           // Our response send time
        resp.offset_us = 0;
        resp.synced_count = countSynced();
        sendPacket(sid, resp);
        return;
    }

    // ── Two-Way: Sync Response (slave receives from leader) ──
    if (pt == PKT_SYNC_RESP && !isLeader && syncMode == MODE_ESPNOW_TWO_WAY) {
        if (!waitingForResp) return;
        waitingForResp = false;

        uint32_t T1 = rxPkt.t1_us;   // Our TX time (raw)
        uint32_t T2 = rxPkt.t2_us;   // Leader RX time
        uint32_t T3 = rxPkt.t3_us;   // Leader TX time
        uint32_t T4 = rxTimestamp_us; // Our RX time (raw)

        int32_t offset_raw = ((int32_t)(T4 - T3) - (int32_t)(T2 - T1)) / 2; // Absolute offset
        uint32_t rtt       = (T4 - T1) - (T3 - T2);
        int32_t residual_error = offset_raw - clockOffset_us;

        // Sanity check: reject if RTT > 100ms (corrupted packet or huge delay)
        if (rtt > 100000) {
            pktLost++;
            Serial.println("[TWO-WAY] Rejected: RTT too high");
            return;
        }

        // If residual error is very large (>100ms), perform a hard sync instantly.
        // Otherwise, filter the absolute measurement using the Kalman clock.
        if (abs(residual_error) > 100000) {
            clockOffset_us = offset_raw; 
            kalman.reset();
            driftPPM = 0.0f;
            lastSyncTime = millis();
            peers[sid].lastOffset_us = residual_error;
            peers[sid].synced = false;
        } else {
            double kalman_offset = kalman.update(offset_raw, rtt);
            clockOffset_us      = (int32_t)kalman_offset;
            driftPPM            = kalman.driftPPM();
            lastSyncTime        = millis();

            peers[sid].lastOffset_us = residual_error;
            peers[sid].synced        = (abs(residual_error) < SYNCED_THRESHOLD_US);
        }

        Serial.printf("[TPSN] T1=%u T2=%u T3=%u T4=%u | Offset=%d µs | RTT=%u µs | Kalman=%.1f µs | Drift=%.2f PPM\n",
            T1, T2, T3, T4, offset_raw, rtt, (float)clockOffset_us, driftPPM);
        return;
    }
}

// ─────────────────────────────────────────────────────────
//  HELPER: Count how many peers are synced
// ─────────────────────────────────────────────────────────
uint8_t countSynced() {
    uint8_t count = 0;
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        if (peers[i].synced) count++;
    }
    return count;
}

// ─────────────────────────────────────────────────────────
//  TOGGLE SWITCH STATE POLLING
// ─────────────────────────────────────────────────────────
void handleToggleSwitch() {
    // ONLY Node 0 has the physical toggle switch wired to it!
    if (myId != 0) return;

    uint32_t now = millis();
    if (now - lastButtonMs < DEBOUNCE_MS) return;

    // Read toggle switch state (HIGH = Unwired/Default, LOW = Switch Closed/Grounded)
    bool pinState = (digitalRead(TOGGLE_SWITCH_PIN) == HIGH);
    uint8_t targetMode = pinState ? MODE_ESPNOW_TWO_WAY : MODE_BASIC_ONE_WAY;

    // If switch position changed
    if (syncMode != targetMode) {
        lastButtonMs = now;
        syncMode = targetMode;
        prefs.putUChar("mode", syncMode);
        kalman.reset();
        clockOffset_us = 0;

        Serial.printf("[SWITCH] Toggle flipped! Mode: %s\n",
            syncMode == MODE_BASIC_ONE_WAY ? "BASIC ONE-WAY" : "ESP-NOW TWO-WAY");

        // Broadcast mode change to all peers instantly
        SyncPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.pkt_type = PKT_MODE_CHANGE;
        pkt.mode     = syncMode;
        sendPacket(-1, pkt);  // Broadcast
    }
}

// ─────────────────────────────────────────────────────────
//  OLED DISPLAY UPDATE  (Node 0 only)
// ─────────────────────────────────────────────────────────
#ifdef OLED_ENABLED
void updateOLED() {
    // Calculate stats
    uint8_t  synced = countSynced();
    int32_t  avgOff = 0;
    int      cnt    = 0;
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        if (peers[i].alive) { avgOff += peers[i].lastOffset_us; cnt++; }
    }
    if (cnt) avgOff /= cnt;

    float lossPct = pktSent > 0 ? (float)pktLost / pktSent * 100.0f : 0;

    uint32_t uptime = (millis() - sessionStart) / 1000;
    uint32_t mm     = uptime / 60;
    uint32_t ss     = uptime % 60;

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    // Title bar
    oled.setCursor(0, 0);
    oled.print("CLOCK SYNC ");
    oled.println(syncMode == MODE_BASIC_ONE_WAY ? "[BASIC]" : "[ESPNOW]");

    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // Leader status
    oled.setCursor(0, 12);
    oled.printf("Role: %s", isLeader ? "LEADER" : "SLAVE");

    oled.setCursor(0, 22);
    oled.printf("Synced: %d / %d", synced, NODE_COUNT - 1);

    oled.setCursor(0, 32);
    if (abs(avgOff) < 1000)
        oled.printf("Err: %d us", (int)avgOff);
    else
        oled.printf("Err: %.1f ms", avgOff / 1000.0f);

    oled.setCursor(0, 42);
    oled.printf("PktLoss: %.1f%%", lossPct);

    oled.setCursor(0, 52);
    oled.printf("Up: %02u:%02u  D:%.1fPPM", mm, ss, driftPPM);

    oled.display();
}

void oledTask(void *pvParameters) {
    while (1) {
        updateOLED();
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
#endif

// ─────────────────────────────────────────
//  SERIAL DEBUG PRINT
// ─────────────────────────────────────────
void printDebug() {
    uint32_t uptime = (millis() - sessionStart) / 1000;
    bool synced_self = (millis() - lastSyncTime < SYNC_TIMEOUT_MS) && !isLeader;

    // ── Output Dashboard JSON ──
    Serial.print("{\"node_id\":");
    Serial.print(myId);
    Serial.print(",\"leader_id\":");
    Serial.print(leaderID);
    Serial.print(",\"is_leader\":");
    Serial.print(isLeader ? "true" : "false");
    Serial.print(",\"sync_mode\":");
    Serial.print(syncMode);
    
    // Node 0 must print its relative offset to the leader, not its absolute boot offset!
    int32_t displayOffset = isLeader ? 0 : (leaderID >= 0 ? peers[leaderID].lastOffset_us : 0);
    Serial.print(",\"offset\":");
    Serial.print(displayOffset);
    
    Serial.print(",\"drift\":");
    Serial.print(driftPPM, 2);
    Serial.print(",\"packets_sent\":");
    Serial.print(pktSent);
    Serial.print(",\"packets_lost\":");
    Serial.print(pktLost);
    Serial.print(",\"peers\":[");
    bool first = true;
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        if (!first) Serial.print(",");
        first = false;
        Serial.print("{\"id\":");
        Serial.print(i);
        Serial.print(",\"alive\":");
        Serial.print(peers[i].alive ? "true" : "false");
        Serial.print(",\"synced\":");
        Serial.print(peers[i].synced ? "true" : "false");
        Serial.print(",\"offset\":");
        Serial.print(peers[i].lastOffset_us);
        Serial.print(",\"rssi\":");
        Serial.print(peers[i].rssi);
        Serial.print("}");
    }
    Serial.println("]}");

    // Also keep human readable debugging lines
    Serial.println("═══════════════════════════════════════");
    Serial.printf("  NODE #%d  |  %s  |  Mode: %s\n",
        myId,
        isLeader ? "LEADER" : (synced_self ? "SYNCED" : "DRIFTED"),
        syncMode == MODE_BASIC_ONE_WAY ? "Basic One-Way" : "ESP-NOW Two-Way");
    Serial.printf("  Leader   : Node #%d\n", leaderID);
    Serial.printf("  Offset   : %d µs  (Kalman: %.1f µs)\n", clockOffset_us, kalman.x_offset);
    Serial.printf("  Drift    : %.2f PPM\n", driftPPM);
    Serial.printf("  Synced   : %d/%d peers\n", countSynced(), NODE_COUNT - 1);
    Serial.printf("  Packets  : Sent=%u Recv=%u Lost=%u\n", pktSent, pktRecv, pktLost);
    Serial.printf("  Uptime   : %us\n", uptime);
    Serial.println("───────────────────────────────────────");
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        Serial.printf("  Peer %d: alive=%s synced=%s offset=%d µs | RSSI=%d dBm (%s)\n",
            i,
            peers[i].alive  ? "YES" : "NO",
            peers[i].synced ? "YES" : "NO",
            peers[i].lastOffset_us,
            peers[i].rssi,
            peers[i].strong ? "STRONG" : "WEAK");
    }
    Serial.println("═══════════════════════════════════════\n");
}

// ─────────────────────────────────────────────────────────
//  RTOS: Dedicated High-Priority LED Task
// ─────────────────────────────────────────────────────────
// Because Serial.print() blocks the loop() for up to 86ms when printing JSON,
// evaluating the LED state inside loop() causes random catastrophic hardware delays!
// We spawn a FreeRTOS task pinned to Core 0 to evaluate the LED every 1ms flawlessly.
TaskHandle_t ledTaskHandle;
void ledTask(void *pvParameters) {
    for (;;) {
        updateLedOutput();
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms sleep allows watchdog to reset and other tasks to run
    }
}

// ─────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.printf("\n\n=== Node #%d Booting ===\n", myId);

    // LEDs
    pinMode(LED_PPS,  OUTPUT);
    pinMode(LED_MODE, OUTPUT);
    digitalWrite(LED_PPS,  LOW);
    digitalWrite(LED_MODE, LOW);

    // Spawn RTOS LED Blinker Task on Core 0
    xTaskCreatePinnedToCore(
        ledTask,        // Function
        "LED_Task",     // Name
        2048,           // Stack Size
        NULL,           // Parameters
        2,              // Priority (High)
        &ledTaskHandle, // Handle
        0               // Core 0 (Network Core)
    );

    // Initialize NVS
    // Toggle Switch Pin
    pinMode(TOGGLE_SWITCH_PIN, INPUT_PULLUP);

    // NVS: load saved mode
    prefs.begin("clock-sync", false);
    syncMode = prefs.getUChar("mode", MODE_BASIC_ONE_WAY);
    Serial.printf("Loaded mode from flash: %s\n",
        syncMode == MODE_BASIC_ONE_WAY ? "BASIC ONE-WAY" : "ESP-NOW TWO-WAY");

    // WiFi in Station mode (ESP-NOW needs this)
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.disconnect();
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());

    // ESP-NOW init
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed");
        while (1) { delay(500); digitalWrite(LED_MODE, !digitalRead(LED_MODE)); }
    }
    esp_now_register_recv_cb(onReceive);

    // Register all peers
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, NODE_MACS[i], 6);
        peerInfo.channel = WIFI_CHANNEL;
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.printf("WARN: Failed to add peer Node %d\n", i);
        } else {
            Serial.printf("Peer %d registered\n", i);
        }
    }

    // Initialize peer state
    for (int i = 0; i < NODE_COUNT; i++) {
        peers[i].alive = false;
        peers[i].synced = false;
        peers[i].lastOffset_us = 0;
        peers[i].lastSeenMs = 0;
        peers[i].rssi = -127;
        peers[i].strong = false;
    }

    // Elect leader (initially just self)
    electLeader();

#ifdef OLED_ENABLED
    Wire.begin();
    Wire.setClock(400000); // 400kHz Fast I2C
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("WARN: OLED not found");
    } else {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(10, 20);
        oled.println("Clock Sync Node");
        oled.setCursor(10, 35);
        oled.printf("  Node #%d - LEADER", myId);
        oled.display();
        Serial.println("OLED initialized");
        
        // Start OLED FreeRTOS Task pinned to Core 1
        xTaskCreatePinnedToCore(oledTask, "OLED_Task", 4096, NULL, 1, NULL, 1);
    }
#endif

    sessionStart = millis();
    Serial.println("Setup complete. Running...\n");
}

// ─────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // Process any received packet
    processPacket();

    // Handle mode toggle switch
    handleToggleSwitch();

    // ── Check for dead peers (not seen in 3s) ──────────
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        if (peers[i].alive && (now - peers[i].lastSeenMs > 3000)) {
            peers[i].alive  = false;
            peers[i].synced = false;
            Serial.printf("[PEER] Node %d went offline\n", i);
            electLeader();
        }
    }

    // ── NODE 0 ARBITRATOR ──────────────────────────────
    if (myId == 0) {
        static uint32_t lastArbiterCheck = 0;
        if (now - lastArbiterCheck >= 2000) {
            lastArbiterCheck = now;
            electLeader(); // Periodically check if someone's RSSI got better!
        }
    }

    // ── LEADER ACTIONS ─────────────────────────────────
    if (isLeader) {
        // Announce leadership every heartbeat interval
        if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            lastHeartbeat = now;

            SyncPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.t1_us        = correctedMicros();
            pkt.synced_count = countSynced();

            if (syncMode == MODE_BASIC_ONE_WAY) {
                pkt.pkt_type = PKT_HEARTBEAT;
            } else {
                // In two-way mode, leader just sends a leadership announcement
                pkt.pkt_type = PKT_LEADER_ELECT;
                pkt.tree_level = myId; // MUST TELL SLAVES WE ARE THE LEADER!
            }
            sendPacket(-1, pkt);
        }
    }

    // ── SLAVE ACTIONS ──────────────────────────────────
    if (!isLeader && leaderID >= 0) {
        if (syncMode == MODE_ESPNOW_TWO_WAY) {
            // Send sync request to leader
            if (!waitingForResp && (now - lastSyncSend >= SYNC_INTERVAL_MS)) {
                lastSyncSend = now;

                SyncPacket req;
                memset(&req, 0, sizeof(req));
                req.pkt_type = PKT_SYNC_REQ;
                req.t1_us    = micros();  // Our TX timestamp (RAW)
                req.offset_us = peers[leaderID].lastOffset_us; // Report our RESIDUAL error to the dashboard!
                pending_T1   = req.t1_us;
                waitingForResp = true;
                sendPacket(-1, req); // Broadcast so Node 0 reads telemetry AND Leader processes sync!
            }

            // Timeout waiting for response
            if (waitingForResp && (now - lastSyncSend > 1000)) {
                waitingForResp = false;
                pktLost++;
                Serial.println("[TWO-WAY] Timeout — packet lost");
            }
        } else if (syncMode == MODE_BASIC_ONE_WAY) {
            // In Basic One-Way, the slave is a silent listener.
            // But we MUST send a telemetry ping to the Leader so we don't disappear from the Dashboard!
            if (now - lastSyncSend >= 1000) {
                lastSyncSend = now;
                SyncPacket tele;
                memset(&tele, 0, sizeof(tele));
                tele.pkt_type  = PKT_SYNC_REQ; // Master reads offset from this packet
                tele.t1_us     = micros();
                tele.offset_us = peers[leaderID].lastOffset_us; // Send our calculated One-Way offset
                sendPacket(-1, tele); // Broadcast so Node 0 receives it for the Dashboard!
            }
        }
    }

    // ── SERIAL DEBUG OUTPUT ───────────────────────────
    if (now - lastDebugPrint >= DEBUG_INTERVAL_MS) {
        lastDebugPrint = now;
        printDebug();
    }

    // ── OLED UPDATE (Now handled by FreeRTOS task on Core 1) ─────────
}
