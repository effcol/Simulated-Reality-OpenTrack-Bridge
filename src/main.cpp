// leia_track_app — Standalone Leia eye tracking to OpenTrack UDP converter
// Reads Leia SDK eye positions, filters with One-Euro, maps lean to rotation,
// sends as OpenTrack UDP to VRto3D. Console app for parameter tuning.
//
// Usage: Run with VRto3D (use_open_track=true, port 4242) + SteamVR active.
//        Lean head to rotate camera. Use hotkeys to tune parameters.

#define NOMINMAX
#include <winsock2.h>  // Must be before windows.h
#include <windows.h>

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <conio.h>
#include <cstdio>
#include <algorithm>

#include "sr/management/srcontext.h"
#include "sr/utility/exception.h"
#include "sr/sense/eyetracker/predictingeyetracker.h"
#include "sr/sense/eyetracker/eyepair.h"
#include "sr/sense/core/inputstream.h"

#include "one_euro_filter.h"
#include "track_pipeline.h"
#include "opentrack_udp.h"

// Ctrl+letter codes returned by _getch() on Windows
#define CTRL_L  12
#define CTRL_X  24

// --- Eye data listener ---

class EyeListener : public SR::EyePairListener {
    std::mutex mutex_;
    float left_[3]  = {0.0f, 0.0f, 600.0f};
    float right_[3] = {65.0f, 0.0f, 600.0f};
    uint64_t frame_time_ = 0;
    bool has_data_ = false;

public:
    SR::InputStream<SR::EyePairStream> stream;

    void accept(const SR_eyePair& frame) override {
        std::lock_guard<std::mutex> lock(mutex_);
        left_[0]  = static_cast<float>(frame.left.x);
        left_[1]  = static_cast<float>(frame.left.y);
        left_[2]  = static_cast<float>(frame.left.z);
        right_[0] = static_cast<float>(frame.right.x);
        right_[1] = static_cast<float>(frame.right.y);
        right_[2] = static_cast<float>(frame.right.z);
        frame_time_ = frame.time;
        has_data_ = true;
    }

    bool get(float left[3], float right[3], uint64_t& time_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_data_) return false;
        left[0] = left_[0]; left[1] = left_[1]; left[2] = left_[2];
        right[0] = right_[0]; right[1] = right_[1]; right[2] = right_[2];
        time_us = frame_time_;
        return true;
    }
};

// --- Config file (Steam/config/vrto3d/) ---

static std::string get_config_path() {
    char steam_path[MAX_PATH] = {};
    DWORD size = sizeof(steam_path);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "InstallPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(steam_path), &size);
        RegCloseKey(key);
    }

    if (steam_path[0] != '\0') {
        std::string dir = std::string(steam_path) + "\\config\\vrto3d";
        CreateDirectoryA((std::string(steam_path) + "\\config").c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\leia_track_config.txt";
    }

    // Fallback: next to the exe
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string path(exe_path);
    auto last_slash = path.find_last_of("\\/");
    if (last_slash != std::string::npos) path = path.substr(0, last_slash + 1);
    return path + "leia_track_config.txt";
}

static bool save_config(const TrackConfig& cfg) {
    std::string path = get_config_path();
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "# Leia Track App — Settings\n";
    f << "# Edit values here or tune in-app with hotkeys (auto-saves).\n\n";
    f << "filter_mincutoff = " << cfg.filter_mincutoff << "\n";
    f << "filter_beta = " << cfg.filter_beta << "\n";
    f << "sens_yaw = " << cfg.sens_yaw << "\n";
    f << "sens_pitch = " << cfg.sens_pitch << "\n";
    f << "curve_power = " << cfg.curve_power << "\n";
    f << "mag_strength = " << cfg.mag_strength << "\n";
    f << "mag_radius = " << cfg.mag_radius << "\n";
    f << "dead_zone_cm = " << cfg.dead_zone_cm << "\n";
    f << "max_yaw = " << cfg.max_yaw << "\n";
    f << "max_pitch = " << cfg.max_pitch << "\n";

    f.close();
    return true;
}

static bool load_config(TrackConfig& cfg) {
    std::string path = get_config_path();
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
        };
        trim(key);
        trim(val);

        try {
            float v = std::stof(val);
            if (!std::isfinite(v)) continue;

            if (key == "filter_mincutoff") cfg.filter_mincutoff = v;
            else if (key == "filter_beta") cfg.filter_beta = v;
            else if (key == "sens_yaw") cfg.sens_yaw = v;
            else if (key == "sens_pitch") cfg.sens_pitch = v;
            else if (key == "curve_power") cfg.curve_power = v;
            else if (key == "mag_strength") cfg.mag_strength = v;
            else if (key == "mag_radius") cfg.mag_radius = v;
            else if (key == "dead_zone_cm") cfg.dead_zone_cm = v;
            else if (key == "max_yaw") cfg.max_yaw = v;
            else if (key == "max_pitch") cfg.max_pitch = v;
        } catch (...) {
            continue;
        }
    }
    return true;
}

// --- Console helpers ---

static void print_banner() {
    std::printf("========================================================\n");
    std::printf("  Leia Track App v0.2              by evilkermitreturns\n");
    std::printf("========================================================\n");
    std::printf("  Leia eye tracking -> One-Euro filter -> OpenTrack UDP\n");
    std::printf("  Standalone head tracking for any SteamVR game.\n");
    std::printf("  Requires: Leia display + VRto3D (use_open_track=true)\n");
    std::printf("========================================================\n\n");
    std::printf("  CONTROLS (only active when this window is focused)\n");
    std::printf("    Ctrl+X  Calibrate center (look straight, sit naturally)\n");
    std::printf("    Ctrl+L  Lock/unlock hotkeys (prevent accidental changes)\n");
    std::printf("    1/2     Smoothness at rest (min_cutoff down/up)\n");
    std::printf("    3/4     Movement response (beta down/up)\n");
    std::printf("    5/6     Yaw sensitivity (left/right lean)\n");
    std::printf("    7/8     Pitch sensitivity (up/down lean)\n");
    std::printf("    9/0     Response curve (more linear / more curved)\n");
    std::printf("    -/=     Max yaw range (down/up)\n");
    std::printf("    [/]     Max pitch range (down/up)\n");
    std::printf("  Settings auto-save on every change.\n");
    std::printf("  Config: Steam/config/vrto3d/leia_track_config.txt\n");
    std::printf("========================================================\n\n");
}

// --- Main ---

int main() {
    // Prevent Ctrl+C from killing app without cleanup
    SetConsoleCtrlHandler(nullptr, TRUE);

    print_banner();

    // 1. Init UDP sender
    OpenTrackSender sender;
    if (!sender.init("127.0.0.1", 4242)) {
        std::printf("ERROR: Failed to initialize UDP socket.\n");
        return 1;
    }
    std::printf("[OK] UDP sender ready (localhost:4242)\n");

    // 2. Init Leia SDK
    SR::SRContext* context = nullptr;
    SR::PredictingEyeTracker* tracker = nullptr;
    EyeListener listener;

    try {
        context = new SR::SRContext();
        std::printf("[OK] SR Context created\n");
    } catch (SR::ServerNotAvailableException&) {
        std::printf("ERROR: Leia SR Service not available.\n");
        std::printf("       Make sure the SR Platform is installed and running.\n");
        return 1;
    } catch (std::runtime_error& e) {
        std::printf("ERROR: SR Context failed: %s\n", e.what());
        return 1;
    }

    try {
        tracker = SR::PredictingEyeTracker::create(*context);
        listener.stream.set(tracker->openEyePairStream(&listener));
        context->initialize();
        std::printf("[OK] Eye tracker started\n");
    } catch (std::exception& e) {
        std::printf("ERROR: Eye tracker init failed: %s\n", e.what());
        delete context;
        return 1;
    }

    // 3. Create pipeline (auto-load saved settings if available)
    TrackConfig cfg;
    if (load_config(cfg)) {
        std::printf("[OK] Settings loaded from: %s\n", get_config_path().c_str());
    }
    TrackPipeline pipeline(cfg);

    // 4. Wait for first eye data, then calibrate
    std::printf("\nWaiting for eye data...\n");
    float left[3], right[3];
    uint64_t time_us;
    bool got_first = false;

    for (int i = 0; i < 300 && !got_first; ++i) {
        tracker->predict(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (listener.get(left, right, time_us)) {
            float ipd = std::fabs(right[0] - left[0]);
            if (ipd > 10.0f) got_first = true;
        }
    }

    if (!got_first) {
        std::printf("WARNING: No face detected after 5 seconds. Calibrating with defaults.\n");
        left[0] = -32.5f; left[1] = 0.0f; left[2] = 600.0f;
        right[0] = 32.5f; right[1] = 0.0f; right[2] = 600.0f;
    } else {
        std::printf("[OK] Face detected. IPD = %.1f mm\n", std::fabs(right[0] - left[0]));
    }

    pipeline.calibrate(left, right);
    std::printf("[OK] Center calibrated. Press Ctrl+X to recalibrate.\n\n");

    // 5. Main loop
    auto start_time = std::chrono::high_resolution_clock::now();
    int frame_count = 0;
    int udp_sent_count = 0;
    int udp_fail_count = 0;
    bool face_lost_announced = false;
    bool keys_locked = false;

    while (true) {
        auto loop_start = std::chrono::high_resolution_clock::now();

        tracker->predict(80);

        if (listener.get(left, right, time_us)) {
            auto now = std::chrono::high_resolution_clock::now();
            float timestamp_sec = std::chrono::duration<float>(now - start_time).count();

            TrackResult r = pipeline.process(left, right, timestamp_sec);

            if (r.valid) {
                bool sent_ok = sender.send(r.yaw_deg, r.pitch_deg, r.roll_deg);
                if (sent_ok) udp_sent_count++; else udp_fail_count++;
                face_lost_announced = false;

                if (frame_count % 30 == 0) {
                    std::printf("\r  Lean: X=%+5.1f Y=%+5.1f | Filt: X=%+5.1f Y=%+5.1f | Out: Yaw=%+6.1f Pitch=%+5.1f | UDP:%d",
                        r.lean_x_cm, r.lean_y_cm,
                        r.filt_x_cm, r.filt_y_cm,
                        r.yaw_deg, r.pitch_deg,
                        udp_sent_count);
                    if (udp_fail_count > 0) std::printf(" FAIL:%d", udp_fail_count);
                    if (keys_locked) std::printf(" [LOCKED]");
                    std::printf("   ");
                    std::fflush(stdout);
                }
            } else {
                sender.sendIdentity();
                if (!face_lost_announced) {
                    std::printf("\n  [FACE LOST] Sending identity rotation\n");
                    face_lost_announced = true;
                }
            }
        }

        // Handle input
        bool config_changed = false;

        while (_kbhit()) {
            int key = _getch();

            // Ctrl+X = calibrate (always works, even when locked)
            if (key == CTRL_X) {
                if (listener.get(left, right, time_us)) {
                    pipeline.calibrate(left, right);
                    std::printf("\n  [CALIBRATED] Center reset.\n");
                }
                continue;
            }

            // Ctrl+L = toggle lock
            if (key == CTRL_L) {
                keys_locked = !keys_locked;
                std::printf("\n  [%s] Tuning hotkeys %s.\n",
                    keys_locked ? "LOCKED" : "UNLOCKED",
                    keys_locked ? "disabled" : "enabled");
                continue;
            }

            // All tuning keys below are blocked when locked
            if (keys_locked) continue;

            switch (key) {
                // Smoothing: 1/2 = min_cutoff down/up
                case '1':
                    cfg.filter_mincutoff = std::max(0.001f, cfg.filter_mincutoff / 2.0f);
                    pipeline.config().filter_mincutoff = cfg.filter_mincutoff;
                    pipeline.reset_filters();
                    std::printf("\n  min_cutoff = %.4f (smoother)\n", cfg.filter_mincutoff);
                    config_changed = true;
                    break;
                case '2':
                    cfg.filter_mincutoff = std::min(10.0f, cfg.filter_mincutoff * 2.0f);
                    pipeline.config().filter_mincutoff = cfg.filter_mincutoff;
                    pipeline.reset_filters();
                    std::printf("\n  min_cutoff = %.4f (less smooth)\n", cfg.filter_mincutoff);
                    config_changed = true;
                    break;

                // Response: 3/4 = beta down/up
                case '3':
                    cfg.filter_beta = std::max(0.001f, cfg.filter_beta / 10.0f);
                    pipeline.config().filter_beta = cfg.filter_beta;
                    pipeline.reset_filters();
                    std::printf("\n  beta = %.4f (less responsive)\n", cfg.filter_beta);
                    config_changed = true;
                    break;
                case '4':
                    cfg.filter_beta = std::min(100.0f, cfg.filter_beta * 10.0f);
                    pipeline.config().filter_beta = cfg.filter_beta;
                    pipeline.reset_filters();
                    std::printf("\n  beta = %.4f (more responsive)\n", cfg.filter_beta);
                    config_changed = true;
                    break;

                // Yaw sensitivity: 5/6
                case '5':
                    cfg.sens_yaw = std::max(0.5f, cfg.sens_yaw - 0.5f);
                    pipeline.config().sens_yaw = cfg.sens_yaw;
                    std::printf("\n  sens_yaw = %.1f\n", cfg.sens_yaw);
                    config_changed = true;
                    break;
                case '6':
                    cfg.sens_yaw = std::min(20.0f, cfg.sens_yaw + 0.5f);
                    pipeline.config().sens_yaw = cfg.sens_yaw;
                    std::printf("\n  sens_yaw = %.1f\n", cfg.sens_yaw);
                    config_changed = true;
                    break;

                // Pitch sensitivity: 7/8
                case '7':
                    cfg.sens_pitch = std::max(0.5f, cfg.sens_pitch - 0.5f);
                    pipeline.config().sens_pitch = cfg.sens_pitch;
                    std::printf("\n  sens_pitch = %.1f\n", cfg.sens_pitch);
                    config_changed = true;
                    break;
                case '8':
                    cfg.sens_pitch = std::min(20.0f, cfg.sens_pitch + 0.5f);
                    pipeline.config().sens_pitch = cfg.sens_pitch;
                    std::printf("\n  sens_pitch = %.1f\n", cfg.sens_pitch);
                    config_changed = true;
                    break;

                // Curve: 9/0
                case '9':
                    cfg.curve_power = std::max(1.0f, cfg.curve_power - 0.1f);
                    pipeline.config().curve_power = cfg.curve_power;
                    std::printf("\n  curve_power = %.2f\n", cfg.curve_power);
                    config_changed = true;
                    break;
                case '0':
                    cfg.curve_power = std::min(3.0f, cfg.curve_power + 0.1f);
                    pipeline.config().curve_power = cfg.curve_power;
                    std::printf("\n  curve_power = %.2f\n", cfg.curve_power);
                    config_changed = true;
                    break;

                // Max yaw: -/=
                case '-':
                    cfg.max_yaw = std::max(5.0f, cfg.max_yaw - 5.0f);
                    pipeline.config().max_yaw = cfg.max_yaw;
                    std::printf("\n  max_yaw = %.0f deg\n", cfg.max_yaw);
                    config_changed = true;
                    break;
                case '=':
                    cfg.max_yaw = std::min(90.0f, cfg.max_yaw + 5.0f);
                    pipeline.config().max_yaw = cfg.max_yaw;
                    std::printf("\n  max_yaw = %.0f deg\n", cfg.max_yaw);
                    config_changed = true;
                    break;

                // Max pitch: [/]
                case '[':
                    cfg.max_pitch = std::max(5.0f, cfg.max_pitch - 5.0f);
                    pipeline.config().max_pitch = cfg.max_pitch;
                    std::printf("\n  max_pitch = %.0f deg\n", cfg.max_pitch);
                    config_changed = true;
                    break;
                case ']':
                    cfg.max_pitch = std::min(90.0f, cfg.max_pitch + 5.0f);
                    pipeline.config().max_pitch = cfg.max_pitch;
                    std::printf("\n  max_pitch = %.0f deg\n", cfg.max_pitch);
                    config_changed = true;
                    break;
            }
        }

        // Auto-save on any change
        if (config_changed) {
            save_config(cfg);
        }

        frame_count++;

        auto loop_end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
        auto remaining = std::chrono::milliseconds(8) - elapsed;
        if (remaining.count() > 0) {
            std::this_thread::sleep_for(remaining);
        }
    }

    // Unreachable in normal operation (user closes window)
    sender.shutdown();
    delete context;
    return 0;
}
