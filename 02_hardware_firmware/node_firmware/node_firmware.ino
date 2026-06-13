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
//  LED HELPERS
// ─────────────────────────────────────────────────────────
enum LedState { LED_UNSYNC, LED_SYNCING, LED_SYNCED, LED_LEADER };
LedState currentLed = LED_UNSYNC;
uint32_t ledBlinkTimer = 0;
bool     ledBlinkState = false;

void setLed(LedState state) {
    currentLed = state;
}

void updateLedOutput() {
    bool g = false, y = false, r = false;
    uint32_t now = millis();

    switch (currentLed) {
        case LED_UNSYNC:
            r = true; g = false; y = false;
            break;
        case LED_SYNCING:
            // Yellow blink at 2Hz
            if (now - ledBlinkTimer > 250) { ledBlinkTimer = now; ledBlinkState = !ledBlinkState; }
            y = ledBlinkState;
            break;
        case LED_SYNCED:
            g = true;
            break;
        case LED_LEADER:
            // Fast green blink at 4Hz
            if (now - ledBlinkTimer > 125) { ledBlinkTimer = now; ledBlinkState = !ledBlinkState; }
            g = ledBlinkState;
            break;
    }

    digitalWrite(LED_GREEN,  g ? HIGH : LOW);
    digitalWrite(LED_YELLOW, y ? HIGH : LOW);
    digitalWrite(LED_RED,    r ? HIGH : LOW);
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
//  LEADER ELECTION  (lowest node ID that is alive)
// ─────────────────────────────────────────────────────────
void electLeader() {
    int electedLeader = -1;

    // Step 1: Find lowest ID that is alive and has a strong signal to us
    for (int i = 0; i < NODE_COUNT; i++) {
        bool candidateAlive = (i == myId) ? true : peers[i].alive;
        bool candidateStrong = (i == myId) ? true : peers[i].strong;
        
        if (candidateAlive && candidateStrong) {
            electedLeader = i;
            break; // Lowest ID wins among the strong and alive
        }
    }

    // Step 2: Fall back to lowest ID that is simply alive if no strong signals exist
    if (electedLeader == -1) {
        for (int i = 0; i < NODE_COUNT; i++) {
            bool candidateAlive = (i == myId) ? true : peers[i].alive;
            if (candidateAlive) {
                electedLeader = i;
                break;
            }
        }
    }

    if (electedLeader == -1) electedLeader = myId; // Default fallback to self

    int newLeader = electedLeader;
    bool wasLeader = isLeader;
    isLeader = (newLeader == myId);
    leaderID = newLeader;

    if (!wasLeader && isLeader) {
        Serial.println("[ELECT] I became LEADER (Strong Signal / Lowest ID)");
        setLed(LED_LEADER);
    } else if (wasLeader && !isLeader) {
        Serial.printf("[ELECT] Lost leadership → Leader is Node %d\n", leaderID);
        setLed(LED_SYNCING);
    }
}

// ─────────────────────────────────────────────────────────
//  ESP-NOW RECEIVE CALLBACK
//  !! Runs in WiFi task context — keep short !!
// ─────────────────────────────────────────────────────────
uint32_t rxTimestamp_us = 0;  // Captured precisely on arrival
SyncPacket rxPkt;
volatile bool rxPending = false;

void IRAM_ATTR onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    rxTimestamp_us = micros();   // Capture T2 immediately
    if (len == sizeof(SyncPacket)) {
        memcpy(&rxPkt, data, sizeof(SyncPacket));
        if (info && info->rx_ctrl) {
            rxRssi = info->rx_ctrl->rssi;
        } else {
            rxRssi = -127;
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
    rxPending = false;

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
        leaderID = sid;
        if (sid < myId) { isLeader = false; setLed(LED_SYNCING); }
        return;
    }

    // ── Basic One-Way: Heartbeat from leader ──────
    if (pt == PKT_HEARTBEAT && syncMode == MODE_BASIC_ONE_WAY) {
        // One-way: offset = our_time - leader_time - baseline_latency (1200 us)
        uint32_t leaderTime = rxPkt.t1_us;
        int32_t  rawOffset  = (int32_t)(correctedMicros() - leaderTime - 1200);
        
        // If offset is very large (>100ms, e.g. on boot), perform a hard sync instantly.
        // Otherwise, use soft step adjustment to smooth network jitter.
        if (abs(rawOffset) > 100000) {
            clockOffset_us += rawOffset;
        } else {
            clockOffset_us += rawOffset / 2;
        }
        
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

        uint32_t T1 = rxPkt.t1_us;   // Our TX time
        uint32_t T2 = rxPkt.t2_us;   // Leader RX time
        uint32_t T3 = rxPkt.t3_us;   // Leader TX time
        uint32_t T4 = rxTimestamp_us; // Our RX time

        // TPSN offset formula: offset = ((T4-T3) - (T2-T1)) / 2
        // Corrected sign: Since correctedMicros() is (micros() - clockOffset_us),
        // clockOffset_us must be negative when the slave is behind (T4 - T3 < T2 - T1).
        int32_t offset_raw = ((int32_t)(T4 - T3) - (int32_t)(T2 - T1)) / 2;
        uint32_t rtt       = (T4 - T1) - (T3 - T2);

        // Sanity check: reject if RTT > 100ms (corrupted packet or huge delay)
        if (rtt > 100000) {
            pktLost++;
            Serial.println("[TWO-WAY] Rejected: RTT too high");
            return;
        }

        // If offset is very large (>100ms, like on boot), perform a hard sync instantly.
        // Otherwise, filter the measurement using the Kalman clock.
        if (abs(offset_raw) > 100000) {
            clockOffset_us = clockOffset_us + offset_raw;
            kalman.reset();
            driftPPM = 0.0f;
            lastSyncTime = millis();
            peers[sid].lastOffset_us = offset_raw;
            peers[sid].synced = false;
        } else {
            double kalman_offset = kalman.update(offset_raw, rtt);
            clockOffset_us      = (int32_t)kalman_offset;
            driftPPM            = kalman.driftPPM();
            lastSyncTime        = millis();

            peers[sid].lastOffset_us = (int32_t)kalman_offset;
            peers[sid].synced        = (abs(kalman_offset) < SYNCED_THRESHOLD_US);
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
    uint32_t now = millis();
    if (now - lastButtonMs < DEBOUNCE_MS) return;

    // Read toggle switch state (LOW = Switch Closed/On, HIGH = Switch Open/Off)
    bool pinState = (digitalRead(TOGGLE_SWITCH_PIN) == LOW);
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
#endif

// ─────────────────────────────────────────
//  SERIAL DEBUG PRINT
// ─────────────────────────────────────────
void printDebug() {
    uint32_t uptime = (millis() - sessionStart) / 1000;
    bool synced_self = (millis() - lastSyncTime < SYNC_TIMEOUT_MS) && !isLeader;

    // Output raw JSON line for the PC serial bridge script
    Serial.print("{\"node_id\":");
    Serial.print(myId);
    Serial.print(",\"is_leader\":");
    Serial.print(isLeader ? "true" : "false");
    Serial.print(",\"sync_mode\":");
    Serial.print(syncMode);
    Serial.print(",\"offset\":");
    Serial.print(clockOffset_us);
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
//  SETUP
// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.printf("\n\n=== Node #%d Booting ===\n", myId);

    // LEDs
    pinMode(LED_GREEN,  OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_RED,    OUTPUT);
    digitalWrite(LED_RED, HIGH);  // Start with red (unsync)

    // Toggle Switch Pin
    pinMode(TOGGLE_SWITCH_PIN, INPUT_PULLUP);

    // NVS: load saved mode
    prefs.begin("clksync", false);
    syncMode = prefs.getUChar("mode", MODE_BASIC_ONE_WAY);
    Serial.printf("Loaded mode from flash: %s\n",
        syncMode == MODE_BASIC_ONE_WAY ? "BASIC ONE-WAY" : "ESP-NOW TWO-WAY");

    // WiFi in Station mode (ESP-NOW needs this)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());

    // ESP-NOW init
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed");
        while (1) { delay(500); digitalWrite(LED_RED, !digitalRead(LED_RED)); }
    }
    esp_now_register_recv_cb(onReceive);

    // Register all peers
    for (int i = 0; i < NODE_COUNT; i++) {
        if (i == myId) continue;
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, NODE_MACS[i], 6);
        peerInfo.channel = 0;
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

    // ── Update LED state ───────────────────────────────
    if (isLeader) {
        setLed(LED_LEADER);
    } else if (lastSyncTime > 0 && (now - lastSyncTime < SYNC_TIMEOUT_MS)) {
        bool isSynced = (abs(clockOffset_us) < (int32_t)SYNCED_THRESHOLD_US);
        setLed(isSynced ? LED_SYNCED : LED_SYNCING);
    } else {
        setLed(LED_UNSYNC);
    }
    updateLedOutput();

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
                req.t1_us    = micros();  // Our TX timestamp
                pending_T1   = req.t1_us;
                waitingForResp = true;
                sendPacket(leaderID, req);
            }

            // Timeout waiting for response
            if (waitingForResp && (now - lastSyncSend > 1000)) {
                waitingForResp = false;
                pktLost++;
                Serial.println("[TWO-WAY] Timeout — packet lost");
            }
        }
    }

    // ── SERIAL DEBUG OUTPUT ───────────────────────────
    if (now - lastDebugPrint >= DEBUG_INTERVAL_MS) {
        lastDebugPrint = now;
        printDebug();
    }

    // ── OLED UPDATE (Node 0 only, every 500ms) ─────────
#ifdef OLED_ENABLED
    if (now - lastOledUpdate >= 500) {
        lastOledUpdate = now;
        updateOLED();
    }
#endif
}
