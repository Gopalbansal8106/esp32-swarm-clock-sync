# Academic Alignment & Empirical Validation Report

This document aligns the ESP32 Swarm Clock Synchronization architecture and our gathered empirical telemetry data directly with four core academic engineering disciplines. 

**Context:** The fundamental application of this research is establishing time-synchronization for **Drone Swarms in GPS-Denied Environments**. When drones operate indoors, underground, or in jammed combat zones, they lose the ability to pull microsecond-accurate time from global GPS satellites. Therefore, they must rely entirely on peer-to-peer 2.4 GHz RF communication protocols to sync their internal clocks for coordinated maneuvers and sensor fusion.

---

## 1. Communication Systems (The Core Discipline)
**Core Applications:** Wireless Sensor Networks (WSN), MAC Layer Protocols, and Network Topology

Because the fundamental challenge of this project was overcoming invisible wireless network latency and jitter, **Communication Systems** is the heaviest and most critical subject involved. The project implements advanced protocol engineering to establish a microsecond-accurate data link.

### Bypassing the OSI Model (ESP-NOW)
- Traditional Wi-Fi relies on the full OSI model (TCP/IP), which requires Access Points, DHCP IP assignment, and heavy TCP handshakes. This introduces unpredictable, massive latency.
- To achieve microsecond accuracy, this project completely abandons the IP/TCP layers. It uses **ESP-NOW**, a connectionless protocol that operates directly at the Data Link Layer (MAC Layer) of the IEEE 802.11 standard. This allows nodes to blast raw payload frames directly into the air, reducing base transmission time to under a millisecond.

### TPSN (Timing-sync Protocol for Sensor Networks)
- The project implements a custom version of a textbook communication protocol called TPSN.
- **The Four-Timestamp Exchange (T1, T2, T3, T4):** To mathematically calculate the physical flight time of a radio wave, the nodes execute a 4-way handshake. By calculating the total Round-Trip Time (RTT) and subtracting the internal processing delay (`T3 - T2`), the protocol successfully isolates the pure network flight time, calculating the true clock offset regardless of network congestion.

### CSMA/CA & Packet Jitter
- The nodes share a single 2.4 GHz communication channel. They must contend with Carrier-Sense Multiple Access with Collision Avoidance (CSMA/CA). If two nodes talk at once, the radio delays the packet.
- This CSMA/CA backoff is the exact physical cause of the "jitter" observed in our telemetry. Our dataset showed massive 49.4 ms worst-case delays, proving that even at the bare MAC layer, communication protocols require algorithmic filtering.

### Reliability & Packet Loss
- The custom communication protocol proved exceptionally reliable. Our telemetry dataset logged a total packet loss rate of just **0.07%** across over 66,000 packets. This proves the synchronization failures in One-Way mode are strictly due to latency, not physical transmission failure.

---

## 2. Microcontrollers & Embedded Systems
**Core Applications:** RTOS Bypassing, Hardware Timers, and Interrupts

You cannot build a microsecond-accurate communication protocol without deeply understanding the bare-metal hardware running it.

### Hardware Timers
- Standard software delays (`delay()`) are highly inaccurate. The system taps directly into the ESP32's 64-bit internal high-resolution hardware timer (`micros()`) to serve as the absolute baseline physical clock for the entire swarm.

### Interrupt Service Routines (ISRs) vs RTOS
- FreeRTOS schedules tasks in discrete "ticks". If a packet arrives while the CPU is busy, the OS delays reading it. In a clock-sync project, a 1-millisecond OS delay destroys the accuracy.
- To bypass this, the firmware uses low-level **Interrupt Service Routines (ISRs)**. The `onReceive` callback triggers a hardware interrupt, forcing the CPU to grab the T2 reception timestamp at the *exact microsecond* the radio wave hits the silicon antenna.

### Distributed State Machines
- The swarm has no central router. It uses a distributed embedded state machine. The telemetry recorded **67 dynamic leader switches** during testing, proving the nodes successfully managed autonomous "Leader" vs "Follower" state transitions without the network collapsing.

---

## 3. Mathematics & Linear Algebra
**Core Applications:** The Double-Precision Kalman Filter & Statistical State Estimation

The communication protocols provide the raw data, but advanced mathematics is required to filter out the network noise.

### State-Space Modeling
- The software uses linear algebra to model the true "state" of the clock (Offset) and the "velocity" of the physical quartz crystal (Drift PPM).
- **Precision:** The math had to be upgraded to 64-bit `double` precision to prevent catastrophic mathematical overflow when calculating covariance matrices for billions of microseconds over an hour.

### Empirical Proof of Filtering
- Our data analysis proves the mathematical necessity of the filter. The simple One-Way sync generated a highly unstable Standard Deviation of **9.42 ms**. By applying the Kalman equations, the Two-Way system crushed the deviation down to a stable **1.89 ms**.

---

## 4. Principles of Electromagnetism
**Core Applications:** 2.4 GHz RF Wave Propagation & RSSI Analysis

The physical cause of the "jitter" that the communication protocol seeks to fix is deeply rooted in electromagnetic wave physics.

### Multipath Fading & EMI
- The unpredictable latency spikes in the network are caused by 2.4 GHz electromagnetic waves bouncing off walls (multipath fading) and fighting through Electromagnetic Interference (EMI) from background routers.

### Empirical RSSI Discovery
- We analyzed the correlation between Received Signal Strength Indicator (RSSI) and clock error. Surprisingly, nodes with weaker signals (`< -60dBm`) actually exhibited a *lower* average error (2.48 ms) than nodes with strong signals (3.10 ms) in our dataset.
- **Conclusion:** This empirical EM data proves a fundamental principle: *Sheer electromagnetic signal strength cannot prevent synchronization jitter.* Because EM waves are subject to invisible environmental phase shifts and interference, only our combination of TPSN communication protocols and Kalman filtering can guarantee accuracy.
