// ============================================================
//  kalman.h  —  2-State Kalman Filter (Offset + Drift)
//  Header-only implementation for ESP32 clock synchronization
// ============================================================
#pragma once
#include <Arduino.h>

class KalmanClock {
public:
    // State vector: [offset_us, drift_ppm]
    double x_offset;   // Estimated offset in microseconds
    double x_drift;    // Estimated drift in PPM

    // Error covariance matrix (2x2, stored as P00, P01, P10, P11)
    double P00, P01, P10, P11;

    // Process noise
    double q_offset;   // from config.h KALMAN_Q_OFFSET
    double q_drift;    // from config.h KALMAN_Q_DRIFT

    // Measurement noise
    double r;          // from config.h KALMAN_R

    // Last update time for drift integration
    uint32_t last_us;
    bool initialized;

    KalmanClock(double q_off = 0.5, double q_drft = 0.05, double r_meas = 50.0)
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
    double update(double measured_offset_us, double rtt_us) {
        uint32_t now = micros();

        if (!initialized) {
            x_offset = measured_offset_us;
            x_drift  = 0;
            last_us  = now;
            initialized = true;
            return x_offset;
        }

        // dt in seconds
        double dt = (double)(now - last_us) / 1e6;
        last_us = now;
        if (dt <= 0 || dt > 5.0) dt = 0.001; // Guard against wraparound

        // ── 1. PREDICT ──────────────────────────
        // State transition: offset grows with drift
        double x_off_pred = x_offset + x_drift * dt;
        double x_dri_pred = x_drift;  // Drift assumed constant

        // Covariance prediction
        double P00_pred = P00 + dt * (P10 + P01) + dt * dt * P11 + q_offset;
        double P01_pred = P01 + dt * P11;
        double P10_pred = P10 + dt * P11;
        double P11_pred = P11 + q_drift;

        // ── 2. UPDATE ───────────────────────────
        // Measurement noise scales with RTT (longer RTT = noisier)
        double r_scaled = r + (rtt_us * 0.1);

        // Innovation covariance: S = P00 + R
        double S = P00_pred + r_scaled;

        // Kalman gain
        double K0 = P00_pred / S;
        double K1 = P10_pred / S;

        // Innovation (measurement - prediction)
        double innov = measured_offset_us - x_off_pred;

        // State update
        x_offset = x_off_pred + K0 * innov;
        x_drift  = x_dri_pred + K1 * innov;

        // Covariance update
        P00 = (1.0 - K0) * P00_pred;
        P01 = (1.0 - K0) * P01_pred;
        P10 = P10_pred - K1 * P00_pred;
        P11 = P11_pred - K1 * P01_pred;

        return x_offset;
    }

    // Apply the estimated offset to micros() to get corrected time
    uint64_t correctedMicros() {
        return (uint64_t)micros() - (int64_t)x_offset;
    }

    // Drift in PPM (positive = this node runs fast)
    double driftPPM() { return x_drift * 1e6; }
};
