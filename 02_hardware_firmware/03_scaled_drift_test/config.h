// ============================================================================
//  config.h  —  Basic Sync + Scaled Drift Test Config
// ============================================================================
#pragma once
#include <Arduino.h>

#define THIS_NODE_ID   2   // ← Change per board: 0, 1, 2, or 3

#define NODE_COUNT  4
const uint8_t NODE_MACS[NODE_COUNT][6] = {
    {0x30, 0xAE, 0xA4, 0x8C, 0x1B, 0xD4},
    {0x68, 0x25, 0xDD, 0x30, 0x05, 0x50},
    {0x30, 0xAE, 0xA4, 0x8B, 0x3B, 0xA8},
    {0x14, 0x33, 0x5C, 0x02, 0x28, 0x10}
};

#define SERIAL_BAUD 115200
#define REPORT_INTERVAL_MS 500

// Artificial Drift Tuning (PPM values to add to each node's natural clock)
// Node 0 is the reference (0 PPM added)
const float ARTIFICIAL_DRIFT_PPM[NODE_COUNT] = {
    0.0f,     // Node 0 (Master)
    1500.0f,  // Node 1: Fast drift (+1.5 ms per second)
    -2500.0f, // Node 2: Slow drift (-2.5 ms per second)
    4000.0f   // Node 3: Very fast drift (+4.0 ms per second)
};
