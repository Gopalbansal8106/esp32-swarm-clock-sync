// ============================================================
//  kalman.h  —  2-State Kalman Filter (Offset + Drift)
//  Header-only implementation for ESP32 clock synchronization
// ============================================================
#pragma once
#include <Arduino.h>

class KalmanClock {
public:
    // State vector: [offset_us, drift_ppm]
    float x_offset;   // Estimated offset in microseconds
    float x_drift;    // Estimated drift in PPM

    // Error covariance matrix (2x2, stored as P00, P01, P10, P11)
    float P00, P01, P10, P11;

    // Process noise
    float q_offset;   // from config.h KALMAN_Q_OFFSET
    float q_drift;    // from config.h KALMAN_Q_DRIFT

    // Measurement noise
    float r;          // from config.h KALMAN_R

    // Last update time for drift integration
    uint32_t last_us;
    bool initialized;

    KalmanClock(float q_off = 0.5f, float q_drft = 0.05f, float r_meas = 50.0f)
        : x_offset(0), x_drift(0),
          P00(1000), P01(0), P10(0), P11(100),
          q_offset(q_off), q_drift(q_drft), r(r_meas),
          last_us(0), initialized(false) {}

    void reset() {
        x_offset = 0; x_drift = 0;
        P00 = 1000; P01 = 0; P10 = 0; P11 = 100;
        initialized = false;
    }

    // ─────────────────────────────────────────
    //  Call this with the measured offset (µs)
    //  and the round-trip time (µs) from TPSN.
    //  Returns the Kalman-estimated offset.
    // ─────────────────────────────────────────
    float update(float measured_offset_us, float rtt_us) {
        uint32_t now = micros();

        if (!initialized) {
            x_offset = measured_offset_us;
            x_drift  = 0;
            last_us  = now;
            initialized = true;
            return x_offset;
        }

        // dt in seconds
        float dt = (float)(now - last_us) / 1e6f;
        last_us = now;
        if (dt <= 0 || dt > 5.0f) dt = 0.001f; // Guard against wraparound

        // ── 1. PREDICT ──────────────────────────
        // State transition: offset grows with drift
        float x_off_pred = x_offset + x_drift * dt;
        float x_dri_pred = x_drift;  // Drift assumed constant

        // Covariance prediction
        float P00_pred = P00 + dt * (P10 + P01) + dt * dt * P11 + q_offset;
        float P01_pred = P01 + dt * P11;
        float P10_pred = P10 + dt * P11;
        float P11_pred = P11 + q_drift;

        // ── 2. UPDATE ───────────────────────────
        // Measurement noise scales with RTT (longer RTT = noisier)
        float r_scaled = r + (rtt_us * 0.1f);

        // Innovation covariance: S = P00 + R
        float S = P00_pred + r_scaled;

        // Kalman gain
        float K0 = P00_pred / S;
        float K1 = P10_pred / S;

        // Innovation (measurement - prediction)
        float innov = measured_offset_us - x_off_pred;

        // State update
        x_offset = x_off_pred + K0 * innov;
        x_drift  = x_dri_pred + K1 * innov;

        // Covariance update
        P00 = (1.0f - K0) * P00_pred;
        P01 = (1.0f - K0) * P01_pred;
        P10 = P10_pred - K1 * P00_pred;
        P11 = P11_pred - K1 * P01_pred;

        return x_offset;
    }

    // Apply the estimated offset to micros() to get corrected time
    uint64_t correctedMicros() {
        return (uint64_t)micros() - (int64_t)x_offset;
    }

    // Drift in PPM (positive = this node runs fast)
    float driftPPM() { return x_drift * 1e6f; }
};
