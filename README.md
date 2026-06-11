# Low-Cost Wireless Clock Synchronization in GPS-Denied Environments
### ESP32 + ESP-NOW | Simulation Dashboard + Hardware Firmware

---

## Project Structure

```
EL SIM CL/
├── 01_simulation_dashboard/     ← Python FastAPI backend + HTML/JS frontend
│   ├── backend/
│   │   ├── main.py              WebSocket API server (FastAPI)
│   │   └── simulation.py        Clock sync simulation engine
│   ├── frontend/
│   │   └── index.html           Dashboard UI (vanilla HTML/CSS/JS)
│   ├── config.py                Simulation parameters & tuning
│   └── start.bat                One-click launcher (Windows)
│
└── 02_hardware_firmware/        ← Arduino sketches for ESP32 boards
    ├── mac_printer/
    │   └── mac_printer.ino      STEP 1: Flash this to get MAC addresses
    ├── node_firmware/
    │   ├── node_firmware.ino    STEP 2: Main sync firmware (all 4 nodes)
    │   ├── config.h             EDIT THIS: MACs, pins, timing config
    │   └── kalman.h             2-state Kalman filter (header-only)
    └── jammer_firmware/
        └── jammer_firmware.ino  STEP 3: Interference demo node
```

---

## Quick Start

### Simulation Dashboard
```
Double-click: 01_simulation_dashboard\start.bat
Open browser: http://localhost:8000
```

### Hardware Setup (ESP32)
See **Step-by-Step** section below.

---

## Hardware Inventory

| Item | Qty | Used For |
|------|-----|----------|
| ESP32 Dev Board | 5 | 4× sync nodes, 1× jammer |
| Green LED + 220Ω | 4 | Sync status (solid = synced) |
| Yellow LED + 220Ω | 4 | Synchronizing in progress |
| Red LED + 220Ω | 5 | Unsynchronized / Jammer active |
| OLED SSD1306 I2C (0.96") | 1 | Live stats display on Node 0 |
| USB cable to PC | 1 | Node 0 only (others use chargers) |
| USB chargers | 4 | Power for Nodes 1–3 + Jammer |
| Breadboard + jumpers | — | Prototyping |

---

## LED Wiring (per sync node)

| GPIO | Component |
|------|-----------|
| 25 | Green LED → 220Ω → GND |
| 26 | Yellow LED → 220Ω → GND |
| 27 | Red LED → 220Ω → GND |
| 0 | BOOT button (built-in, mode toggle) |
| 21 | OLED SDA (Node 0 only) |
| 22 | OLED SCL (Node 0 only) |

---

## Step-by-Step: Flash Hardware

### Step 1 — Get MAC Addresses
1. Open `mac_printer/mac_printer.ino` in Arduino IDE
2. Flash to each of the 4 sync ESP32 boards (one at a time)
3. Open Serial Monitor at **115200 baud**
4. Note the MAC address printed for each board

### Step 2 — Configure Firmware
1. Open `node_firmware/config.h`
2. Fill in all 4 MAC addresses in `NODE_MACS[]`
3. Required libraries (install via Arduino Library Manager):
   - `Adafruit SSD1306`
   - `Adafruit GFX Library`
   - ESP32 board support (espressif/arduino-esp32)

### Step 3 — Flash Sync Nodes
For each of the 4 sync boards:
1. Set `THIS_NODE_ID` in `config.h` (0, 1, 2, or 3)
2. Flash `node_firmware/node_firmware.ino`
3. Node 0 gets the OLED display

### Step 4 — Flash Jammer Node
1. Flash `jammer_firmware/jammer_firmware.ino` to the 5th ESP32
2. Press BOOT button to cycle: OFF → LOW → HIGH interference
3. Move it physically closer to sync nodes to increase packet loss

---

## Sync Modes

| Mode | Algorithm | How to Switch |
|------|-----------|---------------|
| Basic One-Way | Leader broadcasts timestamp, slaves adjust | Press BOOT on any node |
| ESP-NOW Two-Way + Kalman | TPSN 4-timestamp exchange + Kalman filter | Press BOOT again |

Mode change is **broadcast wirelessly** to all nodes instantly. Mode is **saved to flash** and survives power cycles.

---

## Demo Script (Presentation)

1. Power all 4 nodes → **all red** (unsynchronized, random offsets)
2. Open Serial Monitor on Node 0 (PC) — shows live offset/drift
3. Press BOOT → **Basic One-Way** mode — LEDs turn yellow then green
4. Press BOOT again → **Two-Way + Kalman** — more accurate, serial shows T1/T2/T3/T4
5. Turn ON jammer (press its BOOT) — some nodes go red (packet loss)
6. Increase jammer to HIGH — more nodes affected
7. Turn OFF jammer — nodes re-sync automatically
8. Show OLED display: Mode, Synced count, Error, Packet Loss %

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                SOFTWARE SIMULATION                   │
│   Python FastAPI ←WebSocket→ HTML/JS Dashboard       │
│   Simulates: drift, packet loss, Kalman, TPSN        │
└─────────────────────────────────────────────────────┘
              ↕ mirrors real hardware behavior
┌─────────────────────────────────────────────────────┐
│                HARDWARE NODES (ESP32)                │
│  Node 0 (Leader) ←──ESP-NOW──→ Nodes 1, 2, 3        │
│  OLED + Serial    2.4GHz wireless   LED indicators   │
│                      ↑                              │
│              [Jammer Node] — floods channel          │
└─────────────────────────────────────────────────────┘
```
