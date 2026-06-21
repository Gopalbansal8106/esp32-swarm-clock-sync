# ESP32 Swarm Clock Synchronization: System Architecture Report

This document provides a comprehensive overview of the system architecture, detailing the hardware mesh, synchronization algorithms, and the full-stack telemetry dashboard.

---

## 1. System Overview & Motivation

**The Problem:** In modern robotics, coordinated drone swarms rely on exact time synchronization for sensor fusion and collision avoidance. Normally, drones pull microsecond-accurate time directly from GPS satellites. However, in **GPS-denied environments** (e.g., indoors, underground, or jammed combat zones), this global time source is completely lost.

**The Solution:** This project creates a decentralized, self-healing swarm of ESP32 microcontrollers that simulates a drone swarm maintaining microsecond-level clock synchronization using only peer-to-peer RF communication. The system is designed to mathematically model, track, and compensate for hardware crystal oscillator drift and network latency, proving the superiority of advanced Two-Way filtering algorithms over basic One-Way broadcasts when GPS is unavailable.

The architecture is divided into three distinct layers:
1. **Hardware Mesh Layer:** The physical ESP32 nodes and wireless protocol.
2. **Algorithmic Layer:** The mathematical filters governing time synchronization.
3. **Telemetry & Presentation Layer:** The Python backend and React frontend dashboard.

---

## 2. Hardware Mesh Layer (ESP32)

### Wireless Protocol: ESP-NOW
The swarm operates on **ESP-NOW**, a connectionless, low-latency Wi-Fi protocol developed by Espressif. 
- **Why ESP-NOW?** Standard Wi-Fi requires heavy overhead (handshakes, access points, DHCP), which introduces massive unpredictable latency. ESP-NOW allows nodes to blast raw MAC-layer packets directly to each other in under a millisecond, making it ideal for real-time clock synchronization.

### Node Roles & Dynamic Election
The swarm is completely decentralized. There is no central router.
- **The Leader:** One node dynamically assumes the role of the "Time Leader." It acts as the absolute source of truth for the swarm's clock. It periodically broadcasts `HEARTBEAT` packets to the swarm.
- **The Followers:** All other nodes listen to the Leader and adjust their internal clocks to match.
- **The Bridge Node:** One specific node (Node 0) is physically connected to the PC via USB. It actively participates in the swarm while acting as a serial bridge, forwarding all wireless telemetry data up to the PC.
- **Dynamic Election:** If the Leader goes offline, the followers detect the loss of heartbeats and automatically elect a new Leader based on a deterministic hierarchy (lowest Node ID), ensuring the swarm never collapses.

---

## 3. Algorithmic Layer

The core scientific focus of the project is comparing two synchronization architectures.

### Mode 1: Basic One-Way Sync (The Control)
- **Mechanism:** The Leader broadcasts its current timestamp. The Follower receives it and instantly snaps its clock to match the Leader's timestamp.
- **The Flaw:** This assumes network transmission takes 0 seconds. If a packet is delayed by Wi-Fi interference (jitter), the Follower incorrectly sets its clock to the delayed time.
- **Result:** The clock is violently unstable, constantly bouncing around due to unpredictable network latency.

### Mode 2: Two-Way Sync + Kalman Filter (The Proposed Solution)
To solve the physical limitations of One-Way sync, a two-stage mathematical approach is used.

**Stage A: Two-Way PTP (Precision Time Protocol)**
Instead of a simple broadcast, nodes exchange a 4-way handshake to calculate the exact speed of light/radio flight time.
1. `T1`: Leader sends sync request.
2. `T2`: Follower records exact reception time.
3. `T3`: Follower sends response.
4. `T4`: Leader records exact reception time.
*By subtracting the processing time (`T3 - T2`) from the total round trip (`T4 - T1`), the node calculates the exact network flight time and extracts the true hardware offset.*

**Stage B: The Kalman Filter**
Even with Two-Way sync, occasional noisy packets occur. The system runs the raw offset through a **Kalman Filter**.
- **Velocity Tracking (Drift PPM):** The filter doesn't just track the *current* time error; it calculates the physical frequency imperfection of the ESP32's quartz crystal. If the crystal runs 10 Parts-Per-Million (PPM) too fast, the Kalman filter learns this "velocity" and pre-emptively slows the software clock down, achieving incredible stability (crushing 99th percentile errors from 49ms down to 5ms).

---

## 4. Telemetry & Presentation Layer

To visualize the invisible wireless mesh, a modern full-stack application sits on the PC.

### The Python Backend
- **Serial Ingestion:** A background thread aggressively reads raw JSON data streaming over the USB COM port from the Bridge Node.
- **Database Logging:** It structures and saves every packet into a local CSV database (`logs/`) for rigorous empirical analysis and grading.
- **API Server:** It runs a lightweight **FastAPI** web server, broadcasting the live hardware state via WebSockets or REST endpoints to the frontend.

### The React Frontend
Built using modern web technologies (HTML, CSS, React, Plotly), the dashboard provides real-time situational awareness of the hardware.
- **Topology Map:** A force-directed graph visualizes the physical layout of the nodes, drawing lines to indicate who is synced to whom, and displaying the physical radio signal strength (RSSI).
- **Oscilloscope/Plotly Graphs:** Live rolling graphs plot the microsecond-level synchronization error of the nodes, allowing the user to visually see the violent jitter of One-Way sync versus the flatline stability of Two-Way sync.
- **Node Dashboards:** Individual cards display the health of each ESP32, including their exact calculated Clock Drift (PPM), packet loss rates, and current role (Leader/Syncing).
