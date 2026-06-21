// ============================================================
//  config.h  —  Clock Sync Node Configuration
//  Edit MAC_ADDRESSES and pin definitions to match your hardware
// ============================================================
#pragma once

// ─────────────────────────────────────────────
//  NODE IDENTITY
//  Flash "mac_printer.ino" on each board first,
//  note the MAC, then fill them in here in order.
// ─────────────────────────────────────────────
// Which node number is THIS board? (0 = leader candidate)
// You set this per-board before flashing. Node 0 has the OLED.
#define THIS_NODE_ID   2   // ← Change per board: 0, 1, 2, 3, or 4

// MAC addresses of all 5 sync nodes (filled from physical nodes telemetry)
// Node 0: 30:AE:A4:8C:1B:D4
// Node 1: 68:25:DD:30:05:50
// Node 2: 30:AE:A4:8B:3B:A8
// Node 3: 14:33:5C:02:28:10
// Node 4: 14:2B:2F:C8:E1:C8
#define NODE_COUNT  4
const uint8_t NODE_MACS[NODE_COUNT][6] = {
    {0x30, 0xAE, 0xA4, 0x8C, 0x1B, 0xD4},  // Node 0 (Leader Display)
    {0x68, 0x25, 0xDD, 0x30, 0x05, 0x50},  // Node 1
    {0x30, 0xAE, 0xA4, 0x8B, 0x3B, 0xA8},  // Node 2
    {0x14, 0x33, 0x5C, 0x02, 0x28, 0x10}   // Node 3
};

// ─────────────────────────────────────────────
//  LED PINS  (connect LED → 220Ω → pin)
// ─────────────────────────────────────────────
#define LED_GREEN   25    // Synchronized / Elected Leader
#define LED_YELLOW  26    // Synchronizing in progress
#define LED_RED     27    // Unsynchronized / Drifted / Fault

// ─────────────────────────────────────────────
//  TOGGLE SWITCH (External physical toggle switch)
//  Connect between Pin 13 and GND
// ─────────────────────────────────────────────
#define TOGGLE_SWITCH_PIN  13     // Active LOW (internal pull-up enabled)
#define DEBOUNCE_MS        50

// ─────────────────────────────────────────────
//  OLED  (only physically wired on Node 0)
//  SDA = GPIO 21, SCL = GPIO 22
//  Enable only on Node 0 to save compile space on others
// ─────────────────────────────────────────────
#if THIS_NODE_ID == 0
  #define OLED_ENABLED
  #define OLED_WIDTH  128
  #define OLED_HEIGHT  64
  #define OLED_ADDR   0x3C
#endif

// ─────────────────────────────────────────────
//  SYNC TIMING
// ─────────────────────────────────────────────
#define SYNC_INTERVAL_MS        500    // How often a slave requests sync
#define HEARTBEAT_INTERVAL_MS  1000    // Leader broadcasts heartbeat
#define SYNC_TIMEOUT_MS        3000    // Mark node drifted if no sync in this long
#define SYNCED_THRESHOLD_US     800    // Below this offset → SYNCHRONIZED (800µs)

// ─────────────────────────────────────────────
//  KALMAN FILTER TUNING
// ─────────────────────────────────────────────
#define KALMAN_Q_OFFSET   0.5f    // Process noise — offset model
#define KALMAN_Q_DRIFT    0.05f   // Process noise — drift model
#define KALMAN_R          50.0f   // Measurement noise (µs² based on hardware)

// ─────────────────────────────────────────────
//  SERIAL DEBUG
// ─────────────────────────────────────────────
#define SERIAL_BAUD   115200
#define DEBUG_INTERVAL_MS  500    // Print debug line every 500ms

// ─────────────────────────────────────────────
//  SYNC MODES & WIFI
// ─────────────────────────────────────────────
#define MODE_BASIC_ONE_WAY   0
#define MODE_ESPNOW_TWO_WAY  1
#define WIFI_CHANNEL         13   // Set to 13 to avoid 2.4GHz home router interference

// ─────────────────────────────────────────────
//  PACKET TYPES
// ─────────────────────────────────────────────
#define PKT_HEARTBEAT      0x01   // Leader → All: basic one-way timestamp
#define PKT_SYNC_REQ       0x02   // Slave → Leader: two-way request (T1)
#define PKT_SYNC_RESP      0x03   // Leader → Slave: two-way response (T1,T2,T3)
#define PKT_MODE_CHANGE    0x04   // Any → All: new sync mode broadcast
#define PKT_LEADER_ELECT   0x05   // Election announcement
