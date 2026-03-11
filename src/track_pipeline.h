// track_pipeline.h — Leia eye position to OpenTrack rotation mapping
// Pipeline: lean offset → One-Euro filter → magnetic center → sensitivity curve → clamp
#pragma once

#include "one_euro_filter.h"
#include <cmath>
#include <algorithm>

struct TrackConfig {
    // One-Euro filter (tune with keyboard during testing)
    float filter_freq      = 60.0f;
    float filter_mincutoff = 0.02f;   // Smoothness at rest
    float filter_beta      = 0.3f;    // Responsiveness during movement

    // Sensitivity: degrees of camera rotation per cm of head lean
    float sens_yaw         = 3.0f;
    float sens_pitch       = 2.0f;

    // Curve: >1.0 = gentle near center, aggressive at extremes
    float curve_power      = 1.2f;

    // Magnetic center
    float mag_strength     = 0.15f;
    float mag_radius       = 2.0f;

    // Output clamp (degrees)
    float max_yaw          = 45.0f;
    float max_pitch        = 45.0f;

    // Dead zone
    float dead_zone_cm     = 0.2f;
};

struct TrackResult {
    float yaw_deg   = 0.0f;
    float pitch_deg = 0.0f;
    float roll_deg  = 0.0f;
    float lean_x_cm = 0.0f;
    float lean_y_cm = 0.0f;
    float filt_x_cm = 0.0f;
    float filt_y_cm = 0.0f;
    bool  valid     = false;
};

class TrackPipeline {
    OneEuroFilter filter_x_, filter_y_;
    float center_x_ = 0.0f;
    float center_y_ = 0.0f;
    TrackConfig cfg_;

public:
    TrackPipeline() { reset_filters(); }
    explicit TrackPipeline(const TrackConfig& cfg) : cfg_(cfg) { reset_filters(); }

    void reset_filters() {
        filter_x_ = OneEuroFilter(cfg_.filter_freq, cfg_.filter_mincutoff, cfg_.filter_beta);
        filter_y_ = OneEuroFilter(cfg_.filter_freq, cfg_.filter_mincutoff, cfg_.filter_beta);
    }

    void calibrate(float left_eye[3], float right_eye[3]) {
        center_x_ = (left_eye[0] + right_eye[0]) / 2.0f / 10.0f;
        center_y_ = (left_eye[1] + right_eye[1]) / 2.0f / 10.0f;
        reset_filters();
    }

    TrackResult process(float left_eye[3], float right_eye[3], float timestamp_sec) {
        TrackResult r;

        // 1. Face detection via IPD threshold
        float ipd = std::fabs(right_eye[0] - left_eye[0]);
        if (ipd < 10.0f) {
            r.valid = false;
            return r;
        }

        // 2. Head midpoint in cm
        float mid_x = (left_eye[0] + right_eye[0]) / 2.0f / 10.0f;
        float mid_y = (left_eye[1] + right_eye[1]) / 2.0f / 10.0f;

        // 3. Lean offset from calibrated center
        r.lean_x_cm = mid_x - center_x_;
        r.lean_y_cm = mid_y - center_y_;

        // 4. One-Euro filter
        r.filt_x_cm = filter_x_.filter(r.lean_x_cm, timestamp_sec);
        r.filt_y_cm = filter_y_.filter(r.lean_y_cm, timestamp_sec);

        // 5. Magnetic center pull
        float pull_x = cfg_.mag_strength * std::exp(-std::fabs(r.filt_x_cm) / cfg_.mag_radius);
        float pull_y = cfg_.mag_strength * std::exp(-std::fabs(r.filt_y_cm) / cfg_.mag_radius);
        float eff_x = r.filt_x_cm * (1.0f - pull_x);
        float eff_y = r.filt_y_cm * (1.0f - pull_y);

        // 6. Smooth dead zone
        eff_x = smooth_deadzone(eff_x, cfg_.dead_zone_cm);
        eff_y = smooth_deadzone(eff_y, cfg_.dead_zone_cm);

        // 7. Sensitivity curve + clamp
        r.yaw_deg   = -apply_curve(eff_x, cfg_.sens_yaw,   cfg_.max_yaw);
        r.pitch_deg = apply_curve(eff_y, cfg_.sens_pitch,  cfg_.max_pitch);
        r.roll_deg  = 0.0f;

        // 8. NaN guard
        if (!std::isfinite(r.yaw_deg))   r.yaw_deg   = 0.0f;
        if (!std::isfinite(r.pitch_deg)) r.pitch_deg = 0.0f;

        r.valid = true;
        return r;
    }

    TrackConfig& config() { return cfg_; }
    const TrackConfig& config() const { return cfg_; }

private:
    float smooth_deadzone(float val, float dz) const {
        float abs_val = std::fabs(val);
        if (abs_val < dz) {
            float t = abs_val / dz;
            float scale = t * t * t;
            return (val > 0.0f ? 1.0f : -1.0f) * abs_val * scale;
        }
        return val;
    }

    float apply_curve(float val, float sens, float max_deg) const {
        if (val == 0.0f) return 0.0f;
        float sign = (val > 0.0f) ? 1.0f : -1.0f;
        float curved = std::pow(std::fabs(val), cfg_.curve_power);
        float deg = sign * curved * sens;
        return std::clamp(deg, -max_deg, max_deg);
    }
};
