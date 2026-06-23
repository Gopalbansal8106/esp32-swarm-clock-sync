# 🚁 SwarmSync: Microsecond Drone Clock Synchronization in GPS-Denied Environments

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Hardware](https://img.shields.io/badge/Hardware-ESP32-brightgreen)
![Protocol](https://img.shields.io/badge/Protocol-ESP--NOW-orange)

SwarmSync is a decentralized, low-cost wireless clock synchronization architecture designed for autonomous drone swarms operating in GPS-denied environments (e.g., underground, indoor, or signal-jammed zones). 

By completely bypassing the standard TCP/IP OSI model and executing a **Two-Way Time Transfer (TWTT)** protocol directly at the bare-metal MAC layer, this system achieves microsecond-level synchronization accuracy using affordable commercial microcontrollers.

## 🌟 Key Innovations
* **Bare-Metal MAC Layer Communication:** Utilizes the ESP-NOW protocol to eliminate heavy networking handshakes (DHCP/TCP), achieving sub-millisecond transmission speeds.
* **Hardware-Level Precision (ISRs):** Uses low-level Hardware Interrupts to bypass RTOS scheduling delays, capturing timestamps the exact microsecond a radio wave hits the silicon antenna.
* **Embedded 64-Bit Kalman Filtering:** Smooths out unpredictable multipath RF jitter and calculates physical hardware clock drift (PPM) using a highly optimized, double-precision Kalman filter running directly on the ESP32.
* **Electronic Warfare (DDoS) Immunity:** Because ESP-NOW natively filters packets by MAC address at the hardware layer, the swarm successfully maintained a **0% packet loss rate** and survived a severe network flooding attack (10 garbage packets per millisecond).

## 📊 Empirical Performance (177,000+ Packet Stress Test)
In an extensive empirical stress test utilizing over 177,000 telemetry packets, the system was benchmarked against severe network congestion and DDoS-style interference:

| Evaluation Metric | Basic One-Way Sync | Two-Way + Kalman Filter |
| :--- | :--- | :--- |
| **Average Sync Error (Sterile)** | `3.592 ms` | **`0.151 ms`** |
| **Average Sync Error (Attacked)**| `5.739 ms` | **`1.290 ms`** |
| **Physical Packet Loss Rate** | `System Frozen` | **`0.00%`** |

*The Two-Way Kalman architecture proved to be **4.4x more resilient** than basic broadcast synchronization, successfully replicating military-grade timing stability on a $4 commercial microcontroller.*

## 📂 Repository Structure
* `/01_simulations` - Standalone MATLAB and Simulink block-diagram models used to mathematically tune the Kalman covariance matrices.
* `/02_hardware_firmware` - Bare-metal C++ firmware for the ESP32 nodes (Leader, Follower, Bridge) utilizing FreeRTOS multi-core task pinning.
* `/03_hardware_dashboard` - A live-updating Python (FastAPI) and React.js telemetry dashboard featuring force-directed topology mapping and microsecond oscilloscope tracking.

## 🚀 Getting Started
1. Flash the `node_firmware.ino` onto your ESP32 microcontrollers using the Arduino IDE.
2. Ensure one ESP32 is hardwired to your host PC via USB to act as the silent Bridge Node.
3. Navigate to `03_hardware_dashboard` and run `python main.py`.
4. Open `http://localhost:8081` to view the live swarm telemetry.

## 🛡️ Future Scope
* **Machine Learning Prediction:** Training TinyML models on the ESP32 to analyze historical RSSI degradation to preemptively predict connection loss.
* **Cryptographic Defense:** Integrating AES-CCMP encryption into the ESP-NOW MAC layer to defend against timestamp spoofing from Software Defined Radios (SDR).
* **Tree-Level Topology:** Migrating from a flat mesh to a hierarchical routing system to support massive swarms (100+ nodes) without risking 2.4GHz broadcast storms.

---
*Developed by Gopal Bansal, Vinay Krishna BV, Rohan K G, and Inaya J Gujaran at RV College of Engineering.*
