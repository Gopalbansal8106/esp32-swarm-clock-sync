// ============================================================================
//  config.h  —  Raw Clock Telemetry Config
// ============================================================================
#pragma once
#include <Arduino.h>

// Set THIS_NODE_ID per board before uploading:
// Node 0 stays on PC. Nodes 1, 2, 3 are powered by chargers.
#define THIS_NODE_ID   3   // ← Change per board: 0, 1, 2, or 3

#define NODE_COUNT  4
const uint8_t NODE_MACS[NODE_COUNT][6] = {
    {0x30, 0xAE, 0xA4, 0x8C, 0x1B, 0xD4},  // Node 0 (USB to PC)
    {0x68, 0x25, 0xDD, 0x30, 0x05, 0x50},  // Node 1
    {0x30, 0xAE, 0xA4, 0x8B, 0x3B, 0xA8},  // Node 2
    {0x14, 0x33, 0x5C, 0x02, 0x28, 0x10}   // Node 3
};

#define SERIAL_BAUD 115200
#define REPORT_INTERVAL_MS 500
